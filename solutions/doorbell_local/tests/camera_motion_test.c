#include "camera_motion.h"

#include <stdio.h>
#include <stdlib.h>

#define WIDTH  32
#define HEIGHT 24

static void require_true(bool value, const char *message)
{
    if (!value) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static uint16_t gray565(unsigned level)
{
    unsigned r = level * 31U / 255U;
    unsigned g = level * 63U / 255U;
    unsigned b = level * 31U / 255U;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static void fill_frame(uint16_t *frame, unsigned level)
{
    uint16_t pixel = gray565(level);
    for (int i = 0; i < WIDTH * HEIGHT; ++i) {
        frame[i] = pixel;
    }
}

static camera_motion_t make_motion(void)
{
    camera_motion_t motion;
    camera_motion_config_t config = camera_motion_default_config();
    config.sample_step = 2;
    require_true(camera_motion_init(&motion, &config),
                 "motion detector initializes");
    return motion;
}

static void test_zero_startup_frame_is_ignored(void)
{
    uint16_t frame[WIDTH * HEIGHT] = {0};
    camera_motion_t motion = make_motion();
    require_true(!camera_motion_update_rgb565(&motion, frame, WIDTH, HEIGHT,
                                              NULL),
                 "empty startup buffer does not trigger");
    require_true(!motion.has_baseline,
                 "empty startup buffer does not become the baseline");
}

static void test_static_scene_does_not_trigger(void)
{
    uint16_t frame[WIDTH * HEIGHT];
    camera_motion_t motion = make_motion();
    fill_frame(frame, 80);
    require_true(!camera_motion_update_rgb565(&motion, frame, WIDTH, HEIGHT,
                                              NULL),
                 "first valid frame establishes baseline");
    require_true(!camera_motion_update_rgb565(&motion, frame, WIDTH, HEIGHT,
                                              NULL),
                 "unchanged scene stays quiet");
}

static void test_uniform_exposure_change_is_ignored(void)
{
    uint16_t frame[WIDTH * HEIGHT];
    camera_motion_t motion = make_motion();
    fill_frame(frame, 80);
    camera_motion_update_rgb565(&motion, frame, WIDTH, HEIGHT, NULL);
    fill_frame(frame, 115);
    require_true(!camera_motion_update_rgb565(&motion, frame, WIDTH, HEIGHT,
                                              NULL),
                 "global auto-exposure shift is not local motion");
}

static void test_local_person_sized_change_triggers(void)
{
    uint16_t frame[WIDTH * HEIGHT];
    uint16_t changed_permille = 0;
    camera_motion_t motion = make_motion();
    fill_frame(frame, 80);
    camera_motion_update_rgb565(&motion, frame, WIDTH, HEIGHT, NULL);
    for (int y = 4; y < 22; ++y) {
        for (int x = 10; x < 24; ++x) {
            frame[y * WIDTH + x] = gray565(210);
        }
    }
    require_true(camera_motion_update_rgb565(&motion, frame, WIDTH, HEIGHT,
                                             &changed_permille),
                 "localized person-sized change confirms camera presence");
    require_true(changed_permille >= 80,
                 "reported motion ratio crosses configured threshold");
}

int main(void)
{
    test_zero_startup_frame_is_ignored();
    test_static_scene_does_not_trigger();
    test_uniform_exposure_change_is_ignored();
    test_local_person_sized_change_triggers();
    puts("PASS: camera motion confirmation");
    return 0;
}
