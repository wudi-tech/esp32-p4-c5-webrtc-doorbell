#pragma once

#include "vision_tracker.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VISION_MODEL_INITIALIZING = 0,
    VISION_MODEL_READY,
    VISION_MODEL_UNAVAILABLE,
} vision_model_state_t;

typedef struct {
    uint32_t timestamp_ms;
    uint16_t source_width;
    uint16_t source_height;
    vision_update_t update;
    vision_model_state_t pedestrian_model;
    vision_model_state_t face_model;
} vision_snapshot_t;

int vision_snapshot_format_json(char *output, size_t output_size,
                                const vision_snapshot_t *snapshot);
const char *vision_model_state_name(vision_model_state_t state);

#ifdef __cplusplus
}
#endif
