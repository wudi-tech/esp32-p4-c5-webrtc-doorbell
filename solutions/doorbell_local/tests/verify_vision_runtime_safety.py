"""Regression checks for the on-device vision/runtime failure seen on 2026-09-10."""

from pathlib import Path
import re


SOLUTION_DIR = Path(__file__).resolve().parents[1]
MAIN_DIR = SOLUTION_DIR / "main"


def main() -> int:
    scheduler = (MAIN_DIR / "main.c").read_text(encoding="utf-8")
    detector = (MAIN_DIR / "pedestrain_detect.cpp").read_text(encoding="utf-8")
    event = (MAIN_DIR / "vision_event.c").read_text(encoding="utf-8")
    webrtc = (MAIN_DIR / "webrtc.c").read_text(encoding="utf-8")

    detect_branch = re.search(
        r'else if \(strcmp\(thread_name, "detect"\) == 0\) \{(?P<body>.*?)\n\s*\}',
        scheduler,
        flags=re.DOTALL,
    )
    assert detect_branch is not None, "detect scheduler branch is missing"
    assert "stack_size = 24 * 1024;" in detect_branch.group("body"), (
        "detect must have a 24 KiB stack; the default 4 KiB stack overflowed "
        "inside float-to-text conversion"
    )

    assert 'char json[1024]' not in scheduler, (
        "vision JSON must not reserve another 1 KiB on the AI thread stack"
    )
    assert '\\"score\\":%.2f' not in event, (
        "vision JSON must not invoke the stack-heavy newlib float formatter"
    )
    assert '\\"score\\":%u.%02u' in event, (
        "vision events must preserve the browser API while formatting the "
        "confidence from integer components"
    )

    assert "pedestrian_format_probe_current" not in detector, (
        "the documented RGB565 capture path must not rotate through four "
        "incompatible pixel interpretations"
    )
    assert ".pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565LE" in detector
    assert "vision_tracker_t tracker;" in detector, (
        "a single fixed capture format needs only one tracker"
    )

    assert '"BOOT level changed: %d -> %d"' in webrtc
    assert '"BOOT ring request: %s"' in webrtc

    print("PASS: vision runtime stack, format, and BOOT diagnostics are safe")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
