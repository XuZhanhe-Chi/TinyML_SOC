#!/usr/bin/env python3
"""Patch generated VenusCoreRVTop RTL for the GW5A board flow.

The source generator is kept vendor-neutral.  This script applies the small
Gowin-facing edits that are awkward to express without changing vendored
VexRiscv code:

* add SRAM boot initmem defaults;
* tie off VexRiscv ports that are unused in the selected CPU configuration.
* add optional simulation-only QSPI AHB response ports.
"""

from __future__ import annotations

import argparse
import re
from pathlib import Path


SRAM_MARKER_BEGIN = "  // GW5A SRAM boot initialization: begin\n"
SRAM_MARKER_END = "  // GW5A SRAM boot initialization: end\n"
VEX_TIEOFF_MARKER_BEGIN = "  // GW5A VexRiscv unused port tie-offs: begin\n"
VEX_TIEOFF_MARKER_END = "  // GW5A VexRiscv unused port tie-offs: end\n"
ICACHE_TIEOFF_MARKER_BEGIN = "  // GW5A InstructionCache unused decode output tie-offs: begin\n"
ICACHE_TIEOFF_MARKER_END = "  // GW5A InstructionCache unused decode output tie-offs: end\n"
SIM_QSPI_MARKER_BEGIN = "  // Simulation-only fast QSPI AHB response: begin\n"
SIM_QSPI_MARKER_END = "  // Simulation-only fast QSPI AHB response: end\n"

INIT_BLOCK = f"""{SRAM_MARKER_BEGIN}`ifndef GW5A_SRAM_INIT_MEMB0
`define GW5A_SRAM_INIT_MEMB0 "build/fw/kws_xip_rt_boot_0.memb"
`endif
`ifndef GW5A_SRAM_INIT_MEMB1
`define GW5A_SRAM_INIT_MEMB1 "build/fw/kws_xip_rt_boot_1.memb"
`endif
`ifndef GW5A_SRAM_INIT_MEMB2
`define GW5A_SRAM_INIT_MEMB2 "build/fw/kws_xip_rt_boot_2.memb"
`endif
`ifndef GW5A_SRAM_INIT_MEMB3
`define GW5A_SRAM_INIT_MEMB3 "build/fw/kws_xip_rt_boot_3.memb"
`endif

  initial begin
      $readmemb(`GW5A_SRAM_INIT_MEMB0, ram_symbol0);
      $readmemb(`GW5A_SRAM_INIT_MEMB1, ram_symbol1);
      $readmemb(`GW5A_SRAM_INIT_MEMB2, ram_symbol2);
      $readmemb(`GW5A_SRAM_INIT_MEMB3, ram_symbol3);
  end
{SRAM_MARKER_END}"""

VEX_TIEOFF_BLOCK = f"""{VEX_TIEOFF_MARKER_BEGIN}  assign IBusCachedPlugin_mmuBus_rsp_bypassTranslation = 1'b0;
  assign IBusCachedPlugin_iBusRsp_output_payload_rsp_error = 1'b0;
  assign IBusCachedPlugin_iBusRsp_output_payload_isRvc = 1'b0;
{VEX_TIEOFF_MARKER_END}"""

ICACHE_TIEOFF_BLOCK = f"""{ICACHE_TIEOFF_MARKER_BEGIN}  assign io_cpu_decode_physicalAddress = 32'h00000000;
  assign io_cpu_decode_data = 32'h00000000;
{ICACHE_TIEOFF_MARKER_END}"""


def require_replace(text: str, old: str, new: str, description: str) -> tuple[str, bool]:
    if old in text:
        return text.replace(old, new, 1), True
    if new in text:
        return text, False
    raise SystemExit(f"cannot find {description}")


def patch_sram_init(text: str, path: Path) -> tuple[str, bool]:
    if SRAM_MARKER_BEGIN in text:
        return text, False

    match = re.search(
        r"(  \(\* syn_ramstyle = \"block_ram\" \*\) reg \[7:0\] ram_symbol0 \[0:16383\];\n"
        r"  \(\* syn_ramstyle = \"block_ram\" \*\) reg \[7:0\] ram_symbol1 \[0:16383\];\n"
        r"  \(\* syn_ramstyle = \"block_ram\" \*\) reg \[7:0\] ram_symbol2 \[0:16383\];\n"
        r"  \(\* syn_ramstyle = \"block_ram\" \*\) reg \[7:0\] ram_symbol3 \[0:16383\];\n)",
        text,
    )
    if not match:
        raise SystemExit(f"cannot find SRAM anchor in {path}")

    idx = match.end()
    return text[:idx] + INIT_BLOCK + text[idx:], True


def patch_vexriscv_tieoffs(text: str, path: Path) -> tuple[str, bool]:
    changed = False

    replacements = [
        (
            "    .dBus_rsp_error                (                                              ), //i",
            "    .dBus_rsp_error                (1'b0                                          ), //i",
            "VexRiscv dBus_rsp_error port",
        ),
        (
            "    .io_cpu_fetch_isRemoved                (                                                          ), //i",
            "    .io_cpu_fetch_isRemoved                (1'b0                                                      ), //i",
            "InstructionCache io_cpu_fetch_isRemoved port",
        ),
        (
            "    .io_cpu_decode_isValid                 (                                                          ), //i",
            "    .io_cpu_decode_isValid                 (1'b0                                                      ), //i",
            "InstructionCache io_cpu_decode_isValid port",
        ),
        (
            "    .io_cpu_decode_isStuck                 (                                                          ), //i",
            "    .io_cpu_decode_isStuck                 (1'b0                                                      ), //i",
            "InstructionCache io_cpu_decode_isStuck port",
        ),
        (
            "    .io_cpu_decode_pc                      (                                                          ), //i",
            "    .io_cpu_decode_pc                      (32'h00000000                                              ), //i",
            "InstructionCache io_cpu_decode_pc port",
        ),
    ]
    for old, new, description in replacements:
        text, did_change = require_replace(text, old, new, description)
        changed = changed or did_change

    if VEX_TIEOFF_MARKER_BEGIN not in text:
        driven = [
            name
            for name in (
                "IBusCachedPlugin_mmuBus_rsp_bypassTranslation",
                "IBusCachedPlugin_iBusRsp_output_payload_rsp_error",
                "IBusCachedPlugin_iBusRsp_output_payload_isRvc",
            )
            if re.search(rf"\bassign\s+{name}\s*=", text)
        ]
        if driven:
            raise SystemExit(f"unexpected existing VexRiscv tie-off assignment in {path}: {', '.join(driven)}")

        anchor = "  reg [31:0] RegFilePlugin_regFile [0:31] /* verilator public */ ;\n\n"
        if anchor not in text:
            raise SystemExit(f"cannot find VexRiscv tie-off anchor in {path}")
        text = text.replace(anchor, anchor + VEX_TIEOFF_BLOCK, 1)
        changed = True

    if ICACHE_TIEOFF_MARKER_BEGIN not in text:
        driven = [
            name
            for name in ("io_cpu_decode_physicalAddress", "io_cpu_decode_data")
            if re.search(rf"\bassign\s+{name}\s*=", text)
        ]
        if driven:
            raise SystemExit(f"unexpected existing InstructionCache tie-off assignment in {path}: {', '.join(driven)}")

        anchor = "  reg [31:0] banks_0 [0:1023];\n  reg [21:0] ways_0_tags [0:127];\n\n"
        if anchor not in text:
            raise SystemExit(f"cannot find InstructionCache tie-off anchor in {path}")
        text = text.replace(anchor, anchor + ICACHE_TIEOFF_BLOCK, 1)
        changed = True

    return text, changed


def patch_sim_qspi_ports(text: str, path: Path) -> tuple[str, bool]:
    if SIM_QSPI_MARKER_BEGIN in text:
        return text, False

    port_anchor = "module VenusCoreRVTop (\n"
    sim_ports = """module VenusCoreRVTop (
`ifdef TINYML_SOC_SIM_FAST_QSPI
  input  wire [31:0]   sim_qspi_hrdata,
  input  wire          sim_qspi_hreadyout,
  input  wire          sim_qspi_hresp,
`endif
"""
    if port_anchor not in text:
        raise SystemExit(f"cannot find top-level port anchor in {path}")
    text = text.replace(port_anchor, sim_ports, 1)

    assignments = """  assign s1_qspi_HRDATA = qspi_xip_ahb_HRDATA;
  assign s1_qspi_HREADYOUT = qspi_xip_ahb_HREADYOUT;
  assign s1_qspi_HRESP = qspi_xip_ahb_HRESP;
"""
    sim_assignments = f"""{SIM_QSPI_MARKER_BEGIN}`ifdef TINYML_SOC_SIM_FAST_QSPI
  assign s1_qspi_HRDATA = sim_qspi_hrdata;
  assign s1_qspi_HREADYOUT = sim_qspi_hreadyout;
  assign s1_qspi_HRESP = sim_qspi_hresp;
`else
  assign s1_qspi_HRDATA = qspi_xip_ahb_HRDATA;
  assign s1_qspi_HREADYOUT = qspi_xip_ahb_HREADYOUT;
  assign s1_qspi_HRESP = qspi_xip_ahb_HRESP;
`endif
{SIM_QSPI_MARKER_END}"""
    if assignments not in text:
        raise SystemExit(f"cannot find QSPI AHB assignment anchor in {path}")
    return text.replace(assignments, sim_assignments, 1), True


def patch_rtl(path: Path) -> bool:
    text = path.read_text(encoding="utf-8")
    changed = False

    text, did_change = patch_sram_init(text, path)
    changed = changed or did_change

    text, did_change = patch_vexriscv_tieoffs(text, path)
    changed = changed or did_change

    text, did_change = patch_sim_qspi_ports(text, path)
    changed = changed or did_change

    if changed:
        path.write_text(text, encoding="utf-8")
    return changed


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("verilog", type=Path)
    args = ap.parse_args()
    changed = patch_rtl(args.verilog)
    print(f"[OK] {'patched' if changed else 'already patched'} GW5A RTL post-processing: {args.verilog}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
