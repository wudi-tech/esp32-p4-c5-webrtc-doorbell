#include "vision_tracker.h"

#include <math.h>
#include <string.h>

vision_tracker_config_t vision_tracker_default_config(void)
{
    vision_tracker_config_t config = {
        .pedestrian_min_score = 0.70f,
        .face_min_score = 0.75f,
        .min_area_permille = 2U,
        .pedestrian_max_area_permille = 850U,
        .face_max_area_permille = 600U,
        .min_iou_permille = 250U,
        .confirm_window = 4U,
        .confirm_hits = 3U,
        .clear_frames = 3U,
    };
    return config;
}

bool vision_tracker_init(vision_tracker_t *tracker,
                         const vision_tracker_config_t *config)
{
    if (tracker == NULL || config == NULL ||
        !isfinite(config->pedestrian_min_score) ||
        !isfinite(config->face_min_score) ||
        config->pedestrian_min_score < 0.0f ||
        config->pedestrian_min_score > 1.0f || config->face_min_score < 0.0f ||
        config->face_min_score > 1.0f || config->min_area_permille == 0U ||
        config->min_area_permille >= config->pedestrian_max_area_permille ||
        config->min_area_permille >= config->face_max_area_permille ||
        config->pedestrian_max_area_permille > 1000U ||
        config->face_max_area_permille > 1000U ||
        config->min_iou_permille > 1000U || config->confirm_window == 0U ||
        config->confirm_window > 8U || config->confirm_hits == 0U ||
        config->confirm_hits > config->confirm_window ||
        config->clear_frames == 0U) {
        return false;
    }
    memset(tracker, 0, sizeof(*tracker));
    tracker->config = *config;
    tracker->next_track_id = 1U;
    return true;
}

const char *vision_kind_name(vision_kind_t kind)
{
    switch (kind) {
    case VISION_KIND_PEDESTRIAN:
        return "pedestrian";
    case VISION_KIND_FACE:
        return "face";
    case VISION_KIND_MOTION:
        return "motion";
    case VISION_KIND_NONE:
    default:
        return "none";
    }
}

static bool detection_is_plausible(const vision_tracker_config_t *config,
                                   const vision_detection_t *detection,
                                   uint16_t frame_width,
                                   uint16_t frame_height)
{
    if (detection == NULL || frame_width == 0U || frame_height == 0U ||
        (detection->kind != VISION_KIND_PEDESTRIAN &&
         detection->kind != VISION_KIND_FACE) ||
        !isfinite(detection->score) || detection->x1 < 0 ||
        detection->y1 < 0 || detection->x2 <= detection->x1 ||
        detection->y2 <= detection->y1 ||
        detection->x2 >= (int)frame_width ||
        detection->y2 >= (int)frame_height) {
        return false;
    }

    float minimum_score = detection->kind == VISION_KIND_FACE
                              ? config->face_min_score
                              : config->pedestrian_min_score;
    uint16_t maximum_area = detection->kind == VISION_KIND_FACE
                                ? config->face_max_area_permille
                                : config->pedestrian_max_area_permille;
    if (detection->score < minimum_score) {
        return false;
    }

    int64_t box_area = (int64_t)(detection->x2 - detection->x1) *
                       (detection->y2 - detection->y1);
    int64_t frame_area = (int64_t)frame_width * frame_height;
    uint16_t area_permille =
        (uint16_t)(box_area * 1000LL / frame_area);
    return area_permille >= config->min_area_permille &&
           area_permille <= maximum_area;
}

static uint16_t detection_iou_permille(const vision_target_t *target,
                                       const vision_detection_t *detection)
{
    int left = target->x1 > detection->x1 ? target->x1 : detection->x1;
    int top = target->y1 > detection->y1 ? target->y1 : detection->y1;
    int right = target->x2 < detection->x2 ? target->x2 : detection->x2;
    int bottom = target->y2 < detection->y2 ? target->y2 : detection->y2;
    int64_t intersection = 0;
    if (right > left && bottom > top) {
        intersection = (int64_t)(right - left) * (bottom - top);
    }
    int64_t target_area =
        (int64_t)(target->x2 - target->x1) * (target->y2 - target->y1);
    int64_t detection_area = (int64_t)(detection->x2 - detection->x1) *
                             (detection->y2 - detection->y1);
    int64_t union_area = target_area + detection_area - intersection;
    return union_area > 0
               ? (uint16_t)(intersection * 1000LL / union_area)
               : 0U;
}

static uint8_t history_mask(uint8_t window)
{
    return window == 8U ? UINT8_MAX : (uint8_t)((1U << window) - 1U);
}

static uint8_t bit_count(uint8_t value)
{
    uint8_t count = 0U;
    while (value != 0U) {
        count = (uint8_t)(count + (value & 1U));
        value >>= 1U;
    }
    return count;
}

static int find_matching_slot(const vision_tracker_t *tracker,
                              const vision_detection_t *detection,
                              const bool matched[VISION_MAX_TARGETS])
{
    int best = -1;
    uint16_t best_iou = 0U;
    for (size_t i = 0; i < VISION_MAX_TARGETS; ++i) {
        const vision_track_slot_t *slot = &tracker->slots[i];
        if (!slot->active || matched[i] ||
            slot->target.kind != detection->kind) {
            continue;
        }
        uint16_t iou = detection_iou_permille(&slot->target, detection);
        if (iou >= tracker->config.min_iou_permille && iou >= best_iou) {
            best = (int)i;
            best_iou = iou;
        }
    }
    return best;
}

static int find_free_slot(const vision_tracker_t *tracker)
{
    for (size_t i = 0; i < VISION_MAX_TARGETS; ++i) {
        if (!tracker->slots[i].active) {
            return (int)i;
        }
    }
    return -1;
}

static void apply_hit(vision_tracker_t *tracker, vision_track_slot_t *slot,
                      const vision_detection_t *detection, uint32_t now_ms)
{
    if (!slot->active) {
        memset(slot, 0, sizeof(*slot));
        slot->active = true;
        slot->target.track_id = tracker->next_track_id++;
        if (tracker->next_track_id == 0U) {
            tracker->next_track_id = 1U;
        }
    }
    slot->target.kind = detection->kind;
    slot->target.score = detection->score;
    slot->target.x1 = detection->x1;
    slot->target.y1 = detection->y1;
    slot->target.x2 = detection->x2;
    slot->target.y2 = detection->y2;
    slot->target.last_seen_ms = now_ms;
    slot->hit_history =
        (uint8_t)(((slot->hit_history << 1U) | 1U) &
                  history_mask(tracker->config.confirm_window));
    slot->miss_streak = 0U;
    if (bit_count(slot->hit_history) >= tracker->config.confirm_hits) {
        slot->target.confirmed = true;
    }
}

static void apply_miss(vision_tracker_t *tracker, vision_track_slot_t *slot)
{
    slot->hit_history =
        (uint8_t)((slot->hit_history << 1U) &
                  history_mask(tracker->config.confirm_window));
    if (slot->miss_streak < UINT8_MAX) {
        ++slot->miss_streak;
    }
    if (slot->miss_streak >= tracker->config.clear_frames ||
        (!slot->target.confirmed && slot->hit_history == 0U)) {
        memset(slot, 0, sizeof(*slot));
    }
}

static bool point_inside(const vision_target_t *inner,
                         const vision_target_t *outer)
{
    int center_x = inner->x1 + (inner->x2 - inner->x1) / 2;
    int center_y = inner->y1 + (inner->y2 - inner->y1) / 2;
    return center_x >= outer->x1 && center_x <= outer->x2 &&
           center_y >= outer->y1 && center_y <= outer->y2;
}

vision_update_t vision_tracker_update(vision_tracker_t *tracker,
                                      const vision_detection_t *detections,
                                      size_t detection_count,
                                      uint16_t frame_width,
                                      uint16_t frame_height,
                                      uint32_t now_ms)
{
    vision_update_t update;
    bool matched[VISION_MAX_TARGETS] = {false};
    memset(&update, 0, sizeof(update));
    if (tracker == NULL || frame_width == 0U || frame_height == 0U ||
        (detections == NULL && detection_count != 0U)) {
        return update;
    }

    for (size_t i = 0; i < detection_count; ++i) {
        if (!detection_is_plausible(&tracker->config, &detections[i],
                                    frame_width, frame_height)) {
            continue;
        }
        int slot_index = find_matching_slot(tracker, &detections[i], matched);
        if (slot_index < 0) {
            slot_index = find_free_slot(tracker);
        }
        if (slot_index < 0) {
            continue;
        }
        apply_hit(tracker, &tracker->slots[slot_index], &detections[i], now_ms);
        matched[slot_index] = true;
    }

    for (size_t i = 0; i < VISION_MAX_TARGETS; ++i) {
        if (tracker->slots[i].active && !matched[i]) {
            apply_miss(tracker, &tracker->slots[i]);
        }
    }

    for (size_t i = 0; i < VISION_MAX_TARGETS; ++i) {
        if (!tracker->slots[i].active) {
            continue;
        }
        update.targets[update.target_count++] = tracker->slots[i].target;
        if (tracker->slots[i].target.confirmed &&
            tracker->slots[i].target.kind == VISION_KIND_PEDESTRIAN) {
            ++update.confirmed_person_count;
        }
    }
    for (size_t i = 0; i < update.target_count; ++i) {
        const vision_target_t *face = &update.targets[i];
        if (!face->confirmed || face->kind != VISION_KIND_FACE) {
            continue;
        }
        bool belongs_to_pedestrian = false;
        for (size_t j = 0; j < update.target_count; ++j) {
            const vision_target_t *person = &update.targets[j];
            if (person->confirmed && person->kind == VISION_KIND_PEDESTRIAN &&
                point_inside(face, person)) {
                belongs_to_pedestrian = true;
                break;
            }
        }
        if (!belongs_to_pedestrian) {
            ++update.confirmed_person_count;
        }
    }
    update.confirmed_present = update.confirmed_person_count > 0U;
    return update;
}
