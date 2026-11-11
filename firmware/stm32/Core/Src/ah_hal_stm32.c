#include "ah_hal.h"
#include "main.h"
#include "usart.h"

#include <string.h>

extern UART_HandleTypeDef huart1; /* SCS bus, defined in main.c */
/* huart2 (Pi link) is defined in usart.c */

#define AH_UART_RX_RING 256u
static volatile uint8_t g_rx_ring[AH_UART_RX_RING];
static volatile uint16_t g_rx_head;
static volatile uint16_t g_rx_tail;
static volatile uint32_t g_rx_overflow;
static uint8_t g_rx_byte;

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

void ah_hal_uart_init(void)
{
    g_rx_head = 0u;
    g_rx_tail = 0u;
    g_rx_overflow = 0u;
    HAL_NVIC_SetPriority(USART2_IRQn, 1u, 0u);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
    (void)HAL_UART_Receive_IT(&huart2, &g_rx_byte, 1u);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    uint16_t next;
    if (huart != &huart2) {
        return;
    }
    next = (uint16_t)((g_rx_head + 1u) % AH_UART_RX_RING);
    if (next != g_rx_tail) {
        g_rx_ring[g_rx_head] = g_rx_byte;
        g_rx_head = next;
    } else {
        g_rx_overflow++;
    }
    (void)HAL_UART_Receive_IT(&huart2, &g_rx_byte, 1u);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart == &huart2) {
        (void)HAL_UART_Receive_IT(&huart2, &g_rx_byte, 1u);
    }
}

size_t ah_hal_uart_read(uint8_t *data, size_t cap)
{
    size_t n = 0u;
    if (data == NULL || cap == 0u) {
        return 0u;
    }
    while (n < cap && g_rx_tail != g_rx_head) {
        data[n++] = g_rx_ring[g_rx_tail];
        g_rx_tail = (uint16_t)((g_rx_tail + 1u) % AH_UART_RX_RING);
    }
    return n;
}

bool ah_hal_uart_write(const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0u) {
        return false;
    }
    return HAL_UART_Transmit(&huart2, (uint8_t *)data, (uint16_t)len, 20u) == HAL_OK;
}

void ah_hal_watchdog_init(void)
{
    /* Direct IWDG registers: LSI 40 kHz / 64 * 312 ~= 500 ms. */
    IWDG->KR = 0xCCCCu;  /* start */
    IWDG->KR = 0x5555u;  /* unlock PR/RLR */
    IWDG->PR = 4u;       /* /64 */
    IWDG->RLR = 312u;
    while (IWDG->SR != 0u) {
    }
    IWDG->KR = 0xAAAAu;  /* reload */
}

void ah_hal_watchdog_feed(void)
{
    IWDG->KR = 0xAAAAu;
}
