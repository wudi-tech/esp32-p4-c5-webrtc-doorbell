#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DOORBELL_RADAR_ENTER = 0,
    DOORBELL_RADAR_LEAVE,
} doorbell_radar_event_type_t;

typedef struct {
    doorbell_radar_event_type_t event;
    uint32_t timestamp_ms;
    uint8_t track_id;
    int16_t x_mm;
    int16_t y_mm;
    int16_t speed_cm_s;
} doorbell_radar_event_t;

int doorbell_radar_event_format_json(char *output, size_t output_size,
                                     const doorbell_radar_event_t *event);

#ifdef __cplusplus
}
#endif
