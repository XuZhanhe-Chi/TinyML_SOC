package ip.soc

import spinal.core._
import spinal.lib._
import spinal.lib.bus.amba3.ahblite._

/**
 * AhbLite3SoCInterconnect2M
 * ------------------------
 * 一个面向 SoC 的“轻量级”2-master AHB-Lite3 互联：
 * - master: CPU / NPU(DMA)
 * - slave : SRAM / QSPI_XIP / APB_BRIDGE
 *
 * 设计目标：
 * - 避免 `AhbLite3CrossbarFactory` 生成的“master<->master 组合路径”，提升 FPGA 综合时序；
 * - 仲裁为“状态机+打拍”的固定优先级（默认 CPU 优先）；
 * - slave 选择在地址相位锁存，数据相位使用锁存的选择返回 HRDATA/HRESP/HREADYOUT。
 *
 * 说明：
 * - 该互联是功能正确的 AHB-Lite3（支持 wait-state），但不追求极致吞吐（grant 仅在总线完全空闲时切换）。
 * - NPU 默认不允许访问 APB window（若访问将返回 ERROR）。
 */
case class AhbLite3SoCInterconnect2M(
    ahbCfg: AhbLite3Config,
    s0SramBase: Long,
    s0SramSizeBytes: Long,
    s1QspiBase: Long,
    s1QspiSizeBytes: Long,
    s2ApbBase: Long,
    s2ApbSizeBytes: Long,
    npuAllowApb: Boolean = false
) extends Component {
  val io = new Bundle {
    val m_cpu = slave(AhbLite3(ahbCfg)).setName("m_cpu")
    val m_npu = slave(AhbLite3(ahbCfg)).setName("m_npu")

    val s_sram = master(AhbLite3(ahbCfg)).setName("s_sram")
    val s_qspi = master(AhbLite3(ahbCfg)).setName("s_qspi")
    val s_apb = master(AhbLite3(ahbCfg)).setName("s_apb")
  }
  noIoPrefix()

  require(ahbCfg.addressWidth == 32, "AhbLite3SoCInterconnect2M assumes addressWidth=32")
  require(ahbCfg.dataWidth == 32, "AhbLite3SoCInterconnect2M assumes dataWidth=32")
  require(s0SramSizeBytes > 0 && s1QspiSizeBytes > 0 && s2ApbSizeBytes > 0, "sizeBytes must be > 0")

  object Sel extends SpinalEnum {
    val DEFAULT, SRAM, QSPI, APB = newElement()
  }

  private def hitRange(addr: UInt, base: Long, sizeBytes: Long): Bool = {
    val base_u = U(base, 32 bits)
    val end_u = U(base + sizeBytes, 32 bits)
    (addr >= base_u) && (addr < end_u)
  }

  // --------------------------------------------------------------------------
  // 1) 仲裁：只在“总线完全空闲”时切换 grant（固定优先级：CPU > NPU）
  // --------------------------------------------------------------------------
  private val grant_cpu_reg = RegInit(True) // True->CPU, False->NPU

  private val cpu_req = io.m_cpu.HSEL && io.m_cpu.HTRANS(1)
  private val npu_req = io.m_npu.HSEL && io.m_npu.HTRANS(1)

  // 当前被选中的 master 信号
  private val cur_hsel = grant_cpu_reg ? io.m_cpu.HSEL | io.m_npu.HSEL
  private val cur_haddr = grant_cpu_reg ? io.m_cpu.HADDR | io.m_npu.HADDR
  private val cur_hwrite = grant_cpu_reg ? io.m_cpu.HWRITE | io.m_npu.HWRITE
  private val cur_hsize = grant_cpu_reg ? io.m_cpu.HSIZE | io.m_npu.HSIZE
  private val cur_hburst = grant_cpu_reg ? io.m_cpu.HBURST | io.m_npu.HBURST
  private val cur_hprot = grant_cpu_reg ? io.m_cpu.HPROT | io.m_npu.HPROT
  private val cur_htrans = grant_cpu_reg ? io.m_cpu.HTRANS | io.m_npu.HTRANS
  private val cur_hmastlock = grant_cpu_reg ? io.m_cpu.HMASTLOCK | io.m_npu.HMASTLOCK
  private val cur_hwdata = grant_cpu_reg ? io.m_cpu.HWDATA | io.m_npu.HWDATA

  private val cur_addr_phase_valid = cur_hsel && cur_htrans(1)

  // --------------------------------------------------------------------------
  // 2) 地址译码：当前周期（地址相位）选 slave；并在 HREADYOUT=1 时锁存到数据相位
  // --------------------------------------------------------------------------
  private val sel_addr = Sel()
  sel_addr := Sel.DEFAULT
  when(hitRange(cur_haddr, s0SramBase, s0SramSizeBytes)) {
    sel_addr := Sel.SRAM
  } elsewhen (hitRange(cur_haddr, s1QspiBase, s1QspiSizeBytes)) {
    sel_addr := Sel.QSPI
  } elsewhen (hitRange(cur_haddr, s2ApbBase, s2ApbSizeBytes)) {
    sel_addr := Sel.APB
  }

  // NPU 默认不允许访问 APB window
  private val sel_addr_sanitized = Sel()
  sel_addr_sanitized := sel_addr
  if (!npuAllowApb) {
    when(!grant_cpu_reg && (sel_addr === Sel.APB)) {
      sel_addr_sanitized := Sel.DEFAULT
    }
  }

  private val sel_data_reg = Reg(Sel()) init (Sel.DEFAULT)
  private val data_valid_reg = RegInit(False) // 当前周期是否处于某个有效 transfer 的数据相位

  // --------------------------------------------------------------------------
  // 3) slave side 驱动：只用 HSEL 区分目标，其他信号广播（降低 mux 深度）
  // --------------------------------------------------------------------------
  io.s_sram.HADDR := cur_haddr
  io.s_sram.HWRITE := cur_hwrite
  io.s_sram.HSIZE := cur_hsize
  io.s_sram.HBURST := cur_hburst
  io.s_sram.HPROT := cur_hprot
  io.s_sram.HTRANS := cur_htrans
  io.s_sram.HMASTLOCK := cur_hmastlock
  io.s_sram.HWDATA := cur_hwdata

  io.s_qspi.HADDR := cur_haddr
  io.s_qspi.HWRITE := cur_hwrite
  io.s_qspi.HSIZE := cur_hsize
  io.s_qspi.HBURST := cur_hburst
  io.s_qspi.HPROT := cur_hprot
  io.s_qspi.HTRANS := cur_htrans
  io.s_qspi.HMASTLOCK := cur_hmastlock
  io.s_qspi.HWDATA := cur_hwdata

  io.s_apb.HADDR := cur_haddr
  io.s_apb.HWRITE := cur_hwrite
  io.s_apb.HSIZE := cur_hsize
  io.s_apb.HBURST := cur_hburst
  io.s_apb.HPROT := cur_hprot
  io.s_apb.HTRANS := cur_htrans
  io.s_apb.HMASTLOCK := cur_hmastlock
  io.s_apb.HWDATA := cur_hwdata

  io.s_sram.HSEL := cur_hsel && (sel_addr_sanitized === Sel.SRAM)
  io.s_qspi.HSEL := cur_hsel && (sel_addr_sanitized === Sel.QSPI)
  io.s_apb.HSEL := cur_hsel && (sel_addr_sanitized === Sel.APB)

  // HREADY input：按 AHB 语义，应为“当前数据相位的 ready”（来自 sel_data_reg 对应 slave 的 HREADYOUT）
  private val bus_hrdata = Bits(32 bits)
  private val bus_hresp = Bool()
  private val bus_hreadyout = Bool()

  bus_hrdata := B(0, 32 bits)
  bus_hresp := True // default: ERROR
  bus_hreadyout := True
  switch(sel_data_reg) {
    is(Sel.SRAM) {
      bus_hrdata := io.s_sram.HRDATA
      bus_hresp := io.s_sram.HRESP
      bus_hreadyout := io.s_sram.HREADYOUT
    }
    is(Sel.QSPI) {
      bus_hrdata := io.s_qspi.HRDATA
      bus_hresp := io.s_qspi.HRESP
      bus_hreadyout := io.s_qspi.HREADYOUT
    }
    is(Sel.APB) {
      bus_hrdata := io.s_apb.HRDATA
      bus_hresp := io.s_apb.HRESP
      bus_hreadyout := io.s_apb.HREADYOUT
    }
    default {
      bus_hrdata := B(0, 32 bits)
      bus_hresp := True
      bus_hreadyout := True
    }
  }

  io.s_sram.HREADY := bus_hreadyout
  io.s_qspi.HREADY := bus_hreadyout
  io.s_apb.HREADY := bus_hreadyout

  // 锁存数据相位的 slave 选择（仅在数据相位完成时更新）
  when(bus_hreadyout) {
    sel_data_reg := sel_addr_sanitized
    data_valid_reg := cur_addr_phase_valid
  }

  // 总线空闲：无数据相位 + 当前周期地址相位不发起传输
  private val bus_idle = !data_valid_reg && !cur_addr_phase_valid

  when(bus_idle) {
    when(cpu_req) {
      grant_cpu_reg := True
    } elsewhen (npu_req) {
      grant_cpu_reg := False
    }
  }

  // --------------------------------------------------------------------------
  // 4) master side 返回：非 granted master 仅在“没有请求”时给 ready=1，否则 ready=0 进行反压
  // --------------------------------------------------------------------------
  private val cpu_granted = grant_cpu_reg
  private val npu_granted = !grant_cpu_reg

  io.m_cpu.HRDATA := cpu_granted ? bus_hrdata | B(0, 32 bits)
  io.m_cpu.HRESP := cpu_granted ? bus_hresp | False
  // 非授予 master：当它不发起请求时保持 ready=1（避免把内部状态机“冻住”），
  // 当它发起请求但没拿到 grant 时 ready=0（反压）。
  io.m_cpu.HREADYOUT := cpu_granted ? bus_hreadyout | !cpu_req

  io.m_npu.HRDATA := npu_granted ? bus_hrdata | B(0, 32 bits)
  io.m_npu.HRESP := npu_granted ? bus_hresp | False
  // 非授予 master：同上
  io.m_npu.HREADYOUT := npu_granted ? bus_hreadyout | !npu_req
}
