#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "radar_ld2450.h"

static const uint8_t protocol_sample[LD2450_FRAME_SIZE] = {
    0xAA, 0xFF, 0x03, 0x00,
    0x0E, 0x03, 0xB1, 0x86, 0x10, 0x00, 0x40, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x55, 0xCC,
};

/* Captured from the physical LD2450 on 2026-09-04. */
static const uint8_t live_idle_sample[LD2450_FRAME_SIZE] = {
    0xAA, 0xFF, 0x03, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x55, 0xCC,
};

/* Captured while a single target moved through the radar field of view. */
static const uint8_t live_one_target_sample[LD2450_FRAME_SIZE] = {
    0xAA, 0xFF, 0x03, 0x00,
    0x1C, 0x04, 0xB2, 0x82, 0x10, 0x00, 0x68, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x55, 0xCC,
};

typedef struct {
    size_t count;
    uint8_t last_raw_frame[LD2450_FRAME_SIZE];
    ld2450_frame_t last_frame;
} capture_t;

static void capture_frame(const uint8_t raw_frame[LD2450_FRAME_SIZE],
                          const ld2450_frame_t *frame, void *ctx)
{
    capture_t *capture = ctx;
    capture->count++;
    memcpy(capture->last_raw_frame, raw_frame, LD2450_FRAME_SIZE);
    capture->last_frame = *frame;
}

static void test_protocol_sample(void)
{
    ld2450_frame_t frame = {0};
    assert(ld2450_parse_frame(protocol_sample, sizeof(protocol_sample), &frame) ==
           LD2450_PARSE_OK);
    assert(frame.target_count == 1);
    assert(frame.targets[0].valid);
    assert(frame.targets[0].x_mm == -782);
    assert(frame.targets[0].y_mm == 1713);
    assert(frame.targets[0].speed_cm_s == -16);
    assert(frame.targets[0].distance_resolution_mm == 320);
    assert(!frame.targets[1].valid);
    assert(!frame.targets[2].valid);
}

static void test_two_targets_and_direction_bits(void)
{
    uint8_t frame_bytes[LD2450_FRAME_SIZE];
    memcpy(frame_bytes, protocol_sample, sizeof(frame_bytes));
    const uint8_t second_target[LD2450_TARGET_DATA_SIZE] = {
        0x64, 0x80, 0xC8, 0x00, 0x1E, 0x80, 0xF4, 0x01,
    };
    memcpy(&frame_bytes[LD2450_HEADER_SIZE + LD2450_TARGET_DATA_SIZE],
           second_target, sizeof(second_target));

    ld2450_frame_t frame = {0};
    assert(ld2450_parse_frame(frame_bytes, sizeof(frame_bytes), &frame) ==
           LD2450_PARSE_OK);
    assert(frame.target_count == 2);
    assert(frame.targets[1].valid);
    assert(frame.targets[1].x_mm == 100);
    assert(frame.targets[1].y_mm == -200);
    assert(frame.targets[1].speed_cm_s == 30);
    assert(frame.targets[1].distance_resolution_mm == 500);
}

static void test_live_captured_frames(void)
{
    ld2450_frame_t frame = {0};
    assert(ld2450_parse_frame(live_idle_sample, sizeof(live_idle_sample),
                              &frame) == LD2450_PARSE_OK);
    assert(frame.target_count == 0);

    assert(ld2450_parse_frame(live_one_target_sample,
                              sizeof(live_one_target_sample), &frame) ==
           LD2450_PARSE_OK);
    assert(frame.target_count == 1);
    assert(frame.targets[0].x_mm == -1052);
    assert(frame.targets[0].y_mm == 690);
    assert(frame.targets[0].speed_cm_s == -16);
    assert(frame.targets[0].distance_resolution_mm == 360);
}

static void test_rejects_bad_frame_boundaries(void)
{
    ld2450_frame_t frame = {0};
    assert(ld2450_parse_frame(protocol_sample, LD2450_FRAME_SIZE - 1, &frame) ==
           LD2450_PARSE_BAD_LENGTH);

    uint8_t bad_frame[LD2450_FRAME_SIZE];
    memcpy(bad_frame, protocol_sample, sizeof(bad_frame));
    bad_frame[0] = 0xAB;
    assert(ld2450_parse_frame(bad_frame, sizeof(bad_frame), &frame) ==
           LD2450_PARSE_BAD_HEADER);

    memcpy(bad_frame, protocol_sample, sizeof(bad_frame));
    bad_frame[LD2450_FRAME_SIZE - 1] = 0xCD;
    assert(ld2450_parse_frame(bad_frame, sizeof(bad_frame), &frame) ==
           LD2450_PARSE_BAD_TAIL);
}

static void test_stream_recovers_from_noise_and_fragmentation(void)
{
    ld2450_stream_decoder_t decoder;
    ld2450_stream_decoder_init(&decoder);
    capture_t capture = {0};
    const uint8_t noise[] = {0x12, 0xAA, 0x10, 0xFF, 0xAA};

    assert(ld2450_stream_feed(&decoder, noise, sizeof(noise), capture_frame,
                              &capture) == 0);
    assert(ld2450_stream_feed(&decoder, &protocol_sample[1], 8, capture_frame,
                              &capture) == 0);
    assert(ld2450_stream_feed(&decoder, &protocol_sample[9],
                              LD2450_FRAME_SIZE - 9, capture_frame,
                              &capture) == 1);
    assert(capture.count == 1);
    assert(memcmp(capture.last_raw_frame, protocol_sample,
                  LD2450_FRAME_SIZE) == 0);
    assert(capture.last_frame.targets[0].x_mm == -782);
}

static void test_raw_sample_tracker_captures_each_target_count_once(void)
{
    ld2450_sample_tracker_t tracker = {0};

    assert(ld2450_should_capture_sample(&tracker, 0));
    assert(!ld2450_should_capture_sample(&tracker, 0));
    assert(ld2450_should_capture_sample(&tracker, 1));
    assert(!ld2450_should_capture_sample(&tracker, 1));
    assert(ld2450_should_capture_sample(&tracker, 2));
    assert(ld2450_should_capture_sample(&tracker, 3));
    assert(!ld2450_should_capture_sample(&tracker, LD2450_MAX_TARGETS + 1));
}

int main(void)
{
    test_protocol_sample();
    test_two_targets_and_direction_bits();
    test_live_captured_frames();
    test_rejects_bad_frame_boundaries();
    test_stream_recovers_from_noise_and_fragmentation();
    test_raw_sample_tracker_captures_each_target_count_once();
    puts("PASS: LD2450 protocol parser");
    return 0;
}
