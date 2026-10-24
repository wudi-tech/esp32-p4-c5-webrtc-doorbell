/* DoorBell WebRTC application code

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "esp_webrtc.h"
#include "media_lib_os.h"
#include "driver/gpio.h"
#include "common.h"
#include "esp_log.h"
#include "esp_webrtc_defaults.h"
#include "esp_peer_default.h"
#include "webrtc_http_server.h"
#include "door_lock.h"
#include "esp_peer.h"
#include <string.h>
#include <inttypes.h>

#define TAG "DOOR_BELL"

// Customized commands
#define DOOR_BELL_OPEN_DOOR_CMD           "OPEN_DOOR"
#define DOOR_BELL_DOOR_OPENING_CMD        "DOOR_OPENING"
#define DOOR_BELL_DOOR_OPENED_CMD         "DOOR_OPENED"
#define DOOR_BELL_DOOR_CLOSING_CMD        "DOOR_CLOSING"
#define DOOR_BELL_DOOR_LOCKED_CMD         "DOOR_LOCKED"
#define DOOR_BELL_DOOR_FAILED_CMD         "DOOR_FAILED"
#define DOOR_BELL_RING_CMD                "RING"
#define DOOR_BELL_RING_STOPPED_CMD        "RING_STOPPED"
#define DOOR_BELL_CALL_ACCEPTED_CMD        "ACCEPT_CALL"
#define DOOR_BELL_CALL_DENIED_CMD         "DENY_CALL"
#define DOOR_BELL_CALL_ENDED_CMD          "CALL_ENDED"
#define DOOR_BELL_CALL_END_FAILED_CMD     "CALL_END_FAILED"
#define DOOR_BELL_ENABLE_RTP_TRANSFORMER  "ENABLE_RTP_TRANSFORMER"
#define DOOR_BELL_DISABLE_RTP_TRANSFORMER "DISABLE_RTP_TRANSFORMER"

#define SAME_STR(a, b) (strncmp(a, b, sizeof(b) - 1) == 0)
#define SEND_CMD(webrtc, cmd) \
    esp_webrtc_send_custom_data(webrtc, ESP_WEBRTC_CUSTOM_DATA_VIA_SIGNALING, (uint8_t *)cmd, strlen(cmd))
#define ELEMS(arr)       sizeof(arr)/sizeof(arr[0])
#define RTP_HEADER_SIZE  12
#define DOOR_BELL_RING_DURATION_MS 4000

typedef enum {
    DOOR_BELL_STATE_NONE,
    DOOR_BELL_STATE_RINGING,
    DOOR_BELL_STATE_CONNECTING,
    DOOR_BELL_STATE_CONNECTED,
    DOOR_BELL_STATE_DISCONNECTING,
} door_bell_state_t;

typedef enum {
    DOOR_BELL_TONE_RING,
    DOOR_BELL_TONE_OPEN_DOOR,
    DOOR_BELL_TONE_JOIN_SUCCESS,
} door_bell_tone_type_t;

typedef struct {
    const uint8_t *start;
    const uint8_t *end;
    int            duration;
} door_bell_tone_data_t;

typedef struct {
    esp_peer_data_channel_info_t info;
    esp_webrtc_handle_t owner_handle;
    int send_count;
    int recv_count;
    bool used;
} user_data_ch_t;

typedef struct {
    bool enable;
    door_bell_state_t expected_state;
    door_bell_state_t previous_state;
    esp_webrtc_handle_t target_handle;
} peer_connection_change_request_t;

typedef struct {
    esp_webrtc_handle_t target_handle;
    uint32_t generation;
} ring_stop_request_t;

static esp_webrtc_handle_t webrtc;
static door_bell_state_t   door_bell_state;
static media_lib_mutex_handle_t door_bell_mutex;
static media_lib_mutex_handle_t webrtc_api_mutex;
static media_lib_mutex_handle_t webrtc_lifecycle_mutex;
static media_lib_mutex_handle_t data_channel_mutex;
static bool                monitor_key;
static bool                key_thread_active;
static bool                data_thread_enabled;
static user_data_ch_t      user_ch[2];
static bool data_running = false;
static uint32_t ring_generation;

extern const uint8_t ring_music_start[] asm("_binary_ring_aac_start");
extern const uint8_t ring_music_end[] asm("_binary_ring_aac_end");
extern const uint8_t open_music_start[] asm("_binary_open_aac_start");
extern const uint8_t open_music_end[] asm("_binary_open_aac_end");
extern const uint8_t join_music_start[] asm("_binary_join_aac_start");
extern const uint8_t join_music_end[] asm("_binary_join_aac_end");

// RTP Transformer test context
typedef struct {
    uint32_t frame_count;
    uint32_t error_count;
} rtp_transformer_ctx_t;

static rtp_transformer_ctx_t sender_transformer_ctx;
static rtp_transformer_ctx_t receiver_transformer_ctx;
static bool rtp_transformer_enabled = false;

static int play_tone(door_bell_tone_type_t type)
{
    door_bell_tone_data_t tone_data[] = {
        { ring_music_start, ring_music_end, DOOR_BELL_RING_DURATION_MS },
        { open_music_start, open_music_end, 0 },
        { join_music_start, join_music_end, 0 },
    };
    if (type >= sizeof(tone_data) / sizeof(tone_data[0])) {
        return 0;
    }
    return play_music(tone_data[type].start, (int)(tone_data[type].end - tone_data[type].start), tone_data[type].duration);
}

int play_tone_int(int t)
{
    return play_tone((door_bell_tone_type_t)t);
}

static int door_bell_sync_init(void)
{
    bool door_bell_mutex_created = false;
    bool webrtc_api_mutex_created = false;
    bool webrtc_lifecycle_mutex_created = false;

    if (door_bell_mutex == NULL) {
        if (media_lib_mutex_create(&door_bell_mutex) != 0) {
            return -1;
        }
        door_bell_mutex_created = true;
    }
    if (webrtc_api_mutex == NULL) {
        if (media_lib_mutex_create(&webrtc_api_mutex) != 0) {
            goto sync_init_failed;
        }
        webrtc_api_mutex_created = true;
    }
    if (webrtc_lifecycle_mutex == NULL) {
        if (media_lib_mutex_create(&webrtc_lifecycle_mutex) != 0) {
            goto sync_init_failed;
        }
        webrtc_lifecycle_mutex_created = true;
    }
    if (data_channel_mutex == NULL &&
        media_lib_mutex_create(&data_channel_mutex) != 0) {
        goto sync_init_failed;
    }
    return 0;

sync_init_failed:
    if (webrtc_lifecycle_mutex_created) {
        media_lib_mutex_destroy(webrtc_lifecycle_mutex);
        webrtc_lifecycle_mutex = NULL;
    }
    if (webrtc_api_mutex_created) {
        media_lib_mutex_destroy(webrtc_api_mutex);
        webrtc_api_mutex = NULL;
    }
    if (door_bell_mutex_created) {
        media_lib_mutex_destroy(door_bell_mutex);
        door_bell_mutex = NULL;
    }
    return -1;
}

int doorbell_webrtc_init(void)
{
    return door_bell_sync_init();
}

static bool door_bell_sync_ready(void)
{
    return door_bell_mutex != NULL && webrtc_api_mutex != NULL &&
           webrtc_lifecycle_mutex != NULL && data_channel_mutex != NULL;
}

static bool doorbell_worker_enabled(bool data_worker)
{
    bool enabled = false;
    if (door_bell_mutex != NULL &&
        media_lib_mutex_lock(door_bell_mutex, MEDIA_LIB_MAX_LOCK_TIME) == 0) {
        enabled = data_worker ? data_thread_enabled : monitor_key;
        media_lib_mutex_unlock(door_bell_mutex);
    }
    return enabled;
}

static void doorbell_worker_finished(bool data_worker)
{
    if (door_bell_mutex != NULL &&
        media_lib_mutex_lock(door_bell_mutex, MEDIA_LIB_MAX_LOCK_TIME) == 0) {
        if (data_worker) {
            data_running = false;
        } else {
            key_thread_active = false;
        }
        media_lib_mutex_unlock(door_bell_mutex);
    }
}

static int stop_doorbell_workers(void)
{
    if (door_bell_mutex == NULL) {
        return 0;
    }
    if (media_lib_mutex_lock(door_bell_mutex, MEDIA_LIB_MAX_LOCK_TIME) != 0) {
        ESP_LOGE(TAG, "Failed to request doorbell worker stop");
        return -1;
    }
    monitor_key = false;
    data_thread_enabled = false;
    media_lib_mutex_unlock(door_bell_mutex);

    while (true) {
        bool active = true;
        if (media_lib_mutex_lock(door_bell_mutex,
                                 MEDIA_LIB_MAX_LOCK_TIME) == 0) {
            active = key_thread_active || data_running;
            media_lib_mutex_unlock(door_bell_mutex);
        }
        if (!active) {
            break;
        }
        media_lib_thread_sleep(10);
    }
    return 0;
}

static void door_bell_change_state_locked(door_bell_state_t state)
{
    door_bell_state = state;
    if (state == DOOR_BELL_STATE_CONNECTING ||
        state == DOOR_BELL_STATE_DISCONNECTING ||
        state == DOOR_BELL_STATE_NONE) {
        stop_music();
    }
}

static void door_bell_change_state(esp_webrtc_handle_t source_handle,
                                   door_bell_state_t state)
{
    if (door_bell_mutex == NULL ||
        media_lib_mutex_lock(door_bell_mutex, MEDIA_LIB_MAX_LOCK_TIME) != 0) {
        ESP_LOGE(TAG, "Unable to lock doorbell state for transition to %d",
                 state);
        return;
    }
    if (webrtc != source_handle) {
        ESP_LOGI(TAG, "Ignoring state %d from stale WebRTC handle", state);
    } else if (door_bell_state == DOOR_BELL_STATE_DISCONNECTING &&
        state == DOOR_BELL_STATE_CONNECTED) {
        ESP_LOGI(TAG, "Ignoring CONNECTED while peer teardown is pending");
    } else {
        door_bell_change_state_locked(state);
    }
    media_lib_mutex_unlock(door_bell_mutex);
}

static void restore_peer_change_state_locked(
    door_bell_state_t expected_state, door_bell_state_t previous_state)
{
    if (door_bell_state != expected_state) {
        return;
    }
    door_bell_state = previous_state;
    if (previous_state == DOOR_BELL_STATE_RINGING) {
        int tone_ret = play_tone(DOOR_BELL_TONE_RING);
        if (tone_ret != 0) {
            ESP_LOGW(TAG, "Failed to restore ring tone: %d", tone_ret);
        }
    }
}

static void ring_stop_worker(void *arg)
{
    ring_stop_request_t *request = arg;
    media_lib_thread_sleep(DOOR_BELL_RING_DURATION_MS);
    if (request != NULL && webrtc_api_mutex != NULL && door_bell_mutex != NULL &&
        media_lib_mutex_lock(webrtc_api_mutex, MEDIA_LIB_MAX_LOCK_TIME) == 0) {
        bool notify = false;
        if (media_lib_mutex_lock(door_bell_mutex, MEDIA_LIB_MAX_LOCK_TIME) == 0) {
            notify = webrtc == request->target_handle &&
                     door_bell_state == DOOR_BELL_STATE_RINGING &&
                     ring_generation == request->generation;
            media_lib_mutex_unlock(door_bell_mutex);
        }
        if (notify) {
            int ret = SEND_CMD(request->target_handle,
                               DOOR_BELL_RING_STOPPED_CMD);
            if (ret != 0) {
                ESP_LOGW(TAG, "Failed to send ring-stop event: %d", ret);
            }
        }
        media_lib_mutex_unlock(webrtc_api_mutex);
    }
    media_lib_free(request);
    media_lib_thread_destroy(NULL);
}

static int schedule_ring_stop(esp_webrtc_handle_t target_handle,
                              uint32_t generation)
{
    ring_stop_request_t *request = media_lib_calloc(1, sizeof(*request));
    if (request == NULL) {
        return -1;
    }
    request->target_handle = target_handle;
    request->generation = generation;
    int ret = media_lib_thread_create_from_scheduler(
        NULL, "ring_stop", ring_stop_worker, request);
    if (ret != 0) {
        media_lib_free(request);
    }
    return ret;
}

doorbell_ring_request_result_t doorbell_request_ring(const char *source)
{
    const char *request_source = source != NULL ? source : "unknown";
    doorbell_ring_request_result_t result = DOORBELL_RING_NOT_READY;
    bool reserved = false;
    uint32_t reserved_generation = 0;

    if (webrtc_api_mutex == NULL || door_bell_mutex == NULL ||
        media_lib_mutex_lock(webrtc_api_mutex, MEDIA_LIB_MAX_LOCK_TIME) != 0) {
        ESP_LOGW(TAG, "Ring request from %s rejected: %s", request_source,
                 doorbell_ring_result_name(result));
        return result;
    }
    if (
        media_lib_mutex_lock(door_bell_mutex, MEDIA_LIB_MAX_LOCK_TIME) != 0) {
        ESP_LOGW(TAG, "Ring request from %s rejected: %s", request_source,
                 doorbell_ring_result_name(result));
        media_lib_mutex_unlock(webrtc_api_mutex);
        return result;
    }

    esp_webrtc_handle_t ring_handle = webrtc;
    result = doorbell_ring_policy_evaluate(
        ring_handle != NULL, door_bell_state == DOOR_BELL_STATE_NONE);
    door_bell_state_t previous_state = door_bell_state;
    if (result == DOORBELL_RING_ACCEPTED) {
        door_bell_state = DOOR_BELL_STATE_RINGING;
        reserved_generation = ++ring_generation;
        reserved = true;
    } else {
        ESP_LOGW(TAG, "Ring request from %s rejected: %s", request_source,
                 doorbell_ring_result_name(result));
    }
    media_lib_mutex_unlock(door_bell_mutex);

    if (!reserved) {
        media_lib_mutex_unlock(webrtc_api_mutex);
        return result;
    }

    int send_ret = SEND_CMD(ring_handle, DOOR_BELL_RING_CMD);
    if (media_lib_mutex_lock(door_bell_mutex, MEDIA_LIB_MAX_LOCK_TIME) != 0) {
        media_lib_mutex_unlock(webrtc_api_mutex);
        return send_ret == 0 ? DOORBELL_RING_ACCEPTED
                             : DOORBELL_RING_SEND_FAILED;
    }
    if (send_ret != 0) {
        ESP_LOGE(TAG, "Ring request from %s failed to send: %d",
                 request_source, send_ret);
        if (door_bell_state == DOOR_BELL_STATE_RINGING) {
            door_bell_state = previous_state;
        }
        result = DOORBELL_RING_SEND_FAILED;
    } else if (door_bell_state == DOOR_BELL_STATE_RINGING) {
        int tone_ret = play_tone(DOOR_BELL_TONE_RING);
        if (tone_ret != 0) {
            ESP_LOGW(TAG, "Ring tone failed for %s request: %d",
                     request_source, tone_ret);
        }
        ESP_LOGI(TAG, "Ring request from %s accepted", request_source);
        if (schedule_ring_stop(ring_handle, reserved_generation) != 0) {
            ESP_LOGW(TAG, "Failed to schedule browser ring-stop event");
        }
        result = DOORBELL_RING_ACCEPTED;
    } else {
        ESP_LOGI(TAG, "Ring request from %s accepted without tone after state transition",
                 request_source);
        result = DOORBELL_RING_ACCEPTED;
    }
    media_lib_mutex_unlock(door_bell_mutex);
    media_lib_mutex_unlock(webrtc_api_mutex);
    return result;
}

static void door_lock_state_changed(door_lock_state_t state, void *ctx)
{
    (void)ctx;
    if (door_bell_mutex == NULL ||
        media_lib_mutex_lock(door_bell_mutex, MEDIA_LIB_MAX_LOCK_TIME) != 0) {
        return;
    }
    esp_webrtc_handle_t callback_handle = webrtc;
    if (callback_handle == NULL) {
        media_lib_mutex_unlock(door_bell_mutex);
        return;
    }

    switch (state) {
        case DOOR_LOCK_STATE_OPENING:
            SEND_CMD(callback_handle, DOOR_BELL_DOOR_OPENING_CMD);
            break;
        case DOOR_LOCK_STATE_UNLOCKED:
            SEND_CMD(callback_handle, DOOR_BELL_DOOR_OPENED_CMD);
            break;
        case DOOR_LOCK_STATE_CLOSING:
            SEND_CMD(callback_handle, DOOR_BELL_DOOR_CLOSING_CMD);
            break;
        case DOOR_LOCK_STATE_LOCKED:
            SEND_CMD(callback_handle, DOOR_BELL_DOOR_LOCKED_CMD);
            break;
        case DOOR_LOCK_STATE_FAULT:
            SEND_CMD(callback_handle, DOOR_BELL_DOOR_FAILED_CMD);
            break;
        default:
            break;
    }
    media_lib_mutex_unlock(door_bell_mutex);
}

#ifdef CONFIG_DOORBELL_SUPPORT_PEDESTRIAN_DETECT
static esp_capture_sink_handle_t detect_sink = NULL;

esp_capture_sink_handle_t get_detect_sink(void)
{
    return detect_sink;
}

int pedestrian_detected(esp_capture_rgn_t *rgn, void *ctx)
{
    (void)rgn;
    (void)ctx;
    char region_str[128];
    snprintf(region_str, sizeof(region_str) - 1, "PEDESTRIAN_DETECTED");
    if (door_bell_mutex == NULL ||
        media_lib_mutex_lock(door_bell_mutex, MEDIA_LIB_MAX_LOCK_TIME) != 0) {
        return 0;
    }
    esp_webrtc_handle_t detection_handle = webrtc;
    if (detection_handle != NULL) {
        SEND_CMD(detection_handle, region_str);
    }
    media_lib_mutex_unlock(door_bell_mutex);
    doorbell_report_vision_detection();
    return 0;
}

static void vision_snapshot_received(const vision_snapshot_t *snapshot,
                                     void *ctx)
{
    (void)ctx;
    doorbell_report_vision_snapshot(snapshot);
}
#endif

static void peer_connection_change_worker(void *arg)
{
    peer_connection_change_request_t *request = arg;
    esp_webrtc_handle_t peer_handle = NULL;

    if (request != NULL && webrtc_api_mutex != NULL &&
        door_bell_mutex != NULL &&
        media_lib_mutex_lock(webrtc_api_mutex, MEDIA_LIB_MAX_LOCK_TIME) == 0) {
        if (media_lib_mutex_lock(door_bell_mutex, MEDIA_LIB_MAX_LOCK_TIME) ==
            0) {
            if (webrtc == request->target_handle &&
                door_bell_state == request->expected_state) {
                peer_handle = request->target_handle;
            }
            media_lib_mutex_unlock(door_bell_mutex);
        }

        if (peer_handle != NULL) {
            int ret = esp_webrtc_enable_peer_connection(peer_handle, request->enable);
            if (ret != 0) {
                ESP_LOGE(TAG, "Failed to %s peer connection: %d",
                         request->enable ? "enable" : "disable", ret);
            }
            bool handle_is_current = false;
            if (media_lib_mutex_lock(door_bell_mutex,
                                     MEDIA_LIB_MAX_LOCK_TIME) == 0) {
                handle_is_current = webrtc == peer_handle;
                if (handle_is_current &&
                    door_bell_state == request->expected_state) {
                    if (ret == 0) {
                        if (!request->enable) {
                            door_bell_change_state_locked(
                                DOOR_BELL_STATE_NONE);
                        }
                    } else {
                        restore_peer_change_state_locked(
                            request->expected_state, request->previous_state);
                    }
                }
                media_lib_mutex_unlock(door_bell_mutex);
            }
            if (!request->enable && handle_is_current) {
                const char *result_cmd = ret == 0
                                             ? DOOR_BELL_CALL_ENDED_CMD
                                             : DOOR_BELL_CALL_END_FAILED_CMD;
                int send_ret = SEND_CMD(peer_handle, result_cmd);
                if (send_ret != 0) {
                    ESP_LOGW(TAG, "Failed to send %s: %d", result_cmd, send_ret);
                }
            }
        }
        media_lib_mutex_unlock(webrtc_api_mutex);
    }

    media_lib_free(request);
    media_lib_thread_destroy(NULL);
}

static int schedule_peer_connection_change(
    esp_webrtc_handle_t target_handle, bool enable,
    door_bell_state_t expected_state, door_bell_state_t previous_state)
{
    peer_connection_change_request_t *request =
        media_lib_calloc(1, sizeof(*request));
    if (request == NULL) {
        return -1;
    }
    request->enable = enable;
    request->expected_state = expected_state;
    request->previous_state = previous_state;
    request->target_handle = target_handle;
    int ret = media_lib_thread_create_from_scheduler(
        NULL, enable ? "peer_enable" : "peer_disable",
        peer_connection_change_worker, request);
    if (ret != 0) {
        media_lib_free(request);
    }
    return ret;
}

static int door_bell_on_cmd(esp_webrtc_custom_data_via_t via, uint8_t *data, int size, void *ctx)
{
    // Only handle signaling message
    if (via != ESP_WEBRTC_CUSTOM_DATA_VIA_SIGNALING) {
        return 0;
    }
    if (size == 0 || door_bell_mutex == NULL ||
        media_lib_mutex_lock(door_bell_mutex, MEDIA_LIB_MAX_LOCK_TIME) != 0) {
        return 0;
    }
    esp_webrtc_handle_t command_handle = webrtc;
    if (command_handle == NULL) {
        media_lib_mutex_unlock(door_bell_mutex);
        return 0;
    }
    ESP_LOGI(TAG, "Receive command %.*s", size, (char *)data);
    const char *cmd = (const char *)data;
    if (SAME_STR(cmd, DOOR_BELL_OPEN_DOOR_CMD)) {
        esp_err_t ret = door_lock_request_open();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Door open request failed: %s", esp_err_to_name(ret));
            SEND_CMD(command_handle, DOOR_BELL_DOOR_FAILED_CMD);
        } else if (door_bell_state == DOOR_BELL_STATE_CONNECTED) {
            if (play_open_door_prompt_in_call() != 0) {
                ESP_LOGW(TAG, "Open-door prompt was not mixed into the call");
            }
        } else if (door_bell_state < DOOR_BELL_STATE_CONNECTING) {
            play_tone(DOOR_BELL_TONE_OPEN_DOOR);
        }
    } else if (SAME_STR(cmd, DOOR_BELL_CALL_ACCEPTED_CMD)) {
        if (door_bell_state != DOOR_BELL_STATE_NONE &&
            door_bell_state != DOOR_BELL_STATE_RINGING) {
            ESP_LOGW(TAG, "Ignoring ACCEPT_CALL in state %d", door_bell_state);
        } else {
            door_bell_state_t state_before_connect = door_bell_state;
            door_bell_change_state_locked(DOOR_BELL_STATE_CONNECTING);
            if (schedule_peer_connection_change(
                    command_handle, true, DOOR_BELL_STATE_CONNECTING,
                    state_before_connect) != 0) {
                ESP_LOGE(TAG, "Failed to schedule peer connection enable");
                restore_peer_change_state_locked(
                    DOOR_BELL_STATE_CONNECTING, state_before_connect);
            }
        }
    } else if (SAME_STR(cmd, DOOR_BELL_CALL_DENIED_CMD)) {
        if (door_bell_state == DOOR_BELL_STATE_NONE) {
            SEND_CMD(command_handle, DOOR_BELL_CALL_ENDED_CMD);
        } else if (door_bell_state != DOOR_BELL_STATE_RINGING &&
            door_bell_state != DOOR_BELL_STATE_CONNECTING &&
            door_bell_state != DOOR_BELL_STATE_CONNECTED) {
            ESP_LOGW(TAG, "Ignoring DENY_CALL in state %d", door_bell_state);
        } else {
            door_bell_state_t state_before_disconnect = door_bell_state;
            door_bell_change_state_locked(DOOR_BELL_STATE_DISCONNECTING);
            if (schedule_peer_connection_change(
                    command_handle, false, DOOR_BELL_STATE_DISCONNECTING,
                    state_before_disconnect) != 0) {
                ESP_LOGE(TAG, "Failed to schedule peer connection disable");
                restore_peer_change_state_locked(
                    DOOR_BELL_STATE_DISCONNECTING, state_before_disconnect);
                SEND_CMD(command_handle, DOOR_BELL_CALL_END_FAILED_CMD);
            }
        }
    } else if (SAME_STR(cmd, DOOR_BELL_ENABLE_RTP_TRANSFORMER)) {
        if (!rtp_transformer_enabled) {
            rtp_transformer_enabled = true;
            ESP_LOGI(TAG, "RTP transformer enabled via command");
        }
    } else if (SAME_STR(cmd, DOOR_BELL_DISABLE_RTP_TRANSFORMER)) {
        if (rtp_transformer_enabled) {
            rtp_transformer_enabled = false;
            ESP_LOGI(TAG, "RTP transformer disabled via command");
        }
    }
    media_lib_mutex_unlock(door_bell_mutex);
    return 0;
}

int close_data_channel(int id)
{
    if (id < 0 || id >= (int)ELEMS(user_ch) || webrtc_api_mutex == NULL ||
        data_channel_mutex == NULL ||
        media_lib_mutex_lock(webrtc_api_mutex,
                             MEDIA_LIB_MAX_LOCK_TIME) != 0) {
        return -1;
    }
    char *label = NULL;
    if (media_lib_mutex_lock(data_channel_mutex,
                             MEDIA_LIB_MAX_LOCK_TIME) != 0) {
        media_lib_mutex_unlock(webrtc_api_mutex);
        return -1;
    }
    if (webrtc != NULL && user_ch[id].used &&
        user_ch[id].owner_handle == webrtc &&
        user_ch[id].info.label != NULL) {
        label = strdup(user_ch[id].info.label);
    }
    media_lib_mutex_unlock(data_channel_mutex);

    int ret = 0;
    if (webrtc != NULL && label != NULL) {
        ESP_LOGI(TAG, "Start to Close data channel %s", label);
        esp_peer_handle_t peer_handle = NULL;
        esp_webrtc_get_peer_connection(webrtc, &peer_handle);
        if (peer_handle != NULL) {
            ret = esp_peer_close_data_channel(peer_handle, label);
        }
    }
    free(label);
    media_lib_mutex_unlock(webrtc_api_mutex);
    return ret;
}

static void add_channel(esp_peer_data_channel_info_t *ch,
                        esp_webrtc_handle_t owner_handle)
{
    if (data_channel_mutex == NULL || ch == NULL) {
        return;
    }
    char def_label[2] = "0";
    char *new_label = NULL;
    def_label[0] += ch->stream_id % 10;
    new_label = strdup(ch->label ? ch->label : def_label);
    if (new_label == NULL) {
        ESP_LOGE(TAG, "Failed to copy data channel label");
        return;
    }
    if (media_lib_mutex_lock(data_channel_mutex,
                             MEDIA_LIB_MAX_LOCK_TIME) != 0) {
        free(new_label);
        return;
    }
    int slot = -1;
    for (int i = 0; i < ELEMS(user_ch); i++) {
        if (user_ch[i].used && user_ch[i].owner_handle == owner_handle &&
            user_ch[i].info.stream_id == ch->stream_id) {
            slot = i;
            break;
        }
        if (slot < 0 && !user_ch[i].used) {
            slot = i;
        }
    }
    char *old_label = NULL;
    if (slot >= 0) {
        old_label = (char *)user_ch[slot].info.label;
        user_ch[slot].used = true;
        user_ch[slot].owner_handle = owner_handle;
        user_ch[slot].info.label = new_label;
        user_ch[slot].info.stream_id = ch->stream_id;
        user_ch[slot].send_count = 0;
        user_ch[slot].recv_count = 0;
        new_label = NULL;
    }
    media_lib_mutex_unlock(data_channel_mutex);
    free(old_label);
    free(new_label);
}

static void remove_channel(esp_peer_data_channel_info_t *ch,
                           esp_webrtc_handle_t owner_handle)
{
    if (data_channel_mutex == NULL || ch == NULL ||
        media_lib_mutex_lock(data_channel_mutex,
                             MEDIA_LIB_MAX_LOCK_TIME) != 0) {
        return;
    }
    char *removed_label = NULL;
    for (int i = 0; i < ELEMS(user_ch); i++) {
        if (user_ch[i].used && user_ch[i].owner_handle == owner_handle &&
            user_ch[i].info.stream_id == ch->stream_id) {
            user_ch[i].used = false;
            removed_label = (char *)user_ch[i].info.label;
            user_ch[i].info.label = NULL;
            break;
        }
    }
    media_lib_mutex_unlock(data_channel_mutex);
    if (removed_label != NULL) {
        ESP_LOGI(TAG, "Removed %s id %d finished", removed_label,
                 ch->stream_id);
    }
    free(removed_label);
}

static void clear_channels(void)
{
    if (data_channel_mutex == NULL ||
        media_lib_mutex_lock(data_channel_mutex,
                             MEDIA_LIB_MAX_LOCK_TIME) != 0) {
        return;
    }
    char *labels[ELEMS(user_ch)] = {};
    for (int i = 0; i < ELEMS(user_ch); i++) {
        labels[i] = (char *)user_ch[i].info.label;
        memset(&user_ch[i], 0, sizeof(user_ch[i]));
    }
    media_lib_mutex_unlock(data_channel_mutex);
    for (int i = 0; i < ELEMS(labels); i++) {
        free(labels[i]);
    }
}

#define CNT_TO_CHAR(c) (((c) & 0xFF) % 94 + 33)

static void data_thread_hdlr(void *arg)
{
#define SEND_PERIOD 1000
    int time = 0;
    int last_send_time = -SEND_PERIOD;
    int str_len = 8192;
    char *str = calloc(1, 8192);
    while (str != NULL && doorbell_worker_enabled(true)) {
        if (time >= last_send_time + SEND_PERIOD) {
            if (webrtc_api_mutex != NULL &&
                media_lib_mutex_lock(webrtc_api_mutex,
                                     MEDIA_LIB_MAX_LOCK_TIME) == 0) {
                esp_webrtc_handle_t data_handle = webrtc;
                struct {
                    char *label;
                    uint16_t stream_id;
                    int send_count;
                } channels[ELEMS(user_ch)] = {};
                if (data_channel_mutex != NULL &&
                    media_lib_mutex_lock(data_channel_mutex,
                                         MEDIA_LIB_MAX_LOCK_TIME) == 0) {
                    for (int i = 0; i < ELEMS(user_ch); i++) {
                        if (user_ch[i].used &&
                            user_ch[i].owner_handle == data_handle &&
                            user_ch[i].info.label != NULL) {
                            channels[i].label = strdup(user_ch[i].info.label);
                            if (channels[i].label != NULL) {
                                channels[i].stream_id =
                                    user_ch[i].info.stream_id;
                                channels[i].send_count = user_ch[i].send_count++;
                            }
                        }
                    }
                    media_lib_mutex_unlock(data_channel_mutex);
                }
                esp_peer_handle_t peer_handle = NULL;
                if (data_handle != NULL) {
                    esp_webrtc_get_peer_connection(data_handle, &peer_handle);
                }
                for (int i = 0; i < ELEMS(channels); i++) {
                    if (channels[i].label == NULL) {
                        continue;
                    }
                    int n = snprintf(str, str_len - 1,
                                     "Send to %s count %d\n",
                                     channels[i].label,
                                     channels[i].send_count);
                    memset(str + n, CNT_TO_CHAR(channels[i].send_count),
                           str_len - n);
                    esp_peer_data_frame_t data_frame = {
                        .type = ESP_PEER_DATA_CHANNEL_STRING,
                        .stream_id = channels[i].stream_id,
                        .data = (uint8_t *)str,
                        .size = str_len,
                    };
                    if (peer_handle != NULL) {
                        esp_peer_send_data(peer_handle, &data_frame);
                    }
                    printf("Send string %.*s", n, str);
                    free(channels[i].label);
                }
                media_lib_mutex_unlock(webrtc_api_mutex);
            }
            last_send_time = time;
        }
        media_lib_thread_sleep(50);
        time += 50;;
    }
    free(str);
    doorbell_worker_finished(true);
    media_lib_thread_destroy(NULL);
}

static int webrtc_data_channel_opened(esp_peer_data_channel_info_t *ch, void *ctx)
{
    ESP_LOGI(TAG, "Channel %s opened stream id %d", ch->label ? ch->label : "NULL", ch->stream_id);
    add_channel(ch, (esp_webrtc_handle_t)ctx);
    return 0;
}

static bool verify_data(uint8_t *data, int size)
{
    char *line_end = strchr((char*)data, '\n');
    char * count = strstr((char*)data, "the ");
    if (line_end == NULL || count == NULL) {
        return false;
    }
    int n = (line_end - (char*)data) + 1;
    int left_size = size - n;
    int send_count = atoi(count + strlen("the "));
    uint8_t expect = CNT_TO_CHAR(send_count);
    for (int i = 0; i < left_size; i++) {
        if (data[n + i] != expect) {
            return false;
        }
    }
    return true;
}

static int webrtc_on_data(esp_peer_data_frame_t *frame, void *ctx)
{
    char *channel_label = NULL;
    if (data_channel_mutex != NULL &&
        media_lib_mutex_lock(data_channel_mutex,
                             MEDIA_LIB_MAX_LOCK_TIME) == 0) {
        for (int i = 0; i < ELEMS(user_ch); i++) {
            if (user_ch[i].used &&
                user_ch[i].info.stream_id == frame->stream_id &&
                user_ch[i].info.label != NULL) {
                channel_label = strdup(user_ch[i].info.label);
                break;
            }
        }
        media_lib_mutex_unlock(data_channel_mutex);
    }
    char *line_end = strchr((char*)frame->data, '\n');
    if (line_end == NULL) {
        free(channel_label);
        return -1;
    }
    int str_len = (line_end - (char*)frame->data);
    bool verified = verify_data(frame->data, frame->size);
    if (verified == false) {
        ESP_LOGE(TAG, "Get data label %s verify:%d data: %.*s",
            channel_label ? channel_label : "NULL",
            verified,
            str_len, (char *)frame->data);
    } else {
        ESP_LOGI(TAG, "Get data label %s verify:%d data: %.*s",
            channel_label ? channel_label : "NULL",
            verified,
            str_len, (char *)frame->data);
    }
    free(channel_label);
    return 0;
}

static int webrtc_data_channel_closed(esp_peer_data_channel_info_t *ch, void *ctx)
{
    remove_channel(ch, (esp_webrtc_handle_t)ctx);
    return 0;
}

static int rtp_sender_get_encoded_size(esp_peer_rtp_frame_t *frame, bool *in_place, void *ctx)
{
    rtp_transformer_ctx_t *t_ctx = (rtp_transformer_ctx_t *)ctx;
    // Skip for video payload
    if (frame->payload_type >= 96) {
        return ESP_PEER_ERR_NOT_SUPPORT;
    }
    int tail_size = snprintf(NULL, 0, "TAIL_%" PRIu32, t_ctx->frame_count);
    // Make sure encoded size < one MTU
    frame->encoded_size = frame->orig_size + sizeof(uint32_t) * 2 + tail_size + 1;
    if (frame->encoded_size > 1300) {
        return ESP_PEER_ERR_NOT_SUPPORT;
    }
    // Not support in place transform
    *in_place = false;
    return 0;
}

static int rtp_sender_transform(esp_peer_rtp_frame_t *frame, void *ctx)
{
    rtp_transformer_ctx_t *t_ctx = (rtp_transformer_ctx_t *)ctx;
    // Skip for video payload
    if (frame->payload_type >= 96 || frame->orig_size < RTP_HEADER_SIZE) {
        return ESP_PEER_ERR_NOT_SUPPORT;
    }
    char tail_string[64];
    int written = snprintf(tail_string, sizeof(tail_string), "TAIL_%" PRIu32, t_ctx->frame_count);
    tail_string[written] = '\0';
    uint32_t tail_len = written + 1;
    // Build new payload: [4-byte size][original payload][tail string]
    uint32_t new_size = frame->orig_size + sizeof(uint32_t) * 2 + tail_len;
    uint8_t *new_data = frame->encoded_data;
    int src_ofst = 0;
    int dst_ofst = 0;
    memcpy(new_data + dst_ofst, frame->orig_data + src_ofst, RTP_HEADER_SIZE);
    dst_ofst += RTP_HEADER_SIZE;
    src_ofst += RTP_HEADER_SIZE;

    uint32_t payload_size = frame->orig_size - RTP_HEADER_SIZE;
    memcpy(new_data + dst_ofst, &payload_size, sizeof(uint32_t));
    dst_ofst += sizeof(uint32_t);

    memcpy(new_data + dst_ofst, frame->orig_data + src_ofst, payload_size);
    dst_ofst += payload_size;
    memcpy(new_data + dst_ofst, &tail_len, sizeof(uint32_t));
    dst_ofst += sizeof(uint32_t);
    memcpy(new_data + dst_ofst, tail_string, tail_len);
    dst_ofst += tail_len;
    frame->encoded_size = new_size;
    t_ctx->frame_count++;
    if (t_ctx->frame_count % 100 == 0) {
        ESP_LOGI(TAG, "[SENDER] RTP transformed: frame_count=%u, error_count=%u",
                 t_ctx->frame_count, t_ctx->error_count);
    }
    return 0;
}

static int rtp_receiver_get_encoded_size(esp_peer_rtp_frame_t *frame, bool *in_place, void *ctx)
{
    rtp_transformer_ctx_t *t_ctx = (rtp_transformer_ctx_t *)ctx;
    // Skip for video payload
    if (frame->payload_type >= 96) {
        return ESP_PEER_ERR_NOT_SUPPORT;
    }
    uint32_t original_size = 0;
    memcpy(&original_size, frame->orig_data + RTP_HEADER_SIZE, sizeof(uint32_t));
    if (RTP_HEADER_SIZE + original_size + sizeof(uint32_t) * 2 > frame->orig_size) {
        t_ctx->error_count++;
        return ESP_PEER_ERR_BAD_DATA;
    }
    uint32_t tail_size = 0;
    memcpy(&tail_size, frame->orig_data + RTP_HEADER_SIZE + original_size + sizeof(uint32_t), sizeof(uint32_t));
    uint32_t encoded_size = RTP_HEADER_SIZE + original_size + sizeof(uint32_t) * 2 + tail_size;
    if (encoded_size != frame->orig_size) {
        t_ctx->error_count++;
        return ESP_PEER_ERR_BAD_DATA;
    }
    frame->encoded_size = RTP_HEADER_SIZE + original_size;
    *in_place = true;
    return 0;
}

static int rtp_receiver_transform(esp_peer_rtp_frame_t *frame, void *ctx)
{
    rtp_transformer_ctx_t *t_ctx = (rtp_transformer_ctx_t *)ctx;
    uint32_t original_size = 0;
    uint8_t *original_data = NULL;
    uint32_t tail_size = 0;
    int ofst = RTP_HEADER_SIZE;
    memcpy(&original_size, frame->orig_data + ofst, sizeof(uint32_t));
    ofst += sizeof(uint32_t);
    original_data = frame->orig_data + ofst;
    ofst += original_size;
    memcpy(&tail_size, frame->orig_data + ofst, sizeof(uint32_t));
    ofst += sizeof(uint32_t);
    char *tail_str = (char*)frame->orig_data + ofst;
    t_ctx->frame_count++;
    if (strncmp(tail_str, "TAIL_", 5) != 0) {
        t_ctx->error_count++;
        return ESP_PEER_ERR_BAD_DATA;
    }
    if (frame->encoded_data != frame->orig_data) {
        memcpy(frame->encoded_data, frame->orig_data, RTP_HEADER_SIZE);
        memcpy(frame->encoded_data + RTP_HEADER_SIZE, original_data, original_size);
    } else {
        memmove(frame->orig_data + RTP_HEADER_SIZE, original_data, original_size);
    }
    frame->encoded_size = RTP_HEADER_SIZE + original_size;
    if (t_ctx->frame_count % 100 == 0) {
        ESP_LOGI(TAG, "[RECEIVER] RTP transformed: frame_count=%u, error_count=%u",
                 t_ctx->frame_count, t_ctx->error_count);
    }
    return 0;
}

static void setup_rtp_transformers(esp_peer_handle_t peer_handle)
{
    // Set up sender transformer
    esp_peer_rtp_transform_cb_t sender_transform_cb = {
        .get_encoded_size = rtp_sender_get_encoded_size,
        .transform = rtp_sender_transform,
    };
    int ret = esp_peer_set_rtp_transformer(peer_handle,
                                           ESP_PEER_RTP_TRANSFORM_ROLE_SENDER,
                                           rtp_transformer_enabled ? &sender_transform_cb : NULL,
                                           rtp_transformer_enabled ? &sender_transformer_ctx : NULL);
    if (ret == ESP_PEER_ERR_NONE) {
        ESP_LOGI(TAG, "RTP sender transformer set up successfully");
    } else {
        ESP_LOGE(TAG, "Failed to set up RTP sender transformer: %d", ret);
    }
    // Set up receiver transformer
    esp_peer_rtp_transform_cb_t receiver_transform_cb = {
        .get_encoded_size = rtp_receiver_get_encoded_size,
        .transform = rtp_receiver_transform,
    };
    ret = esp_peer_set_rtp_transformer(peer_handle,
                                       ESP_PEER_RTP_TRANSFORM_ROLE_RECEIVER,
                                       rtp_transformer_enabled ? &receiver_transform_cb : NULL,
                                       rtp_transformer_enabled ? &receiver_transformer_ctx : NULL);
    if (ret == ESP_PEER_ERR_NONE) {
        ESP_LOGI(TAG, "RTP receiver transformer set up successfully");
    } else {
        ESP_LOGE(TAG, "Failed to set up RTP receiver transformer: %d", ret);
    }
}

static int webrtc_event_handler(esp_webrtc_event_t *event, void *ctx)
{
    esp_webrtc_handle_t source_handle = (esp_webrtc_handle_t)ctx;
    if (event->type == ESP_WEBRTC_EVENT_CONNECTED) {
        door_bell_change_state(source_handle, DOOR_BELL_STATE_CONNECTED);
    } else if (event->type == ESP_WEBRTC_EVENT_CONNECT_FAILED || event->type == ESP_WEBRTC_EVENT_DISCONNECTED) {
        door_bell_change_state(source_handle, DOOR_BELL_STATE_NONE);
        // Reset transformer contexts
        memset(&sender_transformer_ctx, 0, sizeof(sender_transformer_ctx));
        memset(&receiver_transformer_ctx, 0, sizeof(receiver_transformer_ctx));
    } else if (event->type == ESP_WEBRTC_EVENT_PAIRED) {
        if (door_bell_mutex == NULL ||
            media_lib_mutex_lock(door_bell_mutex,
                                 MEDIA_LIB_MAX_LOCK_TIME) != 0) {
            return 0;
        }
        esp_webrtc_handle_t event_handle = source_handle;
        if (webrtc != event_handle) {
            media_lib_mutex_unlock(door_bell_mutex);
            return 0;
        }
        esp_peer_handle_t peer_handle = NULL;
        esp_webrtc_get_peer_connection(event_handle, &peer_handle);
        setup_rtp_transformers(peer_handle);
        esp_peer_addr_t addr = {};
        esp_peer_get_paired_addr(peer_handle, &addr);
        ESP_LOGI(TAG, "Paired with %d.%d.%d.%d:%d", addr.ipv4[0], addr.ipv4[1], addr.ipv4[2], addr.ipv4[3], addr.port);
        media_lib_mutex_unlock(door_bell_mutex);
    }
    return 0;
}

void send_cmd(char *cmd)
{
    if (SAME_STR(cmd, "ring")) {
        doorbell_request_ring("command");
    }
}

static void key_monitor_thread(void *arg)
{
    gpio_config_t io_conf;
    memset(&io_conf, 0, sizeof(io_conf));
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = BIT64(DOOR_BELL_RING_BUTTON);
    io_conf.pull_down_en = 1;
    esp_err_t ret = 0;
    ret |= gpio_config(&io_conf);

    media_lib_thread_sleep(50);
    int last_level = gpio_get_level(DOOR_BELL_RING_BUTTON);
    int init_level = last_level;

    while (doorbell_worker_enabled(false)) {
        media_lib_thread_sleep(50);
        if (!doorbell_worker_enabled(false)) {
            break;
        }
        int level = gpio_get_level(DOOR_BELL_RING_BUTTON);
        if (level != last_level) {
            int previous_level = last_level;
            last_level = level;
            ESP_LOGI(TAG, "BOOT level changed: %d -> %d", previous_level,
                     level);
            if (level != init_level) {
                doorbell_ring_request_result_t result =
                    doorbell_request_ring("boot");
                ESP_LOGI(TAG, "BOOT ring request: %s",
                         doorbell_ring_result_name(result));
            }
        }
    }
    doorbell_worker_finished(false);
    media_lib_thread_destroy(NULL);
}

static int start_doorbell_workers(void)
{
    if (media_lib_mutex_lock(door_bell_mutex, MEDIA_LIB_MAX_LOCK_TIME) != 0) {
        return -1;
    }
    monitor_key = true;
    key_thread_active = true;
    media_lib_mutex_unlock(door_bell_mutex);

    media_lib_thread_handle_t key_thread;
    int ret = media_lib_thread_create_from_scheduler(
        &key_thread, "Key", key_monitor_thread, NULL);
    if (ret != 0) {
        if (media_lib_mutex_lock(door_bell_mutex,
                                 MEDIA_LIB_MAX_LOCK_TIME) == 0) {
            monitor_key = false;
            key_thread_active = false;
            media_lib_mutex_unlock(door_bell_mutex);
        }
        return ret;
    }

    if (media_lib_mutex_lock(door_bell_mutex, MEDIA_LIB_MAX_LOCK_TIME) != 0) {
        return -1;
    }
    data_thread_enabled = true;
    data_running = true;
    media_lib_mutex_unlock(door_bell_mutex);

    media_lib_thread_handle_t data_thread;
    ret = media_lib_thread_create_from_scheduler(
        &data_thread, "data", data_thread_hdlr, NULL);
    if (ret != 0) {
        if (media_lib_mutex_lock(door_bell_mutex,
                                 MEDIA_LIB_MAX_LOCK_TIME) == 0) {
            data_thread_enabled = false;
            data_running = false;
            media_lib_mutex_unlock(door_bell_mutex);
        }
    }
    return ret;
}

int start_webrtc(char *url)
{
    int ret = -1;

    if (!door_bell_sync_ready()) {
        ESP_LOGE(TAG, "WebRTC synchronization is not initialized");
        return -1;
    }
    if (network_is_connected() == false) {
        ESP_LOGE(TAG, "Wifi not connected yet");
        return -1;
    }
    if (media_lib_mutex_lock(webrtc_lifecycle_mutex,
                             MEDIA_LIB_MAX_LOCK_TIME) != 0) {
        ESP_LOGE(TAG, "Failed to lock WebRTC lifecycle for restart");
        return -1;
    }
    ret = stop_doorbell_workers();
    if (ret != 0) {
        goto start_lifecycle_exit;
    }
    if (media_lib_mutex_lock(webrtc_api_mutex,
                             MEDIA_LIB_MAX_LOCK_TIME) != 0) {
        ESP_LOGE(TAG, "Failed to lock WebRTC API for restart");
        ret = -1;
        goto start_lifecycle_exit;
    }
    if (media_lib_mutex_lock(door_bell_mutex, MEDIA_LIB_MAX_LOCK_TIME) != 0) {
        ESP_LOGE(TAG, "Failed to lock doorbell state for WebRTC restart");
        media_lib_mutex_unlock(webrtc_api_mutex);
        ret = -1;
        goto start_lifecycle_exit;
    }
    esp_webrtc_handle_t previous_handle = webrtc;
    webrtc = NULL;
    if (previous_handle != NULL) {
        door_bell_change_state_locked(DOOR_BELL_STATE_NONE);
    }
    media_lib_mutex_unlock(door_bell_mutex);
    if (previous_handle != NULL) {
        esp_webrtc_close(previous_handle);
        clear_channels();
    }

    esp_peer_default_cfg_t peer_cfg = {
        .agent_recv_timeout = 500,
    };
    esp_webrtc_cfg_t cfg = {
        .peer_cfg = {
            .audio_info = {
#ifdef WEBRTC_SUPPORT_OPUS
                .codec = ESP_PEER_AUDIO_CODEC_OPUS,
                .sample_rate = 16000,
                .channel = 2,
#else
                .codec = ESP_PEER_AUDIO_CODEC_G711A,
                .sample_rate = 8000,
                .channel = 1,
#endif
            },
            .video_info = {
                .codec = ESP_PEER_VIDEO_CODEC_H264,
                .width = VIDEO_WIDTH,
                .height = VIDEO_HEIGHT,
                .fps = VIDEO_FPS,
            },
            .audio_dir = ESP_PEER_MEDIA_DIR_SEND_RECV,
            .video_dir = ESP_PEER_MEDIA_DIR_SEND_ONLY,
            .on_custom_data = door_bell_on_cmd,
            // Add following data channel callback for more accurate control over data channel
            .on_channel_open = webrtc_data_channel_opened,
            .on_data = webrtc_on_data,
            .on_channel_close = webrtc_data_channel_closed,
            .enable_data_channel = true,
            .manual_ch_create = true, // If work as SCTP client disable create data channel automatically
            .no_auto_reconnect = true, // No auto connect peer when signaling connected
            .extra_cfg = &peer_cfg,
            .extra_size = sizeof(peer_cfg),
        },
        .signaling_cfg = {
            .signal_url = url,
        },
        .peer_impl = esp_peer_get_default_impl(),
        .signaling_impl = esp_signaling_get_http_impl(),
    };
    esp_webrtc_handle_t opened_webrtc = NULL;
    ret = esp_webrtc_open(&cfg, &opened_webrtc);
    if (ret != 0) {
        ESP_LOGE(TAG, "Fail to open webrtc");
        media_lib_mutex_unlock(webrtc_api_mutex);
        goto start_lifecycle_exit;
    }
    door_lock_set_state_callback(door_lock_state_changed, NULL);
    // Set media provider
    esp_webrtc_media_provider_t media_provider = {};
    media_sys_get_provider(&media_provider);
    esp_webrtc_set_media_provider(opened_webrtc, &media_provider);

    // Keep the camera capture system alive across calls. Recreating the
    // P4 video pipeline after each hangup can delay video on later calls.
    esp_webrtc_set_no_auto_capture(opened_webrtc, true);

#ifdef CONFIG_DOORBELL_SUPPORT_PEDESTRIAN_DETECT
    bool pedestrian_detection_ready = false;
    // Pre-configuration for all sink
    esp_capture_sink_cfg_t sink_cfg = {
        .audio_info = {
#ifdef WEBRTC_SUPPORT_OPUS
            .format_id = ESP_CAPTURE_FMT_ID_OPUS,
#else
            .format_id = ESP_CAPTURE_FMT_ID_G711A,
#endif
            .sample_rate = cfg.peer_cfg.audio_info.sample_rate,
            .channel = cfg.peer_cfg.audio_info.channel,
            .bits_per_sample = 16,
        },
        .video_info = {
            .format_id = ESP_CAPTURE_FMT_ID_H264,
            .width = cfg.peer_cfg.video_info.width,
            .height = cfg.peer_cfg.video_info.height,
            .fps = cfg.peer_cfg.video_info.fps,
        },
    };
    if (cfg.peer_cfg.audio_dir == ESP_PEER_MEDIA_DIR_RECV_ONLY) {
        sink_cfg.audio_info.format_id = ESP_CAPTURE_FMT_ID_NONE;
    }
    if (cfg.peer_cfg.video_dir == ESP_PEER_MEDIA_DIR_RECV_ONLY) {
        sink_cfg.video_info.format_id = ESP_CAPTURE_FMT_ID_NONE;
    }
    esp_capture_sink_handle_t main_sink = NULL;
    esp_capture_sink_setup(media_provider.capture, 0, &sink_cfg, &main_sink);

    // Configuration for capture sink 1 before start
    esp_capture_sink_cfg_t sink_cfg_1 = {
        .video_info = {
            .format_id = ESP_CAPTURE_FMT_ID_RGB565,
            .width = DETECT_WIDTH,
            .height = DETECT_HEIGHT,
            .fps = DETECT_FPS,
        },
    };
    int detect_ret = esp_capture_sink_setup(media_provider.capture, 1,
                                            &sink_cfg_1, &detect_sink);
    if (detect_ret != ESP_CAPTURE_ERR_OK || detect_sink == NULL) {
        ESP_LOGE(TAG, "Failed to set up pedestrian detection sink: %d",
                 detect_ret);
        detect_sink = NULL;
    } else {
        detect_ret = esp_capture_sink_enable(detect_sink,
                                             ESP_CAPTURE_RUN_MODE_ALWAYS);
        if (detect_ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Failed to enable pedestrian detection sink: %d",
                     detect_ret);
            detect_sink = NULL;
        } else {
            pedestrian_detection_ready = true;
        }
    }
#endif

    // Set event handler
    esp_webrtc_set_event_handler(opened_webrtc, webrtc_event_handler,
                                 opened_webrtc);

    // Default disable auto connect of peer connection
    esp_webrtc_enable_peer_connection(opened_webrtc, false);

    // Start webrtc
    ret = esp_webrtc_start(opened_webrtc);
    if (ret != 0) {
        ESP_LOGE(TAG, "Fail to start webrtc");
        esp_webrtc_close(opened_webrtc);
        clear_channels();
        media_lib_mutex_unlock(webrtc_api_mutex);
        goto start_lifecycle_exit;
    }

    if (media_lib_mutex_lock(door_bell_mutex, MEDIA_LIB_MAX_LOCK_TIME) != 0) {
        ESP_LOGE(TAG, "Failed to publish started WebRTC handle");
        esp_webrtc_close(opened_webrtc);
        clear_channels();
        media_lib_mutex_unlock(webrtc_api_mutex);
        ret = -1;
        goto start_lifecycle_exit;
    }
    webrtc = opened_webrtc;
    door_bell_change_state_locked(DOOR_BELL_STATE_NONE);
    media_lib_mutex_unlock(door_bell_mutex);

    ret = start_doorbell_workers();
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to start doorbell workers: %d", ret);
        if (media_lib_mutex_lock(door_bell_mutex,
                                 MEDIA_LIB_MAX_LOCK_TIME) == 0) {
            if (webrtc == opened_webrtc) {
                webrtc = NULL;
                door_bell_change_state_locked(DOOR_BELL_STATE_NONE);
            }
            monitor_key = false;
            data_thread_enabled = false;
            media_lib_mutex_unlock(door_bell_mutex);
        }
        media_lib_mutex_unlock(webrtc_api_mutex);
        stop_doorbell_workers();
        if (media_lib_mutex_lock(webrtc_api_mutex,
                                 MEDIA_LIB_MAX_LOCK_TIME) == 0) {
            esp_webrtc_close(opened_webrtc);
            clear_channels();
            media_lib_mutex_unlock(webrtc_api_mutex);
        }
        goto start_lifecycle_exit;
    }

    play_tone(DOOR_BELL_TONE_JOIN_SUCCESS);
#ifdef CONFIG_DOORBELL_SUPPORT_PEDESTRIAN_DETECT
    if (pedestrian_detection_ready) {
        int capture_ret = esp_capture_start(media_provider.capture);
        if (capture_ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Failed to start capture for pedestrian detection: %d",
                     capture_ret);
        } else {
            pedestrian_detect_cfg_t detect_cfg = {
                .detected = NULL,
                .snapshot = vision_snapshot_received,
            };
            if (start_pedestrian_detection(&detect_cfg) != 0) {
                ESP_LOGE(TAG, "Failed to start pedestrian detection");
            }
        }
    }
#endif
    media_lib_mutex_unlock(webrtc_api_mutex);

start_lifecycle_exit:
    media_lib_mutex_unlock(webrtc_lifecycle_mutex);
    return ret;
}

void query_webrtc(void)
{
    if (webrtc_api_mutex == NULL ||
        media_lib_mutex_lock(webrtc_api_mutex,
                             MEDIA_LIB_MAX_LOCK_TIME) != 0) {
        return;
    }
    esp_webrtc_handle_t query_handle = webrtc;
    if (query_handle != NULL) {
        esp_webrtc_query(query_handle);
    }
    media_lib_mutex_unlock(webrtc_api_mutex);
}

int stop_webrtc(void)
{
    int ret = -1;

    if (!door_bell_sync_ready()) {
        ESP_LOGE(TAG, "WebRTC synchronization is not initialized");
        return -1;
    }
    if (media_lib_mutex_lock(webrtc_lifecycle_mutex,
                             MEDIA_LIB_MAX_LOCK_TIME) != 0) {
        ESP_LOGE(TAG, "Failed to lock WebRTC lifecycle for stop");
        return -1;
    }
    ret = stop_doorbell_workers();
    if (ret != 0) {
        goto stop_lifecycle_exit;
    }
    if (webrtc_api_mutex == NULL ||
        media_lib_mutex_lock(webrtc_api_mutex,
                             MEDIA_LIB_MAX_LOCK_TIME) != 0) {
        ESP_LOGE(TAG, "Failed to lock WebRTC API for stop");
        ret = -1;
        goto stop_lifecycle_exit;
    }
    if (media_lib_mutex_lock(door_bell_mutex, MEDIA_LIB_MAX_LOCK_TIME) != 0) {
        ESP_LOGE(TAG, "Failed to lock doorbell state for WebRTC stop");
        media_lib_mutex_unlock(webrtc_api_mutex);
        ret = -1;
        goto stop_lifecycle_exit;
    }
    esp_webrtc_handle_t handle = webrtc;
    webrtc = NULL;
    if (handle != NULL) {
        door_bell_change_state_locked(DOOR_BELL_STATE_NONE);
    }
    media_lib_mutex_unlock(door_bell_mutex);

    if (handle != NULL) {
        ESP_LOGI(TAG, "Start to close webrtc %p", handle);

#ifdef CONFIG_DOORBELL_SUPPORT_PEDESTRIAN_DETECT
       stop_pedestrian_detection();
       esp_webrtc_media_provider_t media_provider = {};
       media_sys_get_provider(&media_provider);
       esp_capture_stop(media_provider.capture);
        detect_sink = NULL;
#endif
        esp_webrtc_close(handle);
        clear_channels();
    }
    media_lib_mutex_unlock(webrtc_api_mutex);
    ret = 0;

stop_lifecycle_exit:
    media_lib_mutex_unlock(webrtc_lifecycle_mutex);
    return ret;
}
