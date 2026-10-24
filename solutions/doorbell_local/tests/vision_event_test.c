#include "vision_event.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void require_true(bool value, const char *message)
{
    if (!value) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static void test_confirmed_face_snapshot(void)
{
    vision_snapshot_t snapshot = {
        .timestamp_ms = 123U,
        .source_width = 320U,
        .source_height = 180U,
        .pedestrian_model = VISION_MODEL_READY,
        .face_model = VISION_MODEL_READY,
    };
    snapshot.update.confirmed_present = true;
    snapshot.update.confirmed_person_count = 1U;
    snapshot.update.target_count = 1U;
    snapshot.update.targets[0] = (vision_target_t){
        .track_id = 1U,
        .kind = VISION_KIND_FACE,
        .score = 0.86f,
        .x1 = 10,
        .y1 = 20,
        .x2 = 50,
        .y2 = 70,
        .confirmed = true,
        .last_seen_ms = 123U,
    };
    char json[768];
    int length = vision_snapshot_format_json(json, sizeof(json), &snapshot);
    require_true(length > 0, "confirmed face snapshot serializes");
    require_true(strstr(json, "\"type\":\"vision_state\"") != NULL,
                 "message type is present");
    require_true(strstr(json, "\"state\":\"confirmed\"") != NULL,
                 "confirmed state is present");
    require_true(strstr(json, "\"kind\":\"face\"") != NULL,
                 "face kind is present");
    require_true(strstr(json, "\"score\":0.86") != NULL,
                 "model score is serialized honestly");
    require_true(strstr(json, "\"width\":40") != NULL &&
                     strstr(json, "\"height\":50") != NULL,
                 "box dimensions are derived correctly");
}

static void test_bounded_and_invalid_output(void)
{
    vision_snapshot_t snapshot = {
        .timestamp_ms = 1U,
        .source_width = 320U,
        .source_height = 180U,
        .pedestrian_model = VISION_MODEL_UNAVAILABLE,
        .face_model = VISION_MODEL_INITIALIZING,
    };
    char tiny[12] = "unchanged";
    require_true(vision_snapshot_format_json(tiny, sizeof(tiny), &snapshot) < 0,
                 "short output buffer is rejected");
    require_true(tiny[0] == '\0', "truncated output is cleared");
    require_true(vision_snapshot_format_json(NULL, 0U, &snapshot) < 0,
                 "NULL output is rejected");
    char json[512];
    snapshot.source_width = 0U;
    require_true(vision_snapshot_format_json(json, sizeof(json), &snapshot) < 0,
                 "zero source width is rejected");
}

int main(void)
{
    test_confirmed_face_snapshot();
    test_bounded_and_invalid_output();
    puts("PASS: vision event serializer");
    return 0;
}
