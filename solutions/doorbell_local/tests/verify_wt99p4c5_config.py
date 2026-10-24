"""Verify the WT99P4C5-S1 Task 4 and Task 6 configuration contract."""

from pathlib import Path


SOLUTION_DIR = Path(__file__).resolve().parents[1]
REPO_ROOT = Path(__file__).resolve().parents[3]


def active_kconfig_lines(path: Path) -> set[str]:
    return {
        line.strip()
        for line in path.read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    }


def require_lines(actual: set[str], required: set[str], source: Path) -> None:
    missing = sorted(required - actual)
    assert not missing, f"{source.name} is missing: {', '.join(missing)}"


def require_token_guarded(text: str, token: str, guard: str, source: Path) -> None:
    """Require every line containing token to be inside the requested #if."""
    conditions: list[str] = []
    matches = 0
    for line_number, line in enumerate(text.splitlines(), start=1):
        stripped = line.strip()
        if stripped.startswith("#if "):
            conditions.append(stripped[4:].strip())
        elif stripped.startswith("#ifdef "):
            conditions.append(stripped[7:].strip())
        elif stripped.startswith("#endif"):
            if conditions:
                conditions.pop()
        elif token in line:
            matches += 1
            assert guard in conditions, (
                f"{source.name}:{line_number}: {token!r} must be guarded by "
                f"#if {guard}"
            )
    assert matches, f"{source.name} is missing expected token: {token}"


def require_token_outside_guard(text: str, token: str, guard: str, source: Path) -> None:
    """Require at least one occurrence of token outside a feature-only #if."""
    conditions: list[str] = []
    for line in text.splitlines():
        stripped = line.strip()
        if stripped.startswith("#if "):
            conditions.append(stripped[4:].strip())
        elif stripped.startswith("#ifdef "):
            conditions.append(stripped[7:].strip())
        elif stripped.startswith("#endif"):
            if conditions:
                conditions.pop()
        elif token in line and guard not in conditions:
            return
    raise AssertionError(
        f"{source.name}: {token!r} must not depend on #if {guard}"
    )


def main() -> int:
    defaults_path = SOLUTION_DIR / "sdkconfig.defaults.esp32p4"
    defaults = active_kconfig_lines(defaults_path)

    require_lines(
        defaults,
        {
            "CONFIG_ESPTOOLPY_FLASHMODE_QIO=y",
            "CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y",
            "CONFIG_ESP_CONSOLE_UART_DEFAULT=y",
            "CONFIG_CAMERA_OV5647=y",
            "CONFIG_CAMERA_OV5647_MIPI_RAW10_1280X960_BINNING_45FPS=y",
            "CONFIG_CAMERA_OV5647_MIPI_DEFAULT_FMT_RAW10_1280X960_BINNING_45FPS=y",
            "CONFIG_ESP_VIDEO_ENABLE_DVP_VIDEO_DEVICE=n",
            "CONFIG_ESP_VIDEO_ENABLE_ISP_PIPELINE_CONTROLLER=y",
            'CONFIG_ESP_HOSTED_IDF_SLAVE_TARGET="esp32c5"',
            "CONFIG_SLAVE_IDF_TARGET_ESP32C5=y",
            "CONFIG_ESP_HOSTED_PRIV_SDIO_PIN_CMD_SLOT_1=19",
            "CONFIG_ESP_HOSTED_PRIV_SDIO_PIN_CLK_SLOT_1=18",
            "CONFIG_ESP_HOSTED_PRIV_SDIO_PIN_D0_SLOT_1=14",
            "CONFIG_ESP_HOSTED_PRIV_SDIO_PIN_D1_4BIT_BUS_SLOT_1=15",
            "CONFIG_ESP_HOSTED_PRIV_SDIO_PIN_D2_4BIT_BUS_SLOT_1=16",
            "CONFIG_ESP_HOSTED_PRIV_SDIO_PIN_D3_4BIT_BUS_SLOT_1=17",
            "CONFIG_ESP_HOSTED_SDIO_GPIO_RESET_SLAVE=54",
            "CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y",
            "CONFIG_ESP32P4_REV_MIN_100=y",
            "CONFIG_ESP_HOSTED_MEMPOOL_PREFER_SPIRAM=y",
            "CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=n",
            "CONFIG_WIFI_RMT_STATIC_RX_BUFFER_NUM=16",
            "CONFIG_WIFI_RMT_DYNAMIC_RX_BUFFER_NUM=32",
            "CONFIG_WIFI_RMT_TX_BUFFER_TYPE=0",
            "CONFIG_WIFI_RMT_DYNAMIC_RX_MGMT_BUF=0",
            "CONFIG_WIFI_RMT_ESPNOW_MAX_ENCRYPT_NUM=7",
            "CONFIG_WIFI_RMT_NVS_ENABLED=n",
            "CONFIG_FLASH_HUMAN_FACE_DETECT_MSRMNP_S8_V1=y",
            "CONFIG_HUMAN_FACE_DETECT_MSRMNP_S8_V1=y",
            "CONFIG_HUMAN_FACE_DETECT_MODEL_IN_FLASH_RODATA=y",
        },
        defaults_path,
    )

    manifest_path = SOLUTION_DIR / "main" / "idf_component.yml"
    manifest = manifest_path.read_text(encoding="utf-8")
    assert "espressif/human_face_detect:" in manifest
    assert 'version: "^0.5.0"' in manifest, (
        "face detection must use the ESP-DL 3.3-compatible component release"
    )
    assert "espressif/esp_hosted:" in manifest
    assert 'version: "2.12.12"' in manifest, (
        "the verified P4+C5 hosted transport release must be pinned"
    )
    assert "espressif/esp_wifi_remote:" in manifest
    assert 'version: "1.6.3"' in manifest, (
        "the verified remote Wi-Fi Kconfig contract must be pinned"
    )

    forbidden_prefixes = (
        "CONFIG_ESPTOOLPY_FLASHSIZE_8MB=",
        "CONFIG_FLASHMODE_QIO=",
        "CONFIG_CAMERA_SC2336",
        "CONFIG_CAMERA_OV5647_MIPI_RAW10_1280x960_BINNING_45FPS=",
        "CONFIG_IDF_SLAVE_TARGET=",
        "CONFIG_ESP_HOSTED_CUSTOM_SDIO_PINS=",
        "CONFIG_SLAVE_IDF_TARGET_ESP32C6=",
        "CONFIG_ESP_HOSTED_SPI_HD_",
        "CONFIG_ESP_SPI_HD_",
        "CONFIG_NETWORK_USE_ETHERNET=",
        "CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=",
    )
    forbidden = sorted(
        line for line in defaults if line.startswith(forbidden_prefixes)
    )
    assert not forbidden, "unexpected Task 4 settings: " + ", ".join(forbidden)

    settings_path = SOLUTION_DIR / "main" / "settings.h"
    settings = settings_path.read_text(encoding="utf-8")
    for expected in (
        '#define TEST_BOARD_NAME "WT99P4C5_S1"',
        "#define VIDEO_WIDTH  640",
        "#define VIDEO_HEIGHT 480",
        "#define VIDEO_FPS    15",
        "#define DOOR_BELL_RING_BUTTON  35",
        '#include "settings.local.h"',
    ):
        assert expected in settings, f"settings.h is missing: {expected}"

    board_cfg_path = REPO_ROOT / "components" / "codec_board" / "board_cfg.txt"
    board_cfg = board_cfg_path.read_text(encoding="utf-8")
    expected_profile = """Board: WT99P4C5_S1
i2c: {sda: 7, scl: 8}
i2s: {mclk: 13, bclk: 12, ws: 10, dout: 9, din: 11}
in_out: {codec: ES8311, pa: 53, use_mclk: 1, pa_gain:6}
sdcard: {clk: 43, cmd: 44, d0: 39, d1: 40, d2: 41, d3: 42}
camera: {type: mipi}
lcd: {
    bus: mipi, ldo_chan: 3, ldo_voltage: 2500, lane_num: 2,
    lane_bitrate: 1000, dpi_clk: 80, bit_depth: 16, fb_num: 2
    dsi_hsync: 1344,  dsi_vsync: 635,
    dsi_hbp: 160, dsi_hfp: 160,
    dsi_vbp: 23, dsi_vfp: 12,
    rst: -1,
    width: 1024, height: 600,
}"""
    assert expected_profile in board_cfg, (
        "board_cfg.txt must define the verified WT99P4C5-S1 profile"
    )

    page_path = SOLUTION_DIR / "main" / "webrtc_test.html"
    page = page_path.read_text(encoding="utf-8")
    assert "video: true" not in page, (
        "the WT99P4C5-S1 doorbell is board-video/send-only; the browser "
        "must not block call setup on an unused webcam"
    )
    assert "iceServers: []" in page, (
        "the local doorbell page must not wait on public STUN servers"
    )
    assert "iceCandidatePoolSize: 0" in page
    assert 'id="localVideoBox" hidden' in page, (
        "the unused browser-video preview must not appear as a misleading "
        "black video panel"
    )
    assert 'id="commandNotice"' in page
    assert "showCommandNotice('门已打开')" in page, (
        "OPEN_DOOR acknowledgement must have a visible browser response"
    )
    assert "const DISCONNECT_GRACE_PERIOD_MS = 15000;" in page, (
        "a transient WebRTC disconnected state must not immediately hang up"
    )
    assert "function scheduleDisconnectCleanup()" in page
    connection_handler = page[
        page.index("peerConnection.onconnectionstatechange") : page.index(
            "return peerConnection;", page.index("peerConnection.onconnectionstatechange")
        )
    ]
    assert "scheduleDisconnectCleanup();" in connection_handler
    assert "peerConnection.connectionState === 'disconnected' ||" not in connection_handler, (
        "browser must not close a call immediately on a transient disconnected state"
    )

    signaling_path = SOLUTION_DIR / "main" / "webrtc_http_server.c"
    signaling = signaling_path.read_text(encoding="utf-8")
    assert "static sse_session_t *event_stream = NULL;" in signaling, (
        "a refreshed browser must replace a stale SSE listener immediately"
    )
    assert "event_stream->stopping = true;" in signaling
    assert "Only allow one listener" not in signaling, (
        "stale SSE listeners must not block a page refresh"
    )

    ignore_path = REPO_ROOT / ".gitignore"
    ignore = ignore_path.read_text(encoding="utf-8")
    assert "**/settings.local.h" in ignore, "local Wi-Fi settings must be ignored"

    template_path = SOLUTION_DIR / "main" / "settings.local.h.example"
    template = template_path.read_text(encoding="utf-8")
    assert "YOUR_2_4_GHZ_WIFI_SSID" in template
    assert "YOUR_WIFI_PASSWORD" in template

    for tone_name in ("ring", "open", "join"):
        tone_path = SOLUTION_DIR / "main" / f"{tone_name}.aac"
        assert tone_path.read_bytes().startswith(b"\xff\xf1"), (
            f"{tone_name}.aac must be real ADTS AAC data, not a Windows "
            "symlink placeholder"
        )

    network_path = REPO_ROOT / "solutions" / "common" / "network.c"
    network = network_path.read_text(encoding="utf-8")
    assert "if (ssid && ssid[0] != '\\0')" in network, (
        "an explicit build-time Wi-Fi SSID must take precedence over stale NVS"
    )
    assert "} else if (load_from_nvs())" in network, (
        "NVS must be a fallback only when no explicit Wi-Fi SSID is supplied"
    )

    media_sys_path = SOLUTION_DIR / "main" / "media_sys.c"
    media_sys = media_sys_path.read_text(encoding="utf-8")
    assert "media_lib_thread_sleep(1);" in media_sys, (
        "the music player must yield so it cannot starve the P4 idle task"
    )
    dvp_guard = "CONFIG_ESP_VIDEO_ENABLE_DVP_VIDEO_DEVICE"
    require_token_guarded(
        media_sys, "esp_video_init_dvp_config_t dvp_config", dvp_guard, media_sys_path
    )
    require_token_guarded(
        media_sys, "cam_config.dvp = &dvp_config", dvp_guard, media_sys_path
    )

    webrtc_path = SOLUTION_DIR / "main" / "webrtc.c"
    webrtc = webrtc_path.read_text(encoding="utf-8")
    capture_guard = "CONFIG_DOORBELL_SUPPORT_PEDESTRIAN_DETECT"
    require_token_outside_guard(
        webrtc, "esp_webrtc_set_no_auto_capture(opened_webrtc, true);", capture_guard, webrtc_path
    )

    print("PASS: WT99P4C5-S1 Task 4 configuration is consistent")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
