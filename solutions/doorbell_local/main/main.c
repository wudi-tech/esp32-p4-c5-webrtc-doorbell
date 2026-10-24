/* Door Bell Demo

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_wifi.h>
#include <esp_event.h>
#include "door_lock.h"
#include "radar_ld2450.h"
#include "presence_fusion.h"
#include "doorbell_event.h"
#include "multimodal_fusion.h"
#include "webrtc_http_server.h"
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include "argtable3/argtable3.h"
#include "esp_console.h"
#include "esp_webrtc.h"
#include "media_lib_adapter.h"
#include "media_lib_os.h"
#include "esp_timer.h"
#include "webrtc_utils_time.h"
#include "esp_cpu.h"
#include "settings.h"
#include "media_lib_netif.h"
#include "common.h"
#include "esp_capture.h"

static const char *TAG = "Webrtc_Test";
static presence_fusion_t radar_presence;
static ld2450_target_t last_radar_target;
static size_t last_radar_slot = SIZE_MAX;
#ifdef CONFIG_DOORBELL_SUPPORT_PEDESTRIAN_DETECT
static multimodal_fusion_t sensor_fusion;
static media_lib_mutex_handle_t sensor_fusion_mutex;
static bool last_vision_confirmed;
static char vision_snapshot_json[1024];
#endif

static void request_sensor_ring(const char *source)
{
    doorbell_ring_request_result_t ring_result = doorbell_request_ring(source);
    if (ring_result != DOORBELL_RING_ACCEPTED) {
        ESP_LOGW(TAG, "%s ring request not accepted: %s", source,
                 doorbell_ring_result_name(ring_result));
    }
}

#ifdef CONFIG_DOORBELL_SUPPORT_PEDESTRIAN_DETECT
static bool update_multimodal_radar(bool present, bool ring_eligible,
                                    uint32_t now_ms)
{
    if (sensor_fusion_mutex == NULL ||
        media_lib_mutex_lock(sensor_fusion_mutex,
                             MEDIA_LIB_MAX_LOCK_TIME) != 0) {
        return false;
    }
    bool should_ring = multimodal_fusion_on_radar(
        &sensor_fusion, present, ring_eligible, now_ms);
    media_lib_mutex_unlock(sensor_fusion_mutex);
    return should_ring;
}

void doorbell_report_vision_detection(void)
{
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    if (sensor_fusion_mutex == NULL ||
        media_lib_mutex_lock(sensor_fusion_mutex,
                             MEDIA_LIB_MAX_LOCK_TIME) != 0) {
        return;
    }
    bool should_ring = multimodal_fusion_on_vision(&sensor_fusion, now_ms);
    media_lib_mutex_unlock(sensor_fusion_mutex);
    if (should_ring) {
        ESP_LOGI(TAG, "Radar and camera pedestrian detection fused");
        request_sensor_ring("radar+vision");
    }
}

void doorbell_report_vision_snapshot(const vision_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    if (vision_snapshot_format_json(vision_snapshot_json,
                                    sizeof(vision_snapshot_json), snapshot) > 0 &&
        !webrtc_http_server_publish_latest_vision(vision_snapshot_json)) {
        ESP_LOGD(TAG, "Vision snapshot not queued because SSE is not ready");
    }

    vision_evidence_t evidence = VISION_EVIDENCE_NONE;
    for (size_t i = 0; i < snapshot->update.target_count; ++i) {
        const vision_target_t *target = &snapshot->update.targets[i];
        if (!target->confirmed) {
            continue;
        }
        if (target->kind == VISION_KIND_PEDESTRIAN) {
            evidence = VISION_EVIDENCE_PEDESTRIAN;
            break;
        }
        if (target->kind == VISION_KIND_FACE) {
            evidence = VISION_EVIDENCE_FACE;
        }
    }

    bool should_ring = false;
    if (evidence != VISION_EVIDENCE_NONE && sensor_fusion_mutex != NULL &&
        media_lib_mutex_lock(sensor_fusion_mutex,
                             MEDIA_LIB_MAX_LOCK_TIME) == 0) {
        should_ring = multimodal_fusion_on_vision_evidence(
            &sensor_fusion, evidence, snapshot->timestamp_ms);
        media_lib_mutex_unlock(sensor_fusion_mutex);
    }
    if (snapshot->update.confirmed_present != last_vision_confirmed) {
        ESP_LOGI(TAG, "Semantic camera presence %s",
                 snapshot->update.confirmed_present ? "confirmed" : "cleared");
        last_vision_confirmed = snapshot->update.confirmed_present;
    }
    if (should_ring) {
        ESP_LOGI(TAG, "Radar and semantic camera detection fused");
        request_sensor_ring("radar+vision");
    }
}
#else
void doorbell_report_vision_detection(void)
{
}

void doorbell_report_vision_snapshot(const vision_snapshot_t *snapshot)
{
    (void)snapshot;
}
#endif

static void radar_frame_received(
    const uint8_t raw_frame[LD2450_FRAME_SIZE],
    const ld2450_frame_t *frame, void *ctx)
{
    (void)raw_frame;
    (void)ctx;
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    presence_fusion_result_t result =
        presence_fusion_process(&radar_presence, frame, now_ms);

#ifdef CONFIG_DOORBELL_SUPPORT_PEDESTRIAN_DETECT
    set_vision_radar_focus(result.in_zone_count > 0U);
#endif

    if (result.has_representative_target) {
        last_radar_target = result.representative_target;
        last_radar_slot = result.representative_slot;
    }

    if (result.state_changed || result.event != PRESENCE_EVENT_NONE) {
        int representative_x_mm = result.has_representative_target
                                      ? result.representative_target.x_mm
                                      : 0;
        int representative_y_mm = result.has_representative_target
                                      ? result.representative_target.y_mm
                                      : 0;
        ESP_LOGI(TAG,
                 "Radar presence event=%s state=%s in-zone=%u direction=%s "
                 "representative_x=%d mm representative_y=%d mm "
                 "ring_requested=%s",
                 presence_event_name(result.event),
                 presence_state_name(result.state),
                 (unsigned)result.in_zone_count,
                 presence_direction_name(result.direction),
                 representative_x_mm, representative_y_mm,
                 result.ring_requested ? "true" : "false");
    }

    if (result.event != PRESENCE_EVENT_NONE &&
        last_radar_slot < LD2450_MAX_TARGETS) {
        doorbell_radar_event_t event = {
            .event = result.event == PRESENCE_EVENT_ENTER_CONFIRMED
                         ? DOORBELL_RADAR_ENTER
                         : DOORBELL_RADAR_LEAVE,
            .timestamp_ms = now_ms,
            .track_id = (uint8_t)(last_radar_slot + 1U),
            .x_mm = last_radar_target.x_mm,
            .y_mm = last_radar_target.y_mm,
            .speed_cm_s = last_radar_target.speed_cm_s,
        };
        char json[256];
        if (doorbell_radar_event_format_json(json, sizeof(json), &event) > 0 &&
            !webrtc_http_server_publish_event(json)) {
            ESP_LOGD(TAG, "Radar event not queued because SSE is not ready");
        }
    }

#ifdef CONFIG_DOORBELL_SUPPORT_PEDESTRIAN_DETECT
    bool fused_ring = false;
    if (result.event == PRESENCE_EVENT_ENTER_CONFIRMED) {
        fused_ring = update_multimodal_radar(true, result.ring_requested, now_ms);
    } else if (result.event == PRESENCE_EVENT_LEAVE_CONFIRMED) {
        (void)update_multimodal_radar(false, false, now_ms);
    }
    if (fused_ring) {
        request_sensor_ring("radar+vision");
    }
#else
    if (result.ring_requested) {
        request_sensor_ring("radar");
    }
#endif
}


#define RUN_ASYNC(name, body)           \
    void run_async##name(void *arg)     \
    {                                   \
        body;                           \
        media_lib_thread_destroy(NULL); \
    }                                   \
    media_lib_thread_create_from_scheduler(NULL, #name, run_async##name, NULL);


static int start_cli(int argc, char **argv)
{
    start_webrtc(NULL);
    return 0;
}

static int stop_cli(int argc, char **argv)
{
    RUN_ASYNC(leave, { stop_webrtc(); });
    return 0;
}

static int close_data_ch_cli(int argc, char **argv)
{
    close_data_channel(argc > 1 ? atoi(argv[1]) : 0);
    return 0;
}

static int cmd_cli(int argc, char **argv)
{
    send_cmd(argc > 1 ? argv[1] : "ring");
    return 0;
}

static int assert_cli(int argc, char **argv)
{
    *(int *)0 = 0;
    return 0;
}

static int sys_cli(int argc, char **argv)
{
    sys_state_show();
    return 0;
}

static int wifi_cli(int argc, char **argv)
{
    if (argc < 1) {
        return -1;
    }
    char *ssid = argv[1];
    char *password = argc > 2 ? argv[2] : NULL;
    return network_connect_wifi(ssid, password);
}


static int capture_to_player_cli(int argc, char **argv)
{
    return test_capture_to_player();
}

static int measure_cli(int argc, char **argv)
{
    void measure_enable(bool enable);
    void show_measure(void);
    measure_enable(true);
    media_lib_thread_sleep(1500);
    measure_enable(false);
    return 0;
}

static int init_console()
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "esp>";
    repl_config.task_stack_size = 10 * 1024;
    repl_config.task_priority = 22;
    repl_config.max_cmdline_length = 1024;
    // install console REPL environment
#if CONFIG_ESP_CONSOLE_UART
    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_config, &repl_config, &repl));
#elif CONFIG_ESP_CONSOLE_USB_CDC
    esp_console_dev_usb_cdc_config_t cdc_config = ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_cdc(&cdc_config, &repl_config, &repl));
#elif CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    esp_console_dev_usb_serial_jtag_config_t usbjtag_config = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&usbjtag_config, &repl_config, &repl));
#endif

    esp_console_cmd_t cmds[] = {
        {
            .command = "start",
            .help = "Start signaling and webrtc.\r\n",
            .func = start_cli,
        },
        {
            .command = "stop",
            .help = "Stop signaling and webrtc.\n",
            .func = stop_cli,
        },
        {
            .command = "cmd",
            .help = "Send command (ring etc)\n",
            .func = cmd_cli,
        },
        {
            .command = "close",
            .help = "Close data channel\n",
            .func = close_data_ch_cli,
        },
        {
            .command = "i",
            .help = "Show system status\r\n",
            .func = sys_cli,
        },
        {
            .command = "assert",
            .help = "Assert system\r\n",
            .func = assert_cli,
        },
        {
            .command = "rec2play",
            .help = "Play capture content\n",
            .func = capture_to_player_cli,
        },
        {
            .command = "wifi",
            .help = "wifi ssid psw\r\n",
            .func = wifi_cli,
        },
        {
            .command = "m",
            .help = "measure system loading\r\n",
            .func = measure_cli,
        },
    };
    for (int i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    }
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
    return 0;
}

static void thread_scheduler(const char *thread_name, media_lib_thread_cfg_t *schedule_cfg)
{
    if (strcmp(thread_name, "venc_0") == 0) {
        // For H264 may need huge stack if use hardware encoder can set it to small value
        schedule_cfg->priority = 10;
#if CONFIG_IDF_TARGET_ESP32S3
        schedule_cfg->stack_size = 20 * 1024;
#endif
    }
#ifdef WEBRTC_SUPPORT_OPUS
    else if (strcmp(thread_name, "aenc_0") == 0) {
        // For OPUS encoder it need huge stack, when use G711 can set it to small value
        schedule_cfg->stack_size = 40 * 1024;
        schedule_cfg->priority = 10;
        schedule_cfg->core_id = 1;
    }
#endif
    else if (strcmp(thread_name, "AUD_SRC") == 0) {
        schedule_cfg->priority = 15;
    } else if (strcmp(thread_name, "detect") == 0) {
        schedule_cfg->stack_size = 24 * 1024;
        schedule_cfg->priority = 8;
    }
    else if (strcmp(thread_name, "pc_task") == 0) {
        schedule_cfg->stack_size = 25 * 1024;
        schedule_cfg->priority = 18;
        schedule_cfg->core_id = 1;
    }
    if (strcmp(thread_name, "detect_filter") == 0) {
        schedule_cfg->priority = 15;
    }
    if (strcmp(thread_name, "start") == 0) {
        schedule_cfg->stack_size = 6 * 1024;
    }
}

static void capture_scheduler(const char *name, esp_capture_thread_schedule_cfg_t *schedule_cfg)
{
    media_lib_thread_cfg_t cfg = {
        .stack_size = schedule_cfg->stack_size,
        .priority = schedule_cfg->priority,
        .core_id = schedule_cfg->core_id,
    };
    schedule_cfg->stack_in_ext = true;
    thread_scheduler(name, &cfg);
    schedule_cfg->stack_size = cfg.stack_size;
    schedule_cfg->priority = cfg.priority;
    schedule_cfg->core_id = cfg.core_id;
}

static char *get_network_ip(void)
{
    media_lib_ipv4_info_t ip_info;
    media_lib_netif_get_ipv4_info(MEDIA_LIB_NET_TYPE_STA, &ip_info);
    return media_lib_ipv4_ntoa(&ip_info.ip);
}

void sctp_show_details(bool enable);

static int network_event_handler(bool connected)
{
    if (connected) {
        // Enter into Room directly
        RUN_ASYNC(start, {
            start_webrtc(NULL);
            ESP_LOGI(TAG, "Use browser to enter https://%s/webrtc/test for test", get_network_ip());
        });
        // sctp_show_details(true);
    } else {
        stop_webrtc();
    }
    return 0;
}

void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    media_lib_add_default_adapter();
    esp_capture_set_thread_scheduler(capture_scheduler);
    media_lib_thread_set_schedule_cb(thread_scheduler);
    init_board();
    if (doorbell_webrtc_init() != 0) {
        ESP_LOGE(TAG, "WebRTC synchronization initialization failed");
        return;
    }
#ifdef CONFIG_DOORBELL_SUPPORT_PEDESTRIAN_DETECT
    multimodal_fusion_config_t sensor_fusion_config =
        multimodal_fusion_default_config();
    if (!multimodal_fusion_init(&sensor_fusion, &sensor_fusion_config) ||
        media_lib_mutex_create(&sensor_fusion_mutex) != 0) {
        ESP_LOGE(TAG, "Multimodal sensor fusion initialization failed");
        return;
    }
#endif
    esp_err_t door_lock_ret = door_lock_init();
    if (door_lock_ret != ESP_OK) {
        ESP_LOGE(TAG, "Door lock initialization failed: %s",
                 esp_err_to_name(door_lock_ret));
    }
    presence_fusion_config_t radar_presence_config =
        presence_fusion_default_config();
    if (!presence_fusion_init(&radar_presence, &radar_presence_config)) {
        ESP_LOGE(TAG, "Radar presence fusion initialization failed");
    } else {
        esp_err_t radar_ret =
            radar_ld2450_init(radar_frame_received, NULL);
        if (radar_ret != ESP_OK) {
            ESP_LOGE(TAG, "LD2450 initialization failed: %s",
                     esp_err_to_name(radar_ret));
        }
    }
    media_sys_buildup();
    init_console();
    network_init(WIFI_SSID, WIFI_PASSWORD, network_event_handler);
    while (1) {
        media_lib_thread_sleep(2000);
        query_webrtc();
    }
}
