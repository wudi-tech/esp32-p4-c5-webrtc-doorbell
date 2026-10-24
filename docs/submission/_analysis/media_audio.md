# ESP32-P4 WebRTC 门铃 · 媒体系统与音频链路技术分析

> 分析对象：工作区根目录下的完整源码快照（`solutions/doorbell_local`、`solutions/common`、`components/*`）
> 分析方式：只读代码走查 + 仓库内既有运行日志交叉验证
> 目标读者：项目作者（AI 辅助 / vibe coding 产出），本文按“从 0 讲清楚”的粒度书写
> 所有结论都给出 `文件:行号` 证据；无法从快照确认的内容统一标注“未在代码中确认”

---

## 0. 一页速览

| 链路 | 入口函数 | 经过的组件 | 实际格式（本工程默认构建） | 关键证据 |
| --- | --- | --- | --- | --- |
| 提示音（提示铃/开门/入会） | `play_music()` | `av_render`（AAC 解码）→ prompt overlay → `i2s_render` → `esp_codec_dev` → ES8311 | ADTS-AAC 文件，44.1k/24k/16k 单声道 | `solutions/doorbell_local/main/media_sys.c:483-562` |
| 通话中“开门”语音提示 | `play_open_door_prompt_in_call()` | 启动时预解码成 16 kHz 单声道 PCM → `pcm_overlay` 混音 → `i2s_render` | 16 kHz/单声道/16 bit PCM | `media_sys.c:152-250`、`doorbell_audio_render.c:252-286` |
| 麦克风上行 | `esp_capture` sink0 | audio dev src（`get_record_handle()`）→ G711A 编码 → `esp_peer_send_audio` | PCMA/8000/单声道 | `media_sys.c:347-357`、`webrtc.c:1220-1238` |
| 扬声器下行 | `pc_on_audio_data` | `av_render_add_audio_data` → G711A 解码 → prompt overlay → `esp_codec_dev_write` | PCMA/8000/单声道 | `components/esp_webrtc/src/esp_webrtc.c:366-382` |
| 摄像头 | `create_video_source()` | `esp_video_init`(MIPI-CSI) → V4L2 src → ISP/PPA → H.264 编码 | 传感器 1280×960 → 编码输出 640×480@15 | `media_sys.c:252-340`、`webrtc.c:1231-1236` |
| 本地显示 | `av_render_alloc_lcd_render()` | LCD 渲染**未初始化**（返回 NULL） | 无显示 | `media_sys.c:374-382`、`board.c:45-58` |

一句话总结：**上行是 esp_capture + G711A，下行是 av_render + G711A，提示音是 av_render + AAC，通话中语音提示是“启动时预解码成 PCM + 在 I2S 渲染器前面插一层混音器”；本地 LCD 显示在当前工程里没有真正启用。**

---

## 1. 先建立直觉：这份媒体代码在做什么

### 1.1 名词速查（给非音频背景的读者）

| 名词 | 含义 | 在本项目中的体现 |
| --- | --- | --- |
| PCM | 未经压缩的采样数据，播放/录音的“原材料” | `int16_t` 数组，如 `open_door_prompt.data`（`media_sys.c:70`） |
| 采样率 | 每秒采样点数，决定音质与数据量 | 通话 8000 Hz；开门提示 16000 Hz（`doorbell_audio_render.c:13`） |
| 声道 | 单声道(1)/立体声(2) | 通话与提示音都是单声道 |
| 位深 | 每个采样点位数，本项目统一 16 bit | `bits_per_sample = 16`（`media_sys.c:148`） |
| 编码格式 | 压缩算法 | AAC（提示音文件）、G711A/PCMA（通话）、H.264（视频） |
| 容器/封装 | 码流的帧边界描述 | ADTS（AAC 文件头，`media_sys.c:128-150` 手工解析） |
| I2S | ESP32 与音频编解码芯片之间的数字音频总线 | `codec_init.c:132-260` 初始化 |
| Codec 芯片 | 把数字 I2S 变成模拟声音（DAC）和反过来（ADC）的芯片 | ES8311 |
| PA | 功放使能脚/增益 | `pa: 53, pa_gain: 6`（`components/codec_board/board_cfg.txt:84`） |
| esp_codec_dev | Espressif 的 codec 抽象层（open/write/read/set_vol） | `components/av_render/render_impl/i2s_render.c:84,126` |
| esp_capture | 采集系统：管住“音频源+视频源 → 编码 → sink 输出” | `media_sys.c:342-360` |
| av_render | 播放系统：管住“码流 → 解码 → 渲染（I2S/LCD）” | `components/av_render/README.md:1-10` |
| sink | esp_capture 的一路输出通道（不同格式/分辨率） | sink0 主路、sink1 检测路（`webrtc.c:1300-1328`） |

### 1.2 媒体系统的三大块

`solutions/doorbell_local/main/media_sys.c` 用三个静态结构体把整机媒体能力组织起来：

```c
typedef struct {                                   // 采集系统
    esp_capture_sink_handle_t   capture_handle;    // esp_capture 句柄
    esp_capture_video_src_if_t *vid_src;           // 摄像头源（V4L2/DVP）
    esp_capture_audio_src_if_t *aud_src;           // 麦克风源（esp_codec_dev）
} capture_system_t;                                // media_sys.c:42-46

typedef struct {                                   // 播放系统
    audio_render_handle_t audio_render;            // 音频渲染器（本项目是 prompt overlay 包装器）
    video_render_handle_t video_render;            // 视频渲染器（LCD，本项目为 NULL）
    av_render_handle_t    player;                  // av_render 总句柄
} player_system_t;                                 // media_sys.c:48-52

static doorbell_audio_prompt_handle_t prompt_render;   // 提示音混音器句柄 media_sys.c:56
static prompt_pcm_buffer_t open_door_prompt;           // 预解码 PCM 缓冲 media_sys.c:70
```

对外只暴露 5 个函数（`solutions/common/media_sys.h:27-74`）：

| 函数 | 谁调用 | 作用 |
| --- | --- | --- |
| `media_sys_buildup()` | `main.c:472` | 注册编解码器、预解码开门提示、建采集与播放系统 |
| `media_sys_get_provider()` | `webrtc.c:1266` | 把 `capture`/`player` 句柄交给 esp_webrtc |
| `play_music()` / `stop_music()` | `webrtc.c:128,251` | 播放/停止 AAC 提示音 |
| `play_open_door_prompt_in_call()` | `webrtc.c:580` | 通话中混入开门语音提示 |
| `test_capture_to_player()` | `main.c:274`（串口命令 `rec2play`） | 自测：采集直接回放 |

### 1.3 启动顺序（决定“谁能拿到谁”）

```text
app_main()                                        main.c:435
 ├─ media_lib_add_default_adapter()               main.c:438   → malloc/线程/锁 适配（实现在快照外，见 §11）
 ├─ esp_capture_set_thread_scheduler(capture_scheduler) main.c:439
 ├─ media_lib_thread_set_schedule_cb(thread_scheduler)  main.c:440
 ├─ init_board()                                   main.c:441   → I2C + I2S + ES8311（必须先于媒体系统）
 ├─ doorbell_webrtc_init()                         main.c:442   → WebRTC 侧互斥锁
 ├─ multimodal_fusion_init / door_lock_init / radar_ld2450_init  main.c:446-471
 ├─ media_sys_buildup()                            main.c:472   → 注册 codec + 预解码提示音 + 建 capture/player
 ├─ init_console()                                 main.c:473
 └─ network_init(...)                              main.c:474   → 联网成功后异步 start_webrtc()
```

顺序上最关键的一点：**`init_board()` 一定在 `media_sys_buildup()` 之前**（`main.c:441` vs `main.c:472`），
所以 `media_sys.c` 里调用 `get_playback_handle()` / `get_record_handle()` 时，ES8311 与 I2S 已经就绪。

### 1.4 端到端数据流总图

```text
                  ┌──────────────────────── 上行（门铃 → 浏览器）────────────────────────┐
 ES8311 ADC ─I2S RX→ esp_capture audio dev src ─→ G711A 编码(aenc_0) ─→ sink0
                                   │                                        │
 OV5647 ─MIPI CSI→ V4L2 src ─ISP/PPA→ H.264 编码(venc_0) ─→ sink0 ─────────┤
 (sink1: RGB565 320x180@2fps → 行人检测，不入 WebRTC)                       ▼
                                                        esp_webrtc._media_send → esp_peer_send_* → SRTP → 浏览器

                  ┌──────────────────────── 下行（浏览器 → 门铃）────────────────────────┐
 浏览器 ─SRTP→ esp_peer → pc_on_audio_data → av_render_add_audio_data
                                              │
                        Adec 线程(G711A 解码) │
                                              ▼
                            ARender 线程 → prompt overlay（混 PCM 提示音）
                                              ▼
                          i2s_render → esp_codec_dev_write → I2S TX → ES8311 DAC → PA → 喇叭

                  ┌──────────────────────── 本地提示音（不经过网络）─────────────────────┐
 ring.aac / open.aac / join.aac（flash 内嵌）→ play_music() → av_render AAC 解码
                                              → prompt overlay（透传）→ i2s_render → 喇叭
```

---

## 2. 板级初始化 `init_board` 与编解码芯片

### 2.1 `board.c` 逐行解释（全文只有 58 行）

```c
static int enable_p4_eye_camera(camera_cfg_t *cfg, bool enable)   // board.c:23-43
{
    if (cfg->pwr == -1) return 0;                                 // board.c:25-27 该板没有 pwr 脚就直接返回
    ... esp_cam_sensor_xclk_allocate/start(xclk_pin, 24 MHz)      // board.c:30-40
}

void init_board()                                                 // board.c:45-58
{
    set_codec_board_type(TEST_BOARD_NAME);                        // board.c:48
    codec_init_cfg_t cfg = {.reuse_dev = false};                  // board.c:50
    if (strcmp(TEST_BOARD_NAME, "ESP32_P4_EYE") == 0) {           // board.c:51
        cfg.in_mode = CODEC_I2S_MODE_PDM;                          // board.c:52 只有 P4_EYE 用 PDM 麦
        ... get_camera_cfg / enable_p4_eye_camera                  // board.c:53-55
    }
    init_codec(&cfg);                                              // board.c:57
}
```

- `TEST_BOARD_NAME` 在 P4 下是字符串 `"WT99P4C5_S1"`（`solutions/doorbell_local/main/settings.h:21-25`）。
- `set_codec_board_type()` 只是查表把该板卡的硬件合同挂到一个静态指针上（`components/codec_board/codec_board.c:25-31`）。
- **`reuse_dev = false` 是刻意设置**，注释写明“播放和录音同时使用时必须为 false”（`board.c:49`）：
  因为播放要 8 kHz、提示音可能是 16 kHz/44.1 kHz，共用一个 `esp_codec_dev` 句柄会导致采样率互相打架。
- `enable_p4_eye_camera()` 对本项目实际**不生效**：仅当板名是 `ESP32_P4_EYE` 才会被调用（`board.c:51`），
  而 `WT99P4C5_S1` 的 `camera: {type: mipi}` 没写 pwr 脚，解析后 `pwr = -1`（`components/codec_board/cfg_parse.c:532-534`），
  函数在 `board.c:25` 就返回了。

### 2.2 板卡硬件合同：`board_cfg.txt` 的 `WT99P4C5_S1` 段

`components/codec_board/board_cfg.txt:81-95`：

```text
Board: WT99P4C5_S1
i2c: {sda: 7, scl: 8}                                     # 控制总线（codec 寄存器）
i2s: {mclk: 13, bclk: 12, ws: 10, dout: 9, din: 11}       # 音频数据总线
in_out: {codec: ES8311, pa: 53, use_mclk: 1, pa_gain: 6}  # 一颗 ES8311 同时做 DAC + ADC
sdcard: {clk: 43, cmd: 44, d0: 39, d1: 40, d2: 41, d3: 42}
camera: {type: mipi}                                      # 只用 type，reset/pwr/xclk 都是默认 -1
lcd: { bus: mipi, ldo_chan: 3, ldo_voltage: 2500, lane_num: 2, lane_bitrate: 1000,
       dpi_clk: 80, bit_depth: 16, fb_num: 2, width: 1024, height: 600, rst: -1, ... }
```

要点：
1. **音频编解码是一颗 ES8311**（不是 ES8311+ES7210 那种“播放+阵列麦”组合），
   `in_out` 关键字让解析器同时写入 IN 和 OUT 方向（`components/codec_board/cfg_parse.c:580-582`）。
2. `use_mclk: 1` → 使用 MCLK（13 脚），ES8311 主时钟由 ESP32-P4 提供。
3. `pa: 53` → 功放使能 GPIO53，`pa_gain: 6` → 硬件增益档位。
4. **摄像头只声明了 `type: mipi`**：`reset/pwr/xclk` 全为默认 `-1`（`cfg_parse.c:529-534`），
   因此 `media_sys.c` 里“创建 24 MHz XCLK 时钟路由”的分支不会执行（`media_sys.c:272` 的 `cam_pin_cfg.xclk != -1` 为假）。
   摄像头时钟来源未在本快照中确认。

### 2.3 `init_codec()` 做了什么（`components/codec_board/codec_init.c:343-560`）

按执行顺序：

| 步骤 | 代码 | 说明 |
| --- | --- | --- |
| 取配置 | `codec_init.c:357-362` | `get_out_codec_cfg()` / `get_in_codec_cfg()` 命中同一个 ES8311 |
| 判断同 I2S | `codec_init.c:370` | `same_i2s = true`（只有一个 i2s 端口） |
| 建 I2C | `codec_init.c:373-377` | I2C0，SDA7/SCL8 |
| 建 I2S | `codec_init.c:132-260` | `i2s_new_channel` 同时建 TX/RX；初始 STD 模式 16000 Hz、32 bit slot、立体声（`codec_init.c:155-157`）——这只是**初始值**，真正采样率在 `esp_codec_dev_open()` 时被改写 |
| 建 I2S 数据接口 | `codec_init.c:401-409` | `audio_codec_new_i2s_data()` |
| 建 ES8311 | `codec_init.c:419-431` | `es8311_codec_new({codec_mode = BOTH, pa_pin=53, use_mclk=1, pa_gain=6})` |
| 建播放设备 | `codec_init.c:469-477` | `esp_codec_dev_new(dev_type = OUT)` → `codec_res.play_dev` |
| 建录音设备 | `codec_init.c:478-486` | **同一个 codec_if，但新建一个 `dev_type = IN` 的句柄**（因为 `reuse_dev=false`，注释写 “separate record from playback so that can set different fs”） |
| 默认音量/增益 | `codec_init.c:546-554` | 播放音量 60.0，录音增益 30.0 |

对外接口（`components/codec_board/include/codec_init.h:98-116`）：
`get_playback_handle()` / `get_record_handle()` 分别返回上述两个 `esp_codec_dev_handle_t`（`codec_init.c:562-580`）。

> 注意：ES7210 / ES7243 的代码路径存在于 `codec_init.c:507-527`，但**本板卡不需要**，不会执行。

### 2.4 媒体系统如何拿到这些句柄

- 播放：`i2s_render_cfg_t {.fixed_clock = true, .play_handle = get_playback_handle()}`（`media_sys.c:364-367`）
- 录音：`esp_capture_audio_dev_src_cfg_t {.record_handle = get_record_handle()}`（`media_sys.c:347-350`）
- 摄像头：`get_camera_cfg()` + `get_i2c_bus_handle(0)`（SCCB 复用 I2C0，100 kHz，`media_sys.c:255,266-267`）

### 2.5 LCD / 显示：**当前工程没有初始化**（重要结论）

- 全仓库搜索 `board_lcd_init`：`doorbell_local` 下**没有任何调用点**（对比 `solutions/whip_demo/main/board.c:27`、`janus_demo`、`kms_demo`、`videocall_demo` 都有 `board_lcd_init()`）。
- `board_get_lcd_handle()` 在面板未初始化时直接返回 `NULL`（`components/codec_board/lcd_init.c:483-489`）。
- `lcd_render_open()` 见到 `lcd_handle == NULL` 就报 “Invalid argument to open” 并返回 NULL（`components/av_render/render_impl/lcd_render.c:64-70`）。
- `media_sys.c:379-382` 正好对 NULL 做了兜底：“Fail to create video render / Allow not display”。
- 运行时日志实锤：`E (254110) AV_RENDER: Video render not set, stream is skipped`
  （`docs/bringup/logs/webrtc-aac-assets-runtime-20260902.log:257`）。

**结论：`doorbell_local` 是一台“只上行视频”的门铃，本机 LCD 屏不作视频显示。**
（提示音、通话语音都走 I2S/喇叭，与显示无关。）

---

## 3. 提示音播放链路（AAC 文件）

### 3.1 三个 AAC 文件如何进入固件

`solutions/doorbell_local/main/CMakeLists.txt:13-15`：

```cmake
idf_component_register(SRCS ${srcs}
                       EMBED_TXTFILES "ring.aac" "open.aac" "join.aac" "webrtc_test.html"
                       "certs/servercert.pem" "certs/prvtkey.pem"
                       INCLUDE_DIRS ".")
```

`EMBED_TXTFILES` 让构建系统把文件变成符号 `_binary_<name>_start/_end`，代码用 `asm()` 别名取地址：

```c
extern const uint8_t ring_music_start[] asm("_binary_ring_aac_start");   // webrtc.c:101
extern const uint8_t ring_music_end[]   asm("_binary_ring_aac_end");     // webrtc.c:102
extern const uint8_t open_music_start[] asm("_binary_open_aac_start");   // webrtc.c:103 / media_sys.c:72
extern const uint8_t join_music_start[] asm("_binary_join_aac_start");   // webrtc.c:105
```

所以“播放提示音”= 把 flash 里的字节数组推给解码器，**不涉及文件系统**。

### 3.2 三个文件的实际参数（本次分析实测解析 ADTS 头）

| 文件 | 大小 | 采样率 | 声道 | 帧数 | 时长 | 播放时的 `duration` 参数 |
| --- | --- | --- | --- | --- | --- | --- |
| `ring.aac` | 10 835 B | 44 100 Hz | 1 | 53 | ≈1.23 s | `DOOR_BELL_RING_DURATION_MS = 4000`（`webrtc.c:46,121`） |
| `open.aac` | 22 422 B | 24 000 Hz | 1 | 70 | ≈2.99 s | `0`（`webrtc.c:122`） |
| `join.aac` | 15 502 B | 16 000 Hz | 1 | 32 | ≈2.05 s | `0`（`webrtc.c:123`） |

> 交叉验证：`open.aac` 预解码成 16 kHz 单声道 PCM 的理论字节数 ≈ 2.99 s × 16000 × 2 B ≈ 95.7 KB，
> 与既有日志记录的 “Prompt asset decoded at startup to 95,574 PCM bytes at 16 kHz mono” 完全吻合
> （`docs/bringup/logs/2026-09-08-chinese-ui-prompt-fallback.md:40`）。
> 另可对照运行日志里的解码采样率：`I2S_RENDER: open channel:1 sample_rate:44100`（同日志文件 `:231`）、
> `sample_rate:16000`（`:214`）。

### 3.3 `play_music()` / `music_play_thread()`（`media_sys.c:483-573`）

```text
play_music(data,size,duration)                     media_sys.c:544
 ├─ 若正在播放 → stop_music()（同步等旧线程退出）   media_sys.c:546-549
 ├─ 记录全局 music_to_play/music_size/music_duration media_sys.c:550-553
 └─ media_lib_thread_create_from_scheduler("music_player", music_play_thread)
                                                    media_sys.c:554-560

music_play_thread:                                 media_sys.c:483-542
 ├─ av_render_add_audio_stream(player,{codec=AAC}) media_sys.c:486-489
 ├─ 循环：
 │   ├─ 读 ADTS 头算出这一帧的 frame_size          media_sys.c:494-502
 │   ├─ av_render_add_audio_data(非阻塞推流)        media_sys.c:503-513
 │   ├─ 到尾部：music_pos=0；
 │   │   duration==0 → 等 FIFO 播完再退出（单次播放）media_sys.c:517-527
 │   └─ duration 递减；media_lib_thread_sleep(1)   media_sys.c:530-536
 └─ 收尾：av_render_reset(player) + 清标志 + 自杀线程 media_sys.c:538-541
```

细节说明（作者后续最容易踩坑的地方）：

1. **推流是按 ADTS 帧切的**：代码从字节流里读 `frame_length = ((b3&3)<<11)|(b4<<3)|(b5>>5)`，
   只把完整的一帧交给解码器（`media_sys.c:498-508`），并且每帧之间 `sleep(1)`，
   注释明确写了原因：**“单核 music 任务不能饿死 P4 的 idle 任务/看门狗”**（`media_sys.c:534-536`）。
2. **`duration` 的语义**：`duration > 0` 时倒计时到 0 前会不断从头循环（铃声就是靠这个补齐 4 秒）；
   `duration == 0` 表示“只播一遍”。判定写在 `media_sys.c:514-528`。
3. **收尾调用 `av_render_reset()`（`media_sys.c:538`）会关闭音频渲染、销毁 Adec/ARender 线程**
   （`components/av_render/src/av_render.c:2068-2168`，其中 `:2161` 关闭音频渲染器）。
   这是运行日志里“每次提示音播完都看到 `Adec got msg:Close` / `Thread Adec exited`”的原因
   （`docs/bringup/logs/webrtc-aac-assets-runtime-20260902.log:221-225,239-243`）。
   副作用见 §9。
4. `music_playing` / `music_stopping` 是一对无锁 `bool` 标志（`media_sys.c:75-76`），
   靠“写完标志再建/等线程”的时序来保证不并发播放；`stop_music()` 是**忙等**到线程自己清标志（`media_sys.c:564-573`）。

### 3.4 `av_render` 的 AAC 解码流水线（提示音为什么能被解码）

`av_render` 是“推流式播放器”（`components/av_render/README.md:1-10`）。本项目建立时给出：

```c
av_render_cfg_t render_cfg = {
    .audio_render = player_sys.audio_render,     // ← prompt overlay 包装器
    .video_render = player_sys.video_render,     // ← NULL（无 LCD）
    .audio_raw_fifo_size    = 4096,              // 非 0 → 创建 Adec 解码线程
    .audio_render_fifo_size = 6 * 1024,          // 非 0 → 创建 ARender 渲染线程
    .video_raw_fifo_size    = 500 * 1024,        // 非 0 → 创建 Vdec 解码线程
    .allow_drop_data        = false,             // 不丢数据（音频优先保真）
};                                               // media_sys.c:383-391
```

线程创建条件来自 `audio_need_decode_in_sync()` / `audio_need_render_in_sync()`
（`components/av_render/src/av_render.c:886-914`：FIFO 尺寸为 0 才不建线程）。

**AAC 一次播放的调用链：**

```text
play_music → av_render_add_audio_data()                        av_render.c:1546-1617（入 adec data_q）
   → [Adec 线程] decode_audio() → adec_decode()                av_render.c:388-411
   → adec frame_cb = av_render_audio_frame_reached()           av_render.c:1313, 948-1018
       ├─ 首次到达：从解码器取真实帧信息 adec_get_frame_info()  av_render.c:958-969
       ├─ audio_render_close() 再 audio_render_open(新格式)    av_render.c:971-992
       │     → 这里就调到 prompt_render_open()，把 16000/声道/位深记下来
       └─ 创建 "ARender" 线程                                   av_render.c:1001-1009
   → [ARender 线程] a_render_body → _render_write_audio         av_render.c:683-721, 597-628
       → audio_render_write(cfg.audio_render, frame)            av_render.c:616
           = prompt_render_write() → audio_render_write(下游 i2s_render)
   → i2s_render_write → esp_codec_dev_write()                   i2s_render.c:91-132（:126）
```

由此可解释日志 `Get samplerate 44100 chanel 1`（`webrtc-aac-assets-runtime-20260902.log:229`）：
AAC 解码器自己解出 44.1 kHz 单声道，`av_render` 用它去 `esp_codec_dev_open()` 重开 I2S。

### 3.5 哪些事件触发提示音（调用点全表）

| 事件 | 代码位置 | 音源 | duration |
| --- | --- | --- | --- |
| 门铃按键/雷达触发“呼叫” | `webrtc.c:387`（`doorbell_request_ring()` 内） | `ring.aac` | 4000 ms 循环 |
| 状态回滚到 RINGING 时补响铃 | `webrtc.c:283`（`restore_peer_change_state_locked`） | `ring.aac` | 4000 ms |
| 未接通时按“开门” | `webrtc.c:584` | `open.aac` | 单次 |
| 已接通时按“开门” | `webrtc.c:580` → `play_open_door_prompt_in_call()` | 预解码 PCM | 不适用（见 §4） |
| WebRTC 启动成功 | `webrtc.c:1384` | `join.aac` | 单次 |
| 状态切到 CONNECTING/DISCONNECTING/NONE | `webrtc.c:248-252` → `stop_music()` | 停止当前音 | — |

接通过程中的时序保护：`doorbell_request_ring()` 接受后启动 `ring_stop_worker`，
4 秒后向浏览器发 `RING_STOPPED`（`webrtc.c:290-331,393`）。

### 3.6 浏览器侧也有一个提示音（同一份 `ring.aac`）

- HTTPS 服务器把 `ring.aac` 以 `audio/aac` 提供给浏览器（`webrtc_http_server.c:362-373`，路由注册在 `:435`）。
- 页面里隐藏的 `<audio id="ringSound" loop>` 引用了 `/webrtc/ring.aac`（`webrtc_test.html:484-487`）。
  → 门铃按响时，**设备喇叭和浏览器两边同时响铃**。

### 3.7 时序图 A：AAC 提示音播放（以“按门铃”为例）

```text
雷达/按键          doorbell_request_ring()      music_player 线程         av_render 内部            ES8311/喇叭
   │                      │                          │                      │                        │
   │──触发───────────────►│ 策略评估(webrtc就绪+空闲) │                      │                        │
   │                      │ 状态=RINGING             │                      │                        │
   │                      │ 发送 "RING" 给浏览器 ────┼───► 浏览器播放 ring.aac（页面 <audio>）        │
   │                      │ play_tone(RING) ────────►│                      │                        │
   │                      │                          │ add_audio_stream(AAC)│                        │
   │                      │                          │ add_audio_data(帧1..N)│                       │
   │                      │                          │        (sleep 1ms/帧) │                        │
   │                      │                          │                      │ adec_decode(AAC)       │
   │                      │                          │                      │ audio_render_close     │
   │                      │                          │                      │ audio_render_open(44100/1/16)
   │                      │                          │                      │ esp_codec_dev_open ───►│
   │                      │                          │                      │ ARender 线程 write ───►│ 出声
   │                      │  schedule_ring_stop ─────►│ (4s 后)              │                        │
   │                      │  ◄── RING_STOPPED ───────┘                      │                        │
   │                      │                          │ 播完 → av_render_reset │ ── 关闭渲染/线程 ─────►│
```

> 日志对照：`RING` 命令 → `Get samplerate 44100 chanel 1` → `I2S_RENDER: open channel:1 sample_rate:44100`
> → 约 4.16 s 后 `ARender/Adec got msg:Close`
> （`docs/bringup/logs/webrtc-aac-assets-runtime-20260902.log:227-243`）。

---

## 4. 通话中“开门”语音提示：预解码 PCM + 叠加混音

这是本项目**最独特、也最容易被误解**的一段代码（`media_sys.c:58-250` + `doorbell_audio_render.c` + `pcm_overlay.c`）。

### 4.1 为什么不能沿用“AAC 播放”

通话中再调 `play_music()` 会有两个致命问题：

1. `av_render_add_audio_stream()` 会**关闭旧解码器、重建音频渲染器**（`av_render.c:1281-1292, 971-992`），
   等于把正在通话的远端音频通路打断；
2. `play_music()` 收尾的 `av_render_reset()`（`media_sys.c:538`）会清空播放管线。

因此作者改成：**启动时把 `open.aac` 解成 PCM 常驻内存；通话时把这段 PCM 叠加到远端音频帧上。**

### 4.2 `prepare_open_door_prompt()` 逐步（`media_sys.c:152-250`）

```text
1) 读 ADTS 头，得到源格式（24 kHz / 1ch / 16bit）         media_sys.c:154-159 → parse_adts_format()
   parse_adts_format() 手写解析：0xFFF 同步字 + 采样率索引表 + 声道     media_sys.c:128-150
2) 配置重采样器：输入=源格式，输出固定 16 kHz/1ch/16bit    media_sys.c:161-170
   resample_cb = prompt_resampled_frame → append_prompt_pcm()          media_sys.c:106-113, 81-104
3) 打开 AAC 解码器（adec_open, codec=AV_RENDER_AUDIO_CODEC_AAC）
   frame_cb = prompt_decoded_frame → audio_resample_write()            media_sys.c:177-196, 115-126
4) 逐帧喂 ADTS：校验同步字、算 frame_size、越界检查、adec_decode()      media_sys.c:197-227
5) 允许尾部 1 个 0 字节，其它残留即报错，然后关闭解码器/重采样器        media_sys.c:228-240
6) 校验结果：缓冲非空且至少 1 个 int16                                 media_sys.c:241-249
   → 日志 "Prepared open-door prompt: %d PCM bytes at 16 kHz mono"    media_sys.c:247
```

**PCM 缓冲的增长策略**（`append_prompt_pcm`，`media_sys.c:81-104`）：初始 64 KB，不够就**翻倍**，用 `media_lib_realloc`；
失败时置 `buffer->error` 让整条链路快速失败。最终约 96 KB（=95,574 B，见 §3.2 交叉验证）。

**失败不影响开机**：`media_sys_buildup()` 对失败只打一条警告 “In-call open-door prompt will be unavailable”（`media_sys.c:407-409`）。

### 4.3 包装层 `doorbell_audio_render.c`：把 I2S 渲染器“包一层”

核心设计：**实现一套 `audio_render_ops_t`，把真正的 I2S 渲染器包在中间，在 `write` 里做混音。**

```c
typedef struct {
    audio_render_handle_t downstream;      // 真正的 i2s_render
    media_lib_mutex_handle_t mutex;        // 串行化“远端写”和“回退写”
    av_render_audio_frame_info_t frame_info;  // 当前通话音频格式
    pcm_overlay_t overlay;                 // 提示音游标 + 数据指针
    bool compatible;                       // 当前格式能否混音
    bool prompt_worker_running / prompt_worker_stop / prompt_progress_logged / fallback_logged;
    int64_t last_remote_write_us;          // 最近一次远端音频写入时刻
} prompt_render_t;                          // doorbell_audio_render.c:25-36
```

- 创建：`av_render_alloc_prompt_overlay()` 先建下游 i2s 渲染器，再注册 ops（`doorbell_audio_render.c:99-121, 223-250`）。
  `audio_render_alloc_handle()` 会立刻调用 `init`（`components/av_render/src/audio_render.c:50-67`），
  所以 `prompt_render_init()` 里的 `av_render_alloc_i2s_render()` 在建播放系统时就完成了。
- 转发型 ops：`get_latency` / `get_frame_info` / `set_speed` 直接透传（`doorbell_audio_render.c:177-195`）。
- `open`：先开下游，再记录格式并判定 `compatible`（`doorbell_audio_render.c:123-145`）：
  ```c
  render->compatible = info->sample_rate == PROMPT_SAMPLE_RATE_HZ   // 16000
                       && (info->channel == 1 || info->channel == 2)
                       && info->bits_per_sample == 16;              // doorbell_audio_render.c:134-136
  ```
  不满足时打警告 “Prompt waiting: current audio is X Hz …”（`:138-141`）。
- `write`（正常混音路径，`:147-175`）：
  ```c
  render->last_remote_write_us = now;                       // 记录“远端在流动”
  pcm_overlay_mix_mono(&overlay, data, samples, channels);  // 就地叠加
  audio_render_write(render->downstream, data);             // 写 I2S
  ```
- `close` / `deinit`：都会先 `doorbell_audio_prompt_stop()` 再关下游（`:197-221`）。

### 4.4 `pcm_overlay` 的四个函数语义（`pcm_overlay.c`）

| 函数 | 作用 | 关键实现 |
| --- | --- | --- |
| `pcm_overlay_start/stop/active` | 游标式播放状态管理（不拷贝数据，只持有 `const int16_t*`） | `pcm_overlay.c:5-31` |
| `pcm_overlay_mix` | 单声道目标：`dst[i] += prompt[i]`，带 `INT16` 饱和截断 | `pcm_overlay.c:33-59`（饱和在 `:47-51`） |
| `pcm_overlay_mix_mono` | 立体声目标：**同一个提示采样同时叠加到左右两个声道** | `pcm_overlay.c:61-96`（`:78-90`） |
| `pcm_overlay_copy_mono` | 只拷贝提示音（不叠加），左右都填同一个值 —— 回退路径用 | `pcm_overlay.c:98-123` |
| `pcm_overlay_fallback_due` | 判断“距上次远端写入已静音 ≥ 阈值” | `pcm_overlay.c:125-132` |

混音是**就地相加 + 饱和**，没有做任何自动增益/避让（没有 ducking）。这点在汇报时可以明确说：
“提示音与通话语音是**等权相加**，理论上存在削顶风险，但因为有 INT16 饱和保护不会爆音失真溢出。”

### 4.5 两条播放路径：混音路径 vs 回退路径

**(a) 混音路径（远端音频在流动时）**：ARender 线程 → `prompt_render_write()` → 就地叠加 → 写 I2S。
日志标记 `Prompt mixed with WebRTC remote audio`（`doorbell_audio_render.c:162-165`）。

**(b) 回退路径（远端静音、没有帧可叠加时）**：独立的 `prompt_fallback` 线程每 5 ms 轮询，
若“距上次远端写入 ≥ 40 ms”就用 `pcm_overlay_copy_mono()` 生成 20 ms 的纯提示音帧直接写 I2S。
常量在 `doorbell_audio_render.c:13-18`：

```c
#define PROMPT_SAMPLE_RATE_HZ        16000
#define PROMPT_FALLBACK_SILENCE_MS   40    // 判定为“远端静音”的阈值
#define PROMPT_FALLBACK_FRAME_MS     20    // 每次产出的帧长（16000*20/1000 = 320 采样/声道）
#define PROMPT_FALLBACK_POLL_MS      5     // 轮询周期
```

线程主体 `prompt_fallback_worker()`（`doorbell_audio_render.c:38-97`）逻辑：

```text
loop:
  lock(mutex)
  if (stop || !overlay.active)      → 退出线程，prompt_worker_running=false
  if (!compatible)                 → 解锁 sleep(5) continue      ← 关键分支，见 §4.6
  if (fallback_due(now, last_remote_write_us, 40ms)):
        copy_mono(overlay → 局部数组) → 日志 + audio_render_write(下游)
        if 写失败 → pcm_overlay_stop()
        if (!overlay.active) → 日志 "Prompt playback completed"，退出
  unlock; sleep(5)
```

**线程与锁的共享**：混音路径和回退路径都要写同一个 I2S 渲染器，所以统一用 `render->mutex` 串行化
（`doorbell_audio_render.c:154` 与 `:44`）。这解决了日志文档里提到的“序列化回退与正常渲染写入”
（`docs/bringup/logs/2026-09-08-chinese-ui-prompt-fallback.md:15-16`）。

### 4.6 兼容性门槛：**这就是本方案的核心限制**

`compatible` 只有在“当前通话音频 = 16 kHz 且 1/2 声道且 16 bit”时为真。
也就是说：

- 若通话走 **Opus/16000**（`webrtc.c:1221-1224`，需要 `WEBRTC_SUPPORT_OPUS` 宏）→ 可混音；
- 若通话走 **G711A/8000**（`webrtc.c:1225-1229`）→ `compatible = false`：
  - 混音路径不生效（`doorbell_audio_render.c:156` 的 `if (render->compatible && ...)` 为假）；
  - 回退路径也**不会**播放（`doorbell_audio_render.c:56-60` 直接 continue）；
  - `prompt_fallback` 线程会以 5 ms 周期空转到通话结束。

### 4.7 关键结论：**当前默认构建下，通话中开门提示音不会响**

证据链（全部来自本快照）：

1. `solutions/doorbell_local/main/settings.h` **没有** `#define WEBRTC_SUPPORT_OPUS`
   （全仓库 `WEBRTC_SUPPORT_OPUS` 只出现在 `whip_demo`、`janus_demo`、`kvs_master`、`openai_demo` 的 `settings.h`，
   以及各方案中受 `#ifdef` 保护的代码里；`doorbell_local/main/settings.h` 全文 88 行内没有该宏）。
2. 因此 `webrtc.c:1225-1229` 走 `#else` 分支：`ESP_PEER_AUDIO_CODEC_G711A / 8000 / 1ch`。
3. 运行时日志实锤：SDP 里是 `a=rtpmap:8 PCMA/8000`，且 `I2S_RENDER: open channel:1 sample_rate:8000 bits:16`
   （`docs/bringup/logs/webrtc-aac-assets-runtime-20260902.log:270,463`），
   以及 `Add audio codec 5 sample_rate 8000`（`:256`；`AV_RENDER_AUDIO_CODEC_G711A = 5`）。
4. 8000 ≠ `PROMPT_SAMPLE_RATE_HZ`(16000) → `compatible = false`（`doorbell_audio_render.c:134-136`）。
5. `play_open_door_prompt_in_call()` 仍然返回 0（“已装载”），所以 `webrtc.c:581` 的警告**不会**打印，
   现象是“按了开门、门锁动作正常、但喇叭不说话”（`media_sys.c:575-589`）。
6. 既有验证文档也把这项列为“尚待人工确认”，并且没有任何运行日志出现过 `Prompt playback armed` /
   `Prompt mixed with WebRTC remote audio` / `Prompt fallback output started` 三条关键日志
   （`docs/bringup/logs/2026-09-08-chinese-ui-prompt-fallback.md:44-53`；全仓库日志检索无这三条）。

> 汇报建议：把这条作为**已知设计约束**主动说明，而不是等评委发现。
> 修法有三种（任选）：① 给 `doorbell_local/main/settings.h` 打开 `WEBRTC_SUPPORT_OPUS`（走 16 kHz 分支，代价是栈要 40 KB，`main.c:371-376` 已备好）；
> ② 把 `open.aac` 预解码目标改成 8 kHz 并把 `compatible` 判定改成“跟随当前通话采样率”；
> ③ 回退路径去掉 `compatible` 限制、改为“采样率不匹配时上采样/下采样后播放”。

### 4.8 时序图 B / C

**B：16 kHz 通话（Opus）下的设计预期路径**

```text
浏览器/信令        door_bell_on_cmd        prompt_fallback 线程   ARender 线程      i2s_render→ES8311
    │  "OPEN_DOOR"      │                        │                   │                  │
    │──────────────────►│ door_lock_request_open()│                   │                  │
    │                   │ 状态==CONNECTED?        │                   │                  │
    │                   │ play_open_door_prompt_in_call()              │                  │
    │                   │  └─ doorbell_audio_prompt_start()            │                  │
    │                   │       pcm_overlay_start(96KB PCM)             │                  │
    │                   │       创建 prompt_fallback 线程 ──►│          │                  │
    │                   │ 日志 "Prompt playback armed"        │          │                  │
    │◄── 远端音频帧 ─────┼────────────────────────────────────────────►│ write(frame)      │
    │                   │                        │        mix_mono(叠加)│─────►│ 语音+提示音 │
    │                   │                        │        (第1次) 日志 "Prompt mixed ..."   │
    │   (远端静音 40ms)  │                        │ fallback_due?是     │                  │
    │                   │                        │ copy_mono 320 采样 ─┼─────►│ 只播提示音  │
    │                   │                        │ 播完 → "Prompt playback completed"       │
```

**C：当前 G711A/8 kHz 构建下的实际行为**

```text
play_open_door_prompt_in_call() → doorbell_audio_prompt_start() → ret = 0（无报错）
        │
        ├─ overlay.active = true
        ├─ compatible = false（8000 ≠ 16000）
        ▼
prompt_fallback 线程：每 5 ms 唤醒一次 → if (!compatible) continue  → 什么都不写
prompt_render_write()：if (compatible && ...) 为假 → 原样透传远端音频
        ▼
结果：门锁开、状态机正常、日志无任何 Prompt 字样、喇叭没有提示语；线程空转直到通话结束调用
      prompt_render_close() → doorbell_audio_prompt_stop() 才退出（doorbell_audio_render.c:197-209）
```

### 4.9 竞态与死锁自查（供作者回答提问）

| 关注点 | 现状 | 评价 |
| --- | --- | --- |
| 远端写入 vs 回退写入 | 共用 `render->mutex`（`doorbell_audio_render.c:44,154`） | 安全；但**持锁调用 `audio_render_write()`（阻塞式 I2S 写）**，会在极端情况下拉长对方等待 |
| `compatible` 读写 | 写：`open`/`close` 持锁；读：`write`/`fallback` 持锁（`:132,154,204,260`） | 一致 |
| `prompt_worker_running` | 只在持锁下读写（`:51,87,270-276,300`） | 一致；`doorbell_audio_prompt_stop()` 用轮询等待退出（`:294-306`） |
| 提示音缓冲生命周期 | `open_door_prompt.data` 常驻不释放，overlay 只持指针（`pcm_overlay.c:11`） | 安全（无需拷贝） |
| `play_music` 的 `music_playing` | 无锁 `bool`（`media_sys.c:75`） | 依赖“同一时刻只有 UI/雷达/LD2450 单一路径触发”，**未在代码中确认**调用方是否已串行化 |

---

## 5. 双向音频：麦克风采集与扬声器回放

### 5.1 上行：门铃 → 浏览器（麦克风）

```text
ES8311 ADC → I2S RX → esp_capture audio dev src            media_sys.c:347-350
   → [esp_capture 内部] AUD_SRC 取数 → G711A 编码器(aenc_0)  
   → sink0（format=ESP_CAPTURE_FMT_ID_G711A, 8000, 1ch）     webrtc.c:1276-1301
   → [pc_send 线程] esp_capture_sink_acquire_frame()         esp_webrtc.c:119-142
   → esp_peer_send_audio() → SRTP → 浏览器
```

- 音频源设备：`esp_capture_new_audio_dev_src({.record_handle = get_record_handle()})`（`media_sys.c:347-350`）。
- 同步模式：`ESP_CAPTURE_SYNC_MODE_AUDIO`（`media_sys.c:354`）——以音频为时钟基准。
- sink0 的音频格式由 peer 配置决定（`webrtc.c:1283-1285`）。
- 运行日志：`ESP_GMF_AENC: Open, type:G711-Alaw, acquire in frame: 320, out frame: 160`
  （`docs/bringup/logs/webrtc-aac-assets-runtime-20260902.log:479`）
  —— 8 kHz 下 20 ms = 160 采样；输入 320 字节（16 bit 单声道）= 160 采样，一致。

### 5.2 下行：浏览器 → 门铃（扬声器）

```text
浏览器 → SRTP → esp_peer → pc_on_audio_data()                esp_webrtc.c:366-382
   → av_render_add_audio_data()                              esp_webrtc.c:380
   → [Adec 线程] G711A 解码 → 8000/1ch/16bit PCM
   → [ARender 线程] audio_render_write(cfg.audio_render)      av_render.c:616
        = prompt_render_write() ──（compatible 时叠加提示音）
   → i2s_render_write → esp_codec_dev_write()                i2s_render.c:126
   → I2S TX → ES8311 DAC → PA(GPIO53) → 喇叭
```

- 播放句柄来自 `esp_webrtc_set_media_provider()`（`webrtc.c:1265-1267`），
  esp_webrtc 内部把它当作 `play_handle`（`esp_webrtc.c:625`）。
- 音频流信息由**对端 SDP**决定：`pc_on_audio_info()` 把接收格式转成 `av_render` 格式并 `av_render_add_audio_stream()`
  （`esp_webrtc.c:328-364`），G711A 被强制为 8000/1ch（`esp_webrtc.c:344-347`）。
- 通话结束时 `stop_stream()` 会 `av_render_reset()`（`esp_webrtc.c:226`），
  这一步同样会触发 `prompt_render_close()` → 停掉可能还挂着的提示音。

### 5.3 为什么一定是 8000 Hz 单声道

`webrtc.c:1218-1238` 是**唯一的音频参数来源**：

```c
.audio_info = {
#ifdef WEBRTC_SUPPORT_OPUS
    .codec = ESP_PEER_AUDIO_CODEC_OPUS, .sample_rate = 16000, .channel = 2,   // webrtc.c:1222-1224
#else
    .codec = ESP_PEER_AUDIO_CODEC_G711A, .sample_rate = 8000, .channel = 1,   // webrtc.c:1226-1228
#endif
},
.audio_dir = ESP_PEER_MEDIA_DIR_SEND_RECV,     // 双向 webrtc.c:1237
.video_dir = ESP_PEER_MEDIA_DIR_SEND_ONLY,     // 只发视频 webrtc.c:1238
```

结合 §4.7 的证据：**本工程最终协商的是 PCMA/8000/单声道**（SDP 与日志双向证实）。
浏览器端 `webrtc_test.html` 的 `getUserMedia` 采集也会被 SDP 约束到同一编解码。

### 5.4 采集与播放同时存在时如何不打架

- `reuse_dev = false`（`board.c:50`）让 ES8311 出现**两个 `esp_codec_dev` 句柄**，
  这样播放可以是 44.1 kHz（铃声）而录音保持 8 kHz（通话）互不干扰；
  代价是共享同一个 codec 芯片与同一对 I2S TX/RX。
- 日志中的直接证据：`I2S_IF: Current mode(playback) and peer mode have same sample_rate 8000, channel 2, bits_per_sample 16`
  （`docs/bringup/logs/webrtc-aac-assets-runtime-20260902.log:474`）——底层 I2S 接口在两种模式间做参数对齐。

### 5.5 时序图 D：一次完整双向音频

```text
浏览器                esp_peer/esp_webrtc        esp_capture(sink0)      av_render            ES8311
  │  ① ACCEPT_CALL ───────►│                        │                       │                   │
  │                         │ DTLS/SRTP + SDP        │                       │                   │
  │                         │ sink0 setup(G711A/H264)│                       │                   │
  │                         │ esp_capture_start() ──►│ AUD_SRC/V4L2 起流     │                   │
  │                         │                        │ aenc_0/venc_0         │                   │
  │  ② 麦克风帧 ◄───────────┼── pc_send 线程取 sink0 帧 ──────────────────────┤                   │
  │     (PCMA/8000)         │                        │                       │                   │
  │  ③ 远端音频帧 ─────────►│ pc_on_audio_data ──────┼──────────────────────►│ Adec→ARender      │
  │                         │                        │                       │ esp_codec_dev_write►│ 出声
  │  ④ 通话中 "OPEN_DOOR" ─►│ play_open_door_prompt_in_call()                │                   │
  │                         │   （见 §4.8 图 B/C：16k 混音 / 8k 不响）        │                   │
  │  ⑤ CALL_ENDED ────────►│ stop_stream → av_render_reset ─────────────────►│ 关渲染/线程       │
```

### 5.6 自测回路 `rec2play`（作者调试用）

`test_capture_to_player()`（`media_sys.c:424-481`）：把采集到的 G711A/8 kHz 音频与 MJPEG/640×480@20fps 视频
直接回灌到播放器，播 2 秒。它**不是**默认运行路径，只在串口命令 `rec2play` 时触发（`main.c:272-275, 338-342`）。
注意它的视频格式是 MJPEG（`media_sys.c:433`），与通话用的 H.264 不同。

---

## 6. 摄像头采集、H.264 编码与显示/渲染

### 6.1 `create_video_source()`（`media_sys.c:252-340`）

```text
get_camera_cfg(&cam_pin_cfg)                                media_sys.c:254-258
 ESP32-P4 分支（编译期 CONFIG_IDF_TARGET_ESP32P4）：
   若 type == MIPI:
      csi_config.sccb_config.i2c_handle = get_i2c_bus_handle(0)  media_sys.c:266
      csi_config.sccb_config.freq       = 100000                media_sys.c:267
      csi_config.reset_pin / pwdn_pin   = -1（本板未配置）       media_sys.c:268-269
      若 xclk != -1 → 申请 ESP clock router 输出 24 MHz         media_sys.c:272-282（本板不执行）
   若 type == DVP（仅 CONFIG_ESP_VIDEO_ENABLE_DVP_VIDEO_DEVICE 时编译）：
      填 8 位并口 + vsync/pclk/de/ xclk 20 MHz                   media_sys.c:284-304
   esp_video_init(&cam_config)                                  media_sys.c:305-309
   esp_capture_new_video_v4l2_src({"/dev/video0", buf_count=2}) media_sys.c:310-314
 ESP32-S3 分支：create_video_source 用 DVP 源                    media_sys.c:317-338
```

要点：P4 走的是 **V4L2 抽象**（`/dev/video0`，2 个缓冲），S3 走 DVP 直连；
传感器类型/引脚全部来自 `codec_board` 的配置表，代码里没有硬编码 GPIO。

**ISP/PPA**：`sdkconfig.defaults.esp32p4:23` 打开 `CONFIG_ESP_VIDEO_ENABLE_ISP_PIPELINE_CONTROLLER=y`，
使能 ISP 管线；缩放由 PPA 硬件完成（日志证据见 §6.4）。

### 6.2 采集系统组装（`media_sys.c:342-360`）

```c
esp_capture_cfg_t cfg = {
    .sync_mode = ESP_CAPTURE_SYNC_MODE_AUDIO,   // 音视频同步基准=音频 media_sys.c:354
    .audio_src = capture_sys.aud_src,           // ES8311 麦克风
    .video_src = capture_sys.vid_src,           // /dev/video0
};
esp_capture_open(&cfg, &capture_sys.capture_handle);          // media_sys.c:358
```

注意：这里**只 open，不 setup sink、不 start**。sink 与启动由 WebRTC 层控制（见 §6.3/§6.6）。
若 `build_capture_system()` 失败，函数只是继续（返回值未检查，`media_sys.c:411`）——属于可改进点。

### 6.3 sink0：主路 H.264（`webrtc.c:1276-1301`，受 `CONFIG_DOORBELL_SUPPORT_PEDESTRIAN_DETECT` 保护）

```c
esp_capture_sink_cfg_t sink_cfg = {
    .audio_info = { .format_id = (OPUS? ESP_CAPTURE_FMT_ID_OPUS : ESP_CAPTURE_FMT_ID_G711A),
                    .sample_rate = cfg.peer_cfg.audio_info.sample_rate,
                    .channel     = cfg.peer_cfg.audio_info.channel,
                    .bits_per_sample = 16 },                       // webrtc.c:1277-1286
    .video_info = { .format_id = ESP_CAPTURE_FMT_ID_H264,
                    .width = VIDEO_WIDTH, .height = VIDEO_HEIGHT, .fps = VIDEO_FPS }, // webrtc.c:1287-1292
};
esp_capture_sink_setup(media_provider.capture, 0, &sink_cfg, &main_sink);             // webrtc.c:1301
```

同时 `esp_webrtc` 内部在 `pc_start()` 也会对 **同一个 sink0** 做一次 setup
（`components/esp_webrtc/src/esp_webrtc.c:611-634`），参数由 peer 配置推导，与上面一致；
`esp_webrtc_set_no_auto_capture(opened_webrtc, true)`（`webrtc.c:1271`）把“启动/停止采集”的责任交回应用层
（`esp_webrtc.c:196-228`）。**同一 sink 被设置两次**属于可清理的重复代码（结论：未观察到功能异常）。

`VIDEO_WIDTH/HEIGHT/FPS` 定义在 `solutions/doorbell_local/main/settings.h:30-38`：
P4 → **640×480@15**；S3 → 320×240@10。

### 6.4 实际视频管线（运行日志实证）

`docs/bringup/logs/webrtc-aac-assets-runtime-20260902.log`：

```text
:420  GMF_VID_PIPE: Build pipe nego for format h264 640x480 15 fps
:422  V4L2_SRC: Best match 1280x960                  ← 传感器按 Kconfig 输出 1280×960
:452  VIDEO_COMM: Video info for vid_overlay ... 1280x960 15fps
:470  PPA_EL: Convert in o_uyy_e_vyy 1280x960 to o_uyy_e_vyy 640x480 ppa:1 hw_native:1
:471  VIDEO_COMM: Video info for vid_enc ... 640x480 15fps
:435  ESP_GMF_TASK: Waiting to run [tsk:venc_0-...]  ← 视频编码线程
```

即：**OV5647 RAW10 1280×960（Kconfig：`sdkconfig.defaults.esp32p4:19-20`）→ ISP（YUV422）→ PPA 硬件缩放到 640×480 → H.264 编码**。
H.264 编码器为软编还是硬编：由 `esp_video_enc_register_default()`（`media_sys.c:403`）按
`espressif/esp_h264 ~1.3`（`main/idf_component.yml:7-10`）的 Kconfig 决定，**未在代码中确认具体选择**。

### 6.5 显示/渲染链路：注册了但不用

- `build_player_system()` 里确实创建了 LCD 渲染器并把句柄交给 `av_render_cfg.video_render`（`media_sys.c:374-386`）；
- 但因为没有调用 `board_lcd_init()`，句柄为 NULL（§2.5），所以：
  - `video_render == NULL`；
  - `av_render_add_video_stream()` 会打印 “Video render not set, stream is skipped”
    （`components/av_render/src/av_render.c:1414-1427`，判空在 `:1423-1426`；日志 `:257` 对应）；
  - 加上 `video_dir = SEND_ONLY`（`webrtc.c:1238`），本来也不接收远端视频。
- 播放系统仍然注册了 H.264 / MJPEG 解码器（`media_sys.c:405`），以备将来接屏或接收远端视频。

### 6.6 sink1：行人检测路（RGB565 320×180@2fps）

```c
esp_capture_sink_cfg_t sink_cfg_1 = {
    .video_info = { .format_id = ESP_CAPTURE_FMT_ID_RGB565,
                    .width = DETECT_WIDTH, .height = DETECT_HEIGHT, .fps = DETECT_FPS },
};                                                              // webrtc.c:1304-1311
esp_capture_sink_setup(media_provider.capture, 1, &sink_cfg_1, &detect_sink);   // webrtc.c:1312
esp_capture_sink_enable(detect_sink, ESP_CAPTURE_RUN_MODE_ALWAYS);             // webrtc.c:1319
```

- 检测分辨率/帧率：`settings.h:40-50`（P4：320×180@2；S3：320×240@1）。
- 只有检测路准备好时才 `esp_capture_start()`（`webrtc.c:1385-1401`），
  随后 `start_pedestrian_detection()` 起推理。
- 该路数据不进入 WebRTC，只喂给 `pedestrain_detect.cpp` 做本地推理。

### 6.7 编解码器注册（`media_sys.c:400-415`）

```c
esp_video_enc_register_default();   // 视频编码（H.264/MJPEG）  media_sys.c:403
esp_audio_enc_register_default();   // 音频编码（G711A/OPUS）   media_sys.c:404
esp_video_dec_register_default();   // 视频解码（播放用）       media_sys.c:405
esp_audio_dec_register_default();   // 音频解码（AAC/G711A…）   media_sys.c:406
```

这四行是“清单式”注册，只注册 Kconfig 使能的实现，从而控制固件体积
（`components/av_render/README.md:95-118` 解释了这套机制）。

---

## 7. 任务、线程、缓冲区与内存（PSRAM）策略

### 7.1 线程总表

| 线程名 | 谁创建 | 栈/优先级/核 | 证据 |
| --- | --- | --- | --- |
| `app_main` | IDF | 默认 | `main.c:435` |
| `music_player` | `play_music()` | 由 `thread_scheduler` 决定（无专门规则→默认） | `media_sys.c:555` |
| `prompt_fallback` | `doorbell_audio_prompt_start()` | 同上传调度器（默认） | `doorbell_audio_render.c:271-272` |
| `Adec` / `ARender` | av_render（FIFO 非 0 时） | — | `av_render.c:1001-1009, 1326` |
| `Vdec` / `VRender` | av_render | — | `av_render.c:1496-1517, 1133` |
| `AUD_SRC` | esp_capture | 优先级 15 | `main.c:378-379` |
| `aenc_0` | esp_capture（仅 OPUS 时调栈） | 栈 40 KB/优先级 10/核 1 | `main.c:370-377` |
| `venc_0` | esp_capture | 优先级 10（S3 下栈 20 KB） | `main.c:363-369` |
| `detect` / `detect_filter` | 行人检测组件 | 栈 24 KB/优先级 8、15 | `main.c:380-391` |
| `pc_task` | esp_webrtc | 栈 25 KB/优先级 18/核 1 | `main.c:384-388` |
| `pc_send` | esp_webrtc | 由调度器默认 | `esp_webrtc.c:205` |
| `ring_stop` / `peer_enable` / `peer_disable` | 门铃状态机 | 默认 | `webrtc.c:326,548` |
| console REPL | esp_console | 栈 10 KB/优先级 22 | `main.c:292-293` |

**栈放 PSRAM**：`capture_scheduler()` 里把 `schedule_cfg->stack_in_ext = true`
（`main.c:397-409`）——所有 esp_capture 内部线程的栈都被放到外部 RAM（PSRAM），
这是 P4 上腾出内部 RAM 给 DMA/网络缓冲的常规做法。

`thread_scheduler`（`main.c:361-395`）是**按线程名打补丁**的调度策略表：名字匹配则改栈/优先级/核。
它的好处是不用改第三方组件；坏处是“名字变了策略就静默失效”，属于需要留意的维护点。

### 7.2 av_render 内部线程的创建条件

```c
static bool audio_need_decode_in_sync(...)  // raw fifo == 0 → 不建 Adec 线程（直接同步解码） av_render.c:886-899
static bool audio_need_render_in_sync(...)  // render fifo == 0 → 不建 ARender 线程        av_render.c:908-914
```

本项目 `audio_raw_fifo_size=4096`、`audio_render_fifo_size=6*1024`、`video_raw_fifo_size=500*1024`
（`media_sys.c:386-390`）全部非 0 → **Adec/ARender/Vdec 三条线程都会创建**。
`video_render_fifo_size` 被显式注释掉（`media_sys.c:390`），所以 `VRender` 不创建，视频在 Vdec 线程内直接渲染
（反正实际没有 LCD）。

### 7.3 缓冲区清单

| 缓冲 | 大小 | 分配方式 | 位置 |
| --- | --- | --- | --- |
| AAC 解码输入 FIFO（raw） | 4 KB | `data_queue_init` → `media_lib_malloc` | `media_sys.c:386`；`av_render.c:764` |
| PCM 渲染 FIFO | 6 KB | 同上 | `media_sys.c:387` |
| 视频解码 FIFO（raw） | 500 KB | 同上（PSRAM 可能性大） | `media_sys.c:388` |
| 提示音 PCM 常驻缓冲 | 起始 64 KB，翻倍至 ≈96 KB | `media_lib_realloc` | `media_sys.c:89-99` |
| 回退路径临时帧 | `320×2×2 = 1280 B`（栈上） | 栈 | `doorbell_audio_render.c:41` |
| 摄像头 V4L2 缓冲 | 2 个 | esp_video/V4L2 | `media_sys.c:312` |
| I2S DMA | `bufsize = 512`（日志显示自动对齐 256 帧） | IDF | 日志 `:216,445` |

### 7.4 PSRAM / 内存策略（`sdkconfig.defaults*.yml`）

| 配置 | 值 | 文件:行 | 作用 |
| --- | --- | --- | --- |
| `CONFIG_SPIRAM` | y | `sdkconfig.defaults:3` | 启用外部 PSRAM |
| `CONFIG_SPIRAM_SPEED_200M` | y | `sdkconfig.defaults.esp32p4:5` | PSRAM 200 MHz |
| `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` | 256 | `sdkconfig.defaults:39` | <256 B 的分配走内部 RAM，减少碎片/提速 |
| `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC` | y | `sdkconfig.defaults:41` | TLS（WebRTC/HTTPS）缓冲放 PSRAM |
| `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP` | P4 下 n | `sdkconfig.defaults.esp32p4:6-7` | 注释说明：远端 C5 无 PSRAM，保持厂商基线 |
| `CONFIG_ESP_HOSTED_MEMPOOL_PREFER_SPIRAM` | y | `sdkconfig.defaults.esp32p4:35` | C5 通信内存池优先 PSRAM |
| `stack_in_ext = true` | 运行时 | `main.c:404` | esp_capture 线程栈放 PSRAM |
| `CONFIG_FREERTOS_HZ` | 1000 | `sdkconfig.defaults:12` | 1 ms tick，配合 `media_lib_thread_sleep(1)` |

`media_lib_malloc/calloc/realloc/free` 的**具体适配实现不在本快照内**（`media_lib_add_default_adapter()` 由
`espressif/media_lib_sal` 提供，见 `components/media_lib_utils/idf_component.yml:13-16`）；
`components/esp_peer/src/media_lib_weak.c:20-37` 只是弱符号兜底（直接 `malloc`）。
**因此“某个缓冲是否落在 PSRAM”严格来说未在代码中确认**，只能从 Kconfig 推断（>256 B 的分配可能落 PSRAM）。

### 7.5 关键同步原语

| 互斥/同步 | 用途 | 位置 |
| --- | --- | --- |
| `prompt_render_t.mutex` | 串行化远端写入与提示音回退写入 | `doorbell_audio_render.c:27,44,154` |
| `door_bell_mutex` | 门铃状态机 | `webrtc.c:90` |
| `webrtc_api_mutex` / `webrtc_lifecycle_mutex` / `data_channel_mutex` | WebRTC 生命周期与数据通道 | `webrtc.c:91-93` |
| `av_render.api_lock` | av_render 的流/数据 API 串行化 | `av_render.c:1217` |

---

## 8. 关键常量与可调参数索引（改哪里？）

| 参数 | 当前值 | 定义位置 |
| --- | --- | --- |
| 板卡名 | `WT99P4C5_S1` | `solutions/doorbell_local/main/settings.h:21-25` |
| 视频分辨率/帧率 | 640×480 @15（P4）/320×240@10（S3） | `settings.h:30-38` |
| 检测分辨率/帧率 | 320×180 @2（P4）/320×240@1（S3） | `settings.h:40-50` |
| Wi-Fi 凭据 | `settings.local.h` 或占位宏 | `settings.h:56-61`、`settings.local.h.example:1-6` |
| 数据通道开关 | true | `settings.h:66` |
| 门铃按键 GPIO | 35（P4）/5（S3） | `settings.h:76,83` |
| 通话音频编解码/采样率/声道 | G711A / 8000 / 1（OPUS 时 16000/2） | `webrtc.c:1220-1229` |
| 视频编解码 | H.264 | `webrtc.c:1232` |
| 媒体方向 | 音频双向、视频仅发送 | `webrtc.c:1237-1238` |
| 铃声时长 | 4000 ms | `webrtc.c:46` |
| 提示音 PCM 目标格式 | 16000 Hz / 1ch / 16bit | `media_sys.c:161-170`、`doorbell_audio_render.c:13` |
| 提示音回退阈值/帧长/轮询 | 40 ms / 20 ms / 5 ms | `doorbell_audio_render.c:13-18` |
| av_render FIFO | 原始缓冲 4096 / 渲染缓冲 6 KB / 视频原始缓冲 500 KB | `media_sys.c:386-390` |
| 是否丢数据 | `allow_drop_data = false` | `media_sys.c:389` |
| 音频同步模式 | `ESP_CAPTURE_SYNC_MODE_AUDIO` | `media_sys.c:354` |
| 摄像头设备节点/缓冲数 | `/dev/video0` / 2 | `media_sys.c:310-314` |
| SCCB(I2C) 频率 | 100 kHz | `media_sys.c:267` |
| 传感器输出 | OV5647 RAW10 1280×960 binning 45fps | `sdkconfig.defaults.esp32p4:18-20` |
| I2S 初始采样率/slot | 16000 / 32 bit stereo（仅初始） | `codec_init.c:155-157` |
| 播放音量 / 录音增益 | 60.0 / 30.0 | `codec_init.c:549,553` |
| 提示音 PCM 初始缓冲 | 64 KB 起、翻倍 | `media_sys.c:89` |
| 线程参数（栈/优先级/核） | 见 §7.1 | `main.c:361-409` |
| 组件依赖版本 | esp_h264 ~1.3、pedestrian_detect ^0.3、esp_hosted 2.12.12、esp_wifi_remote 1.6.3、esp_capture ~1.0 | `main/idf_component.yml:7-33`、`components/esp_webrtc/idf_component.yml:9` |

---

## 9. 风险、疑点与改进建议

| # | 问题 | 影响 | 证据 | 建议 |
| --- | --- | --- | --- | --- |
| 1 | **通话中开门提示音在当前构建下不会播出** | 演示时“按了没声音”，最容易被质疑 | §4.7 完整证据链 | 打开 `WEBRTC_SUPPORT_OPUS`（16 kHz）或让提示音适配 8 kHz |
| 2 | 提示音不兼容时 `prompt_fallback` 线程 5 ms 空转直到通话结束 | 无谓的 CPU 唤醒 | `doorbell_audio_render.c:56-60,93` | 不兼容时直接让线程退出并打日志 |
| 3 | `play_music()` 结束时 `av_render_reset()` 会冲刷**整条**播放管线（含远端音频解码线程） | 若在通话中播提示音会打断远端音频；状态切换虽然会 `stop_music()` 兜底 | `media_sys.c:538`、`av_render.c:2068-2165`、`webrtc.c:248-252` | 通话中不要走 `play_music` 路径（当前设计已如此），但建议加断言/注释固化 |
| 4 | `av_render_add_audio_stream()` 每次换流都会关旧解码器并重开渲染器 | 造成 I2S 反复 close/open（日志可见），有瞬时爆音/丢帧风险 | `av_render.c:1281-1292, 971-992` | 通话与提示音共用同一流格式（这是 §4 方案的另一动机） |
| 5 | 本地 LCD 完全未初始化，`av_render` 里仍注册了视频解码器 | 与“有屏”预期不符；把 `video_render` 挂在 NULL 上 | `board.c:45-58`、日志 `:257` | 若产品需要屏，补 `board_lcd_init()`；否则在 README 明确“无本机显示” |
| 6 | 开机/每次连接都出现 `E ESP_CAPTURE: Fail to enable path for path:0x0` | 日志噪音，可能掩盖真问题 | 日志 `:206`、`task6-runtime-*.log` 多处 | 定位到 esp_capture 内部原因（快照外组件），或在 sink 未就绪时不要 enable |
| 7 | sink0 被应用层与 `esp_webrtc` 各 setup 一次 | 重复配置；不同组件参数漂移风险 | `webrtc.c:1301` vs `esp_webrtc.c:634` | 二选一，或在同处集中配置 |
| 8 | `i2s_render_cfg_t.fixed_clock` 被设置但实现里从未读取 | 配置项具有误导性 | `i2s_render.c:41,61`（全仓库仅赋值） | 文档注明该字段当前无效果 |
| 9 | `music_playing/music_stopping` 无锁 | 多触发源并发时可能重入 | `media_sys.c:75-76` | 用互斥量或统一走一个媒体命令队列 |
| 10 | 播放音量/麦克风增益硬编码 60/30 | 现场无法快速调音 | `codec_init.c:549,553` | 提到 `settings.h` 或用 console 命令 |
| 11 | 提示音与通话语音等权相加，无 ducking | 极端情况下提示音被通话语音掩盖或反之 | `pcm_overlay.c:44-53` | 需要时给提示音加增益系数（如 0.7）并降低远端音量 |
| 12 | `build_capture_system()` / `build_player_system()` 返回值未检查 | 媒体系统建失败仍继续跑 | `media_sys.c:411,413` | 报错并阻止后续网络流程 |
| 13 | 视频 H.264 软/硬编码未在快照中确认 | 汇报时无法准确描述性能边界 | `main/idf_component.yml:7-10` | 查 `esp_h264` Kconfig 与构建日志确认 |

---

## 10. 接口清单：谁调用谁（便于答辩时画图）

**`media_sys.c` 暴露（`solutions/common/media_sys.h`）**
- `media_sys_buildup()` ← `main.c:472`
- `media_sys_get_provider()` ← `webrtc.c:1266`
- `play_music()` ← `webrtc.c:128`（`play_tone`）
- `stop_music()` ← `webrtc.c:251`
- `play_open_door_prompt_in_call()` ← `webrtc.c:580`
- `test_capture_to_player()` ← `main.c:274`

**`doorbell_audio_render.c` 暴露（`doorbell_audio_render.h:12-21`）**
- `av_render_alloc_prompt_overlay()` ← `media_sys.c:368`
- `doorbell_audio_prompt_start()` ← `media_sys.c:582`
- `doorbell_audio_prompt_stop()` ← 内部（`close`/`deinit`）

**`pcm_overlay.c` 暴露（`pcm_overlay.h:18-32`）**
- `pcm_overlay_start/stop/active` ← `doorbell_audio_render.c`
- `pcm_overlay_mix_mono` ← 远端混音路径（`doorbell_audio_render.c:158`）
- `pcm_overlay_copy_mono` ← 回退路径（`doorbell_audio_render.c:67`）
- `pcm_overlay_fallback_due` ← 回退判定（`doorbell_audio_render.c:63`）

**`codec_board` 暴露（`components/codec_board/include/codec_init.h`）**
- `set_codec_board_type()` / `init_codec()` ← `board.c:48,57`
- `get_playback_handle()` ← `media_sys.c:366`；`get_record_handle()` ← `media_sys.c:348`
- `get_camera_cfg()` ← `media_sys.c:255`；`get_i2c_bus_handle()` ← `media_sys.c:266`
- `board_get_lcd_handle()` ← `media_sys.c:375`（当前返回 NULL）

**构建清单**
- 源文件与内嵌资源：`solutions/doorbell_local/main/CMakeLists.txt:1-16`
- 组件依赖与本地覆盖路径：`solutions/doorbell_local/main/idf_component.yml:1-46`
- 方案级 CMake（把 `solutions/common` 加进组件搜索路径）：`solutions/doorbell_local/CMakeLists.txt:8`

---

## 11. 未在代码中确认的事项（诚实清单）

1. **`media_lib_add_default_adapter()` 的具体实现**：由 `espressif/media_lib_sal` 提供（`components/media_lib_utils/idf_component.yml:13-16`），
   其源码不在本快照内；因此“某个缓冲落在内部 RAM 还是 PSRAM”只能按 Kconfig 推断。
2. **H.264 编码是硬件还是软件实现**：取决于 `espressif/esp_h264` 的 Kconfig（快照内无 sdkconfig 生成物）。
3. **`WT99P4C5_S1` 摄像头的 XCLK 来源**：`board_cfg.txt:86` 只写 `type: mipi`，`xclk=-1`，
   代码因此不创建 XCLK 时钟路由（`media_sys.c:272`）；实际时钟由谁提供未确认。
4. **`esp_capture` 组件内部实现**：不在快照内，故 `Fail to enable path`（日志 `:206`）的根因未确认；
   其线程实现细节（`AUD_SRC`/`aenc_0`/`venc_0`）只能依据日志与线程调度回调推断。
5. **`I2S_IF: ... channel 2`（日志 `:474`）的具体含义**：属于 `esp_codec_dev`/I2S 接口内部日志，未在快照中确认（可能是 slot 配置）。
6. **通话中开门提示音的人工听感验证**：既有文档明确标注为“尚待人工确认”
   （`docs/bringup/logs/2026-09-08-chinese-ui-prompt-fallback.md:42-53`），本次仅做静态分析。
7. **`music_playing` 多触发源的并发安全性**：是否所有触发路径都在同一线程/已被上层互斥，未在代码中确认。
8. **浏览器侧 `ring.aac` 与设备侧铃声的同步偏差**：未在任何代码或日志中确认。

---

*本文所有结论均来自工作区快照的静态阅读与快照内既有日志的交叉核对；未对设备做实测。*
