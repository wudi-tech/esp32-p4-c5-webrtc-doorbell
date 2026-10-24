#include "doorbell_event.h"

#include <inttypes.h>
#include <stdio.h>

int doorbell_radar_event_format_json(char *output, size_t output_size,
                                     const doorbell_radar_event_t *event)
{
    if (output == NULL || output_size == 0U || event == NULL ||
        event->track_id < 1U || event->track_id > 3U) {
        return -1;
    }
    const char *event_name = NULL;
    switch (event->event) {
    case DOORBELL_RADAR_ENTER:
        event_name = "enter";
        break;
    case DOORBELL_RADAR_LEAVE:
        event_name = "leave";
        break;
    default:
        return -1;
    }
    int length = snprintf(
        output, output_size,
        "{\"type\":\"radar_event\",\"event\":\"%s\","
        "\"timestamp\":%" PRIu32 ",\"track_id\":%u,\"x_mm\":%d,"
        "\"y_mm\":%d,\"speed\":%d,\"zone\":\"door_approach\","
        "\"confidence\":null,\"source\":\"ld2450\"}",
        event_name, event->timestamp_ms, (unsigned)event->track_id,
        event->x_mm, event->y_mm, event->speed_cm_s);
    if (length < 0 || (size_t)length >= output_size) {
        if (output_size > 0U) {
            output[0] = '\0';
        }
        return -1;
    }
    return length;
}
