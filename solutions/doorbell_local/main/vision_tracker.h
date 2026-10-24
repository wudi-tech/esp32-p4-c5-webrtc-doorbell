#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VISION_MAX_TARGETS 3U

typedef enum {
    VISION_KIND_NONE = 0,
    VISION_KIND_MOTION,
    VISION_KIND_PEDESTRIAN,
    VISION_KIND_FACE,
} vision_kind_t;

typedef struct {
    vision_kind_t kind;
    float score;
    int x1;
    int y1;
    int x2;
    int y2;
} vision_detection_t;

typedef struct {
    uint16_t track_id;
    vision_kind_t kind;
    float score;
    int x1;
    int y1;
    int x2;
    int y2;
    bool confirmed;
    uint32_t last_seen_ms;
} vision_target_t;

typedef struct {
    float pedestrian_min_score;
    float face_min_score;
    uint16_t min_area_permille;
    uint16_t pedestrian_max_area_permille;
    uint16_t face_max_area_permille;
    uint16_t min_iou_permille;
    uint8_t confirm_window;
    uint8_t confirm_hits;
    uint8_t clear_frames;
} vision_tracker_config_t;

typedef struct {
    vision_target_t target;
    uint8_t hit_history;
    uint8_t miss_streak;
    bool active;
} vision_track_slot_t;

typedef struct {
    vision_tracker_config_t config;
    vision_track_slot_t slots[VISION_MAX_TARGETS];
    uint16_t next_track_id;
} vision_tracker_t;

typedef struct {
    vision_target_t targets[VISION_MAX_TARGETS];
    size_t target_count;
    size_t confirmed_person_count;
    bool confirmed_present;
} vision_update_t;

vision_tracker_config_t vision_tracker_default_config(void);
bool vision_tracker_init(vision_tracker_t *tracker,
                         const vision_tracker_config_t *config);
vision_update_t vision_tracker_update(vision_tracker_t *tracker,
                                      const vision_detection_t *detections,
                                      size_t detection_count,
                                      uint16_t frame_width,
                                      uint16_t frame_height,
                                      uint32_t now_ms);
const char *vision_kind_name(vision_kind_t kind);

#ifdef __cplusplus
}
#endif
