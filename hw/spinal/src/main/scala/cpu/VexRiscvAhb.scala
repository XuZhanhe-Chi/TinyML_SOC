package tinymlsoc.cpu

import spinal.core._
import spinal.lib._
import spinal.lib.bus.amba3.ahblite._
import spinal.lib.bus.simple._
import spinal.lib.com.jtag.Jtag
import vexriscv.{Riscv, VexRiscv, VexRiscvConfig}
import vexriscv.ip.DataCacheConfig
import vexriscv.ip.InstructionCacheConfig
import vexriscv.ip.fpu.FpuParameter
import vexriscv.plugin._

/**
 * VexRiscvAhb
 * ----------
 * 目标：
 * - 把 VexRiscv CPU（可选 FPU/I$+D$；IBUS/DBUS 先仲裁合并再出 AHB）封装成可复用的模块；
 * - 对外只暴露一路 AHB-Lite3 Master 以及 JTAG/中断输入；
 * - 上层 SoC 负责 AHB 互联、外设、内存映射等。
 *
 * 说明：
 * - CPU 侧至少包含 ICache；DCache/FPU 由配置开关决定；
 * - 通过 `MuraxMasterArbiter` 把 IBUS/DBUS 合并成 1 路 `PipelinedMemoryBus`；
 * - 再转换为 AHB-Lite3 Master。
 */

case class VexRiscvAhbConfig(
    resetVector: Long = 0x00000000L,
    mtvecInit: Long = 0x00000020L,
    ioRange: UInt => Bool = _ => False,
    enableYaml: Boolean = true,
    yamlPath: String = "cpu0.yaml",
    withDCache: Boolean = true,
    withFpu: Boolean = true,
    withICache: Boolean = true
)

case class VexRiscvAhb(cfg: VexRiscvAhbConfig, debugCd: ClockDomain) extends Component {
  val io = new Bundle {
    val external_irq = in Bool()
    val timer_irq = in Bool()

    val jtag = slave(Jtag()).setName("jtag")

    val ahb_m = master(AhbLite3Master(AhbLite3Config(addressWidth = 32, dataWidth = 32))).setName("ahb_m")
  }
  noIoPrefix()

  private val ahb_cfg = AhbLite3Config(addressWidth = 32, dataWidth = 32)
  private val pmb_cfg = PipelinedMemoryBusConfig(32, 32)

  private val fpu_plugins = if (cfg.withFpu) {
    List(new FpuPlugin(
      externalFpu = false,
      simHalt = false,
      p = FpuParameter(withDouble = false, withDivSqrt = false)
    ))
  } else {
    Nil
  }

  private val misa_ext = if (cfg.withFpu) "imaf" else "ima"

  private val i_bus_plugin = if (cfg.withICache) {
    new IBusCachedPlugin(
      resetVector = cfg.resetVector,
      prediction = STATIC,
      compressedGen = false,
      config = InstructionCacheConfig(
        cacheSize = 4096,
        bytePerLine = 32,
        wayCount = 1,
        addressWidth = 32,
        cpuDataWidth = 32,
        memDataWidth = 32,
        catchIllegalAccess = false,
        catchAccessFault = false,
        asyncTagMemory = false,
        twoCycleRam = false,
        twoCycleCache = false
      )
    )
  } else {
    new IBusSimplePlugin(
      resetVector = cfg.resetVector,
      cmdForkOnSecondStage = true,
      cmdForkPersistence = false,
      prediction = STATIC,
      catchAccessFault = false,
      compressedGen = false,
      bigEndian = false
    )
  }

  private val cpu_config = VexRiscvConfig(
    plugins = List(
      new StaticMemoryTranslatorPlugin(
        ioRange = cfg.ioRange
      ),
      i_bus_plugin,
      (if(cfg.withDCache) new DBusCachedPlugin(
        config = DataCacheConfig(
          cacheSize = 4096,
          bytePerLine = 32,
          wayCount = 1,
          addressWidth = 32,
          cpuDataWidth = 32,
          memDataWidth = 32,
          catchAccessError = false,
          catchIllegal = false,
          catchUnaligned = false,
          withLrSc = false,
          withAmo = false,
          withExclusive = false,
          withInvalidate = false,
          // 时序/面积优先：不启用 write aggregation
          withWriteAggregation = false
        )
      ) else new DBusSimplePlugin()),
      new DecoderSimplePlugin(
        catchIllegalInstruction = true
      ),
      new RegFilePlugin(
        regFileReadyKind = _root_.vexriscv.plugin.SYNC,
        zeroBoot = false
      ),
      new IntAluPlugin,
      new SrcPlugin(
        separatedAddSub = false,
        executeInsertion = true
      ),
      new FullBarrelShifterPlugin,
      new MulPlugin,
      new DivPlugin
    ) ++ fpu_plugins ++ List(
      new HazardSimplePlugin(
        bypassExecute = true,
        bypassMemory = true,
        bypassWriteBack = true,
        bypassWriteBackBuffer = true,
        pessimisticUseSrc = false,
        pessimisticWriteRegFile = false,
        pessimisticAddressMatch = false
      ),
      new DebugPlugin(debugCd, hardwareBreakpointCount = 2),
      new BranchPlugin(
        earlyBranch = false,
        catchAddressMisaligned = true
      ),
      new CsrPlugin(
        config = CsrPluginConfig(
          catchIllegalAccess = false,
          mvendorid = null,
          marchid = null,
          mimpid = null,
          mhartid = null,
          misaExtensionsInit = Riscv.misaToInt(misa_ext),
          misaAccess = CsrAccess.NONE,
          mtvecAccess = CsrAccess.READ_WRITE,
          mtvecInit = cfg.mtvecInit,
          mepcAccess = CsrAccess.READ_WRITE,
          mscratchGen = false,
          mcauseAccess = CsrAccess.READ_ONLY,
          mbadaddrAccess = CsrAccess.READ_ONLY,
          mcycleAccess = CsrAccess.READ_ONLY,
          minstretAccess = CsrAccess.NONE,
          ecallGen = true,
          wfiGenAsWait = false,
          ucycleAccess = CsrAccess.NONE,
          uinstretAccess = CsrAccess.NONE
        )
      )
    ) ++ (if (cfg.enableYaml) List(new YamlPlugin(cfg.yamlPath)) else Nil)
  )

  private val cpu = new VexRiscv(cpu_config)

  // --------------------------------------------------------------------------
  // 1) 从插件中拿到 IBUS/DBUS，并连接中断/JTAG
  // --------------------------------------------------------------------------
  private var i_pmb: PipelinedMemoryBus = null
  private var d_pmb: PipelinedMemoryBus = null

  for (p <- cpu.plugins) p match {
    case csr: CsrPlugin =>
      csr.externalInterrupt := io.external_irq
      csr.timerInterrupt := io.timer_irq

    case ibus: IBusCachedPlugin =>
      i_pmb = ibus.iBus.toPipelinedMemoryBus()
    case ibus: IBusSimplePlugin =>
      i_pmb = ibus.iBus.toPipelinedMemoryBus()

    case dbus: DBusCachedPlugin =>
      d_pmb = dbus.dBus.toPipelinedMemoryBus()
    case dbus: DBusSimplePlugin =>
      d_pmb = dbus.dBus.toPipelinedMemoryBus()

    case debug: DebugPlugin =>
      val jtag_bridge = debug.debugClockDomain {
        debug.io.bus.fromJtag()
      }
      io.jtag <> jtag_bridge

    case _ =>
  }

  // --------------------------------------------------------------------------
  // 2) IBUS/DBUS -> 1x PipelinedMemoryBus
  // --------------------------------------------------------------------------
  private val main_pmb = PipelinedMemoryBus(pmb_cfg)

  // 固定优先级：DBUS 优先（等价于 MuraxMasterArbiter 的仲裁策略）
  // 说明：由于 PipelinedMemoryBus 没有 response ID，这里强制“单 outstanding”，不做 pipeline。
  private val rsp_pending_reg = RegInit(False) clearWhen(main_pmb.rsp.valid)
  private val rsp_target_d_reg = RegInit(False) // 1->DBUS, 0->IBUS

  private val use_d = d_pmb.cmd.valid

  main_pmb.cmd.valid := (i_pmb.cmd.valid || d_pmb.cmd.valid) && !rsp_pending_reg
  main_pmb.cmd.write := use_d && d_pmb.cmd.write
  main_pmb.cmd.address := use_d ? d_pmb.cmd.address | i_pmb.cmd.address
  main_pmb.cmd.data := d_pmb.cmd.data
  main_pmb.cmd.mask := d_pmb.cmd.mask

  // ready：只允许当前被选中的端口前进
  i_pmb.cmd.ready := main_pmb.cmd.ready && !use_d && !rsp_pending_reg
  d_pmb.cmd.ready := main_pmb.cmd.ready && use_d && !rsp_pending_reg

  when(main_pmb.cmd.fire && !main_pmb.cmd.write) {
    rsp_pending_reg := True
    rsp_target_d_reg := use_d
  }

  // response demux
  i_pmb.rsp.valid := main_pmb.rsp.valid && !rsp_target_d_reg
  i_pmb.rsp.payload := main_pmb.rsp.payload
  d_pmb.rsp.valid := main_pmb.rsp.valid && rsp_target_d_reg
  d_pmb.rsp.payload := main_pmb.rsp.payload

  // --------------------------------------------------------------------------
  // 3) PipelinedMemoryBus -> AHB-Lite3 Master
  // --------------------------------------------------------------------------
  private case class PmbToAhbLite3(pmb: PipelinedMemoryBus) extends Area {
    val ahb_m = AhbLite3Master(ahb_cfg)

    // 说明：
    // - 该桥接按“单 outstanding、无 pipeline”实现，保证 AHB 协议在 wait-state 期间不被破坏；
    // - 为避免与 Spinal 的 AHB decoder/arbiter 形成组合环路，HTRANS/ready 不组合依赖 HREADY。
    object AhbPhase extends SpinalEnum {
      val IDLE, ADDR, DATA = newElement()
    }

    val phase_reg = Reg(AhbPhase()) init (AhbPhase.IDLE)
    val write_reg = RegInit(False)
    val addr_reg = Reg(UInt(32 bits)) init (0)
    val data_reg = Reg(Bits(32 bits)) init (0)
    val mask_reg = Reg(Bits(4 bits)) init (0)

    def maskToHsize(mask: Bits): Bits = {
      val hsize = Bits(3 bits)
      hsize := B"010" // word by default
      switch(mask) {
        is(B"0001") { hsize := B"000" }
        is(B"0010") { hsize := B"000" }
        is(B"0100") { hsize := B"000" }
        is(B"1000") { hsize := B"000" }
        is(B"0011") { hsize := B"001" }
        is(B"1100") { hsize := B"001" }
        is(B"1111") { hsize := B"010" }
        default { hsize := B"010" }
      }
      hsize
    }

    // cmd handshake：只在 IDLE 接收一个 cmd，并将其锁存到寄存器中
    pmb.cmd.ready := (phase_reg === AhbPhase.IDLE)
    val cmd_fire = pmb.cmd.fire
    when(cmd_fire) {
      phase_reg := AhbPhase.ADDR
      write_reg := pmb.cmd.write
      addr_reg := pmb.cmd.address
      data_reg := pmb.cmd.data
      mask_reg := pmb.cmd.mask
    }

    // phase transition：ADDR 被采样后进入 DATA；DATA 完成后回到 IDLE
    when(phase_reg === AhbPhase.ADDR) {
      when(ahb_m.HREADY) {
        phase_reg := AhbPhase.DATA
      }
    } elsewhen (phase_reg === AhbPhase.DATA) {
      when(ahb_m.HREADY) {
        phase_reg := AhbPhase.IDLE
      }
    }

    // AHB drive
    ahb_m.HADDR := addr_reg
    ahb_m.HWRITE := write_reg
    ahb_m.HSIZE := write_reg ? maskToHsize(mask_reg) | B"010"
    ahb_m.HBURST := 0
    ahb_m.HPROT := "1111"
    ahb_m.HMASTLOCK := False
    ahb_m.HWDATA := data_reg

    // DATA phase 用 BUSY（01）：不发起新传输，但保持“非 IDLE”以便 decoder/arbiter 维持对当前 slave 的选择。
    ahb_m.HTRANS := B"00"
    when(phase_reg === AhbPhase.ADDR) {
      ahb_m.HTRANS := B"10"
    } elsewhen (phase_reg === AhbPhase.DATA) {
      ahb_m.HTRANS := B"01"
    }

    // read response：DATA phase 完成的那个周期 HRDATA 有效
    val done = (phase_reg === AhbPhase.DATA) && ahb_m.HREADY
    pmb.rsp.valid := done && !write_reg
    pmb.rsp.data := ahb_m.HRDATA
  }

  private val main_conv = PmbToAhbLite3(main_pmb)
  io.ahb_m <> main_conv.ahb_m
}
