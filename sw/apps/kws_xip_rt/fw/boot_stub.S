.section .text._start, "ax", @progbits
.globl _start
_start:
  /* SRAM boot stub: wait briefly, then jump to QSPI XIP window (0x0010_0000). */
  lui   t0, 0x00100
  lui   t1, 0x00010
1:
  addi  t1, t1, -1
  bne   t1, zero, 1b
  jalr  zero, t0, 0
  nop
  nop
  nop
