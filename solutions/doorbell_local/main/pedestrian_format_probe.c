#include "pedestrian_format_probe.h"

#include <stddef.h>

void pedestrian_format_probe_init(pedestrian_format_probe_t *probe)
{
    if (probe == NULL) {
        return;
    }
    probe->next_format = PEDESTRIAN_FORMAT_RGB565_LE;
    probe->locked_format = -1;
}

pedestrian_input_format_t pedestrian_format_probe_current(
    const pedestrian_format_probe_t *probe)
{
    if (probe == NULL) {
        return PEDESTRIAN_FORMAT_RGB565_LE;
    }
    if (probe->locked_format >= 0 &&
        probe->locked_format < PEDESTRIAN_FORMAT_COUNT) {
        return (pedestrian_input_format_t)probe->locked_format;
    }
    if (probe->next_format >= PEDESTRIAN_FORMAT_COUNT) {
        return PEDESTRIAN_FORMAT_RGB565_LE;
    }
    return (pedestrian_input_format_t)probe->next_format;
}

bool pedestrian_format_probe_report(pedestrian_format_probe_t *probe,
                                    bool detection_confirmed)
{
    if (probe == NULL) {
        return false;
    }
    if (probe->locked_format >= 0) {
        return true;
    }
    if (detection_confirmed) {
        probe->locked_format = (int8_t)pedestrian_format_probe_current(probe);
        return true;
    }
    probe->next_format = (uint8_t)((probe->next_format + 1U) %
                                   PEDESTRIAN_FORMAT_COUNT);
    return false;
}

bool pedestrian_format_probe_is_locked(const pedestrian_format_probe_t *probe)
{
    return probe != NULL && probe->locked_format >= 0 &&
           probe->locked_format < PEDESTRIAN_FORMAT_COUNT;
}
