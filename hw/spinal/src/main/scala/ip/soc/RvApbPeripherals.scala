package ip.soc

import ip.apb.{ApbGpioIrq, ApbIrqCtrl}
import ip.i2s.Msm261sApb
import spinal.core._
import spinal.lib._
import spinal.lib.bus.amba3.apb._
import spinal.lib.com.uart._
import spinal.lib.io.TriStateArray
import vexriscv.demo.MuraxApb3Timer

/**
 * RvApbPeripherals
 * ----------------
 * 把 RVTop 上的 APB 外设（GPIO/UART/TIMER/IRQ/NPU-APB）集中在一个模块里，
 * 让顶层只剩下实例化与连线。
 *
 * APB 地址映射（每个窗口 4KB）：
 * - 0x0000 GPIO
 * - 0x1000 GPIO_IRQ
 * - 0x2000 UART
 * - 0x3000 TIMER
 * - 0x4000 IRQ_CTRL
 * - 0x5000 NPU_APB
 * - 0x6000 I2S_MIC (MSM261S)
 */
case class RvApbPeripherals(apbCfg: Apb3Config, gpioWidth: Int = 32, micFifoDepth: Int = 512) extends Component {
  require(apbCfg.dataWidth == 32, "RvApbPeripherals assumes APB dataWidth=32")
  require(gpioWidth > 0 && gpioWidth <= 32, "gpioWidth must be in 1..32")

  val io = new Bundle {
    val apb_m = slave(Apb3(apbCfg)).setName("apb_m")

    val gpio = master(TriStateArray(gpioWidth bits)).setName("gpio")
    val uart = master(Uart()).setName("uart")

    // I2S mic pins
    val mic_sck = out Bool()
    val mic_ws = out Bool()
    val mic_sd = in Bool()

    val gpio_irq = out Bool()
    val uart_irq = out Bool()
    val timer_irq = out Bool()
    val irq_out = out Bool()

    val npu_irq = in Bool()
    val npu_apb = master(Apb3(Apb3Config(addressWidth = 12, dataWidth = 32, selWidth = 1, useSlaveError = false))).setName("npu_apb")
  }
  noIoPrefix()

  // GPIO
  private val gpio_ctrl = Apb3Gpio(gpioWidth = gpioWidth, withReadSync = true)
  io.gpio <> gpio_ctrl.io.gpio

  // GPIO IRQ (based on GPIO read value)
  private val gpio_irq = ApbGpioIrq(gpioWidth = gpioWidth)
  gpio_irq.io.gpio_in := gpio_ctrl.io.gpio.read

  // UART
  private val uart_ctrl_cfg = UartCtrlMemoryMappedConfig(
    uartCtrlConfig = UartCtrlGenerics(
      dataWidthMax = 8,
      clockDividerWidth = 20,
      preSamplingSize = 1,
      samplingSize = 5,
      postSamplingSize = 2
    ),
    txFifoDepth = 16,
    rxFifoDepth = 16
  )
  private val uart_ctrl = Apb3UartCtrl(uart_ctrl_cfg)
  io.uart <> uart_ctrl.io.uart
  io.uart_irq := uart_ctrl.io.interrupt

  // Timer
  private val timer_ctrl = new MuraxApb3Timer()
  io.timer_irq := timer_ctrl.io.interrupt

  // IRQ aggregator
  private val irq_ctrl = ApbIrqCtrl()
  private val src_vec = Bits(4 bits)
  src_vec(0) := BufferCC(gpio_irq.io.irq, init = False)
  src_vec(1) := BufferCC(uart_ctrl.io.interrupt, init = False)
  src_vec(2) := BufferCC(timer_ctrl.io.interrupt, init = False)
  src_vec(3) := BufferCC(io.npu_irq, init = False)
  irq_ctrl.io.src := src_vec
  io.irq_out := irq_ctrl.io.irq

  io.gpio_irq := gpio_irq.io.irq

  // APB Decoder (4KB each)
  private val mic_cfg = Apb3Config(addressWidth = 12, dataWidth = 32, selWidth = 1, useSlaveError = false)
  private val mic = Msm261sApb(fifoDepth = micFifoDepth, apbCfg = mic_cfg)
  io.mic_sck := mic.io.mic_sck
  io.mic_ws := mic.io.mic_ws
  mic.io.mic_sd := io.mic_sd

  Apb3Decoder(
    master = io.apb_m,
    slaves = List(
      gpio_ctrl.io.apb -> (0x0000, 4096),
      gpio_irq.io.apb -> (0x1000, 4096),
      uart_ctrl.io.apb -> (0x2000, 4096),
      timer_ctrl.io.apb -> (0x3000, 4096),
      irq_ctrl.io.apb -> (0x4000, 4096),
      io.npu_apb -> (0x5000, 4096),
      mic.io.apb -> (0x6000, 4096)
    )
  )
}
