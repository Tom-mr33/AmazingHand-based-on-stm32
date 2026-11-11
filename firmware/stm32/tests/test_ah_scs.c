#include "ah_hal.h"
#include "ah_scs.h"
#include "fake_hal.h"

#include <stdio.h>
#include <string.h>

static int expect_hex(const uint8_t *got, size_t got_len, const char *hex)
{
    static const char digits[] = "0123456789abcdef";
    size_t i;
    if (got_len * 2u != strlen(hex)) {
        return -1;
    }
    for (i = 0; i < got_len; ++i) {
        if (hex[i * 2u] != digits[(got[i] >> 4) & 0x0Fu] ||
            hex[i * 2u + 1u] != digits[got[i] & 0x0Fu]) {
            return -1;
        }
    }
    return 0;
}

int main(void)
{
    ah_scs_t scs;
    uint8_t buf[AH_SCS_SYNC_PROFILE_SIZE];
    uint8_t torque_values[AH_SCS_SERVO_COUNT];
    uint16_t speed[AH_SCS_SERVO_COUNT];
    uint16_t torque[AH_SCS_SERVO_COUNT];
    int16_t target[AH_SCS_SERVO_COUNT];
    size_t i;

    fake_hal_reset();
    for (i = 0; i < AH_SCS_SERVO_COUNT; ++i) {
        target[i] = (int16_t)(100 + (int)i * 10);
        fake_hal_set_servo((uint8_t)(i + 1u), target[i], (int16_t)(i * 5), 30, 0, 1);
    }
    if (ah_scs_build_sync_write(target, buf, sizeof(buf)) != AH_SCS_SYNC_FRAME_SIZE ||
        expect_hex(buf, AH_SCS_SYNC_FRAME_SIZE,
                   "fffffe1c832a02016400026e00037800048200058c0006960007a00008aa00da") != 0) {
        fprintf(stderr, "fail line %d\n", __LINE__); return 1;
    }
    if (ah_scs_build_read(1, buf, sizeof(buf)) != AH_SCS_READ_FRAME_SIZE ||
        expect_hex(buf, AH_SCS_READ_FRAME_SIZE, "ffff0104023a08b6") != 0) {
        fprintf(stderr, "fail line %d\n", __LINE__); return 1;
    }
    if (ah_scs_build_sync_torque(0u, buf, sizeof(buf)) != AH_SCS_SYNC_TORQUE_SIZE ||
        expect_hex(buf, AH_SCS_SYNC_TORQUE_SIZE,
                   "fffffe14832801010202020302040205020602070208020d") != 0) {
        fprintf(stderr, "fail line %d\n", __LINE__); return 1;
    }
    for (i = 0; i < AH_SCS_SERVO_COUNT; ++i) {
        speed[i] = 100u;
        torque[i] = 500u;
        torque_values[i] = 2u;
    }
    (void)torque_values;
    if (ah_scs_build_sync_profile(speed, torque, buf, sizeof(buf)) != AH_SCS_SYNC_PROFILE_SIZE ||
        expect_hex(buf, AH_SCS_SYNC_PROFILE_SIZE,
                   "fffffe2c832e04016400f401026400f401036400f401046400f401056400f401066400f401076400f401086400f40134") != 0) {
        fprintf(stderr, "fail line %d\n", __LINE__); return 1;
    }

    ah_scs_init(&scs);
    ah_scs_set_targets(&scs, target);
    ah_scs_tick(&scs, 0u);  /* slot 0: SYNC_WRITE */
    ah_scs_tick(&scs, 2u);  /* slot 1: read 1..4 */
    ah_scs_tick(&scs, 4u);  /* slot 2: read 5..8 */
    for (i = 0; i < AH_SCS_SERVO_COUNT; ++i) {
        if (scs.fb[i].valid != 1u || scs.fb[i].online != 1u || scs.fb[i].raw != target[i]) {
            fprintf(stderr, "servo %u raw=%d valid=%u online=%u\n",
                    (unsigned)(i + 1u), scs.fb[i].raw, scs.fb[i].valid, scs.fb[i].online);
            fprintf(stderr, "fail line %d\n", __LINE__); return 1;
        }
    }
    if (scs.write_count != 3u || scs.read_count != AH_SCS_SERVO_COUNT) {
        fprintf(stderr, "fail line %d\n", __LINE__); return 1;
    }

    /* timeout keeps last-good state but marks invalid/offline; age clears online */
    fake_hal_set_timeout(1u);
    fake_hal_set_time(200u);
    scs.slot = 1u;
    ah_scs_tick(&scs, 200u);
    if (scs.fb[0].valid != 0u || scs.fb[0].online != 0u) {
        fprintf(stderr, "fail line %d\n", __LINE__); return 1;
    }
    if (scs.timeout_count == 0u) {
        fprintf(stderr, "fail line %d\n", __LINE__); return 1;
    }

    printf("AH_SCS_TEST_OK\n");
    return 0;
}
