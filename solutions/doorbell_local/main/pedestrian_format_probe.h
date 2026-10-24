#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PEDESTRIAN_FORMAT_RGB565_LE = 0,
    PEDESTRIAN_FORMAT_RGB565_BE,
    PEDESTRIAN_FORMAT_BGR565_LE,
    PEDESTRIAN_FORMAT_BGR565_BE,
    PEDESTRIAN_FORMAT_COUNT,
} pedestrian_input_format_t;

typedef struct {
    uint8_t next_format;
    int8_t locked_format;
} pedestrian_format_probe_t;

void pedestrian_format_probe_init(pedestrian_format_probe_t *probe);
pedestrian_input_format_t pedestrian_format_probe_current(
    const pedestrian_format_probe_t *probe);
bool pedestrian_format_probe_report(pedestrian_format_probe_t *probe,
                                    bool detection_confirmed);
bool pedestrian_format_probe_is_locked(const pedestrian_format_probe_t *probe);

#ifdef __cplusplus
}
#endif
