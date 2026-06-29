package config

import ip.qspi.QuadSpiXipConfig
import spinal.core._
import venuscore.config.VenusCoreConfig
import tinymlsoc.cpu.VexRiscvAhbConfig

/**
 * VenusCoreRVTopConfig
 * -------------------
 * 用于管理 VenusCoreRVTop 的 SoC 级参数：总线宽度、地址映射、外设参数与 NPU/CPU 配置。
 */
case class VenusCoreRVTopConfig(
                                 // CPU feature switch:
                                 // true  -> full FPU + I/D cache
                                 // false -> only ICache (DBusSimple)
                                 cpuFullFeature: Boolean = false,
                                 // CPU wrapper config
                                 cpuCfg: VexRiscvAhbConfig = VexRiscvAhbConfig(
                                   resetVector = 0x00000000L,
                                   mtvecInit = 0x00000020L,
                                   ioRange = addr => (addr(31 downto 20) === 0x006) || (addr(31 downto 20) === 0x002) || (addr(31 downto 28) === 0xF),
                                   enableYaml = false,
                                   yamlPath = "cpu0.yaml"
                                 ),

                                 // NPU config (APB slave + AHB DMA master)
                                 npuCfg: VenusCoreConfig = VenusCoreConfig.default,

                                 // SoC AHB-Lite3 interconnect config
                                 ahbAddrWidth: Int = 32,
                                 ahbDataWidth: Int = 32,

                                 // AHB address map (base/size in bytes)
                                 s0SramBase: Long = 0x00000000L,
                                 s0SramSizeBytes: Long = 0x00010000L, // 64KB on-chip RAM

                                 s1QspiBase: Long = 0x00100000L,
                                 s1QspiSizeBytes: Long = 0x00400000L, // 4MB window (XIP)

                                 s2ApbBase: Long = 0x00600000L,
                                 s2ApbSizeBytes: Long = 0x00100000L, // 1MB APB bridge window

                                 // QSPI XIP config (internal AHB addrWidth is 24-bit; RVTop will do address truncation)
                                 qspiCfg: QuadSpiXipConfig = QuadSpiXipConfig(),

                                 // MSM261S I2S mic
                                 micFifoDepth: Int = 512,

                                 // APB peripherals
                                 gpioWidth: Int = 8
                               ) {
  require(ahbAddrWidth == 32, "VenusCoreRVTop assumes AHB addressWidth=32")
  require(ahbDataWidth == 32, "VenusCoreRVTop assumes AHB dataWidth=32")
  require(s0SramSizeBytes > 0, "s0SramSizeBytes must be > 0")
  require(s2ApbSizeBytes > 0, "s2ApbSizeBytes must be > 0")
  require(gpioWidth > 0 && gpioWidth <= 32, "gpioWidth must be in 1..32")

  def effectiveCpuCfg: VexRiscvAhbConfig = {
    cpuCfg.copy(withDCache = cpuFullFeature, withFpu = cpuFullFeature)
  }
}

object VenusCoreRVTopConfig {
  def default: VenusCoreRVTopConfig = VenusCoreRVTopConfig()
}
