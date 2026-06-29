package ip.qspi

import spinal.core._
import spinal.lib._
import spinal.lib.bus.amba3.ahblite._
import spinal.lib.io.TriStateArray
import other.QuadSpiXipAhb

/**
 * QuadSpiXipAhbLite32
 * ------------------
 * 适配 32-bit AHB 地址空间的 QSPI XIP AHB-Lite3 Slave：
 * - 顶层 SoC 使用 32-bit AHB 地址；
 * - QSPI XIP 内核只看映射窗口内的低 24-bit（byte address）；
 * - 通过 `socBase` 做地址窗口偏移（io.ahb.HADDR - socBase）。
 */
case class QuadSpiXipAhbLite32(
                                cfg: QuadSpiXipConfig = QuadSpiXipConfig(),
                                ahbCfg: AhbLite3Config = AhbLite3Config(addressWidth = 32, dataWidth = 32),
                                socBase: Long = 0x00100000L
                              ) extends Component {
  require(ahbCfg.addressWidth == 32, "QuadSpiXipAhbLite32 assumes AHB addressWidth=32")
  require(ahbCfg.dataWidth == 32, "QuadSpiXipAhbLite32 assumes AHB dataWidth=32")

  val io = new Bundle {
    val ahb = slave(AhbLite3(ahbCfg)).setName("ahb")
    val qspi_cs_n = out Bool()
    val qspi_sclk = out Bool()
    val qspi_dp = master(TriStateArray(4 bits)).setName("qspi_dp")
  }
  noIoPrefix()

  private val core = QuadSpiXipAhb(cfg.toCoreCfg(ahbCfg))
  core.setName("qspi_xip_core")

  // AHB address remap: window base -> 0
  private val ahb_remap = io.ahb.remapAddress(addr => (addr - U(socBase, 32 bits)).resized)
  core.io.ahb <> ahb_remap

  // QSPI pins
  io.qspi_cs_n := core.io.qspi_cs_n
  io.qspi_sclk := core.io.qspi_sclk
  io.qspi_dp.write := core.io.qspi_dq_o
  io.qspi_dp.writeEnable := core.io.qspi_dq_oe
  core.io.qspi_dq_i := io.qspi_dp.read
}
