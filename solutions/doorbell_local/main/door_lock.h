#pragma once

#include "esp_err.h"
#include "door_lock_state.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DOOR_LOCK_SERVO_GPIO 32
#define DOOR_LOCK_PWM_FREQUENCY_HZ 50
#define DOOR_LOCK_LOCKED_PULSE_US 500
#define DOOR_LOCK_UNLOCKED_PULSE_US 2500
#define DOOR_LOCK_MOTION_TIME_MS 500
#define DOOR_LOCK_HOLD_TIME_MS 5000

typedef void (*door_lock_state_callback_t)(door_lock_state_t state, void *ctx);

esp_err_t door_lock_init(void);
void door_lock_set_state_callback(door_lock_state_callback_t callback, void *ctx);
esp_err_t door_lock_request_open(void);
door_lock_state_t door_lock_get_state(void);

#ifdef __cplusplus
}
#endif
