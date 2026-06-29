#ifndef SW_VEXRISCV_BSP_DRIVERS_UART_UART_H_
#define SW_VEXRISCV_BSP_DRIVERS_UART_UART_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uintptr_t base;
} uart_t;

static inline uart_t uart_from_base(uintptr_t base) { return (uart_t){.base = base}; }

void uart_init(uart_t uart, uint32_t clkdiv, uint32_t frame);
bool uart_tx_has_space(uart_t uart);
void uart_putc(uart_t uart, char c);
void uart_write(uart_t uart, const char *s);
void uart_write_u32_dec(uart_t uart, uint32_t v);
void uart_write_i32_dec(uart_t uart, int32_t v);
void uart_write_hex32(uart_t uart, uint32_t v);
void uart_write_hex8(uart_t uart, uint8_t v);
bool uart_putc_spin(uart_t uart, char c, uint32_t spin_max);
void uart_write_spin(uart_t uart, const char *s, uint32_t spin_max);
void uart_write_u32_dec_spin(uart_t uart, uint32_t v, uint32_t spin_max);
void uart_write_i32_dec_spin(uart_t uart, int32_t v, uint32_t spin_max);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // SW_VEXRISCV_BSP_DRIVERS_UART_UART_H_
