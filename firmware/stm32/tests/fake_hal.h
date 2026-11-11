#ifndef FAKE_HAL_H
#define FAKE_HAL_H

#include <stddef.h>
#include <stdint.h>

void fake_hal_reset(void);
void fake_hal_set_time(uint32_t ms);
void fake_hal_set_timeout(uint8_t on);
void fake_hal_set_servo(uint8_t id, int16_t pos, int16_t load, int8_t temp,
                        uint8_t err, uint8_t online);
void fake_hal_uart_push(const uint8_t *data, size_t len);
size_t fake_hal_uart_tx_read(uint8_t *data, size_t cap);

#endif /* FAKE_HAL_H */
