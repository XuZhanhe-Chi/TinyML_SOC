package ip.apb

import spinal.core._
import spinal.lib._
import spinal.lib.bus.amba3.apb._

/**
 * ApbGpioIrq
 * ---------
 * GPIO 上升沿/下降沿中断控制器（APB3 Slave）。
 *
 * Register map (4KB window, word offsets):
 * - 0x000 INT_EN     (R/W) [gpioWidth-1:0]
 * - 0x004 INT_RISE   (R/W) [gpioWidth-1:0]
 * - 0x008 INT_FALL   (R/W) [gpioWidth-1:0]
 * - 0x00C INT_STATUS (R/W1C) [gpioWidth-1:0]
 */
case class ApbGpioIrq(gpioWidth: Int = 32) extends Component {
  require(gpioWidth > 0 && gpioWidth <= 32, "gpioWidth must be in 1..32")

  private val apb_cfg = Apb3Config(
    addressWidth = 12,
    dataWidth = 32,
    selWidth = 1,
    useSlaveError = true
  )

  val io = new Bundle {
    val apb = slave(Apb3(apb_cfg)).setName("apb")
    val gpio_in = in Bits (gpioWidth bits)
    val irq = out Bool()
  }
  noIoPrefix()

  private val intr_enable_reg = Reg(Bits(gpioWidth bits)) init (0)
  private val intr_rise_reg = Reg(Bits(gpioWidth bits)) init (0)
  private val intr_fall_reg = Reg(Bits(gpioWidth bits)) init (0)
  private val intr_pending_reg = Reg(Bits(gpioWidth bits)) init (0)

  private val input_sync = BufferCC(io.gpio_in, init = B(0, gpioWidth bits))
  private val input_prev_reg = RegNext(input_sync) init (0)
  private val rising = (~input_prev_reg) & input_sync
  private val falling = input_prev_reg & (~input_sync)

  private val apb_sel = io.apb.PSEL.orR
  private val apb_setup = apb_sel && !io.apb.PENABLE
  private val apb_enable = apb_sel && io.apb.PENABLE
  private val apb_write = apb_enable && io.apb.PWRITE
  private val word_addr_width = apb_cfg.addressWidth - 2
  private val word_addr = io.apb.PADDR((apb_cfg.addressWidth - 1) downto 2)
  private val write_bits = io.apb.PWDATA(gpioWidth - 1 downto 0).asBits

  private val captured_word_addr_reg = Reg(UInt(word_addr_width bits)) init (0)
  private val captured_write_bits_reg = Reg(Bits(gpioWidth bits)) init (0)
  when(apb_setup && io.apb.PWRITE) {
    captured_word_addr_reg := word_addr
    captured_write_bits_reg := write_bits
  }

  private val addr_int_en = U(0x000 >> 2, word_addr_width bits)
  private val addr_int_rise = U(0x004 >> 2, word_addr_width bits)
  private val addr_int_fall = U(0x008 >> 2, word_addr_width bits)
  private val addr_int_sta = U(0x00C >> 2, word_addr_width bits)

  when(apb_write) {
    switch(captured_word_addr_reg) {
      is(addr_int_en) {
        intr_enable_reg := captured_write_bits_reg
      }
      is(addr_int_rise) {
        intr_rise_reg := captured_write_bits_reg
      }
      is(addr_int_fall) {
        intr_fall_reg := captured_write_bits_reg
      }
      default {}
    }
  }

  private val status_clear_mask = Bits(gpioWidth bits)
  status_clear_mask := 0
  when(apb_write && captured_word_addr_reg === addr_int_sta) {
    status_clear_mask := captured_write_bits_reg
  }

  private val edge_events = ((rising & intr_rise_reg) | (falling & intr_fall_reg)) & intr_enable_reg
  intr_pending_reg := (intr_pending_reg & ~status_clear_mask) | edge_events

  private val read_data = Bits(32 bits)
  read_data := 0
  switch(word_addr) {
    is(addr_int_en) {
      read_data := intr_enable_reg.resized
    }
    is(addr_int_rise) {
      read_data := intr_rise_reg.resized
    }
    is(addr_int_fall) {
      read_data := intr_fall_reg.resized
    }
    is(addr_int_sta) {
      read_data := intr_pending_reg.resized
    }
    default {}
  }

  io.apb.PREADY := True
  io.apb.PSLVERROR := False
  io.apb.PRDATA := read_data

  io.irq := intr_pending_reg.orR
}
