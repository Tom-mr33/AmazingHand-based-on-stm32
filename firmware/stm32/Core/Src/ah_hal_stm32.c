#include "ah_hal.h"
#include "main.h"
#include "usart.h"

#include <string.h>

extern UART_HandleTypeDef huart1; /* SCS bus, defined in main.c */
/* huart2 (Pi link) is defined in usart.c */

uint32_t ah_hal_now_ms(void)
{
    return HAL_GetTick();
}

uint32_t ah_hal_now_us(void)
{
    return HAL_GetTick() * 1000u;
}

bool ah_hal_bus_write(const uint8_t *data, size_t len)
{
    uint8_t echo;
    size_t i;
    if (data == NULL || len == 0u) {
        return false;
    }
    if (HAL_UART_Transmit(&huart1, (uint8_t *)data, (uint16_t)len, 20u) != HAL_OK) {
        return false;
    }
    for (i = 0; i < len; ++i) {
        if (HAL_UART_Receive(&huart1, &echo, 1u, 2u) != HAL_OK) {
            return false;
        }
    }
    return true;
}

size_t ah_hal_bus_read(uint8_t *data, size_t cap, uint32_t timeout_us)
{
    uint32_t timeout_ms;
    if (data == NULL || cap == 0u) {
        return 0u;
    }
    timeout_ms = (timeout_us + 999u) / 1000u;
    if (timeout_ms == 0u) {
        timeout_ms = 1u;
    }
    if (HAL_UART_Receive(&huart1, data, (uint16_t)cap, timeout_ms) != HAL_OK) {
        return 0u;
    }
    return cap;
}

size_t ah_hal_uart_read(uint8_t *data, size_t cap)
{
    if (data == NULL || cap == 0u) {
        return 0u;
    }
    if (HAL_UART_Receive(&huart2, data, (uint16_t)cap, 1u) != HAL_OK) {
        return 0u;
    }
    return cap;
}

bool ah_hal_uart_write(const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0u) {
        return false;
    }
    return HAL_UART_Transmit(&huart2, (uint8_t *)data, (uint16_t)len, 20u) == HAL_OK;
}
