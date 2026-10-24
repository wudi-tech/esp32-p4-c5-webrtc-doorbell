#pragma once

#include <stdint.h>

#include "audio_render.h"
#include "av_render_default.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *doorbell_audio_prompt_handle_t;

audio_render_handle_t av_render_alloc_prompt_overlay(
    i2s_render_cfg_t *i2s_cfg,
    doorbell_audio_prompt_handle_t *prompt_handle);

int doorbell_audio_prompt_start(doorbell_audio_prompt_handle_t handle,
                                const int16_t *samples,
                                int sample_count);
void doorbell_audio_prompt_stop(doorbell_audio_prompt_handle_t handle);

#ifdef __cplusplus
}
#endif
