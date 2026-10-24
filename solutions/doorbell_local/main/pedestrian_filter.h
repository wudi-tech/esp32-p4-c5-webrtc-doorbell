#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float score;
    int x1;
    int y1;
    int x2;
    int y2;
} pedestrian_detection_t;

typedef struct {
    float min_score;
    uint16_t min_area_permille;
    uint16_t max_area_permille;
    uint16_t min_iou_permille;
    uint8_t confirm_frames;
    uint8_t clear_frames;
} pedestrian_filter_config_t;

typedef struct {
    pedestrian_filter_config_t config;
    pedestrian_detection_t previous;
    uint8_t hit_streak;
    uint8_t miss_streak;
    bool has_previous;
    bool confirmed;
} pedestrian_filter_t;

pedestrian_filter_config_t pedestrian_filter_default_config(void);
bool pedestrian_filter_init(pedestrian_filter_t *filter,
                            const pedestrian_filter_config_t *config);
bool pedestrian_detection_is_plausible(
    const pedestrian_filter_config_t *config,
    const pedestrian_detection_t *detection, int frame_width, int frame_height);
bool pedestrian_filter_update(pedestrian_filter_t *filter,
                              const pedestrian_detection_t *detection,
                              int frame_width, int frame_height);

#ifdef __cplusplus
}
#endif
