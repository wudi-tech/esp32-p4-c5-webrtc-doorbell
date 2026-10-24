#include "doorbell_event.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void require_true(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(EXIT_FAILURE);
    }
}

static void test_enter_event_schema(void)
{
    doorbell_radar_event_t event = {
        .event = DOORBELL_RADAR_ENTER,
        .timestamp_ms = 123456U,
        .track_id = 2U,
        .x_mm = -782,
        .y_mm = 1713,
        .speed_cm_s = -16,
    };
    char json[256];
    int length = doorbell_radar_event_format_json(json, sizeof(json), &event);

    require_true(length > 0 && (size_t)length == strlen(json),
                 "formatter returns the JSON length");
    require_true(strcmp(json,
                        "{\"type\":\"radar_event\",\"event\":\"enter\","
                        "\"timestamp\":123456,\"track_id\":2,\"x_mm\":-782,"
                        "\"y_mm\":1713,\"speed\":-16,\"zone\":\"door_approach\","
                        "\"confidence\":null,\"source\":\"ld2450\"}") == 0,
                 "enter event has the required stable schema");
}

static void test_leave_and_invalid_inputs(void)
{
    doorbell_radar_event_t event = {
        .event = DOORBELL_RADAR_LEAVE,
        .timestamp_ms = 99U,
        .track_id = 1U,
        .x_mm = 10,
        .y_mm = 800,
        .speed_cm_s = 12,
    };
    char json[256];
    require_true(doorbell_radar_event_format_json(json, sizeof(json), &event) > 0 &&
                     strstr(json, "\"event\":\"leave\"") != NULL,
                 "leave event is encoded");
    require_true(doorbell_radar_event_format_json(NULL, sizeof(json), &event) < 0,
                 "NULL output is rejected");
    require_true(doorbell_radar_event_format_json(json, 8, &event) < 0,
                 "truncated JSON is rejected");
    event.track_id = 0;
    require_true(doorbell_radar_event_format_json(json, sizeof(json), &event) < 0,
                 "track IDs are one based");
    event.track_id = 4;
    require_true(doorbell_radar_event_format_json(json, sizeof(json), &event) < 0,
                 "track IDs above the three LD2450 slots are rejected");
}

int main(void)
{
    test_enter_event_schema();
    test_leave_and_invalid_inputs();
    puts("PASS: structured radar event schema");
    return EXIT_SUCCESS;
}
