#include "ah_hal.h"
#include "ah_scs.h"

#include <string.h>

static uint32_t g_now_ms;
static uint8_t g_response[AH_SCS_STATUS_SIZE];
static size_t g_response_len;
static int16_t g_pos[AH_SCS_SERVO_COUNT + 1];
static int16_t g_load[AH_SCS_SERVO_COUNT + 1];
static int8_t g_temp[AH_SCS_SERVO_COUNT + 1];
static uint8_t g_error[AH_SCS_SERVO_COUNT + 1];
static uint8_t g_online[AH_SCS_SERVO_COUNT + 1];
static uint8_t g_force_timeout;
static uint8_t g_uart_rx[1024];
static size_t g_uart_rx_len;
static uint8_t g_uart_tx[4096];
static size_t g_uart_tx_len;

static uint8_t checksum(uint8_t id, uint8_t len, const uint8_t *body, size_t n)
{
    uint16_t sum = (uint16_t)id + (uint16_t)len;
    size_t i;
    for (i = 0; i < n; ++i) {
        sum += body[i];
    }
    return (uint8_t)(~sum & 0xFFu);
}

static void wr_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

void fake_hal_reset(void)
{
    g_now_ms = 0u;
    g_response_len = 0u;
    g_force_timeout = 0u;
    g_uart_rx_len = 0u;
    g_uart_tx_len = 0u;
    memset(g_pos, 0, sizeof(g_pos));
    memset(g_load, 0, sizeof(g_load));
    memset(g_temp, 30, sizeof(g_temp));
    memset(g_error, 0, sizeof(g_error));
    memset(g_online, 1, sizeof(g_online));
}

void fake_hal_set_time(uint32_t ms)
{
    g_now_ms = ms;
}

void fake_hal_set_timeout(uint8_t on)
{
    g_force_timeout = on;
}

void fake_hal_set_servo(uint8_t id, int16_t pos, int16_t load, int8_t temp, uint8_t err, uint8_t online)
{
    if (id >= 1u && id <= AH_SCS_SERVO_COUNT) {
        g_pos[id] = pos;
        g_load[id] = load;
        g_temp[id] = temp;
        g_error[id] = err;
        g_online[id] = online;
    }
}

void fake_hal_uart_push(const uint8_t *data, size_t len)
{
    if (data != NULL && g_uart_rx_len + len <= sizeof(g_uart_rx)) {
        memcpy(&g_uart_rx[g_uart_rx_len], data, len);
        g_uart_rx_len += len;
    }
}

size_t fake_hal_uart_tx_read(uint8_t *data, size_t cap)
{
    size_t n = g_uart_tx_len;
    if (data == NULL || cap < n) {
        return 0u;
    }
    memcpy(data, g_uart_tx, n);
    g_uart_tx_len = 0u;
    return n;
}

uint32_t ah_hal_now_ms(void)
{
    return g_now_ms;
}

uint32_t ah_hal_now_us(void)
{
    return g_now_ms * 1000u;
}

bool ah_hal_bus_write(const uint8_t *data, size_t len)
{
    uint8_t id;
    uint8_t body[9];
    if (data == NULL || len < 8u) {
        return false;
    }
    if (data[4] == 0x02u) { /* READ */
        id = data[2];
        if (id < 1u || id > AH_SCS_SERVO_COUNT || g_force_timeout) {
            g_response_len = 0u;
            return true;
        }
        g_response[0] = 0xFFu;
        g_response[1] = 0xFFu;
        g_response[2] = id;
        g_response[3] = 10u;
        g_response[4] = g_error[id];
        wr_u16(&g_response[5], (uint16_t)g_pos[id]);
        wr_u16(&g_response[7], 0u);
        wr_u16(&g_response[9], (uint16_t)g_load[id]);
        g_response[11] = 120u;
        g_response[12] = (uint8_t)g_temp[id];
        memcpy(body, &g_response[4], 9u);
        g_response[13] = checksum(id, 10u, body, 9u);
        g_response_len = AH_SCS_STATUS_SIZE;
    }
    (void)g_online;
    return true;
}

size_t ah_hal_bus_read(uint8_t *data, size_t cap, uint32_t timeout_us)
{
    (void)timeout_us;
    if (data == NULL || g_response_len == 0u || cap < g_response_len) {
        return 0u;
    }
    memcpy(data, g_response, g_response_len);
    g_response_len = 0u;
    return AH_SCS_STATUS_SIZE;
}

size_t ah_hal_uart_read(uint8_t *data, size_t cap)
{
    size_t n = g_uart_rx_len;
    if (data == NULL || cap < n) {
        return 0u;
    }
    memcpy(data, g_uart_rx, n);
    g_uart_rx_len = 0u;
    return n;
}

bool ah_hal_uart_write(const uint8_t *data, size_t len)
{
    if (data == NULL || g_uart_tx_len + len > sizeof(g_uart_tx)) {
        return false;
    }
    memcpy(&g_uart_tx[g_uart_tx_len], data, len);
    g_uart_tx_len += len;
    return true;
}

void ah_hal_uart_init(void)
{
}

void ah_hal_watchdog_init(void)
{
}

void ah_hal_watchdog_feed(void)
{
}
