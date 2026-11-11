#include "ah_device.h"
#include "ah_hal.h"
#include "ah_scs.h"
#include "fake_hal.h"

#include <stdio.h>

static void tick_all(ah_scs_t *scs, uint32_t t)
{
    ah_scs_tick(scs, t);
    ah_scs_tick(scs, t + 2u);
    ah_scs_tick(scs, t + 4u);
}

int main(void)
{
    ah_scs_t scs;
    ah_device_t dev;
    ah_device_config_t cfg;
    ah_v2_execution_t ex;
    ah_v2_health_t health;
    ah_v2_actual_t actual;
    int16_t targets[AH_DEV_SERVO_COUNT];
    size_t i;

    fake_hal_reset();
    for (i = 0; i < AH_SCS_SERVO_COUNT; ++i) {
        fake_hal_set_servo((uint8_t)(i + 1u), 2048, 0, 30, 0, 1);
    }
    ah_scs_init(&scs);
    for (i = 0; i < AH_SCS_SERVO_COUNT; ++i) {
        targets[i] = 2048;
    }
    ah_scs_set_targets(&scs, targets);
    tick_all(&scs, 0u);

    ah_device_config_default(&cfg);
    cfg.pregrasp_ms = 10u;
    cfg.closing_timeout_ms = 100u;
    cfg.release_ms = 10u;
    cfg.target_timeout_ms = 200u;
    cfg.link_timeout_ms = 1000u;
    ah_device_init(&dev, &cfg);
    ah_device_note_link(&dev, 4u);

    ex.mode = AH_V2_MODE_GRASP;
    for (i = 0; i < AH_DEV_SERVO_COUNT; ++i) {
        ex.target[i] = 1000;
        ex.close[i] = 2000;
        ex.load_threshold[i] = 300u;
        ex.speed[i] = 100u;
        ex.torque_limit[i] = 500u;
    }
    ex.target_timeout_ms = 200u;
    if (ah_device_apply_execution(&dev, &ex, 4u) != 0) { fprintf(stderr, "fail line %d\n", __LINE__); return 1; }
    ah_device_step(&dev, &scs, 4u);
    if (dev.phase != AH_V2_PHASE_PREGRASP) { fprintf(stderr, "fail line %d\n", __LINE__); return 1; }

    /* pregrasp -> closing */
    ah_device_note_link(&dev, 14u);
    tick_all(&scs, 12u);
    ah_device_step(&dev, &scs, 16u);
    if (dev.phase != AH_V2_PHASE_CLOSING) { fprintf(stderr, "fail line %d\n", __LINE__); return 1; }

    /* load contact -> hold with latched actual */
    for (i = 0; i < AH_SCS_SERVO_COUNT; ++i) {
        fake_hal_set_servo((uint8_t)(i + 1u), 1800, 400, 30, 0, 1);
    }
    ah_device_note_link(&dev, 18u);
    tick_all(&scs, 18u);
    ah_device_step(&dev, &scs, 22u);
    if (dev.phase != AH_V2_PHASE_HOLD) { fprintf(stderr, "fail line %d\n", __LINE__); return 1; }
    ah_device_get_targets(&dev, targets);
    if (targets[0] != 1800) { fprintf(stderr, "fail line %d\n", __LINE__); return 1; }
    ah_device_get_health(&dev, &health);
    if (health.phase != AH_V2_PHASE_HOLD) { fprintf(stderr, "fail line %d\n", __LINE__); return 1; }

    /* target watchdog -> fault */
    ah_device_step(&dev, &scs, 300u);
    if (dev.phase != AH_V2_PHASE_FAULT || dev.stop_reason != AH_V2_STOP_TARGET_TIMEOUT) {
        { fprintf(stderr, "fail line %d\n", __LINE__); return 1; }
    }
    ah_device_get_health(&dev, &health);
    if ((health.fault_bits & AH_V2_FAULT_WATCHDOG) == 0u) { fprintf(stderr, "fail line %d\n", __LINE__); return 1; }

    /* reset from fault, then stop/reset */
    if (ah_device_reset(&dev) != 0 || dev.phase != AH_V2_PHASE_IDLE) { fprintf(stderr, "fail line %d\n", __LINE__); return 1; }
    if (ah_device_apply_execution(&dev, &ex, 400u) != 0) { fprintf(stderr, "fail line %d\n", __LINE__); return 1; }
    ah_device_stop(&dev, AH_V2_STOP_USER, 400u);
    if (dev.phase != AH_V2_PHASE_FAULT || dev.torque_enabled != 0u) { fprintf(stderr, "fail line %d\n", __LINE__); return 1; }
    if (ah_device_apply_execution(&dev, &ex, 401u) == 0) { fprintf(stderr, "fail line %d\n", __LINE__); return 1; }
    if (ah_device_reset(&dev) != 0) { fprintf(stderr, "fail line %d\n", __LINE__); return 1; }

    /* estop latch */
    ah_device_set_estop(&dev, 1u, 500u);
    if (dev.phase != AH_V2_PHASE_FAULT || dev.stop_reason != AH_V2_STOP_ESTOP) { fprintf(stderr, "fail line %d\n", __LINE__); return 1; }
    if (ah_device_reset(&dev) == 0) { fprintf(stderr, "fail line %d\n", __LINE__); return 1; }
    ah_device_set_estop(&dev, 0u, 501u);
    if (ah_device_reset(&dev) != 0) { fprintf(stderr, "fail line %d\n", __LINE__); return 1; }

    /* link watchdog (only active phases) */
    dev.cfg.link_timeout_ms = 100u;
    if (ah_device_apply_execution(&dev, &ex, 600u) != 0) { fprintf(stderr, "fail line %d\n", __LINE__); return 1; }
    ah_device_note_link(&dev, 600u);
    ah_device_step(&dev, &scs, 800u);
    if (dev.phase != AH_V2_PHASE_FAULT || dev.stop_reason != AH_V2_STOP_LINK_TIMEOUT) { fprintf(stderr, "fail line %d\n", __LINE__); return 1; }

    /* overtemp -> FAULT/OVERTEMP */
    ah_device_reset(&dev);
    dev.cfg.link_timeout_ms = 1000u;
    for (i = 0; i < AH_SCS_SERVO_COUNT; ++i) {
        fake_hal_set_servo((uint8_t)(i + 1u), 1800, 0, 80, 0, 1);
    }
    ah_device_note_link(&dev, 900u);
    tick_all(&scs, 900u);
    if (ah_device_apply_execution(&dev, &ex, 902u) != 0) { fprintf(stderr, "fail line %d\n", __LINE__); return 1; }
    ah_device_step(&dev, &scs, 906u);
    if (dev.phase != AH_V2_PHASE_FAULT || dev.stop_reason != AH_V2_STOP_FAULT) { fprintf(stderr, "fail line %d\n", __LINE__); return 1; }
    if ((dev.fault_bits & AH_V2_FAULT_OVERTEMP) == 0u) { fprintf(stderr, "fail line %d\n", __LINE__); return 1; }

    /* overload -> FAULT/OVERLOAD */
    ah_device_reset(&dev);
    for (i = 0; i < AH_SCS_SERVO_COUNT; ++i) {
        fake_hal_set_servo((uint8_t)(i + 1u), 1800, 0, 30, (i == 0u) ? 2u : 0u, 1);
    }
    ah_device_note_link(&dev, 1000u);
    tick_all(&scs, 1000u);
    if (ah_device_apply_execution(&dev, &ex, 1002u) != 0) { fprintf(stderr, "fail line %d\n", __LINE__); return 1; }
    ah_device_step(&dev, &scs, 1006u);
    if (dev.phase != AH_V2_PHASE_FAULT || (dev.fault_bits & AH_V2_FAULT_OVERLOAD) == 0u) { fprintf(stderr, "fail line %d\n", __LINE__); return 1; }

    /* offline -> FAULT/OFFLINE */
    ah_device_reset(&dev);
    fake_hal_set_timeout(1u);
    ah_device_note_link(&dev, 1100u);
    tick_all(&scs, 1100u);
    if (ah_device_apply_execution(&dev, &ex, 1102u) != 0) { fprintf(stderr, "fail line %d\n", __LINE__); return 1; }
    ah_device_step(&dev, &scs, 1106u);
    if (dev.phase != AH_V2_PHASE_FAULT || (dev.fault_bits & AH_V2_FAULT_OFFLINE) == 0u) { fprintf(stderr, "fail line %d\n", __LINE__); return 1; }

    (void)actual;
    printf("AH_DEVICE_TEST_OK\n");
    return 0;
}
