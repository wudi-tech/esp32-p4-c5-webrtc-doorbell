/* Pedestrian detection

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "common.h"
#include "camera_motion.h"
#include "esp_cache.h"
#include "esp_capture_types.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "human_face_detect.hpp"
#include "pedestrian_detect.hpp"
#include "pedestrian_filter.h"
#include "vision_event.h"
#include "vision_tracker.h"
#include "data_queue.h"
#include "media_lib_os.h"

#include <new>

#ifdef __cplusplus
extern "C" {
#endif

static const char *TAG = "PEDESTRIAN_DETECT";

typedef struct {
    pedestrian_detect_cfg_t cfg;
    bool                    filtering;
    bool                    detecting;
    bool                    filter_exited;
    bool                    detect_exited;
    volatile bool           radar_focus;
    volatile bool           degraded;
    bool                    last_confirmed;
    data_queue_t           *q;
} detect_t;

static detect_t *detector;

static void log_camera_probe_frame(const esp_capture_stream_frame_t *frame)
{
    static unsigned frame_count;
    if (frame_count >= 12U || frame == NULL || frame->data == NULL) {
        return;
    }
    const uint8_t *bytes = (const uint8_t *)frame->data;
    unsigned nonzero_samples = 0;
    uint32_t hash = 2166136261U;
    for (size_t i = 0; i < frame->size; i += 64U) {
        nonzero_samples += bytes[i] != 0U;
        hash = (hash ^ bytes[i]) * 16777619U;
    }
    ++frame_count;
    ESP_LOGI(TAG,
             "AI frame probe %u/12: size=%u nonzero_samples=%u hash=%08lx",
             frame_count, (unsigned)frame->size, nonzero_samples,
             (unsigned long)hash);
}

static void detecting_task(void *arg)
{
    detect_t *detect = (detect_t *)arg;
    PedestrianDetect *pedestrian_lib = new (std::nothrow) PedestrianDetect();
    HumanFaceDetect *face_lib = new (std::nothrow) HumanFaceDetect();
    vision_tracker_t tracker;
    vision_tracker_config_t tracker_config = vision_tracker_default_config();
    unsigned slow_streak = 0U;
    unsigned recovery_streak = 0U;
    do {
        if (pedestrian_lib == NULL && face_lib == NULL) {
            ESP_LOGE(TAG, "Both vision models are unavailable");
            break;
        }
        if (!vision_tracker_init(&tracker, &tracker_config)) {
            delete pedestrian_lib;
            pedestrian_lib = NULL;
            delete face_lib;
            face_lib = NULL;
        }
        if (pedestrian_lib == NULL && face_lib == NULL) {
            break;
        }
        if (pedestrian_lib != NULL) {
            pedestrian_lib->set_score_thr(tracker_config.pedestrian_min_score);
        }
        ESP_LOGI(TAG, "Vision models: pedestrian=%s face=%s",
                 pedestrian_lib != NULL ? "ready" : "unavailable",
                 face_lib != NULL ? "ready" : "unavailable");
        while (detect->detecting) {
            void *data = NULL;
            int size = 0;
            data_queue_read_lock(detect->q, &data, &size);
            if (data == NULL) {
                break;
            }
            esp_capture_stream_frame_t *frame = (esp_capture_stream_frame_t *)data;
            dl::image::img_t img = {
                .data = (void *)frame->data,
                .width = DETECT_WIDTH,
                .height = DETECT_HEIGHT,
                .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565LE,
            };
            int64_t inference_started_us = esp_timer_get_time();
            vision_detection_t detections[VISION_MAX_TARGETS] = {};
            size_t detection_count = 0U;
            bool has_plausible_pedestrian = false;
            if (pedestrian_lib != NULL) {
                auto &pedestrian_results = pedestrian_lib->run(img);
                pedestrian_filter_config_t plausibility =
                    pedestrian_filter_default_config();
                for (const auto &res : pedestrian_results) {
                    pedestrian_detection_t candidate = {
                        .score = res.score,
                        .x1 = res.box[0],
                        .y1 = res.box[1],
                        .x2 = res.box[2],
                        .y2 = res.box[3],
                    };
                    if (!pedestrian_detection_is_plausible(
                            &plausibility, &candidate, DETECT_WIDTH,
                            DETECT_HEIGHT)) {
                        continue;
                    }
                    has_plausible_pedestrian = true;
                    if (detection_count < VISION_MAX_TARGETS) {
                        detections[detection_count++] = {
                            .kind = VISION_KIND_PEDESTRIAN,
                            .score = res.score,
                            .x1 = res.box[0],
                            .y1 = res.box[1],
                            .x2 = res.box[2],
                            .y2 = res.box[3],
                        };
                    }
                }
            }
            if (face_lib != NULL &&
                (detect->radar_focus || !has_plausible_pedestrian)) {
                auto &face_results = face_lib->run(img);
                for (const auto &res : face_results) {
                    if (detection_count >= VISION_MAX_TARGETS) {
                        break;
                    }
                    detections[detection_count++] = {
                        .kind = VISION_KIND_FACE,
                        .score = res.score,
                        .x1 = res.box[0],
                        .y1 = res.box[1],
                        .x2 = res.box[2],
                        .y2 = res.box[3],
                    };
                }
            }
            data_queue_read_unlock(detect->q);

            uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
            vision_update_t update = vision_tracker_update(
                &tracker, detections, detection_count,
                DETECT_WIDTH, DETECT_HEIGHT, now_ms);

            vision_snapshot_t snapshot = {
                .timestamp_ms = now_ms,
                .source_width = DETECT_WIDTH,
                .source_height = DETECT_HEIGHT,
                .update = update,
                .pedestrian_model = pedestrian_lib != NULL
                                        ? VISION_MODEL_READY
                                        : VISION_MODEL_UNAVAILABLE,
                .face_model = face_lib != NULL ? VISION_MODEL_READY
                                               : VISION_MODEL_UNAVAILABLE,
            };
            if (detect->cfg.snapshot != NULL) {
                detect->cfg.snapshot(&snapshot, detect->cfg.ctx);
            }
            if (update.confirmed_present && !detect->last_confirmed &&
                detect->cfg.detected != NULL && update.target_count > 0U) {
                const vision_target_t *target = &update.targets[0];
                esp_capture_rgn_t rgn = {
                    .x = (uint16_t)target->x1,
                    .y = (uint16_t)target->y1,
                    .width = (uint16_t)(target->x2 - target->x1),
                    .height = (uint16_t)(target->y2 - target->y1),
                };
                detect->cfg.detected(&rgn, detect->cfg.ctx);
            }
            if (update.confirmed_present != detect->last_confirmed) {
                ESP_LOGI(TAG, "Vision presence %s (%u target%s)",
                         update.confirmed_present ? "confirmed" : "cleared",
                         (unsigned)update.confirmed_person_count,
                         update.confirmed_person_count == 1U ? "" : "s");
                detect->last_confirmed = update.confirmed_present;
            }

            int64_t elapsed_us = esp_timer_get_time() - inference_started_us;
            if (elapsed_us > 450000) {
                ++slow_streak;
                recovery_streak = 0U;
            } else if (elapsed_us < 350000) {
                slow_streak = 0U;
                if (detect->degraded) {
                    ++recovery_streak;
                }
            } else {
                slow_streak = 0U;
                recovery_streak = 0U;
            }
            if (!detect->degraded && slow_streak >= 3U) {
                detect->degraded = true;
                slow_streak = 0U;
                ESP_LOGW(TAG, "AI inference slow; reducing detector to 1 FPS");
            } else if (detect->degraded && recovery_streak >= 30U) {
                detect->degraded = false;
                recovery_streak = 0U;
                ESP_LOGI(TAG, "AI inference recovered; restoring detector to 2 FPS");
            }
        }
    } while (0);
    delete pedestrian_lib;
    delete face_lib;
    detect->detect_exited = true;
    ESP_LOGI(TAG, "Detecting exited");
    vTaskDelete(NULL);
}

static void filter_task(void *arg)
{
    detect_t *detect = (detect_t *)arg;
    camera_motion_t motion;
    camera_motion_config_t motion_config = camera_motion_default_config();
    esp_capture_stream_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.stream_type = ESP_CAPTURE_STREAM_TYPE_VIDEO;
    esp_capture_sink_handle_t sink = get_detect_sink();
    if (sink == NULL) {
        ESP_LOGE(TAG, "Pedestrian detection sink is unavailable");
        detect->filter_exited = true;
        media_lib_thread_destroy(NULL);
        return;
    }
    if (!camera_motion_init(&motion, &motion_config)) {
        ESP_LOGE(TAG, "Failed to initialize camera motion confirmation");
        detect->filter_exited = true;
        media_lib_thread_destroy(NULL);
        return;
    }
    while (detect->filtering) {
        static unsigned frame_sequence;
        int ret = esp_capture_sink_acquire_frame(sink, &frame, false);
        if (ret != ESP_CAPTURE_ERR_OK) {
            break;
        }
        esp_err_t cache_ret = esp_cache_msync(
            frame.data, frame.size, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
        if (cache_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to synchronize detector frame cache: %s",
                     esp_err_to_name(cache_ret));
            esp_capture_sink_release_frame(sink, &frame);
            break;
        }
        log_camera_probe_frame(&frame);
        uint16_t changed_permille = 0U;
        bool motion_confirmed = camera_motion_update_rgb565(
            &motion, (const uint16_t *)frame.data, DETECT_WIDTH,
            DETECT_HEIGHT, &changed_permille);
        if (!motion.has_baseline) {
            esp_capture_sink_release_frame(sink, &frame);
            continue;
        }
        if (motion_confirmed) {
            ESP_LOGI(TAG, "Camera motion confirmed: changed=%u.%u%%",
                     changed_permille / 10U, changed_permille % 10U);
        }
        ++frame_sequence;
        if (detect->degraded && (frame_sequence & 1U) != 0U) {
            esp_capture_sink_release_frame(sink, &frame);
            continue;
        }
        int size = sizeof(esp_capture_stream_frame_t) + frame.size;
        // Changed function name to match actual API
        void *data = data_queue_get_buffer(detect->q, size);
        if (data) {
            esp_capture_stream_frame_t *filter = (esp_capture_stream_frame_t *)data;
            *filter = frame;
            filter->data = (uint8_t *)data + sizeof(esp_capture_stream_frame_t);
            memcpy(filter->data, frame.data, frame.size);
            data_queue_send_buffer(detect->q, size);
        }
        esp_capture_sink_release_frame(sink, &frame);
        if (data == NULL && detect->filtering) {
            ESP_LOGE(TAG, "Fail to get buffer");
            break;
        }
    }
    ESP_LOGI(TAG, "Detect filtering exited");
    detect->filter_exited = true;
    media_lib_thread_destroy(NULL);
}

int start_pedestrian_detection(pedestrian_detect_cfg_t *cfg)
{
    if (cfg == NULL) {
        ESP_LOGE(TAG, "Pedestrian detection config is required");
        return -1;
    }
    if (detector) {
        ESP_LOGW(TAG, "Detect already running");
        return 0;
    }
    detector = (detect_t *)calloc(1, sizeof(detect_t));
    if (detector == NULL) {
        ESP_LOGE(TAG, "Failed to allocate pedestrian detector");
        return -1;
    }
    do {
        // Create dual buffer to avoid block output too long
        int size = DETECT_WIDTH * DETECT_HEIGHT * 2 + 256;
        detector->q = data_queue_init(size);
        if (detector->q == NULL) {
            ESP_LOGE(TAG, "Fail to init queue");
            break;
        }
        detector->cfg = *cfg;
        detector->filtering = true;
        detector->detecting = true;
        int ret = media_lib_thread_create_from_scheduler(NULL, "detect_filter", filter_task, detector);
        if (ret != 0) {
            detector->filtering = false;
            break;
        }
        ret = media_lib_thread_create_from_scheduler(NULL, "detect", detecting_task, detector);
        if (ret != 0) {
            detector->detecting = false;
            break;
        }
        return 0;
    } while (0);
    stop_pedestrian_detection();
    return -1;
}

void stop_pedestrian_detection()
{
    if (detector == NULL) {
        return;
    }
    if (detector->detecting || detector->filtering) {
        bool detecting = detector->detecting;
        bool filtering = detector->filtering;
        detector->detecting = false;
        detector->filtering = false;
        data_queue_wakeup(detector->q);
        if (detecting) {
            while (!detector->detect_exited) {
                media_lib_thread_sleep(10);
            }
        }
        if (filtering) {
            while (!detector->filter_exited) {
                media_lib_thread_sleep(10);
            }
        }
    }
    if (detector->q) {
        data_queue_deinit(detector->q);
    }
    free(detector);
    detector = NULL;
}

void set_vision_radar_focus(bool focused)
{
    if (detector != NULL) {
        detector->radar_focus = focused;
    }
}

#ifdef __cplusplus
}
#endif
