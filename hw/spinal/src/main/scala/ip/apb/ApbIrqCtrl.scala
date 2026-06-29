package ip.apb

import spinal.core._
import spinal.lib._
import spinal.lib.bus.amba3.apb._

/**
 * ApbIrqCtrl
 * ----------
 * 通用 IRQ 聚合器（APB3 Slave）。
 *
 * sources: 4 路 Bool 输入（同步后参与 pending 计算）
 *
 * Register map (4KB window, word offsets):
 * - 0x000 ENABLE   (R/W) [3:0]
 * - 0x004 PENDING  (R/W1C) [3:0]
 * - 0x008 RAW      (R) [3:0]
 */
case class ApbIrqCtrl() extends Component {
  private val apb_cfg = Apb3Config(
    addressWidth = 12,
    dataWidth = 32,
    selWidth = 1,
    useSlaveError = true
  )

  val io = new Bundle {
    val apb = slave(Apb3(apb_cfg)).setName("apb")
    val src = in Bits (4 bits)
    val irq = out Bool()
  }
  noIoPrefix()

  private val irq_enable_reg = Reg(Bits(4 bits)) init (0)
  private val irq_pending_reg = Reg(Bits(4 bits)) init (0)

  private val src_sync = BufferCC(io.src, init = B(0, 4 bits))

  private val apb_sel = io.apb.PSEL.orR
  private val apb_setup = apb_sel && !io.apb.PENABLE
  private val apb_enable = apb_sel && io.apb.PENABLE
  private val apb_write = apb_enable && io.apb.PWRITE
  private val word_addr_width = apb_cfg.addressWidth - 2
  private val word_addr = io.apb.PADDR((apb_cfg.addressWidth - 1) downto 2)
  private val write_bits = io.apb.PWDATA(3 downto 0).asBits

  private val captured_word_addr_reg = Reg(UInt(word_addr_width bits)) init (0)
  private val captured_write_bits_reg = Reg(Bits(4 bits)) init (0)
  when(apb_setup && io.apb.PWRITE) {
    captured_word_addr_reg := word_addr
    captured_write_bits_reg := write_bits
  }

  private val addr_enable = U(0x000 >> 2, word_addr_width bits)
  private val addr_pending = U(0x004 >> 2, word_addr_width bits)
  private val addr_raw = U(0x008 >> 2, word_addr_width bits)

  private val clear_mask = Bits(4 bits)
  clear_mask := 0

  when(apb_write) {
    switch(captured_word_addr_reg) {
      is(addr_enable) {
        irq_enable_reg := captured_write_bits_reg.resized
      }
      is(addr_pending) {
        clear_mask := captured_write_bits_reg.resized
      }
      default {}
    }
  }

  irq_pending_reg := (irq_pending_reg & ~clear_mask) | (src_sync & irq_enable_reg)

  private val read_data = Bits(32 bits)
  read_data := 0
  switch(word_addr) {
    is(addr_enable) {
      read_data := irq_enable_reg.resized
    }
    is(addr_pending) {
      read_data := irq_pending_reg.resized
    }
    is(addr_raw) {
      read_data := src_sync.resized
    }
    default {}
  }

  io.apb.PREADY := True
  io.apb.PSLVERROR := False
  io.apb.PRDATA := read_data

  io.irq := irq_pending_reg.orR
}
