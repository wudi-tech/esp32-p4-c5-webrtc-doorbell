#include "vision_event.h"

#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>

const char *vision_model_state_name(vision_model_state_t state)
{
    switch (state) {
    case VISION_MODEL_READY:
        return "ready";
    case VISION_MODEL_UNAVAILABLE:
        return "unavailable";
    case VISION_MODEL_INITIALIZING:
    default:
        return "initializing";
    }
}

static bool append_json(char *output, size_t output_size, size_t *used,
                        const char *format, ...)
{
    if (*used >= output_size) {
        return false;
    }
    va_list args;
    va_start(args, format);
    int written = vsnprintf(output + *used, output_size - *used, format, args);
    va_end(args);
    if (written < 0 || (size_t)written >= output_size - *used) {
        return false;
    }
    *used += (size_t)written;
    return true;
}

static bool target_is_valid(const vision_target_t *target,
                            const vision_snapshot_t *snapshot)
{
    return target != NULL && target->track_id != 0U &&
           (target->kind == VISION_KIND_PEDESTRIAN ||
            target->kind == VISION_KIND_FACE) &&
           isfinite(target->score) && target->score >= 0.0f &&
           target->score <= 1.0f && target->x1 >= 0 && target->y1 >= 0 &&
           target->x2 > target->x1 && target->y2 > target->y1 &&
           target->x2 < (int)snapshot->source_width &&
           target->y2 < (int)snapshot->source_height;
}

int vision_snapshot_format_json(char *output, size_t output_size,
                                const vision_snapshot_t *snapshot)
{
    if (output == NULL || output_size == 0U || snapshot == NULL ||
        snapshot->source_width == 0U || snapshot->source_height == 0U ||
        snapshot->update.target_count > VISION_MAX_TARGETS) {
        return -1;
    }
    output[0] = '\0';
    const char *state = snapshot->update.confirmed_present
                            ? "confirmed"
                            : (snapshot->update.target_count > 0U ? "candidate"
                                                                  : "clear");
    size_t used = 0U;
    if (!append_json(output, output_size, &used,
                     "{\"type\":\"vision_state\",\"timestamp_ms\":%" PRIu32
                     ",\"state\":\"%s\",\"source_width\":%u,"
                     "\"source_height\":%u,\"person_count\":%u,\"targets\":[",
                     snapshot->timestamp_ms, state,
                     (unsigned)snapshot->source_width,
                     (unsigned)snapshot->source_height,
                     (unsigned)snapshot->update.confirmed_person_count)) {
        output[0] = '\0';
        return -1;
    }
    for (size_t i = 0; i < snapshot->update.target_count; ++i) {
        const vision_target_t *target = &snapshot->update.targets[i];
        unsigned score_hundredths =
            (unsigned)(target->score * 100.0f + 0.5f);
        if (score_hundredths > 100U) {
            score_hundredths = 100U;
        }
        if (!target_is_valid(target, snapshot) ||
            !append_json(output, output_size, &used,
                         "%s{\"track_id\":%u,\"kind\":\"%s\",\"score\":%u.%02u,"
                         "\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d,"
                         "\"confirmed\":%s}",
                         i == 0U ? "" : ",", (unsigned)target->track_id,
                         vision_kind_name(target->kind),
                         score_hundredths / 100U, score_hundredths % 100U,
                         target->x1, target->y1, target->x2 - target->x1,
                         target->y2 - target->y1,
                         target->confirmed ? "true" : "false")) {
            output[0] = '\0';
            return -1;
        }
    }
    if (!append_json(output, output_size, &used,
                     "],\"pedestrian_model\":\"%s\",\"face_model\":\"%s\"}",
                     vision_model_state_name(snapshot->pedestrian_model),
                     vision_model_state_name(snapshot->face_model))) {
        output[0] = '\0';
        return -1;
    }
    return (int)used;
}
