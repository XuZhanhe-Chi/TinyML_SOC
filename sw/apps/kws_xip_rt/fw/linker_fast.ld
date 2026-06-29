ENTRY(_start)

MEMORY
{
  QSPI (rx)  : ORIGIN = 0x00100000, LENGTH = 4M
  SRAM (rwx) : ORIGIN = 0x00000000, LENGTH = 64K
}

KWS_XIP_ACT_BASE = 0x00000000;
KWS_XIP_PARAM_BASE = 0x00120000;

SECTIONS
{
  .text : {
    KEEP(*(.text._start))
    *(.text*)
    *(.rodata*)
  } > QSPI

  .venus_uops : {
    . = ALIGN(64);
    *(.venus_uops)
  } > QSPI

  .venus_params KWS_XIP_PARAM_BASE : {
    . = ALIGN(64);
    *(.venus_params)
  } > QSPI

  .venus_act KWS_XIP_ACT_BASE (NOLOAD) : {
    . = ALIGN(64);
    *(.venus_act)
    . = ALIGN(64);
  } > SRAM

  .data : {
    . = ALIGN(8);
    __data_start = .;
    __global_pointer$ = . + 0x800;
    *(.data*)
    *(.sdata*)
    . = ALIGN(8);
    __data_end = .;
  } > SRAM AT > QSPI
  __data_load_start = LOADADDR(.data);

  .bss : {
    . = ALIGN(8);
    __bss_start = .;
    *(.bss*)
    *(COMMON)
    __bss_end = .;
  } > SRAM
}
