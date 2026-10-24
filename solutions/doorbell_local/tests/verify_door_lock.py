"""Build and verify the Task 7 door-lock contract."""

from pathlib import Path
import os
import subprocess
import tempfile


SOLUTION_DIR = Path(__file__).resolve().parents[1]
MAIN_DIR = SOLUTION_DIR / "main"
TEST_DIR = Path(__file__).resolve().parent


def compile_and_run_state_test() -> None:
    compiler = os.environ.get("CC", r"D:\mingw64\bin\gcc.exe")
    with tempfile.TemporaryDirectory(prefix="door-lock-test-") as temp_dir:
        executable = Path(temp_dir) / "door_lock_state_test.exe"
        subprocess.run(
            [
                compiler,
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                f"-I{MAIN_DIR}",
                str(TEST_DIR / "door_lock_state_test.c"),
                str(MAIN_DIR / "door_lock_state.c"),
                "-o",
                str(executable),
            ],
            check=True,
        )
        subprocess.run([str(executable)], check=True)


def require_tokens(path: Path, tokens: tuple[str, ...]) -> None:
    text = path.read_text(encoding="utf-8")
    for token in tokens:
        assert token in text, f"{path.name} is missing {token!r}"


def main() -> int:
    compile_and_run_state_test()

    require_tokens(
        MAIN_DIR / "door_lock.h",
        (
            "DOOR_LOCK_SERVO_GPIO 32",
            "DOOR_LOCK_PWM_FREQUENCY_HZ 50",
            "DOOR_LOCK_LOCKED_PULSE_US 500",
            "DOOR_LOCK_UNLOCKED_PULSE_US 2500",
            "DOOR_LOCK_MOTION_TIME_MS 500",
            "DOOR_LOCK_HOLD_TIME_MS 5000",
        ),
    )
    require_tokens(
        MAIN_DIR / "CMakeLists.txt",
        ('"door_lock.c"', '"door_lock_state.c"'),
    )
    require_tokens(
        MAIN_DIR / "door_lock.c",
        (
            '#include "driver/ledc.h"',
            "ledc_timer_config",
            "ledc_channel_config",
            "ledc_set_duty(",
            "ledc_update_duty(",
        ),
    )
    door_lock_source = (MAIN_DIR / "door_lock.c").read_text(encoding="utf-8")
    if "ledc_set_duty_and_update" in door_lock_source:
        raise AssertionError(
            "door_lock.c must not use ledc_set_duty_and_update because it "
            "requires the LEDC fade service"
        )
    require_tokens(
        MAIN_DIR / "webrtc.c",
        (
            '#include "door_lock.h"',
            "door_lock_request_open()",
            "DOOR_OPENING",
            "DOOR_OPENED",
            "DOOR_CLOSING",
            "DOOR_LOCKED",
            "DOOR_FAILED",
        ),
    )
    require_tokens(
        MAIN_DIR / "webrtc_test.html",
        (
            "case 'DOOR_OPENING':",
            "case 'DOOR_OPENED':",
            "case 'DOOR_CLOSING':",
            "case 'DOOR_LOCKED':",
            "case 'DOOR_FAILED':",
        ),
    )

    print("PASS: Task 7 door-lock contract is consistent")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
