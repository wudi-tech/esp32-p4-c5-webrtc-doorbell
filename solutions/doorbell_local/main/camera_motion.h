#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CAMERA_MOTION_MAX_SAMPLES 1200

typedef struct {
    uint8_t sample_step;
    uint8_t pixel_delta;
    uint16_t min_changed_permille;
    uint16_t min_valid_permille;
} camera_motion_config_t;

typedef struct {
    camera_motion_config_t config;
    uint8_t previous[CAMERA_MOTION_MAX_SAMPLES];
    uint16_t sample_count;
    bool has_baseline;
} camera_motion_t;

camera_motion_config_t camera_motion_default_config(void);
bool camera_motion_init(camera_motion_t *motion,
                        const camera_motion_config_t *config);
bool camera_motion_update_rgb565(camera_motion_t *motion,
                                 const uint16_t *pixels, int width, int height,
                                 uint16_t *changed_permille);

#ifdef __cplusplus
}
#endif
