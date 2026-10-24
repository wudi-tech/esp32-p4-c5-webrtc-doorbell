#include "camera_motion.h"

#include <stddef.h>
#include <string.h>

camera_motion_config_t camera_motion_default_config(void)
{
    camera_motion_config_t config = {
        .sample_step = 8U,
        .pixel_delta = 18U,
        .min_changed_permille = 80U,
        .min_valid_permille = 500U,
    };
    return config;
}

bool camera_motion_init(camera_motion_t *motion,
                        const camera_motion_config_t *config)
{
    if (motion == NULL || config == NULL || config->sample_step == 0U ||
        config->pixel_delta == 0U || config->min_changed_permille > 1000U ||
        config->min_valid_permille > 1000U) {
        return false;
    }
    memset(motion, 0, sizeof(*motion));
    motion->config = *config;
    return true;
}

static uint8_t rgb565_luma(uint16_t pixel)
{
    uint32_t red = ((pixel >> 11) & 0x1fU) * 255U / 31U;
    uint32_t green = ((pixel >> 5) & 0x3fU) * 255U / 63U;
    uint32_t blue = (pixel & 0x1fU) * 255U / 31U;
    return (uint8_t)((77U * red + 150U * green + 29U * blue) >> 8);
}

bool camera_motion_update_rgb565(camera_motion_t *motion,
                                 const uint16_t *pixels, int width, int height,
                                 uint16_t *changed_permille)
{
    if (changed_permille != NULL) {
        *changed_permille = 0U;
    }
    if (motion == NULL || pixels == NULL || width <= 0 || height <= 0) {
        return false;
    }

    uint32_t columns = ((uint32_t)width + motion->config.sample_step - 1U) /
                       motion->config.sample_step;
    uint32_t rows = ((uint32_t)height + motion->config.sample_step - 1U) /
                    motion->config.sample_step;
    uint32_t expected_count = columns * rows;
    if (expected_count == 0U || expected_count > CAMERA_MOTION_MAX_SAMPLES) {
        return false;
    }

    uint16_t count = 0U;
    uint16_t valid = 0U;
    int32_t delta_sum = 0;
    for (int y = 0; y < height; y += motion->config.sample_step) {
        for (int x = 0; x < width; x += motion->config.sample_step) {
            uint8_t luma = rgb565_luma(pixels[y * width + x]);
            valid += luma > 2U;
            if (motion->has_baseline && motion->sample_count == expected_count) {
                delta_sum += (int32_t)luma - motion->previous[count];
            }
            ++count;
        }
    }
    if (count == 0U ||
        (uint32_t)valid * 1000U / count < motion->config.min_valid_permille) {
        return false;
    }
    if (!motion->has_baseline || motion->sample_count != count) {
        count = 0U;
        for (int y = 0; y < height; y += motion->config.sample_step) {
            for (int x = 0; x < width; x += motion->config.sample_step) {
                motion->previous[count++] =
                    rgb565_luma(pixels[y * width + x]);
            }
        }
        motion->sample_count = count;
        motion->has_baseline = true;
        return false;
    }

    int32_t global_delta = delta_sum / count;
    uint16_t changed = 0U;
    count = 0U;
    for (int y = 0; y < height; y += motion->config.sample_step) {
        for (int x = 0; x < width; x += motion->config.sample_step) {
            uint8_t luma = rgb565_luma(pixels[y * width + x]);
            int32_t residual =
                (int32_t)luma - motion->previous[count] - global_delta;
            if (residual < 0) {
                residual = -residual;
            }
            changed += residual >= motion->config.pixel_delta;
            motion->previous[count++] = luma;
        }
    }

    uint16_t ratio = (uint16_t)((uint32_t)changed * 1000U / count);
    if (changed_permille != NULL) {
        *changed_permille = ratio;
    }
    return ratio >= motion->config.min_changed_permille;
}
