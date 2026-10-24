#include "doorbell_audio_render.h"

#include <stdbool.h>
#include <inttypes.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "media_lib_os.h"
#include "pcm_overlay.h"

#define TAG "PROMPT_RENDER"

#define PROMPT_SAMPLE_RATE_HZ          16000
#define PROMPT_FALLBACK_SILENCE_MS     40
#define PROMPT_FALLBACK_FRAME_MS       20
#define PROMPT_FALLBACK_MONO_SAMPLES   \
    (PROMPT_SAMPLE_RATE_HZ * PROMPT_FALLBACK_FRAME_MS / 1000)
#define PROMPT_FALLBACK_POLL_MS        5

typedef struct {
    i2s_render_cfg_t i2s_cfg;
    doorbell_audio_prompt_handle_t *prompt_handle_out;
} prompt_render_cfg_t;

typedef struct {
    audio_render_handle_t downstream;
    media_lib_mutex_handle_t mutex;
    av_render_audio_frame_info_t frame_info;
    pcm_overlay_t overlay;
    bool compatible;
    bool prompt_worker_running;
    bool prompt_worker_stop;
    bool prompt_progress_logged;
    bool fallback_logged;
    int64_t last_remote_write_us;
} prompt_render_t;

static void prompt_fallback_worker(void *arg)
{
    prompt_render_t *render = arg;
    int16_t fallback_samples[PROMPT_FALLBACK_MONO_SAMPLES * 2];

    while (true) {
        if (media_lib_mutex_lock(render->mutex, MEDIA_LIB_MAX_LOCK_TIME) != 0) {
            media_lib_thread_sleep(PROMPT_FALLBACK_POLL_MS);
            continue;
        }

        if (render->prompt_worker_stop ||
            !pcm_overlay_active(&render->overlay)) {
            render->prompt_worker_running = false;
            media_lib_mutex_unlock(render->mutex);
            break;
        }

        if (!render->compatible) {
            media_lib_mutex_unlock(render->mutex);
            media_lib_thread_sleep(PROMPT_FALLBACK_POLL_MS);
            continue;
        }

        int64_t now_us = esp_timer_get_time();
        if (pcm_overlay_fallback_due(
                true, now_us, render->last_remote_write_us,
                PROMPT_FALLBACK_SILENCE_MS * 1000LL)) {
            unsigned channels = render->frame_info.channel;
            size_t copied_frames = pcm_overlay_copy_mono(
                &render->overlay, fallback_samples,
                PROMPT_FALLBACK_MONO_SAMPLES * channels, channels);
            if (copied_frames > 0) {
                if (!render->fallback_logged) {
                    ESP_LOGI(TAG, "Prompt fallback output started after remote-audio silence");
                    render->fallback_logged = true;
                }
                av_render_audio_frame_t frame = {
                    .data = (uint8_t *)fallback_samples,
                    .size = (int)(copied_frames * channels * sizeof(int16_t)),
                };
                int ret = audio_render_write(render->downstream, &frame);
                if (ret != 0) {
                    ESP_LOGE(TAG, "Prompt fallback output failed: %d", ret);
                    pcm_overlay_stop(&render->overlay);
                }
            }
            if (!pcm_overlay_active(&render->overlay)) {
                ESP_LOGI(TAG, "Prompt playback completed");
                render->prompt_worker_running = false;
                media_lib_mutex_unlock(render->mutex);
                break;
            }
        }
        media_lib_mutex_unlock(render->mutex);
        media_lib_thread_sleep(PROMPT_FALLBACK_POLL_MS);
    }

    media_lib_thread_destroy(NULL);
}

static audio_render_handle_t prompt_render_init(void *cfg, int cfg_size)
{
    prompt_render_cfg_t *prompt_cfg = cfg;
    if (prompt_cfg == NULL || cfg_size != sizeof(*prompt_cfg) ||
        prompt_cfg->prompt_handle_out == NULL) {
        return NULL;
    }

    prompt_render_t *render = media_lib_calloc(1, sizeof(*render));
    if (render == NULL) {
        return NULL;
    }
    render->downstream = av_render_alloc_i2s_render(&prompt_cfg->i2s_cfg);
    if (render->downstream == NULL || media_lib_mutex_create(&render->mutex) != 0) {
        if (render->downstream != NULL) {
            audio_render_free_handle(render->downstream);
        }
        media_lib_free(render);
        return NULL;
    }
    *prompt_cfg->prompt_handle_out = render;
    return render;
}

static int prompt_render_open(audio_render_handle_t handle,
                              av_render_audio_frame_info_t *info)
{
    prompt_render_t *render = handle;
    if (render == NULL || info == NULL) {
        return -1;
    }
    int ret = audio_render_open(render->downstream, info);
    if (ret == 0 &&
        media_lib_mutex_lock(render->mutex, MEDIA_LIB_MAX_LOCK_TIME) == 0) {
        render->frame_info = *info;
        render->compatible = info->sample_rate == PROMPT_SAMPLE_RATE_HZ &&
                             (info->channel == 1 || info->channel == 2) &&
                             info->bits_per_sample == 16;
        render->last_remote_write_us = esp_timer_get_time();
        if (!render->compatible) {
            ESP_LOGW(TAG, "Prompt waiting: current audio is %" PRIu32 " Hz, %u channel, %u bit",
                     info->sample_rate, info->channel, info->bits_per_sample);
        }
        media_lib_mutex_unlock(render->mutex);
    }
    return ret;
}

static int prompt_render_write(audio_render_handle_t handle,
                               av_render_audio_frame_t *audio_data)
{
    prompt_render_t *render = handle;
    if (render == NULL || audio_data == NULL) {
        return -1;
    }
    if (media_lib_mutex_lock(render->mutex, MEDIA_LIB_MAX_LOCK_TIME) == 0) {
        render->last_remote_write_us = esp_timer_get_time();
        if (render->compatible && audio_data->data != NULL && audio_data->size > 0) {
            bool was_active = pcm_overlay_active(&render->overlay);
            size_t mixed_frames = pcm_overlay_mix_mono(
                &render->overlay, (int16_t *)audio_data->data,
                (size_t)audio_data->size / sizeof(int16_t),
                render->frame_info.channel);
            if (mixed_frames > 0 && !render->prompt_progress_logged) {
                ESP_LOGI(TAG, "Prompt mixed with WebRTC remote audio");
                render->prompt_progress_logged = true;
            }
            if (was_active && !pcm_overlay_active(&render->overlay)) {
                ESP_LOGI(TAG, "Prompt playback completed");
            }
        }
        int ret = audio_render_write(render->downstream, audio_data);
        media_lib_mutex_unlock(render->mutex);
        return ret;
    }
    return -1;
}

static int prompt_render_get_latency(audio_render_handle_t handle,
                                     uint32_t *latency)
{
    prompt_render_t *render = handle;
    return render == NULL ? -1 : audio_render_get_latency(render->downstream, latency);
}

static int prompt_render_get_frame_info(audio_render_handle_t handle,
                                        av_render_audio_frame_info_t *info)
{
    prompt_render_t *render = handle;
    return render == NULL ? -1 : audio_render_get_frame_info(render->downstream, info);
}

static int prompt_render_set_speed(audio_render_handle_t handle, float speed)
{
    prompt_render_t *render = handle;
    return render == NULL ? -1 : audio_render_set_speed(render->downstream, speed);
}

static int prompt_render_close(audio_render_handle_t handle)
{
    prompt_render_t *render = handle;
    if (render == NULL) {
        return -1;
    }
    doorbell_audio_prompt_stop(render);
    if (media_lib_mutex_lock(render->mutex, MEDIA_LIB_MAX_LOCK_TIME) == 0) {
        render->compatible = false;
        media_lib_mutex_unlock(render->mutex);
    }
    return audio_render_close(render->downstream);
}

static void prompt_render_deinit(audio_render_handle_t handle)
{
    prompt_render_t *render = handle;
    if (render == NULL) {
        return;
    }
    doorbell_audio_prompt_stop(render);
    media_lib_mutex_destroy(render->mutex);
    audio_render_free_handle(render->downstream);
    media_lib_free(render);
}

audio_render_handle_t av_render_alloc_prompt_overlay(
    i2s_render_cfg_t *i2s_cfg,
    doorbell_audio_prompt_handle_t *prompt_handle)
{
    if (i2s_cfg == NULL || prompt_handle == NULL) {
        return NULL;
    }
    *prompt_handle = NULL;
    prompt_render_cfg_t prompt_cfg = {
        .i2s_cfg = *i2s_cfg,
        .prompt_handle_out = prompt_handle,
    };
    audio_render_cfg_t cfg = {
        .ops = {
            .init = prompt_render_init,
            .open = prompt_render_open,
            .write = prompt_render_write,
            .get_latency = prompt_render_get_latency,
            .get_frame_info = prompt_render_get_frame_info,
            .set_speed = prompt_render_set_speed,
            .close = prompt_render_close,
            .deinit = prompt_render_deinit,
        },
        .cfg = &prompt_cfg,
        .cfg_size = sizeof(prompt_cfg),
    };
    return audio_render_alloc_handle(&cfg);
}

int doorbell_audio_prompt_start(doorbell_audio_prompt_handle_t handle,
                                const int16_t *samples,
                                int sample_count)
{
    prompt_render_t *render = handle;
    if (render == NULL || samples == NULL || sample_count <= 0) {
        return -1;
    }
    if (media_lib_mutex_lock(render->mutex, MEDIA_LIB_MAX_LOCK_TIME) != 0) {
        return -1;
    }
    pcm_overlay_start(&render->overlay, samples, (size_t)sample_count);
    render->prompt_worker_stop = false;
    render->prompt_progress_logged = false;
    render->fallback_logged = false;
    bool queued_for_audio_path = !render->compatible;
    int ret = 0;
    if (!render->prompt_worker_running) {
        render->prompt_worker_running = true;
        ret = media_lib_thread_create_from_scheduler(
            NULL, "prompt_fallback", prompt_fallback_worker, render);
        if (ret != 0) {
            render->prompt_worker_running = false;
            pcm_overlay_stop(&render->overlay);
        }
    }
    media_lib_mutex_unlock(render->mutex);
    if (ret == 0) {
        ESP_LOGI(TAG, "Prompt playback armed: %d mono samples", sample_count);
        if (queued_for_audio_path) {
            ESP_LOGI(TAG, "Prompt queued until the 16 kHz call audio path is ready");
        }
    }
    return ret;
}

void doorbell_audio_prompt_stop(doorbell_audio_prompt_handle_t handle)
{
    prompt_render_t *render = handle;
    if (render == NULL) {
        return;
    }
    while (true) {
        if (media_lib_mutex_lock(render->mutex, MEDIA_LIB_MAX_LOCK_TIME) != 0) {
            return;
        }
        render->prompt_worker_stop = true;
        pcm_overlay_stop(&render->overlay);
        bool worker_running = render->prompt_worker_running;
        media_lib_mutex_unlock(render->mutex);
        if (!worker_running) {
            return;
        }
        media_lib_thread_sleep(PROMPT_FALLBACK_POLL_MS);
    }
}
