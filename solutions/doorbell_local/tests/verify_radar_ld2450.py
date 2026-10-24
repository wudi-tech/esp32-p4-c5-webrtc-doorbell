"""Build and run the host-side LD2450 protocol parser tests."""

from pathlib import Path
import os
import subprocess
import tempfile


SOLUTION_DIR = Path(__file__).resolve().parents[1]
MAIN_DIR = SOLUTION_DIR / "main"
TEST_DIR = Path(__file__).resolve().parent


def function_region(source: str, start: str, end: str) -> str:
    start_index = source.index(start)
    end_index = source.index(end, start_index)
    return source[start_index:end_index]


def assert_in_order(source: str, *tokens: str) -> None:
    position = -1
    for token in tokens:
        next_position = source.find(token, position + 1)
        assert next_position >= 0, f"webrtc.c is missing concurrency guard {token!r}"
        assert next_position > position
        position = next_position


def main() -> int:
    compiler = os.environ.get("CC", r"D:\mingw64\bin\gcc.exe")
    with tempfile.TemporaryDirectory(prefix="ld2450-test-") as temp_dir:
        executable = Path(temp_dir) / "radar_ld2450_parser_test.exe"
        subprocess.run(
            [
                compiler,
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                f"-I{MAIN_DIR}",
                str(TEST_DIR / "radar_ld2450_parser_test.c"),
                str(MAIN_DIR / "radar_ld2450.c"),
                "-o",
                str(executable),
            ],
            check=True,
        )
        subprocess.run([str(executable)], check=True)

    required_tokens = {
        MAIN_DIR / "radar_ld2450.h": (
            "LD2450_UART_BAUD_RATE 256000",
            "LD2450_UART_RX_GPIO 4",
            "LD2450_UART_TX_GPIO 5",
            "radar_ld2450_init(ld2450_frame_callback_t callback, void *ctx)",
        ),
        MAIN_DIR / "radar_ld2450.c": (
            "uart_read_bytes",
            "ld2450_stream_feed",
            "app_frame_callback(raw_frame, frame, app_frame_callback_ctx)",
            "No valid LD2450 frame for",
        ),
        MAIN_DIR / "CMakeLists.txt": (
            '"radar_ld2450.c"',
            '"presence_fusion.c"',
            '"doorbell_ring_policy.c"',
        ),
        MAIN_DIR / "main.c": (
            '#include "radar_ld2450.h"',
            '#include "presence_fusion.h"',
            "presence_fusion_process",
            'request_sensor_ring("radar")',
            "radar_ld2450_init(radar_frame_received, NULL)",
        ),
        MAIN_DIR / "webrtc.c": (
            "doorbell_request_ring",
            "doorbell_ring_policy_evaluate",
            "DOORBELL_RING_SEND_FAILED",
        ),
    }
    for path, tokens in required_tokens.items():
        text = path.read_text(encoding="utf-8")
        for token in tokens:
            assert token in text, f"{path.name} is missing {token!r}"

    webrtc_source = (MAIN_DIR / "webrtc.c").read_text(encoding="utf-8")
    main_source = (MAIN_DIR / "main.c").read_text(encoding="utf-8")
    common_source = (MAIN_DIR / "common.h").read_text(encoding="utf-8")
    ring_request = function_region(
        webrtc_source,
        "doorbell_ring_request_result_t doorbell_request_ring",
        "static void door_lock_state_changed",
    )
    command_callback = function_region(
        webrtc_source,
        "static int door_bell_on_cmd(",
        "int close_data_channel(",
    )
    regression_failures = []
    if "esp_webrtc_enable_peer_connection" in command_callback:
        regression_failures.append(
            "peer enable/disable still runs inside the door-state callback lock"
        )
    if "door_bell_change_state_locked(DOOR_BELL_STATE_NONE)" in ring_request:
        regression_failures.append(
            "ring send-failure rollback still calls the audio-changing state helper"
        )
    assert not regression_failures, "; ".join(regression_failures)

    peer_worker = function_region(
        webrtc_source,
        "static void peer_connection_change_worker(",
        "static int schedule_peer_connection_change(",
    )
    state_change = function_region(
        webrtc_source,
        "static void door_bell_change_state(",
        "doorbell_ring_request_result_t doorbell_request_ring",
    )
    deny_branch = function_region(
        command_callback,
        "} else if (SAME_STR(cmd, DOOR_BELL_CALL_DENIED_CMD))",
        "} else if (SAME_STR(cmd, DOOR_BELL_ENABLE_RTP_TRANSFORMER))",
    )
    accept_branch = function_region(
        command_callback,
        "} else if (SAME_STR(cmd, DOOR_BELL_CALL_ACCEPTED_CMD))",
        "} else if (SAME_STR(cmd, DOOR_BELL_CALL_DENIED_CMD))",
    )
    async_state_failures = []
    if not (
        "door_bell_state == DOOR_BELL_STATE_DISCONNECTING" in state_change
        and "state == DOOR_BELL_STATE_CONNECTED" in state_change
    ):
        async_state_failures.append(
            "CONNECTED callback can overwrite the pending DISCONNECTING reservation"
        )
    if not (
        "door_bell_state_t state_before_disconnect = door_bell_state;"
        in deny_branch
        and "restore_peer_change_state_locked(" in deny_branch
    ):
        async_state_failures.append(
            "DENY scheduling failure does not restore the prior busy state"
        )
    rollback_helper = function_region(
        webrtc_source,
        "static void restore_peer_change_state_locked(",
        "static void peer_connection_change_worker(",
    ) if "static void restore_peer_change_state_locked(" in webrtc_source else ""
    if not (
        "previous_state == DOOR_BELL_STATE_RINGING" in rollback_helper
        and "play_tone(DOOR_BELL_TONE_RING)" in rollback_helper
        and "restore_peer_change_state_locked(" in peer_worker
        and "restore_peer_change_state_locked(" in accept_branch
        and "restore_peer_change_state_locked(" in deny_branch
    ):
        async_state_failures.append(
            "failed ACCEPT/DENY does not restore both RINGING state and ringtone"
        )
    if not all(
        token in accept_branch
        for token in (
            "door_bell_state != DOOR_BELL_STATE_NONE",
            "door_bell_state != DOOR_BELL_STATE_RINGING",
        )
    ):
        async_state_failures.append(
            "ACCEPT does not allow idle direct-call and active-ring entry states"
        )
    if not all(
        token in deny_branch
        for token in (
            "door_bell_state != DOOR_BELL_STATE_RINGING",
            "door_bell_state != DOOR_BELL_STATE_CONNECTING",
            "door_bell_state != DOOR_BELL_STATE_CONNECTED",
        )
    ):
        async_state_failures.append(
            "DENY does not reject NONE, DISCONNECTING, and other stale states"
        )
    review_failures = async_state_failures

    assert ring_request.count(
        "media_lib_mutex_lock(door_bell_mutex, MEDIA_LIB_MAX_LOCK_TIME)"
    ) == 2, "ring request must protect reservation and completion separately"
    assert ring_request.count("media_lib_mutex_unlock(door_bell_mutex)") == 2, (
        "ring request must release both door-state critical sections"
    )
    assert_in_order(
        ring_request,
        "media_lib_mutex_lock(webrtc_api_mutex, MEDIA_LIB_MAX_LOCK_TIME)",
        "media_lib_mutex_lock(door_bell_mutex, MEDIA_LIB_MAX_LOCK_TIME)",
        "doorbell_ring_policy_evaluate(",
        "door_bell_state_t previous_state = door_bell_state;",
        "door_bell_state = DOOR_BELL_STATE_RINGING;",
        "media_lib_mutex_unlock(door_bell_mutex)",
        "SEND_CMD(ring_handle, DOOR_BELL_RING_CMD)",
        "media_lib_mutex_lock(door_bell_mutex, MEDIA_LIB_MAX_LOCK_TIME)",
        "if (send_ret != 0)",
        "if (door_bell_state == DOOR_BELL_STATE_RINGING)",
        "door_bell_state = previous_state;",
        "play_tone(DOOR_BELL_TONE_RING)",
        "media_lib_mutex_unlock(door_bell_mutex)",
        "media_lib_mutex_unlock(webrtc_api_mutex)",
    )

    close_data_channel = function_region(
        webrtc_source, "int close_data_channel(int id)", "static void add_channel"
    )
    data_thread = function_region(
        webrtc_source,
        "static void data_thread_hdlr(",
        "static int webrtc_data_channel_opened",
    )
    start_webrtc = function_region(
        webrtc_source, "int start_webrtc(char *url)", "void query_webrtc(void)"
    )
    stop_webrtc = webrtc_source[webrtc_source.index("int stop_webrtc(void)") :]
    lifecycle_failures = []
    if "int doorbell_webrtc_init(void);" not in common_source:
        lifecycle_failures.append("WebRTC synchronization has no explicit early init API")
    if not (
        "doorbell_webrtc_init()" in main_source
        and main_source.index("doorbell_webrtc_init()")
        < main_source.index("radar_ld2450_init(radar_frame_received, NULL)")
        < main_source.index("network_init(WIFI_SSID, WIFI_PASSWORD, network_event_handler)")
    ):
        lifecycle_failures.append(
            "synchronization is not initialized before radar and network callbacks start"
        )
    if "door_bell_sync_init()" in start_webrtc or "door_bell_sync_init()" in stop_webrtc:
        lifecycle_failures.append(
            "concurrent start/stop still perform unsafe lazy mutex allocation"
        )
    if "static media_lib_mutex_handle_t webrtc_lifecycle_mutex;" not in webrtc_source:
        lifecycle_failures.append(
            "start/stop are not serialized by a dedicated lifecycle mutex"
        )
    sync_init = function_region(
        webrtc_source,
        "static int door_bell_sync_init(void)",
        "static bool doorbell_worker_enabled",
    )
    if not (
        "media_lib_mutex_create(&webrtc_lifecycle_mutex)" in sync_init
        and "media_lib_mutex_destroy" in sync_init
    ):
        lifecycle_failures.append(
            "lifecycle mutex initialization does not clean up partial failure"
        )
    if not (
        "door_bell_state_t previous_state;" in webrtc_source
        and "esp_webrtc_handle_t target_handle;" in webrtc_source
        and "request->previous_state" in peer_worker
        and "request->target_handle" in peer_worker
    ):
        lifecycle_failures.append(
            "peer change requests do not retain handle identity and prior state"
        )
    if not (
        "if (ret == 0)" in peer_worker
        and "restore_peer_change_state_locked(" in peer_worker
    ):
        lifecycle_failures.append(
            "peer change completion does not commit success and roll back failure"
        )
    for name, region in (
        ("close_data_channel", close_data_channel),
        ("data_thread_hdlr", data_thread),
    ):
        if not (
            "media_lib_mutex_lock(webrtc_api_mutex" in region
            and "media_lib_mutex_unlock(webrtc_api_mutex)" in region
        ):
            lifecycle_failures.append(
                f"{name} accesses the WebRTC handle outside the API lifetime lock"
            )
    start_call = start_webrtc.find("esp_webrtc_start(opened_webrtc)")
    publish_handle = start_webrtc.find("webrtc = opened_webrtc;")
    start_workers = start_webrtc.find("start_doorbell_workers()")
    if not (0 <= start_call < publish_handle < start_workers):
        lifecycle_failures.append(
            "start publishes readiness or creates workers before WebRTC start succeeds"
        )
    if not (
        "stop_doorbell_workers()" in start_webrtc
        and "esp_webrtc_close(opened_webrtc)" in start_webrtc
    ):
        lifecycle_failures.append(
            "restart/start failure does not retire workers and close the local handle"
        )
    for name, region in (("start_webrtc", start_webrtc), ("stop_webrtc", stop_webrtc)):
        lifecycle_lock = region.find(
            "media_lib_mutex_lock(webrtc_lifecycle_mutex,"
        )
        lifecycle_unlock = region.find(
            "media_lib_mutex_unlock(webrtc_lifecycle_mutex)"
        )
        worker_stop = region.find("stop_doorbell_workers()")
        if not (
            0 <= lifecycle_lock < worker_stop < lifecycle_unlock
            and region.count("media_lib_mutex_unlock(webrtc_lifecycle_mutex)") == 1
            and "return " not in region[worker_stop:lifecycle_unlock]
        ):
            lifecycle_failures.append(
                f"{name} does not hold one lifecycle lock across worker retirement and all handle changes"
            )
    for name, region in (
        ("peer_connection_change_worker", peer_worker),
        ("data_thread_hdlr", data_thread),
        (
            "key_monitor_thread",
            function_region(
                webrtc_source,
                "static void key_monitor_thread(",
                "static int start_doorbell_workers",
            ),
        ),
    ):
        if "webrtc_lifecycle_mutex" in region:
            lifecycle_failures.append(
                f"{name} must not acquire the lifecycle mutex"
            )
    review_failures.extend(lifecycle_failures)

    channel_failures = []
    if "static media_lib_mutex_handle_t data_channel_mutex;" not in webrtc_source:
        channel_failures.append("data-channel state has no dedicated mutex")
    if "media_lib_mutex_create(&data_channel_mutex)" not in sync_init:
        channel_failures.append("data-channel mutex is not initialized early")
    add_channel = function_region(
        webrtc_source, "static void add_channel(", "static void remove_channel("
    )
    remove_channel = function_region(
        webrtc_source, "static void remove_channel(", "#define CNT_TO_CHAR"
    )
    on_data = function_region(
        webrtc_source, "static int webrtc_on_data(", "static int webrtc_data_channel_closed"
    )
    for name, region in (
        ("add_channel", add_channel),
        ("remove_channel", remove_channel),
        ("webrtc_on_data", on_data),
    ):
        if not (
            "media_lib_mutex_lock(data_channel_mutex" in region
            and "media_lib_mutex_unlock(data_channel_mutex)" in region
        ):
            channel_failures.append(f"{name} accesses user_ch without its mutex")
    if "user_data_ch_t *get_channel(" in webrtc_source:
        channel_failures.append("get_channel exposes an unlocked shared-state pointer")
    if not (
        "esp_webrtc_handle_t owner_handle;" in webrtc_source
        and "add_channel(ch, (esp_webrtc_handle_t)ctx)" in webrtc_source
        and "remove_channel(ch, (esp_webrtc_handle_t)ctx)" in webrtc_source
        and "user_ch[i].owner_handle == owner_handle" in remove_channel
    ):
        channel_failures.append(
            "stale close callbacks can remove a replacement handle's channel"
        )
    if not (
        "strdup(" in data_thread
        and "media_lib_mutex_lock(data_channel_mutex" in data_thread
        and "media_lib_mutex_unlock(data_channel_mutex)" in data_thread
        and data_thread.index("media_lib_mutex_unlock(data_channel_mutex)")
        < data_thread.index("esp_webrtc_get_peer_connection")
    ):
        channel_failures.append(
            "data worker does not copy channel identity before WebRTC API calls"
        )
    if not (
        "media_lib_mutex_lock(data_channel_mutex" in close_data_channel
        and "strdup(" in close_data_channel
        and close_data_channel.index("media_lib_mutex_unlock(data_channel_mutex)")
        < close_data_channel.index("esp_webrtc_get_peer_connection")
    ):
        channel_failures.append(
            "close_data_channel holds or exposes channel state across WebRTC API calls"
        )
    if not (
        "media_lib_mutex_unlock(data_channel_mutex)" in remove_channel
        and "free(" in remove_channel
        and remove_channel.index("media_lib_mutex_unlock(data_channel_mutex)")
        < remove_channel.index("free(")
    ):
        channel_failures.append(
            "channel close frees a label while shared state is still exposed"
        )
    review_failures.extend(channel_failures)
    assert not review_failures, "; ".join(review_failures)

    assert_in_order(
        peer_worker,
        "media_lib_mutex_lock(webrtc_api_mutex, MEDIA_LIB_MAX_LOCK_TIME)",
        "media_lib_mutex_lock(door_bell_mutex, MEDIA_LIB_MAX_LOCK_TIME)",
        "peer_handle = request->target_handle;",
        "media_lib_mutex_unlock(door_bell_mutex)",
        "esp_webrtc_enable_peer_connection(peer_handle, request->enable)",
        "media_lib_mutex_unlock(webrtc_api_mutex)",
    )

    assert_in_order(
        state_change,
        "media_lib_mutex_lock(door_bell_mutex, MEDIA_LIB_MAX_LOCK_TIME)",
        "door_bell_state == DOOR_BELL_STATE_DISCONNECTING",
        "state == DOOR_BELL_STATE_CONNECTED",
        "door_bell_change_state_locked(state)",
        "media_lib_mutex_unlock(door_bell_mutex)",
    )

    assert_in_order(
        start_webrtc,
        "door_bell_sync_ready()",
        "stop_doorbell_workers()",
        "esp_webrtc_start(opened_webrtc)",
        "webrtc = opened_webrtc;",
        "start_doorbell_workers()",
    )

    assert_in_order(
        stop_webrtc,
        "media_lib_mutex_lock(door_bell_mutex, MEDIA_LIB_MAX_LOCK_TIME)",
        "esp_webrtc_handle_t handle = webrtc;",
        "webrtc = NULL;",
        "media_lib_mutex_unlock(door_bell_mutex)",
        "esp_webrtc_close(handle)",
    )
    print("PASS: serialized doorbell ring transaction")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
