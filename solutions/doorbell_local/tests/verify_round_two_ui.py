"""Verify round-two browser and P4 signaling contracts."""

from pathlib import Path


SOLUTION = Path(__file__).resolve().parents[1]
MAIN = SOLUTION / "main"


def require_tokens(path: Path, *tokens: str) -> None:
    source = path.read_text(encoding="utf-8")
    for token in tokens:
        assert token in source, f"{path.name} is missing {token!r}"


def reject_tokens(path: Path, *tokens: str) -> None:
    source = path.read_text(encoding="utf-8")
    for token in tokens:
        assert token not in source, f"{path.name} still contains {token!r}"


def main() -> int:
    require_tokens(
        MAIN / "CMakeLists.txt",
        '"pcm_overlay.c"',
        '"doorbell_audio_render.c"',
    )
    require_tokens(
        MAIN / "media_sys.c",
        "prepare_open_door_prompt",
        "av_render_alloc_prompt_overlay",
        "play_open_door_prompt_in_call",
        "adec_decode",
        "audio_resample_write",
    )
    require_tokens(
        MAIN / "webrtc_http_server.c",
        '"text/html; charset=utf-8"',
    )
    require_tokens(
        MAIN / "webrtc.c",
        '"RING_STOPPED"',
        "ring_generation",
        '"CALL_ENDED"',
        '"CALL_END_FAILED"',
        "play_open_door_prompt_in_call",
    )
    require_tokens(
        MAIN / "doorbell_audio_render.c",
        "PROMPT_FALLBACK_SILENCE_MS",
        "prompt_fallback_worker",
        "pcm_overlay_fallback_due",
        "pcm_overlay_copy_mono",
        "Prompt queued until the 16 kHz call audio path is ready",
        "Prompt fallback output started",
        "Prompt playback completed",
    )
    require_tokens(
        MAIN / "webrtc_test.html",
        '<html lang="zh-CN">',
        "智能门铃 WebRTC 测试台",
        "门外画面",
        "本机画面",
        "接听",
        "挂断",
        "开门",
        "传感器事件",
        "暂无传感器事件",
        "记录雷达进入/离开和摄像头人员存在事件",
        "雷达进入",
        "雷达离开",
        "摄像头检测到人员",
        "横向位置",
        "纵向距离",
        "门前区域",
        "条记录",
        "RTP 数据转换器",
        "min-height: 240px",
        "max-height: 360px",
        "overflow-wrap: anywhere",
        "pedestrianEpisodeActive",
        "PEDESTRIAN_EPISODE_TIMEOUT_MS",
        "ringFallbackTimer",
        "case 'RING_STOPPED':",
        "function requestHangup()",
        "function finalizeHangup(",
        "正在结束通话…",
        "case 'CALL_ENDED':",
        "case 'CALL_END_FAILED':",
        "remoteVideo.load()",
    )
    reject_tokens(
        MAIN / "webrtc_test.html",
        " — ",
        ">Remote Video<",
        ">Local Video<",
        ">Sensor Events<",
        ">Open Door<",
        ">Hang Up<",
        "No detection events yet",
    )
    print("PASS: round-two UI and signaling contracts")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
