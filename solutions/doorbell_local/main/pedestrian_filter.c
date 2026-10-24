#include "pedestrian_filter.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

pedestrian_filter_config_t pedestrian_filter_default_config(void)
{
    pedestrian_filter_config_t config = {
        .min_score = 0.70f,
        .min_area_permille = 5U,
        .max_area_permille = 850U,
        .min_iou_permille = 250U,
        .confirm_frames = 3U,
        .clear_frames = 3U,
    };
    return config;
}

bool pedestrian_filter_init(pedestrian_filter_t *filter,
                            const pedestrian_filter_config_t *config)
{
    if (filter == NULL || config == NULL || !isfinite(config->min_score) ||
        config->min_score < 0.0f || config->min_score > 1.0f ||
        config->min_area_permille >= config->max_area_permille ||
        config->max_area_permille > 1000U ||
        config->min_iou_permille > 1000U || config->confirm_frames == 0U ||
        config->clear_frames == 0U) {
        return false;
    }
    memset(filter, 0, sizeof(*filter));
    filter->config = *config;
    return true;
}

bool pedestrian_detection_is_plausible(
    const pedestrian_filter_config_t *config,
    const pedestrian_detection_t *detection, int frame_width, int frame_height)
{
    if (config == NULL || detection == NULL || frame_width <= 0 ||
        frame_height <= 0 || !isfinite(detection->score) ||
        detection->score < config->min_score || detection->x1 < 0 ||
        detection->y1 < 0 || detection->x2 >= frame_width ||
        detection->y2 >= frame_height || detection->x2 <= detection->x1 ||
        detection->y2 <= detection->y1) {
        return false;
    }

    int64_t box_area = (int64_t)(detection->x2 - detection->x1) *
                       (detection->y2 - detection->y1);
    int64_t frame_area = (int64_t)frame_width * frame_height;
    int64_t area_permille = box_area * 1000LL / frame_area;
    return area_permille >= config->min_area_permille &&
           area_permille <= config->max_area_permille;
}

static uint16_t detection_iou_permille(const pedestrian_detection_t *a,
                                       const pedestrian_detection_t *b)
{
    int left = a->x1 > b->x1 ? a->x1 : b->x1;
    int top = a->y1 > b->y1 ? a->y1 : b->y1;
    int right = a->x2 < b->x2 ? a->x2 : b->x2;
    int bottom = a->y2 < b->y2 ? a->y2 : b->y2;
    int64_t intersection = 0;
    if (right > left && bottom > top) {
        intersection = (int64_t)(right - left) * (bottom - top);
    }
    int64_t area_a = (int64_t)(a->x2 - a->x1) * (a->y2 - a->y1);
    int64_t area_b = (int64_t)(b->x2 - b->x1) * (b->y2 - b->y1);
    int64_t union_area = area_a + area_b - intersection;
    return union_area > 0 ? (uint16_t)(intersection * 1000LL / union_area) : 0U;
}

static void register_miss(pedestrian_filter_t *filter)
{
    if (!filter->confirmed) {
        filter->hit_streak = 0U;
        filter->has_previous = false;
        return;
    }
    if (filter->miss_streak < UINT8_MAX) {
        ++filter->miss_streak;
    }
    if (filter->miss_streak >= filter->config.clear_frames) {
        filter->hit_streak = 0U;
        filter->miss_streak = 0U;
        filter->has_previous = false;
        filter->confirmed = false;
    }
}

bool pedestrian_filter_update(pedestrian_filter_t *filter,
                              const pedestrian_detection_t *detection,
                              int frame_width, int frame_height)
{
    if (filter == NULL ||
        !pedestrian_detection_is_plausible(&filter->config, detection,
                                           frame_width, frame_height)) {
        if (filter != NULL) {
            register_miss(filter);
        }
        return false;
    }

    bool overlaps_previous =
        filter->has_previous &&
        detection_iou_permille(&filter->previous, detection) >=
            filter->config.min_iou_permille;
    if (!overlaps_previous) {
        filter->hit_streak = 1U;
        filter->confirmed = false;
    } else if (filter->hit_streak < UINT8_MAX) {
        ++filter->hit_streak;
    }
    filter->previous = *detection;
    filter->has_previous = true;
    filter->miss_streak = 0U;
    if (filter->hit_streak >= filter->config.confirm_frames) {
        filter->confirmed = true;
    }
    return filter->confirmed;
}
