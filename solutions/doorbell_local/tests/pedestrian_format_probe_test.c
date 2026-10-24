#include "pedestrian_format_probe.h"

#include <stdio.h>
#include <stdlib.h>

static void require_true(bool value, const char *message)
{
    if (!value) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static void test_cycles_all_formats_until_one_is_confirmed(void)
{
    pedestrian_format_probe_t probe;
    pedestrian_format_probe_init(&probe);

    require_true(pedestrian_format_probe_current(&probe) ==
                     PEDESTRIAN_FORMAT_RGB565_LE,
                 "probe starts with the documented capture format");
    pedestrian_format_probe_report(&probe, false);
    require_true(pedestrian_format_probe_current(&probe) ==
                     PEDESTRIAN_FORMAT_RGB565_BE,
                 "probe tries the opposite byte order next");
    pedestrian_format_probe_report(&probe, false);
    require_true(pedestrian_format_probe_current(&probe) ==
                     PEDESTRIAN_FORMAT_BGR565_LE,
                 "probe then tries swapped color channels");
    pedestrian_format_probe_report(&probe, false);
    require_true(pedestrian_format_probe_current(&probe) ==
                     PEDESTRIAN_FORMAT_BGR565_BE,
                 "probe covers the final legal RGB565 interpretation");
    pedestrian_format_probe_report(&probe, false);
    require_true(pedestrian_format_probe_current(&probe) ==
                     PEDESTRIAN_FORMAT_RGB565_LE,
                 "probe wraps while no format is credible");
}

static void test_confirmation_locks_format(void)
{
    pedestrian_format_probe_t probe;
    pedestrian_format_probe_init(&probe);
    pedestrian_format_probe_report(&probe, false);

    require_true(pedestrian_format_probe_report(&probe, true),
                 "credible detection locks the current format");
    require_true(pedestrian_format_probe_is_locked(&probe),
                 "format is reported as locked");
    require_true(pedestrian_format_probe_current(&probe) ==
                     PEDESTRIAN_FORMAT_RGB565_BE,
                 "the confirmed format remains selected");

    pedestrian_format_probe_report(&probe, false);
    require_true(pedestrian_format_probe_current(&probe) ==
                     PEDESTRIAN_FORMAT_RGB565_BE,
                 "ordinary misses do not restart byte-order probing");
}

int main(void)
{
    test_cycles_all_formats_until_one_is_confirmed();
    test_confirmation_locks_format();
    puts("PASS: pedestrian RGB565 format probe");
    return 0;
}
