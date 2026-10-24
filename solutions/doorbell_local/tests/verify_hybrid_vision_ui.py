from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HTML = (ROOT / "main" / "webrtc_test.html").read_text(encoding="utf-8")


def require(token: str, reason: str) -> None:
    if token not in HTML:
        raise AssertionError(f"missing {reason}: {token}")


def main() -> None:
    require('id="visionOverlay"', "video detection overlay")
    require('id="visionStatus"', "Chinese live vision status")
    require("case 'vision_state':", "vision_state SSE routing")
    require("handleVisionState(message)", "snapshot rendering")
    require("VISION_STATE_STALE_MS", "stale snapshot timeout")
    require("视觉检测数据暂停", "stale-state Chinese copy")
    require("drawVisionOverlay", "box drawing")
    require("object-fit: contain", "uncropped video coordinate mapping")
    require("Math.min(rect.width / message.source_width", "letterbox scale mapping")
    require("target.confirmed ? (target.kind === 'face' ? '#fbbf24' : '#22c55e') : '#9ca3af'", "semantic box colors")
    require("摄像头确认有人", "confirmed transition history")
    require("摄像头确认无人", "clear transition history")
    require("latestRadarText", "separate radar live state")
    require("雷达：门前区域", "Chinese radar distance status")
    require("行人", "pedestrian Chinese label")
    require("人脸", "face Chinese label")
    if "近距离人员" in HTML:
        raise AssertionError("ambiguous phrase must not be shown")
    print("PASS: hybrid vision browser UI contract")


if __name__ == "__main__":
    main()
