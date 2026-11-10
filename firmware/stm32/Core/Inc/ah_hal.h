#ifndef AH_HAL_H
#define AH_HAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Time base. Implemented by board layer or host fake. */
uint32_t ah_hal_now_ms(void);
uint32_t ah_hal_now_us(void);

/* SCS half-duplex bus. write drains its own echo on real hardware. */
bool ah_hal_bus_write(const uint8_t *data, size_t len);
size_t ah_hal_bus_read(uint8_t *data, size_t cap, uint32_t timeout_us);

/* Pi link UART. */
size_t ah_hal_uart_read(uint8_t *data, size_t cap);
bool ah_hal_uart_write(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* AH_HAL_H */
