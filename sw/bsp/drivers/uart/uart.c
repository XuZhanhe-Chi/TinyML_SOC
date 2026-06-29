#include "uart.h"

enum {
  UART_REG_DATA = 0x00,
  UART_REG_STATUS = 0x04,
  UART_REG_CLKDIV = 0x08,
  UART_REG_FRAME = 0x0C,
};

static inline volatile uint32_t *uart_reg(uart_t uart, uint32_t off) {
  return (volatile uint32_t *)(uart.base + off);
}

void uart_init(uart_t uart, uint32_t clkdiv, uint32_t frame) {
  *uart_reg(uart, UART_REG_CLKDIV) = clkdiv;
  *uart_reg(uart, UART_REG_FRAME) = frame;
}

bool uart_tx_has_space(uart_t uart) {
  uint32_t st = *uart_reg(uart, UART_REG_STATUS);
  uint32_t tx_avail = (st >> 16) & 0x1Fu;
  return tx_avail != 0u;
}

void uart_putc(uart_t uart, char c) {
  while (!uart_tx_has_space(uart)) {
  }
  *uart_reg(uart, UART_REG_DATA) = (uint32_t)(uint8_t)c;
}

void uart_write(uart_t uart, const char *s) {
  if (!s) return;
  while (*s) uart_putc(uart, *s++);
}

void uart_write_u32_dec(uart_t uart, uint32_t v) {
  char buf[11];
  int n = 0;
  if (v == 0) {
    uart_putc(uart, '0');
    return;
  }
  while (v && n < (int)sizeof(buf)) {
    buf[n++] = (char)('0' + (v % 10u));
    v /= 10u;
  }
  while (n--) uart_putc(uart, buf[n]);
}

void uart_write_i32_dec(uart_t uart, int32_t v) {
  if (v < 0) {
    uart_putc(uart, '-');
    uint32_t u = (uint32_t)(-(v + 1)) + 1u;
    uart_write_u32_dec(uart, u);
    return;
  }
  uart_write_u32_dec(uart, (uint32_t)v);
}

void uart_write_hex8(uart_t uart, uint8_t v) {
  static const char hex[] = "0123456789ABCDEF";
  uart_putc(uart, hex[(v >> 4) & 0xF]);
  uart_putc(uart, hex[v & 0xF]);
}

void uart_write_hex32(uart_t uart, uint32_t v) {
  for (int i = 28; i >= 0; i -= 4) {
    uint32_t nib = (v >> i) & 0xFu;
    uart_putc(uart, (char)(nib < 10 ? ('0' + nib) : ('A' + (nib - 10))));
  }
}

bool uart_putc_spin(uart_t uart, char c, uint32_t spin_max) {
  for (uint32_t i = 0; i < spin_max; ++i) {
    if (uart_tx_has_space(uart)) {
      *uart_reg(uart, UART_REG_DATA) = (uint32_t)(uint8_t)c;
      return true;
    }
  }
  return false;
}

void uart_write_spin(uart_t uart, const char *s, uint32_t spin_max) {
  if (!s) return;
  while (*s) {
    if (!uart_putc_spin(uart, *s++, spin_max)) {
      return;
    }
  }
}

void uart_write_u32_dec_spin(uart_t uart, uint32_t v, uint32_t spin_max) {
  char buf[11];
  int n = 0;
  if (v == 0) {
    uart_putc_spin(uart, '0', spin_max);
    return;
  }
  while (v && n < (int)sizeof(buf)) {
    buf[n++] = (char)('0' + (v % 10u));
    v /= 10u;
  }
  while (n--) {
    if (!uart_putc_spin(uart, buf[n], spin_max)) {
      return;
    }
  }
}

void uart_write_i32_dec_spin(uart_t uart, int32_t v, uint32_t spin_max) {
  if (v < 0) {
    if (!uart_putc_spin(uart, '-', spin_max)) {
      return;
    }
    uint32_t u = (uint32_t)(-(v + 1)) + 1u;
    uart_write_u32_dec_spin(uart, u, spin_max);
    return;
  }
  uart_write_u32_dec_spin(uart, (uint32_t)v, spin_max);
}
