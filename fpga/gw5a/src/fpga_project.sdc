//Copyright (C)2014-2025 GOWIN Semiconductor Corporation.
//All rights reserved.
//File Title: Timing Constraints file
//Tool Version: V1.9.12 (64-bit)
//Created Time: 2025-12-05 17:23:16
create_clock -name clk -period 20 -waveform {0 10} [get_ports {clk}]
create_clock -name jtag_tck -period 50 -waveform {0 25} [get_ports {jtag_tck}]
set_false_path -from [get_clocks {clk}] -to [get_clocks {jtag_tck}]
set_false_path -from [get_clocks {jtag_tck}] -to [get_clocks {clk}]
