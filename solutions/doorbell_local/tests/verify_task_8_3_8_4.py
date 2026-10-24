"""Build host tests and verify Task 8.3/8.4 integration contracts."""

from pathlib import Path
import os
import subprocess
import tempfile


SOLUTION = Path(__file__).resolve().parents[1]
MAIN = SOLUTION / "main"
TESTS = Path(__file__).resolve().parent


def compile_and_run(compiler: str, test: str, production: str) -> None:
    with tempfile.TemporaryDirectory(prefix="task-8-fusion-") as directory:
        executable = Path(directory) / (Path(test).stem + ".exe")
        subprocess.run(
            [
                compiler,
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                f"-I{MAIN}",
                str(TESTS / test),
                str(MAIN / production),
                "-o",
                str(executable),
            ],
            check=True,
            timeout=30,
        )
        subprocess.run([str(executable)], check=True, timeout=30)


def require_tokens(path: Path, *tokens: str) -> None:
    source = path.read_text(encoding="utf-8")
    for token in tokens:
        assert token in source, f"{path.name} is missing {token!r}"


def main() -> int:
    compiler = os.environ.get("CC", r"D:\mingw64\bin\gcc.exe")
    compile_and_run(compiler, "doorbell_event_test.c", "doorbell_event.c")
    compile_and_run(compiler, "multimodal_fusion_test.c", "multimodal_fusion.c")
    compile_and_run(
        compiler,
        "pedestrian_format_probe_test.c",
        "pedestrian_format_probe.c",
    )
    compile_and_run(compiler, "camera_motion_test.c", "camera_motion.c")

    require_tokens(
        MAIN / "webrtc_http_server.h",
        "webrtc_http_server_publish_event",
    )
    require_tokens(
        MAIN / "webrtc_http_server.c",
        "telemetry_queue",
        "MAX_TELEMETRY_QUEUE_SIZE",
        "xQueueReceive(telemetry_queue",
        "vision.sequence == session->last_vision_sequence",
    )
    require_tokens(
        MAIN / "main.c",
        "doorbell_radar_event_format_json",
        "webrtc_http_server_publish_event",
        "multimodal_fusion_on_radar",
        "doorbell_report_vision_detection",
    )
    require_tokens(
        MAIN / "webrtc.c",
        "doorbell_report_vision_detection",
        "Failed to set up pedestrian detection sink",
        "Failed to enable pedestrian detection sink",
        "Failed to start pedestrian detection",
    )
    require_tokens(
        MAIN / "pedestrain_detect.cpp",
        "if (cfg == NULL)",
        "if (detector == NULL)",
        "if (sink == NULL)",
        "ESP_CACHE_MSYNC_FLAG_DIR_M2C",
        "if (data == NULL && detect->filtering)",
    )
    require_tokens(
        MAIN / "webrtc_test.html",
        "case 'radar_event':",
        "addRadarEvent(message)",
        "textContent",
        "MAX_DETECTION_EVENTS = 100",
    )
    require_tokens(
        MAIN / "CMakeLists.txt",
        '"doorbell_event.c"',
        '"multimodal_fusion.c"',
    )
    require_tokens(
        SOLUTION / "sdkconfig.defaults.esp32p4",
        "CONFIG_DOORBELL_SUPPORT_PEDESTRIAN_DETECT=y",
    )
    require_tokens(
        SOLUTION / "partitions.csv",
        "factory,  app,  factory, 0x10000, 0x800000,",
    )
    print("PASS: Task 8.3/8.4 integration contract")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
