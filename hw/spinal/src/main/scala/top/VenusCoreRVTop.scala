package top

import ip.qspi.QuadSpiXipAhbLite32
import ip.qspi.QuadSpiXipConfig
import ip.soc.AhbLite3SoCInterconnect2M
import ip.soc.RvApbPeripherals
import spinal.core._
import spinal.lib._
import spinal.lib.bus.amba3.ahblite._
import spinal.lib.bus.amba3.apb._
import spinal.lib.com.jtag.Jtag
import spinal.lib.com.uart.Uart
import spinal.lib.io.TriStateArray
import spinal.lib.io.InOutWrapper
import tinymlsoc.cpu.VexRiscvAhb
import venuscore.top.VenusCoreTop
import config.VenusCoreRVTopConfig

/**
 * VenusCoreRVTop
 * -------------
 * CPU(VexRiscv) + NPU(VenusCoreTop) + I2S Mic(Msm261s) 的 SoC 集成顶层。
 *
 * 关键点：
 * - AHB 仲裁/译码使用轻量级 2-master 互联（避免 crossbar 的组合关键路径）；
 * - NPU 通过 APB 配置、通过 AHB DMA 访问系统存储；
 * - I2S 外设为 APB 外设（FIFO 读取方式）。
 */
case class VenusCoreRVTop(cfg: VenusCoreRVTopConfig) extends Component {
  private val ahb_cfg = AhbLite3Config(addressWidth = cfg.ahbAddrWidth, dataWidth = cfg.ahbDataWidth)

  require(cfg.npuCfg.dmaCfg.ahbLite3Cfg.addressWidth == ahb_cfg.addressWidth, "NPU AHB addressWidth must match SoC AHB")
  require(cfg.npuCfg.dmaCfg.ahbLite3Cfg.dataWidth == ahb_cfg.dataWidth, "NPU AHB dataWidth must match SoC AHB")

  val io = new Bundle {
    val jtag = slave(Jtag()).setName("jtag")
    val debug_reset_n = in Bool()

    val gpio = master(TriStateArray(cfg.gpioWidth bits)).setName("gpio")
    val uart = master(Uart()).setName("uart")

    // QSPI physical pins
    val qspi_cs_n = out Bool()
    val qspi_sclk = out Bool()
    val qspi_dp = master(TriStateArray(4 bits)).setName("qspi_dp")

    // I2S mic pins
    val mic_sck = out Bool()
    val mic_ws = out Bool()
    val mic_sd = in Bool()
  }
  noIoPrefix()

  // --------------------------------------------------------------------------
  // Reset domain for DebugPlugin
  // --------------------------------------------------------------------------
  private val debug_reset_async = !io.debug_reset_n
  private val debug_reset_sync = ResetCtrl.asyncAssertSyncDeassert(
    input = debug_reset_async,
    clockDomain = ClockDomain.current
  )
  private val debug_cd = ClockDomain.current.copy(reset = debug_reset_sync)

  // --------------------------------------------------------------------------
  // CPU
  // --------------------------------------------------------------------------
  private val cpu_ahb = VexRiscvAhb(cfg = cfg.effectiveCpuCfg, debugCd = debug_cd)
  cpu_ahb.setName("cpu_ahb")
  cpu_ahb.io.jtag <> io.jtag

  // --------------------------------------------------------------------------
  // NPU
  // --------------------------------------------------------------------------
  private val npu = VenusCoreTop(cfg.npuCfg)
  npu.setName("venuscore_top")

  // --------------------------------------------------------------------------
  // AHB Slaves
  // --------------------------------------------------------------------------
  private val s0_sram_bus = AhbLite3(ahb_cfg).setName("s0_sram")
  private val s1_qspi_bus = AhbLite3(ahb_cfg).setName("s1_qspi")
  private val s2_apb_bus = AhbLite3(ahb_cfg).setName("s2_apb")

  private val s0_sram = AhbLite3OnChipRam(ahb_cfg, byteCount = cfg.s0SramSizeBytes)
  s0_sram.io.ahb <> s0_sram_bus
  s0_sram.ram.addAttribute("syn_ramstyle", "block_ram")
  // QSPI XIP peripheral (maps to S1 region)
  private val qspi_xip = QuadSpiXipAhbLite32(cfg = cfg.qspiCfg, ahbCfg = ahb_cfg, socBase = cfg.s1QspiBase)
  qspi_xip.io.ahb <> s1_qspi_bus
  io.qspi_cs_n := qspi_xip.io.qspi_cs_n
  io.qspi_sclk := qspi_xip.io.qspi_sclk
  io.qspi_dp <> qspi_xip.io.qspi_dp

  // --------------------------------------------------------------------------
  // AHB -> APB bridge (S2 window)
  // --------------------------------------------------------------------------
  private val apb_address_width = log2Up(cfg.s2ApbSizeBytes.toInt)
  private val apb_cfg = Apb3Config(
    addressWidth = apb_address_width,
    dataWidth = 32,
    selWidth = 1,
    useSlaveError = true
  )
  private val ahb_to_apb = AhbLite3ToApb3Bridge(ahb_cfg, apb_cfg)
  ahb_to_apb.io.ahb <> s2_apb_bus
  private val apb_bus = ahb_to_apb.io.apb

  // --------------------------------------------------------------------------
  // APB peripherals (split into ip module)
  // --------------------------------------------------------------------------
  private val apb_periph = RvApbPeripherals(apbCfg = apb_cfg, gpioWidth = cfg.gpioWidth, micFifoDepth = cfg.micFifoDepth)
  apb_periph.setName("apb_periph")
  apb_bus >> apb_periph.io.apb_m
  apb_periph.io.npu_irq := npu.io.venus_irq
  apb_periph.io.npu_apb >> npu.io.apb_s
  io.gpio <> apb_periph.io.gpio
  io.uart <> apb_periph.io.uart
  io.mic_sck := apb_periph.io.mic_sck
  io.mic_ws := apb_periph.io.mic_ws
  apb_periph.io.mic_sd := io.mic_sd

  // --------------------------------------------------------------------------
  // CPU interrupt wiring
  // --------------------------------------------------------------------------
  cpu_ahb.io.external_irq := apb_periph.io.irq_out
  cpu_ahb.io.timer_irq := apb_periph.io.timer_irq

  // --------------------------------------------------------------------------
  // AHB Crossbar (CPU + NPU DMA -> SRAM/QSPI/APB)
  // --------------------------------------------------------------------------
  private val m_cpu = cpu_ahb.io.ahb_m.toAhbLite3().setName("m_cpu")
  private val m_npu = npu.io.ahb_m.toAhbLite3().setName("m_npu")

  private val ahb_ic = AhbLite3SoCInterconnect2M(
    ahbCfg = ahb_cfg,
    s0SramBase = cfg.s0SramBase,
    s0SramSizeBytes = cfg.s0SramSizeBytes,
    s1QspiBase = cfg.s1QspiBase,
    s1QspiSizeBytes = cfg.s1QspiSizeBytes,
    s2ApbBase = cfg.s2ApbBase,
    s2ApbSizeBytes = cfg.s2ApbSizeBytes,
    npuAllowApb = false
  )
  ahb_ic.setName("ahb_ic")
  ahb_ic.io.m_cpu <> m_cpu
  ahb_ic.io.m_npu <> m_npu
  ahb_ic.io.s_sram <> s0_sram_bus
  ahb_ic.io.s_qspi <> s1_qspi_bus
  ahb_ic.io.s_apb <> s2_apb_bus
}

/**
 * VenusCoreRVTopGw5a50Gen
 * -----------------------
 * GW5A board target:
 * - CPU starts from SRAM boot stub at 0x0000_0000. The boot stub is embedded
 *   into Gowin BSRAM initmem and jumps to the QSPI XIP window after a short
 *   delay. This avoids cold-boot instruction fetches before the XIP engine is
 *   ready.
 * - QuadSpiXipConfig keeps flashBase=0x0040_0000, so SoC address 0x0010_0000
 *   maps to external flash offset 0x0040_0000.
 */
object VenusCoreRVTopGw5a50Gen extends App {
  val base = VenusCoreRVTopConfig.default
  val cfg = base.copy(
    cpuCfg = base.cpuCfg.copy(
      resetVector = 0x00000000L,
      mtvecInit = 0x00000000L
    ),
    qspiCfg = QuadSpiXipConfig(quadIoRead = false)
  )
  SpinalConfig(
    targetDirectory = "../../build/rtl",
    oneFilePerComponent = false,
    defaultConfigForClockDomains = ClockDomainConfig(resetKind = SYNC, resetActiveLevel = LOW),
    anonymSignalPrefix = "tmp",
    rtlHeader = "`define SYNTHESIS\n",
    noRandBoot = true
  ).withoutEnumString()
    .withoutAssert
    .generateVerilog(InOutWrapper(VenusCoreRVTop(cfg)))
}
