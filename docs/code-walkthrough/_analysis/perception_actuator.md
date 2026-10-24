# 人员感知链路与执行器实现分析（LD2450 + 摄像头视觉 + 多模态融合 + 舵机门锁 + 语音提示）

> 分析对象：工作区根 `solutions/doorbell_local/main/` 下与雷达、视觉、融合、门锁、门铃业务相关的源码，
> 以及 `docs/superpowers/specs/`、`docs/superpowers/plans/`、`docs/bringup/` 中的设计与实测文档。
>
> 本文为**只读分析**，未修改任何既有文件。所有关键结论均给出 `文件:行号` 证据。
> 未能在代码中找到依据的说法一律标注"未在代码中确认"。

---

## 0. 一句话结论

这套门铃的自动响铃是一条"**双传感器与门**"：

```
LD2450 雷达（UART 帧）──► presence_fusion（区域+去抖+冷却状态机）──┐
                                                                  ├──► multimodal_fusion ──► doorbell_request_ring("radar+vision")
摄像头 RGB565 ──► ESP-DL 行人/人脸模型 ──► vision_tracker ─────────┘                                   │
                                                                                                     ▼
                                                                            WebRTC 信令下发 "RING" + 板载 ring.aac
浏览器点"开门" ──► "OPEN_DOOR" 信令 ──► door_lock（LEDC 50Hz 舵机）──► 开门 5 秒自动回锁
                                    └─► 通话中：open.aac 解码成 PCM 混入通话；非通话：open.aac 直接播放
```

自动响铃的"与门"判定在 `multimodal_fusion.c`；雷达单独、摄像头单独都**不会**响铃。
（证据：`main.c:200-212`、`multimodal_fusion.c:31-39`）

---

## 1. 系统总览

### 1.1 编译开关与文件清单

行人检测整条链路被 Kconfig 开关包住：

- 开关定义：`solutions/doorbell_local/main/Kconfig:3-8`（`DOORBELL_SUPPORT_PEDESTRIAN_DETECT`，默认 `n`，仅 P4/S3 可用）。
- P4 默认打开：`solutions/doorbell_local/sdkconfig.defaults.esp32p4:46`。
- 源文件按开关增删：`solutions/doorbell_local/main/CMakeLists.txt:2-11`（只有 `pedestrain_detect.cpp` 在开关内，其余融合/过滤/跟踪模块始终编译）。
- AAC 与网页资源嵌入：`solutions/doorbell_local/main/CMakeLists.txt:13-16`（`ring.aac`、`open.aac`、`join.aac`、`webrtc_test.html`、HTTPS 证书）。
- 模型依赖：`solutions/doorbell_local/main/idf_component.yml:11-22`
  （`espressif/pedestrian_detect ^0.3.0`、`espressif/human_face_detect ^0.5.0`，均受 `DOORBELL_SUPPORT_PEDESTRIAN_DETECT` 约束）。
- 人脸模型落地方式：`sdkconfig.defaults.esp32p4:47-49`（`MSRMNP_S8_V1`，模型放 flash rodata）。

主要文件与职责：

| 文件 | 职责 |
| --- | --- |
| `radar_ld2450.c/.h` | UART 收字节、流式组帧、LD2450 协议解析、离线诊断 |
| `presence_fusion.c/.h` | 纯 C 的"区域 + 去抖 + 冷却"存在状态机（无 RTOS 依赖） |
| `pedestrain_detect.cpp` | 视觉采集消费、ESP-DL 推理、跟踪、快照回调（C++） |
| `pedestrian_filter.c/.h` | 单帧检测框合理性过滤（分数/面积/IoU/连续帧） |
| `camera_motion.c/.h` | 降采样亮度差分运动检测 |
| `pedestrian_format_probe.c/.h` | RGB565 字节序探测（仅测试使用，见 §4.9） |
| `vision_tracker.c/.h` | 多目标短时跟踪、"4 帧中 3 次"确认、3 帧丢失清除、人脸去重 |
| `vision_event.c/.h` | `vision_state` JSON 序列化 |
| `multimodal_fusion.c/.h` | 雷达与视觉证据的"与门"+ 本次访问只响一次 |
| `door_lock.c/.h` + `door_lock_state.c/.h` | LEDC 舵机 PWM、开门时序、LOCKED/OPENING/UNLOCKED/CLOSING/FAULT 状态机 |
| `main.c` | 把雷达帧 → presence_fusion → 视觉聚焦 → 多模态融合 → 请求响铃串起来 |
| `webrtc.c` | 信令收发、门铃状态机、响铃策略入口、开门命令处理、检测支路启停、播放提示音 |
| `doorbell_ring_policy.c/.h` | 响铃准入判定（ready/busy） |
| `doorbell_event.c/.h` | `radar_event` JSON 序列化 |
| `doorbell_status.c/.h` | 面向未来显示屏的状态快照（当前应用未接线，见 §4.9） |
| `media_sys.c`、`doorbell_audio_render.c`、`pcm_overlay.c` | 音频渲染、AAC 播放、通话中 PCM 混音播报 |

### 1.2 模块之间谁调用谁

自上而下的调用链（→ 表示"调用"）：

```
main.c: app_main (main.c:435)
 ├─ doorbell_webrtc_init()            (main.c:442)
 ├─ multimodal_fusion_init()          (main.c:449)
 ├─ door_lock_init()                  (main.c:455)
 ├─ presence_fusion_init()            (main.c:462)
 ├─ radar_ld2450_init(radar_frame_received, NULL)   (main.c:466)
 ├─ media_sys_buildup()               (main.c:472)
 ├─ init_console()                    (main.c:473)
 └─ network_init(...)                 (main.c:474) → 连上后 network_event_handler → start_webrtc()

radar 侧（运行在 UART 任务 "ld2450_uart" 中）
 radar_uart_task (radar_ld2450.c:168)
   └─ ld2450_stream_feed (radar_ld2450.c:92) → handle_frame (radar_ld2450.c:151)
        └─ radar_frame_received (main.c:141)
             ├─ presence_fusion_process            (main.c:148)
             ├─ set_vision_radar_focus(...)        (main.c:152)
             ├─ doorbell_radar_event_format_json + webrtc_http_server_publish_event (main.c:192-195)
             ├─ update_multimodal_radar(→multimodal_fusion_on_radar)  (main.c:56-68, 201/203)
             └─ request_sensor_ring("radar+vision")  (main.c:46-53, 206)

视觉侧（运行在 detect / detect_filter 任务中）
 start_webrtc (webrtc.c:1171)
   ├─ esp_capture_sink_setup(capture, 1, RGB565 320x180@2fps) (webrtc.c:1312-1313)
   ├─ esp_capture_sink_enable(detect_sink, ALWAYS)            (webrtc.c:1319)
   └─ start_pedestrian_detection(&detect_cfg)                 (webrtc.c:1396)
        ├─ filter_task   (pedestrain_detect.cpp:231)  ← 取帧/运动检测/入队
        └─ detecting_task(pedestrain_detect.cpp:66)   ← 推理/跟踪/快照
              └─ cfg.snapshot = vision_snapshot_received (webrtc.c:1394)
                   └─ doorbell_report_vision_snapshot (main.c:86)
                        ├─ vision_snapshot_format_json + publish_latest_vision (main.c:91-95)
                        ├─ multimodal_fusion_on_vision_evidence               (main.c:116)
                        └─ request_sensor_ring("radar+vision")                (main.c:127)

开门/响铃侧
 doorbell_request_ring (webrtc.c:333) → doorbell_ring_policy_evaluate (doorbell_ring_policy.c:3)
                                       → SEND_CMD(handle, "RING") (webrtc.c:373)
                                       → play_tone(DOOR_BELL_TONE_RING) (webrtc.c:387) → play_music → music_play_thread (media_sys.c:483)
                                       → schedule_ring_stop (webrtc.c:316) → 4s 后发 "RING_STOPPED" (webrtc.c:293-309)

 door_bell_on_cmd("OPEN_DOOR") (webrtc.c:557)
   ├─ door_lock_request_open()          (webrtc.c:575)
   ├─ 通话中 → play_open_door_prompt_in_call() (webrtc.c:580 → media_sys.c:575)
   └─ 通话前 → play_tone(DOOR_BELL_TONE_OPEN_DOOR) (webrtc.c:584)

 door_lock_state_changed 回调 (webrtc.c:407) → 向浏览器发 DOOR_OPENING/OPENED/CLOSING/LOCKED/FAILED (webrtc.c:420-438)
```

### 1.3 线程/任务与优先级汇总

| 任务名 | 创建位置 | 栈 | 优先级 | 周期/超时 |
| --- | --- | --- | --- | --- |
| `ld2450_uart` | `radar_ld2450.c:233-234` | 4096 B（`radar_ld2450.c:141`） | 5（`radar_ld2450.c:142`） | `uart_read_bytes` 超时 100 ms（`radar_ld2450.c:178-179`）；无有效帧 2000 ms 打离线告警（`radar_ld2450.c:143`, `192-198`） |
| `detect_filter` | `pedestrain_detect.cpp:331` | 未在代码中指定（走 media_lib 默认） | 15（`main.c:389-391`） | 阻塞在 `esp_capture_sink_acquire_frame`（`pedestrain_detect.cpp:254`），节奏由 sink 的 2 FPS 决定 |
| `detect` | `pedestrain_detect.cpp:336` | 24 KB（`main.c:381`） | 8（`main.c:382`） | 阻塞在 `data_queue_read_lock`（`pedestrain_detect.cpp:98`） |
| `door_lock` | `door_lock.c:173-174` | 3072 B（`door_lock.c:19`） | 5（`door_lock.c:20`） | `xQueueReceive(..., portMAX_DELAY)` 无限等（`door_lock.c:90`）；单次开门流程 500+5000+500 ms |
| `music_player` | `media_sys.c:555` | media_lib 默认 | media_lib 默认 | 每帧 AAC 后 `sleep(1ms)`（`media_sys.c:536`）；时长由 `music_duration` 控制 |
| `prompt_fallback` | `doorbell_audio_render.c:271-272` | media_lib 默认 | media_lib 默认 | 5 ms 轮询（`doorbell_audio_render.c:18, 93`） |
| `Key`（BOOT 按键） | `webrtc.c:1138-1139` | media_lib 默认 | media_lib 默认 | 50 ms 轮询（`webrtc.c:1106`） |
| `data`（数据通道） | `webrtc.c:1158-1159` | media_lib 默认 | media_lib 默认 | 未在代码中确认 |
| SSE 推流任务 | `webrtc_http_server.c:107` 内 `vTaskDelay(100ms)` | httpd 异步 | — | 100 ms 轮询三个队列 + 5 s 心跳（`webrtc_http_server.c:144, 156, 159-161`） |
| 主循环 | `main.c:475-478` | — | — | 2000 ms 调一次 `query_webrtc()` |

> 注：`thread_scheduler`（`main.c:361-395`）只覆盖 `venc_0/aenc_0/AUD_SRC/detect/detect_filter/pc_task/start` 这些名字；
> `detect` 被设置为 24 KB / 优先级 8，`detect_filter` 优先级 15（`main.c:380-391`）。
> 其余线程使用组件默认值（未在代码中确认具体数值）。

### 1.4 数据流（分层）

```
物理层    LD2450 雷达(256000 8N1)                 OV5647 MIPI-CSI
            │ TX→GPIO4                                 │ ISP/VID_PIPE
协议/采集  UART1 RX 环形缓冲 2048B                      capture sink0: H264 640x480@15 (WebRTC)
            │                                         capture sink1: RGB565 320x180@2fps (AI)
解析层    ld2450_parse_frame → ld2450_frame_t(3 目标)   esp_capture_sink_acquire_frame
            │                                         esp_cache_msync(M2C)
判定层    presence_fusion(区域/去抖/冷却)               camera_motion(仅诊断) + ESP-DL 推理
            │                                          pedestrian_filter → vision_tracker
融合层    multimodal_fusion (雷达 present+可响铃 × 视觉证据 ≤2000ms)
            │
业务层    doorbell_ring_policy → "RING" 信令 + ring.aac + 4s 后 "RING_STOPPED"
            │
执行层    舵机 LEDC PWM(GPIO32) / open.aac 播报 / 浏览器 UI 与 SSE
```

---

## 2. LD2450 雷达链路

### 2.1 硬件与 UART 参数

| 项 | 值 | 证据 |
| --- | --- | --- |
| 串口 | `UART_NUM_1` | `radar_ld2450.c:138` |
| 波特率 | 256000 | `radar_ld2450.h:22` |
| RX / TX GPIO | 4 / 5 | `radar_ld2450.h:23-24` |
| 数据格式 | 8 数据位、无校验、1 停止位、无流控 | `radar_ld2450.c:210-214` |
| RX 缓冲 | 2048 B | `radar_ld2450.c:139` |
| 单次读取块 | 256 B | `radar_ld2450.c:140`, `171` |
| 帧率 | 约 10 帧/秒（文档实测） | `docs/bringup/radar-test-matrix.md:25` |
| 接线 | 雷达 TX→P4 GPIO4，GND 共地，5 V 独立供电 | `docs/bringup/radar-test-matrix.md:11-20` |

初始化流程：`uart_param_config` → `uart_set_pin` → `uart_driver_install(2048, 0, 0)` → 创建任务
（`radar_ld2450.c:216-239`）。`uart_initialized` 做幂等保护（`radar_ld2450.c:204-206, 241`）；
任务创建失败会回滚回调指针并 `uart_driver_delete`（`radar_ld2450.c:234-239`）。

### 2.2 帧协议（这是理解一切的基础）

```
偏移   长度  内容
0x00    4   帧头  AA FF 03 00
0x04    8   目标 1：X(2) Y(2) Speed(2) 距离分辨率(2)   —— 小端
0x0C    8   目标 2：同上
0x14    8   目标 3：同上
0x1C    2   帧尾  55 CC
总计   30   字节，无校验和
```

- 常量：`LD2450_MAX_TARGETS 3`、`LD2450_HEADER_SIZE 4`、`LD2450_TARGET_DATA_SIZE 8`、`LD2450_TAIL_SIZE 2`，
  `LD2450_FRAME_SIZE = 4 + 3*8 + 2 = 30`（`radar_ld2450.h:11-17`）。
- 帧头/帧尾字面量：`radar_ld2450.c:13-16`。
- 协议基线（无校验、目标槽位全 0 表示空槽）：`docs/bringup/radar-test-matrix.md:26-30`。

**目标槽位**：3 个槽位与"人"没有稳定对应关系，槽位号只用于日志/网页的 `track_id`
（`presence_fusion.c:85-95` 明确按"任意槽位在区域内"判断，不依赖槽位编号）。

### 2.3 单帧解析 `ld2450_parse_frame`

`radar_ld2450.c:29-71`，逐步逻辑：

1. 空指针 → `LD2450_PARSE_BAD_ARGUMENT`（`:32-34`）。
2. 长度必须精确等于 30，否则 `LD2450_PARSE_BAD_LENGTH`（`:35-37`）。**不做"从缓冲区里找帧"的操作**，只做定长校验。
3. `memcmp` 校验帧头 `AA FF 03 00`（`:38-40`）与帧尾 `55 CC`（`:41-44`）。
4. `memset` 清空输出结构（`:46`）。
5. 逐槽位（3 次）判断 8 字节是否**全 0**：全 0 → 空槽，跳过且不置 `valid`（`:50-59`）。
6. 非空槽按下表解码（`:61-68`）：

| 字段 | 解码函数 | 单位 | 说明 |
| --- | --- | --- | --- |
| `x_mm` | `decode_direction_magnitude(read_le16(&t[0]))` | mm | 横向，正/负见下 |
| `y_mm` | `decode_direction_magnitude(read_le16(&t[2]))` | mm | 纵向（离雷达距离方向） |
| `speed_cm_s` | `decode_direction_magnitude(read_le16(&t[4]))` | cm/s | 径向速度 |
| `distance_resolution_mm` | `read_le16(&t[6])` | mm | 无符号，原样读出 |

7. 每命中一个非空槽 `target_count++`（`:68`）。

**小端读取**：`read_le16`（`radar_ld2450.c:18-21`）＝ 低字节在前。

**符号/幅值编码**（重点，容易搞反）：
`decode_direction_magnitude`（`radar_ld2450.c:23-27`）取低 15 位为幅值，然后
**bit15 = 1 → 正数，bit15 = 0 → 负数**（`:26`）。
文档独立确认了这一约定：`docs/bringup/radar-test-matrix.md:29-30`
（"bit 15 set is positive; bit 15 clear is negative; bits 0-14 hold the magnitude"），
主机测试用 LD2450 官方协议样例验证为 `X=-782 mm, Y=1713 mm, speed=-16 cm/s, res=320 mm`
（`tests/radar_ld2450_parser_test.c:8-14, 49-62`），实机抓帧验证为 `X=-1052, Y=690, speed=-16`
（`tests/radar_ld2450_parser_test.c:26-32, 92-99`）。

> **注意**：这个符号约定与"常见第三方描述"可能相反，但本仓库的代码、文档、官方样例解码三者一致，
> 因此以本仓库为准；若实机方向与之相反，只需改 `decode_direction_magnitude` 一处。

**没有做的校验**：协议本身没有校验和/CRC，代码也没有额外的合理性校验（例如"坐标超出雷达量程即丢弃"）。
唯一的结构性校验就是长度 + 帧头 + 帧尾（`:35-44`）。

### 2.4 流式组帧 `ld2450_stream_feed`

UART 是字节流，一帧 30 字节可能在任意位置被切开，也可能混入噪声。解决方式是两级状态机：

**第一级：同步头逐字节对齐** `accept_sync_byte`（`radar_ld2450.c:80-90`）

```
若 byte == 期望的帧头第 length 个字节 → 存入 buffer，length++
否则若 byte == 帧头第 0 字节(0xAA)     → buffer[0]=byte, length=1   （重新开始匹配）
否则                                   → length = 0                （丢掉，回到搜索状态）
```

**第二级：凑满 30 字节就解析** `ld2450_stream_feed`（`radar_ld2450.c:92-123`）

- `length < 4` 时只做同步（`:102-105`）。
- 否则把字节填进 `buffer`，直到 `length == 30`（`:107-110`）。
- 调 `ld2450_parse_frame`；**成功**才 `emitted++` 并回调（`:113-119`）。
- 无论成功失败都 `length = 0` 重新同步（`:120`）——所以坏帧只损失一帧，不会卡死。
- 返回本次喂入所产出的有效帧数（`:122`），UART 任务用它的正负判断在线/离线。

**没有字节回溯**：帧尾校验失败时不会把 `AA FF 03 00` 之后的字节重新当作可能的帧头搜索，
而是整帧丢弃并重新找 `0xAA`。协议里 `0xAA` 在数据区出现时理论上可能误同步，但因为帧长固定 +
帧尾校验 + 下一帧仍以 `AA FF 03 00` 开头，实测（`docs/bringup/radar-test-matrix.md:44`）可自恢复。

### 2.5 UART 任务与错误/离线处理

`radar_uart_task`（`radar_ld2450.c:168-200`）：

- 每轮 `uart_read_bytes(..., 256, pdMS_TO_TICKS(100))`（`:178-179`）——100 ms 超时保证任务能周期性检查离线计时。
- `length > 0` 就喂给解码器；**只要有 ≥1 帧被成功解析**，就刷新 `last_valid_frame_tick`（`:183-189`）。
- 离线判定：连续 2000 ms 没有有效帧 → `ESP_LOGW("No valid LD2450 frame for 2000 ms")`，并且
  `offline_logged` 保证只报一次；恢复时打 `"Valid frame stream restored"`（`:192-198`）。
- **关键设计**：串口故障**不会**伪造事件——因为离线只打日志，`presence_fusion` 的状态不会因此改变
  （这也符合设计文档 `docs/superpowers/specs/2026-09-05-ld2450-zone-debounce-ringing-design.md:109`）。
  换言之，人离开时若掉线，系统会一直停在 `PRESENT`（安全方向：宁可保持"有人"）。

**原始帧抓取工具**：`ld2450_should_capture_sample`（`radar_ld2450.c:125-134`）按"目标数 0/1/2/3"各抓一次，
在 `handle_frame` 里以 hexdump 打印（`radar_ld2450.c:155-161`），用于给主机测试补充样例。
抓拍记录只对"第一次出现该目标数"生效，之后不再打印（`:129-132`）。

### 2.6 解析层的对外契约

回调签名：`ld2450_frame_callback_t(const uint8_t raw_frame[30], const ld2450_frame_t*, void *ctx)`
（`radar_ld2450.h:57-59`）。本工程注册的回调是 `main.c:141 radar_frame_received`。
解析全是无副作用的纯函数，可在 PC 上单测（`tests/radar_ld2450_parser_test.c`）。

---

## 3. `presence_fusion`：区域、去抖、状态机、冷却

模块定位：**不依赖 FreeRTOS / ESP-IDF 的纯 C 状态机**（设计意图见
`docs/superpowers/specs/2026-09-05-ld2450-zone-debounce-ringing-design.md:42-46`），
用单调毫秒时间而不是帧数去抖（同文档 `:28-34`）。

### 3.1 关键时间常量（全部集中在一个默认配置函数里）

`presence_fusion_default_config()`（`presence_fusion.c:5-14`）：

| 参数 | 数值 | 含义 |
| --- | --- | --- |
| `x_min_mm` / `x_max_mm` | **-1500 / 1500** | 门前关注区横向半宽 ±1.5 m |
| `y_min_mm` / `y_max_mm` | **300 / 3000** | 纵向 0.3 m ~ 3.0 m（离雷达最近 0.3 m 起） |
| `enter_confirm_ms` | **1000** | 进入确认时间（去抖） |
| `leave_confirm_ms` | **2000** | 离开确认时间（吸收短暂丢帧） |
| `cooldown_ms` | **10000** | 一次确认离开后的重复响铃冷却 |
| `direction_deadband_cm_s` | **10** | 方向静止死区 ±10 cm/s |

与设计文档 `.../2026-09-05-ld2450-zone-debounce-ringing-design.md:15-22` 和实测矩阵
`docs/bringup/radar-test-matrix.md:70-73` 完全一致。

`presence_fusion_init` 会校验配置合法性（区间顺序、确认时间非 0、死区非负），否则返回 `false`
（`presence_fusion.c:16-32`）。

### 3.2 区域（zone）判定

`presence_fusion_target_in_zone`（`presence_fusion.c:34-40`）：

```c
target->valid &&                       // 槽位非空
x_min <= x_mm <= x_max &&              // 边界包含在内（用的是 >= / <=）
y_min <= y_mm <= y_max
```

- **边界值算在区域内**（`>=`/`<=`），符合设计 `:24`。
- 无效目标（空槽）与区域外目标都不计入存在判定（`presence_fusion.c:87-89` 先过滤再计数）。
- 注意：区域判定**只用平面坐标，不用速度**。方向不参与响铃门限（设计 `:88`）。

### 3.3 代表目标与方向

`presence_fusion_process`（`presence_fusion.c:68-150`）：

1. **冷却到期检查**：`cooldown_active && now_ms - cooldown_started_ms >= cooldown_ms` → 解除冷却
   （`:80-83`）。用无符号差值比较，天然抗 `uint32_t` 回绕（设计 `:105`）。
2. **遍历 3 个槽位**，统计 `in_zone_count`（`:86-95`）。
3. **代表目标** = 区域内 `y_mm` **最小**的那个槽位（即离雷达最近/最靠门前），
   平手时保留先遇到的槽位（`:91-94`，比较用的是严格 `<`）。
4. `occupied = in_zone_count != 0`（`:96`）。
5. 有目标时填充 `has_representative_target / representative_slot / representative_target`，
   并按代表目标速度分类方向（`:97-103`）。
6. 方向分类 `presence_fusion_classify_direction`（`:42-55`）：
   - `speed < -10 cm/s` → `APPROACHING`（靠近）
   - `speed > +10 cm/s` → `LEAVING`（远离）
   - 其余（含 ±10 边界）→ `STATIONARY`
   - 配置非法 → `UNKNOWN`（`:45-47`）
   
   符号方向与 `decode_direction_magnitude` 的约定绑定；实机安装方向若相反只需改映射（设计 `:88`）。

### 3.4 状态机（进入/离开确认）

四个状态：`CLEAR → ENTERING → PRESENT → EXITING → CLEAR`（`presence_fusion.h:13-18`），
实现在 `presence_fusion.c:105-147` 的 `switch`：

| 当前状态 | 条件 | 动作 | 事件 |
| --- | --- | --- | --- |
| `CLEAR` | 区域内有目标 | → `ENTERING`，记录 `state_since_ms` | 无（`:106-112`） |
| `ENTERING` | 目标消失 | → `CLEAR`（立即，不产生事件） | 无（`:113-116`） |
| `ENTERING` | 持续有目标 ≥ **1000 ms** | → `PRESENT` | `ENTER_CONFIRMED`；`ring_requested = !cooldown_active`（`:117-122`） |
| `PRESENT` | 目标消失 | → `EXITING`，记录 `state_since_ms` | 无（`:124-130`） |
| `EXITING` | 目标重新出现 | → `PRESENT`（**不产生新进入事件，不重复响铃**） | 无（`:131-134`） |
| `EXITING` | 持续无目标 ≥ **2000 ms** | → `CLEAR`，开始 10 s 冷却 | `LEAVE_CONFIRMED`（`:135-141`） |
| 其它（理论不可达） | — | 强制回 `CLEAR` | 无（`:143-146`） |

要点：

- **去抖是按时间**：`(uint32_t)(now_ms - state_since_ms) >= enter_confirm_ms`（`:117`）。
  雷达约 10 帧/秒，所以确认精度约 100 ms 量级。
- **只在边沿产生事件**：`state_changed` / `event` 只在转换那一帧置位，天然避免"每帧都响铃"
  （设计 `:34`）。
- **`ENTERING` 未满 1 秒就消失 → 直接回 CLEAR**，不进入 `EXITING`，因此"快速掠过"不会触发任何事件
  （`:113-116`）。
- `EXITING` 内重新出现只回到 `PRESENT`，**不**重新计时，也**不**再发 `ENTER_CONFIRMED`（`:131-134`）。

### 3.5 冷却与"一次停留只响一次"

- 冷却只在**确认离开**时开始：`cooldown_active = true; cooldown_started_ms = now_ms`（`:137-138`）。
- 进入确认时若仍在冷却，`ring_requested = false`（`:121`），但 `ENTER_CONFIRMED` 事件照样产生
  （网页仍能看到"有人来过"）。
- **冷却中途到期也不会为本次停留补发响铃**：因为 `ring_requested` 只在 `ENTERING → PRESENT`
  那一帧计算（`:121`），冷却到期只清 `cooldown_active`（`:80-83`），不会重发事件。
  必须"离开 → 冷却结束 → 再次进入并连续 1 秒"才会得到 `ring_requested = true`
  （与设计 `:74` 完全一致）。
- 首次上电没有冷却（`presence_fusion_init` 把结构体清零，`cooldown_active = false`，`:28-30`），
  因此第一次有效进入可以响铃（设计 `:76`）。

### 3.6 结果结构体与调用点

`presence_fusion_result_t`（`presence_fusion.h:52-62`）携带：状态、事件、`state_changed`、
`ring_requested`、`in_zone_count`、代表目标（含槽位）、方向。

唯一调用点：`main.c:148-149`（在雷达 UART 任务上下文里）。
调用后 `main.c` 立刻做三件事（详见 §9 时序）：
1. `set_vision_radar_focus(result.in_zone_count > 0)`（`main.c:152`）→ 影响视觉调度；
2. 状态变化打一条汇总日志（`main.c:160-177`）；
3. 事件非空时发 `radar_event` JSON（`main.c:179-196`），并把进入/离开送给多模态融合（`main.c:198-207`）。

名称辅助函数：`presence_state_name` / `presence_event_name` / `presence_direction_name`
（`presence_fusion.c:152-181`），用于日志。

---

## 4. 摄像头视觉检测链路

### 4.1 采集支路（独立于 WebRTC 视频）

在 `start_webrtc` 里，除了给 WebRTC 用的 sink 0（H264 640×480@15，`settings.h:31-33`, `webrtc.c:1276-1301`），
额外配了一个**只给 AI 用的 sink 1**：

```c
esp_capture_sink_cfg_t sink_cfg_1 = {
    .video_info = { .format_id = ESP_CAPTURE_FMT_ID_RGB565,
                    .width = DETECT_WIDTH,   // 320
                    .height = DETECT_HEIGHT, // 180
                    .fps = DETECT_FPS },     // 2
};
esp_capture_sink_setup(media_provider.capture, 1, &sink_cfg_1, &detect_sink);
esp_capture_sink_enable(detect_sink, ESP_CAPTURE_RUN_MODE_ALWAYS);
```

证据：`webrtc.c:1304-1328`（`DETECT_WIDTH/HEIGHT/FPS` 定义在 `settings.h:40-50`：P4 = 320×180@2 FPS，
S3 = 320×240@1 FPS）。`detect_sink` 通过 `get_detect_sink()`（`webrtc.c:445-448`）给过滤任务；
采集在 `esp_capture_start` 后启动（`webrtc.c:1387`），停止时 `stop_pedestrian_detection` +
`esp_capture_stop` + `detect_sink = NULL`（`webrtc.c:1463-1469`）。

注意 `webrtc.c:1271` 设置了 `esp_webrtc_set_no_auto_capture(true)`，注释说明是为了避免每次挂断重建
P4 视频管线导致下次通话延迟。

### 4.2 `filter_task`：取帧 → 缓存同步 → 运动检测 → 入队

`pedestrain_detect.cpp:231-303`：

1. 取 sink：`get_detect_sink()`，为 NULL 则打印错误并退出任务（`:239-245`）。
   **注意**：这里失败会设置 `filter_exited = true` 并销毁线程，检测链路整体不可用（不会自动重启）。
2. 初始化运动检测器 `camera_motion_init`，失败同样退出（`:246-251`）。
3. 主循环（`while (detect->filtering)`，`:252`）：
   - `esp_capture_sink_acquire_frame(sink, &frame, false)`（`:254`）——按 2 FPS 节奏拿到 RGB565 帧。
   - `esp_cache_msync(frame.data, frame.size, ESP_CACHE_MSYNC_FLAG_DIR_M2C)`（`:258-259`）：
     CSI/DMA 写入的帧在 CPU 读取前必须做 cache 同步，否则会读到脏数据。失败则记日志、释放帧并退出循环（`:260-265`）。
   - `log_camera_probe_frame`（`:266`）：只对**前 12 帧**打印 `size / 非零采样数 / FNV-1a 哈希`
     （`:46-64`，每 64 字节采一个点）。用途是区分"采集卡死"和"模型看不到人"——
     起因见 `docs/bringup/logs/2026-09-09-camera-near-field-confirmation.md:4-13`
     （第一帧全 0 是未就绪帧，第 2~12 帧哈希各异说明管线正常）。
   - `camera_motion_update_rgb565(...)`（`:268-270`）得到 `motion_confirmed` 与 `changed_permille`。
   - `!motion.has_baseline`（还没有基准帧）→ 释放帧 `continue`（`:271-274`）。
   - `motion_confirmed` 只用于**打日志**（`:275-278`），不参与任何决策（见 §4.9 第 2 条）。
   - 降级丢帧：`detect->degraded` 时 `frame_sequence & 1`（丢弃奇数帧）→ 2 FPS 变 1 FPS（`:279-283`）。
   - 把整帧拷进 `data_queue`：`data_queue_get_buffer(q, sizeof(esp_capture_stream_frame_t)+frame.size)`
     → 复制帧头结构 → `filter->data` 指向结构体之后的数据区 → `memcpy` 像素 → `data_queue_send_buffer`（`:284-293`）。
   - 释放采集帧（`:294`）。
   - `data == NULL`（仅在队列 `quit` 时发生）→ 记 "Fail to get buffer" 并退出（`:295-298`）。

队列容量：`DETECT_WIDTH*DETECT_HEIGHT*2 + 256 = 115456` 字节（`:322`），注释说"dual buffer"，
但一帧像素本身就是 `320*180*2 = 115200` 字节，加上队列自己的 4 字节长度头
（`components/media_lib_utils/src/data_queue.c:28`）与 `esp_capture_stream_frame_t` 头部开销，
**两帧必然放不下**，实际只缓存 1 帧（`esp_capture_stream_frame_t` 定义在托管组件
`espressif/esp_capture` 中，本仓库未 vendored，精确字节数未在代码中确认）。
`data_queue_get_buffer` 在空间不足时会**阻塞等待消费者释放**
（`components/media_lib_utils/src/data_queue.c:234-254`），而不是"丢弃最旧帧"。
因此在 AI 变慢时，`filter_task` 会阻塞在入队处，采集侧帧被覆盖/丢弃——这一点与设计文档
`docs/superpowers/specs/2026-09-10-hybrid-person-detection-design.md:70-71`（"推理赶不上输入时丢弃旧帧，只处理最新帧"）
的表述不完全一致，实际机制是"生产者阻塞 + 采集侧覆盖"（覆盖计数未在代码中确认）。

### 4.3 `detecting_task`：模型、分辨率、频率、级联调度

`pedestrain_detect.cpp:66-229`。

**框架与模型**
- 框架：**ESP-DL**（`dl::image::img_t`，头文件 `human_face_detect.hpp` / `pedestrian_detect.hpp`，`:103-108`, `:16-17`）。
- 模型对象：`PedestrianDetect`（行人）与 `HumanFaceDetect`（人脸）各 new 一次，用 `std::nothrow`（`:69-70`）。
- 依赖版本（构建记录）：ESP-DL 3.3.11 / `pedestrian_detect` 0.3.2 / `human_face_detect` 0.5.0
  （`docs/bringup/logs/2026-09-10-hybrid-person-detection-runtime.log:8-10`）。
- 模型阈值：`pedestrian_lib->set_score_thr(tracker_config.pedestrian_min_score)` = **0.70**（`:89-91`，
  默认值来自 `vision_tracker.c:9`；`docs/bringup/logs/2026-09-09-camera-near-field-confirmation.md:20-21` 说明
  分数阈值回到 0.70，靠几何+时序过滤假阳性）。

**输入与分辨率**
```c
dl::image::img_t img = { .data = frame->data,
                         .width = DETECT_WIDTH,   // 320
                         .height = DETECT_HEIGHT, // 180
                         .pix_type = DL_IMAGE_PIX_TYPE_RGB565LE };
```
（`:103-108`）——即**固定按 320×180、RGB565 小端**解释像素。

**检测频率**：由采集 sink 的 `DETECT_FPS = 2` 决定（`settings.h:44`, `webrtc.c:1309`）；
推理变慢时按 §4.8 降到 1 FPS。

**"行人优先、人脸补充"级联**（`:113-159`）：
1. 先跑行人模型 `pedestrian_lib->run(img)`（`:114`）。
2. 每个结果先过 `pedestrian_detection_is_plausible`（默认配置：分数 ≥0.70、面积 5‰~850‰、框在画面内）
   （`:115-129`）；不合理的直接丢弃。
3. 只要有**任一**合理行人框，`has_plausible_pedestrian = true`（`:130`），并写入统一候选数组（最多 3 个，`:131-140`）。
4. 人脸模型只在 `radar_focus || !has_plausible_pedestrian` 时运行（`:143-144`）：
   - 雷达已在门前区域（`radar_focus`）→ 同帧跑两个模型，缩短近距离确认时间；
   - 行人没有合理结果 → 用人脸兜底（近距离只有头肩时行人模型容易失效）。
5. 人脸结果**不做** `pedestrian_filter` 预过滤，直接进候选数组（`:145-158`），
   由 `vision_tracker` 用 0.75 分数 + 面积 2‰~600‰ 把关（`vision_tracker.c:9-13, 78-94`）。
6. 读完帧后 `data_queue_read_unlock`（`:160`）。

**跟踪与输出**
```c
uint32_t now_ms = esp_timer_get_time() / 1000;
vision_update_t update = vision_tracker_update(&tracker, detections, detection_count,
                                               DETECT_WIDTH, DETECT_HEIGHT, now_ms);
```
（`:162-165`）→ 组装 `vision_snapshot_t`（含两个模型状态 READY/UNAVAILABLE）→ 调 `cfg.snapshot`（`:167-180`）。

**旧接口兼容路径**（`:181-198`）：
`update.confirmed_present && !last_confirmed && cfg.detected != NULL && update.target_count > 0` 时，
把第一个目标的框转成 `esp_capture_rgn_t` 调 `cfg.detected`，并打印一行"Vision presence confirmed/cleared"。
**本工程把 `detected` 配成了 `NULL`**（`webrtc.c:1392-1395`），所以：
- `detected` 回调从未被调用；
- 依赖它的 `webrtc.c:450 pedestrian_detected()`（发 `PEDESTRIAN_DETECTED` 信令 + 调 `doorbell_report_vision_detection()`）
  **是死代码**；
- 自动融合响铃**只**通过 `snapshot → doorbell_report_vision_snapshot` 这条路径生效。

### 4.4 `pedestrian_filter`：过滤了哪些假阳性

起因非常具体（真实故障复现）：空场景下 ESP-DL 反复输出覆盖整幅画面的框
`score=0.852 box=[0,0,319,179]`，而旧集成"每帧结果立即接受"，于是持续刷新"摄像头有人"
（`docs/bringup/logs/2026-09-08-pedestrian-false-positive-filter.md:8-22`）。

配置默认值（`pedestrian_filter.c:7-18`）：

| 参数 | 值 | 作用 |
| --- | --- | --- |
| `min_score` | **0.70** | 丢弃低分结果 |
| `min_area_permille` | **5** | 丢弃过小框（<0.5% 画面）——噪点 |
| `max_area_permille` | **850** | 丢弃过大框（>85% 画面）——**这条正是杀死"满屏框"假阳性的关键** |
| `min_iou_permille` | **250** | 与上一帧框 IoU ≥25% 才算"同一个目标在持续" |
| `confirm_frames` | **3** | 连续 3 帧命中才确认 |
| `clear_frames` | **3** | 连续 3 帧丢失才取消确认 |

判定函数：

- `pedestrian_detection_is_plausible`（`pedestrian_filter.c:36-55`）：
  `score >= min_score`、`x1,y1 >= 0`、`x2 < width`、`y2 < height`（注意是**严格小于**，
  所以贴着右/下边缘的满屏框必被拒）、`x2 > x1`、`y2 > y1`，再算面积千分比落在 `[5, 850]`。
  面积用 `int64_t` 计算避免溢出（`:49-52`）。
- `detection_iou_permille`（`:57-72`）：标准交并比，结果以千分比返回；无交集返回 0。
- `pedestrian_filter_update`（`:92-122`）是**带状态**版本：与本帧框与 `previous` 的 IoU 决定
  是"新目标"（`hit_streak = 1`，并清掉 `confirmed`，`:109-111`）还是"延续"（`hit_streak++`，`:112-114`），
  命中数 ≥ `confirm_frames` 才 `confirmed = true`（`:118-120`）。
- `register_miss`（`:74-90`）：未确认时丢帧立即清零命中；已确认时累计 `miss_streak`，
  达到 `clear_frames` 才取消确认。

**重要事实**：生产代码（`pedestrain_detect.cpp`）只用了**无状态**的
`pedestrian_detection_is_plausible` + `pedestrian_filter_default_config()`（`:115-129`），
**没有**调用 `pedestrian_filter_update`。真正的时间确认由 `vision_tracker` 完成（§4.6）。
`pedestrian_filter_update` 目前只被主机测试覆盖（`tests/pedestrian_filter_test.c:33`）。

### 4.5 `camera_motion`：作用与算法

配置（`camera_motion.c:6-15`）：`sample_step = 8`（每 8 像素采一格）、`pixel_delta = 18`
（亮度残差阈值）、`min_changed_permille = 80`（≥8% 采样点变化才算"有运动"）、
`min_valid_permille = 500`（至少 50% 采样点亮度 >2，否则认为是黑屏/未就绪帧）。
采样点上限 `CAMERA_MOTION_MAX_SAMPLES = 1200`（`camera_motion.h:10`），320×180 按 step 8 采得 40×23 = 920 点，
在限内（`:49-56`）。

算法（`camera_motion.c:38-108`）：

1. RGB565 → 亮度：`luma = (77*R + 150*G + 29*B) >> 8`（`:30-36`），R/G/B 先各自展开到 8 位。
2. 双向采样，统计 `valid`（亮度 >2 的点数）；有效比例 <50% 直接返回 `false`（`:61-74`）。
3. 首帧或采样点数变化 → 建立/重建基准，返回 `false`（`:75-86`）。
4. 计算**全局平均亮度差** `global_delta = delta_sum / count`（`:88`），
   然后在残差里减掉它：`residual = |luma - previous - global_delta|`（`:93-98`）。
   **这一步是"消除整体曝光变化"**——日光/自动曝光导致的整幅变亮不会误判为运动
   （对应 `docs/bringup/logs/2026-09-09-camera-near-field-confirmation.md:25-26`）。
5. 统计残差 ≥18 的采样点比例 `changed_permille`（`:99-104`），≥80‰ 返回 `true`（`:108`），
   同时更新基准为当前帧（`:100`，因此是相邻帧差分而非固定背景）。

**在系统中的真实作用**：**仅诊断**。`pedestrain_detect.cpp:267-278` 只把 `changed_permille`
和 `motion_confirmed` 用于打一条日志；运动结果**没有**传给 `multimodal_fusion`，
也没有出现在 `vision_evidence_t`（`multimodal_fusion.h:14-19` 只有 NONE/MOTION/PEDESTRIAN/FACE，
而 `multimodal_fusion_on_vision_evidence` 显式拒绝 MOTION，见 `multimodal_fusion.c:73-77`）。

> 与文档的差异：`docs/bringup/radar-test-matrix.md:119-124` 与
> `docs/bringup/logs/2026-09-09-camera-near-field-confirmation.md:24-27` 称"近距离相机运动作为 fallback
> 进入多模态融合"。**当前代码中该路径不存在**（运动只打日志）。
> 这一行为与 `docs/superpowers/specs/2026-09-10-hybrid-person-detection-design.md:36`（"运动只用于调度 AI，
> 不能作为'有人'的正证据"）一致，属于"文档描述滞后于实现"。

### 4.6 `vision_tracker`：跟踪、确认与去重

配置默认值（`vision_tracker.c:6-20`）：

| 参数 | 值 | 说明 |
| --- | --- | --- |
| `pedestrian_min_score` | 0.70 | 行人阈值 |
| `face_min_score` | 0.75 | 人脸阈值（人脸模型更容易误报，阈值更高） |
| `min_area_permille` | 2 | 通用最小面积 |
| `pedestrian_max_area_permille` | 850 | 行人最大面积（杀满屏框） |
| `face_max_area_permille` | 600 | 人脸最大面积 |
| `min_iou_permille` | 250 | 关联阈值（IoU ≥25%） |
| `confirm_window` | 4 | 命中历史窗口 4 帧（`vision_tracker.c:118-121` 用位掩码实现） |
| `confirm_hits` | 3 | 4 帧里 ≥3 次命中才 `confirmed` —— 即设计说的"四次中三次" |
| `clear_frames` | 3 | 连续 3 帧未匹配才清除轨迹 |

数据结构：3 个轨迹槽（`VISION_MAX_TARGETS = 3`，`vision_tracker.h:11`），每槽有
`target{track_id, kind, score, box, confirmed, last_seen_ms}` + `hit_history`（位图）+ `miss_streak`
（`vision_tracker.h:53-58`）。

`vision_tracker_update`（`vision_tracker.c:214-281`）流程：

1. 逐条检测先过 `detection_is_plausible`（`:62-95`）：
   类型必须是 PEDESTRIAN/FACE（**MOTION 永远不能确认**）、分数达类型阈值、框在画面内、面积在
   `[2, 类型上限]` 千分比内。
2. 关联：`find_matching_slot`（`:133-152`）在**同类型且未被本帧占用**的活跃槽里找 IoU 最大且 ≥25% 的槽；
   找不到就用 `find_free_slot` 开新槽（`:154-162`, `:234-240`）。取最大 IoU 保证多目标不会互相抢槽。
3. `apply_hit`（`:164-189`）：新槽分配自增 `track_id`（回绕时跳过 0，`:170-173`）；更新框与分数；
   `hit_history = (hit_history << 1) | 1` 并按窗口掩码截断（`:182-184`）；`miss_streak = 0`；
   当 `bit_count(hit_history) >= confirm_hits` 时置 `confirmed = true`（`:186-188`）。
   —— 这就是"最近 4 帧里至少 3 帧命中"的滑动窗口实现（不是"连续 3 帧"）。
4. `apply_miss`（`:191-203`）：`hit_history` 左移补 0；`miss_streak++`；
   当 `miss_streak >= clear_frames`（3）**或**（未确认且命中历史已空）时整槽清零（`:199-202`）。
   未确认的候选会更快被淘汰（`:200`），已确认目标给足 3 帧宽限。
5. 汇总输出：把所有活跃槽拷进 `update.targets`；`confirmed && kind==PEDESTRIAN` 计入
   `confirmed_person_count`（`:251-260`）。
6. **人脸去重**：对每个已确认人脸，若其中心点落在某个已确认行人框内，则不重复计数（`:261-278`），
   即"行人框包含人脸框时只算一个人"（设计 `docs/superpowers/specs/2026-09-10-hybrid-person-detection-design.md:60`）。
7. `confirmed_present = confirmed_person_count > 0`（`:279`）——这是对外唯一的存在结论。

### 4.7 `vision_event`：产出什么事件

`vision_event.c` 只做序列化，不产生事件；事件源是 `vision_snapshot_t`（`vision_event.h:18-25`）：

```json
{"type":"vision_state","timestamp_ms":65347,"state":"clear|confirmed|candidate",
 "source_width":320,"source_height":180,"person_count":0,
 "targets":[{"track_id":1,"kind":"pedestrian|face","score":0.86,
             "x":10,"y":20,"width":40,"height":50,"confirmed":true}],
 "pedestrian_model":"ready","face_model":"ready"}
```

- `state` 三态：`confirmed`（`confirmed_present`）/ `candidate`（有目标但未确认）/ `clear`（`:61-64`）。
- 分数以百分数两位整数输出，`score * 100 + 0.5` 四舍五入并夹到 ≤100（`:79-83`）。
- 逐目标做边界校验 `target_is_valid`（`:39-50`），任何一条不合法就直接放弃整个 JSON（返回 −1），
  避免发出半个数组（`:84-97`）。
- 用有界 `vsnprintf` 追加，溢出即整体作废并清空输出（`:22-37`, `:66-76`）。
- 模型状态字符串：`ready/unavailable/initializing`（`:9-20`）。

发布路径：`main.c:91-95` 格式化到 1024 字节静态缓冲 → `webrtc_http_server_publish_latest_vision`。
后者用 **单槽队列 `xQueueOverwrite`**（`webrtc_http_server.c:74-92`，容量 1024 见 `:27, :43`），
只保留最新快照、不占用语义事件队列（`webrtc_http_server.h:38-45`）。
SSE 推流任务用 `xQueuePeek` + `sequence` 比较决定是否发送（`webrtc_http_server.c:138-154`），
序列号未变化就 `vTaskDelay(100ms)` 等下一帧。
语义事件（雷达 enter/leave）走另一条 16 深、每条 256 字节的队列，满时丢最旧（`webrtc_http_server.c:25-26, 54-72`）。

浏览器侧：网页据 `vision_state` 画框、显示"行人 81% / 人脸 86%"、3 秒无更新清框
（设计 `docs/superpowers/specs/2026-09-10-hybrid-person-detection-design.md:114-138`；
实测样例见 `docs/bringup/logs/2026-09-10-hybrid-person-detection-runtime.log:45-49`）。

### 4.8 降级与自适应

`detecting_task:200-221` 以**单帧推理耗时**为指标做迟滞控制：

| 条件 | 动作 |
| --- | --- |
| 单帧耗时 > **450 ms** | `slow_streak++`，清 `recovery_streak` |
| 单帧耗时 < **350 ms** 且当前已降级 | `recovery_streak++` |
| 其它（350~450 ms） | 两条计数都清零（死区，防抖动） |
| 未降级且 `slow_streak >= 3` | 进入降级：`degraded = true`，日志"reducing detector to 1 FPS" |
| 已降级且 `recovery_streak >= 30` | 恢复：`degraded = false`，日志"restoring detector to 2 FPS" |

降级的执行点在 `filter_task:279-283`（丢奇数帧）。这与设计文档
`docs/superpowers/specs/2026-09-10-hybrid-person-detection-design.md:72` 的 450 ms/3 次、350 ms/30 次一致。

其它降级/失败处理：

- 两个模型都创建失败 → 打印 "Both vision models are unavailable" 并退出任务（`:76-79`）。
- 跟踪器初始化失败 → 删掉两个模型对象、`break`，即**整条视觉链路停用**（`:80-88`）。
  此时雷达、门锁、手动按键仍工作（符合设计 `:144`）。
- 模型单边失败：日志区分 `pedestrian=ready/unavailable`、`face=ready/unavailable`（`:92-94`），
  并照常运行另一边。

### 4.9 未接线的模块与"文档-代码"差距（重要）

1. **`pedestrian_format_probe`（`pedestrian_format_probe.c/.h`）在生产路径中未被使用**：
   `pedestrain_detect.cpp` 没有包含它，像素格式固定写死 `DL_IMAGE_PIX_TYPE_RGB565LE`（`:107`）。
   它只出现在主机测试（`tests/pedestrian_format_probe_test.c`）与测试脚本
   （`tests/verify_task_8_3_8_4.py:48`、`tests/verify_vision_runtime_safety.py:39` 甚至断言它不在检测器里）。
   因此文档 `docs/bringup/logs/2026-09-09-camera-near-field-confirmation.md:22-23`
   说的"探测四种 RGB565 字节序直到锁定"在当前代码里**没有生效对象**。
2. **`camera_motion` 的运动结论不进入融合**（见 §4.5），只打日志。
3. **`pedestrian_filter_update` 未被生产代码调用**（只用于主机测试），时间确认全部由 `vision_tracker` 完成。
4. **`doorbell_status.c/.h` 未被应用调用**：`doorbell_status_init` / `doorbell_status_apply_vision`
   在 `main/` 下没有任何调用点（只在 `tests/doorbell_status_test.c` 中被调用）。
   它是设计 `Test 4 Step 4`（`docs/superpowers/plans/2026-09-10-hybrid-person-detection.md:144-146`）
   预留的"未来显示屏接口"。
5. **`cfg.detected` 为 NULL**，导致 `webrtc.c:450 pedestrian_detected()` 与
   `main.c:70 doorbell_report_vision_detection()` 成为死代码（见 §4.3）。
6. `pedestrain_detect.cpp:181` 的 `update.targets[0]` 取第一个目标而不是"最可信/最近的目标"；
   由于该分支当前不被触发，暂不影响行为。

---

## 5. `multimodal_fusion`：把雷达与视觉合成"请求按门铃"

模块极小（82 行），但它是自动响铃的唯一判定点。

### 5.1 状态

`multimodal_fusion_t`（`multimodal_fusion.h:21-29`）：

| 字段 | 含义 |
| --- | --- |
| `radar_present` | 雷达是否已确认门前有人 |
| `radar_ring_eligible` | 本次访问是否具备响铃资格（来自 `presence_fusion.ring_requested`，即"不在冷却中"） |
| `ring_emitted_for_visit` | 本次访问是否已经响过（保证一次停留最多响一次） |
| `has_vision_detection` / `last_vision_ms` / `last_vision_evidence` | 最近的视觉证据与其时间戳 |

配置只有一个参数：`vision_valid_ms = 2000`（`multimodal_fusion.c:5-11`）——即**视觉证据有效期 2000 ms**，
与设计 `docs/superpowers/specs/2026-09-10-hybrid-person-detection-design.md:81` 一致。

### 5.2 判定规则

`request_ring_if_fused`（`multimodal_fusion.c:31-39`）——四个条件**全部**满足才返回 `true`：

```c
fusion->radar_present            &&   // 1. 雷达已确认进入门前区域
fusion->radar_ring_eligible      &&   // 2. 本次访问有响铃资格（非冷却期）
!fusion->ring_emitted_for_visit  &&   // 3. 本次访问还没响过
vision_is_fresh(fusion, now_ms)       // 4. 视觉证据在 2000 ms 内
```

其中 `vision_is_fresh`（`:24-29`）：`has_vision_detection && (now_ms - last_vision_ms) <= 2000`，
用无符号差值，抗时间回绕。

成功时置 `ring_emitted_for_visit = true`（`:37`）——**这就是"一次访问只响一次"的实现**。

### 5.3 雷达侧输入 `multimodal_fusion_on_radar`

`multimodal_fusion.c:41-61`：

- `present == false`（对应 `LEAVE_CONFIRMED`）：清空全部状态——`radar_present=false`、
  `radar_ring_eligible=false`、`ring_emitted_for_visit=false`、`has_vision_detection=false`、
  `last_vision_evidence=NONE`（`:47-54`）。这样下一次访问可以从零开始并重新响铃。
- `present == true` 且**之前不在场**：置 `radar_present = true`，
  记录本次资格 `radar_ring_eligible = ring_eligible`，并清 `ring_emitted_for_visit`（`:55-59`）。
  **注意**：资格只在"从不在场到在场"的那一次调用被记录；之后重复调用不会再更新资格。
  本工程的调用点只在进入确认时调用了一次（`main.c:201`），所以行为正确。
- 最后尝试融合（`:60`）。

### 5.4 视觉侧输入

- `multimodal_fusion_on_vision`（`:63-67`）：固定按 `PEDESTRIAN` 证据上报（旧接口，主工程未直接使用）。
- `multimodal_fusion_on_vision_evidence`（`:69-82`）：只接受 `PEDESTRIAN` 或 `FACE`，
  **显式拒绝 `MOTION`/`NONE`**（`:73-77`）——这是"运动不能作为有人正证据"的设计约束的代码落点。
  接受后刷新 `has_vision_detection`、`last_vision_evidence`、`last_vision_ms`（`:78-80`），再尝试融合。

工程内调用点：`main.c:116-117`（`doorbell_report_vision_snapshot` 里，证据来自快照中第一个
已确认目标，行人优先：`main.c:97-110`）。

### 5.5 抑制规则汇总

| 场景 | 结果 | 证据 |
| --- | --- | --- |
| 雷达有、视觉无/过期 | 不响铃 | `multimodal_fusion.c:34` |
| 视觉有、雷达无 | 不响铃 | `:33` |
| 雷达在冷却期进入 | 不响铃（`radar_ring_eligible=false`） | `:34` + `presence_fusion.c:121` |
| 本次访问已响过 | 不响铃 | `:34` |
| 视觉结果早于雷达确认超过 2000 ms | 不响铃 | `:28, :34` |
| 视觉先到、雷达 2 s 内确认 | 响铃（顺序无关） | `:55-60` |
| 雷达确认离开 | 清空，下次访问可再响 | `:47-54` |
| WebRTC 未就绪 / 正在响铃/连接/通话 | 由 `doorbell_ring_policy` 拒绝 | `doorbell_ring_policy.c:3-13` |

---

## 6. 门铃业务链路（响铃）

### 6.1 准入策略

`doorbell_ring_policy_evaluate(webrtc_ready, doorbell_idle)`（`doorbell_ring_policy.c:3-13`）：

- `!webrtc_ready` → `DOORBELL_RING_NOT_READY`
- `!doorbell_idle` → `DOORBELL_RING_BUSY`（正在响铃/连接/通话都算忙）
- 否则 → `DOORBELL_RING_ACCEPTED`

调用点 `webrtc.c:355-356`，其中 `doorbell_idle = door_bell_state == DOOR_BELL_STATE_NONE`
（门铃状态机定义 `webrtc.c:48-54`：NONE/RINGING/CONNECTING/CONNECTED/DISCONNECTING）。

### 6.2 `doorbell_request_ring`：一次响铃的完整动作

`webrtc.c:333-405`：

1. 依次拿 `webrtc_api_mutex`、`door_bell_mutex`（`:340-352`），拿不到就按 not-ready 拒绝。
2. 调策略判断（`:355-356`）。
3. 接受则进入 `RINGING` 并分配**递增的 `ring_generation`**（`:358-361`）——
   用于防止过期的停止任务误停新铃声。
4. 通过信令通道发 `"RING"` 给浏览器（`:373`，`SEND_CMD` 宏定义 `:42-43`）。
5. 发送失败 → 回滚状态并返回 `SEND_FAILED`（`:379-385`）。
6. 发送成功且仍处 `RINGING` → `play_tone(DOOR_BELL_TONE_RING)` 在板端放 `ring.aac`（`:387-391`），
   并调 `schedule_ring_stop`（`:393-395`）创建 `ring_stop` 线程。
7. `ring_stop_worker`（`:290-314`）`sleep(DOOR_BELL_RING_DURATION_MS = 4000)`（`:46, :293`）后，
   只有当 `webrtc == 目标句柄 && state == RINGING && ring_generation == 期望代次` 时才发 `"RING_STOPPED"`（`:296-309`）。
   这样"板端响 4 秒"和"浏览器停止循环"同步（设计 `docs/superpowers/specs/2026-09-07-doorbell-round-two-fixes-design.md:37-39`）。

三个触发来源共用这一个入口：

| 来源 | 位置 |
| --- | --- |
| 雷达 + 视觉融合 | `main.c:206`（`request_sensor_ring("radar+vision")`） |
| BOOT 按键 | `webrtc.c:1117-1118`（`doorbell_request_ring("boot")`，50 ms 轮询 GPIO35，`settings.h:76`） |
| 控制台 `cmd ring` | `webrtc.c:1086-1088` → `main.c:243-247` |

`play_tone` 的映射表（`webrtc.c:118-129`）：索引 0=`ring.aac`（时长 4000 ms），
1=`open.aac`（时长 0 = 只放一遍），2=`join.aac`（时长 0）。
索引由 `door_bell_tone_type_t` 决定（`webrtc.c:56-60`，`doorbell_ring_policy` 与它无关）。

### 6.3 面向浏览器的门锁状态通知

`door_lock_state_changed`（`webrtc.c:407-440`）是门锁状态回调，注册点在
`webrtc.c:1263`（`door_lock_set_state_callback(door_lock_state_changed, NULL)`，
在 `esp_webrtc_open` 成功之后、设置 media provider 之前）。
回调把门锁状态映射成信令发给浏览器：`DOOR_OPENING / DOOR_OPENED / DOOR_CLOSING / DOOR_LOCKED / DOOR_FAILED`
（`:420-438`，命令名定义 `:27-31`）。**注意**：`webrtc` 句柄为 NULL 时回调直接返回（`:414-418`），
所以未建立 WebRTC 时开门不会发信令、但门锁动作照常执行（二者解耦）。

---

## 7. 执行器一：舵机门锁 `door_lock`

### 7.1 硬件与 PWM 参数

`door_lock.h:10-15`：

| 宏 | 值 | 含义 |
| --- | --- | --- |
| `DOOR_LOCK_SERVO_GPIO` | **32** | PWM 输出引脚（WT99P4C5-S1 J6 pin 20） |
| `DOOR_LOCK_PWM_FREQUENCY_HZ` | **50** | 20 ms 周期 |
| `DOOR_LOCK_LOCKED_PULSE_US` | **500** | 0°，锁定位 |
| `DOOR_LOCK_UNLOCKED_PULSE_US` | **2500** | 180°，开锁位 |
| `DOOR_LOCK_MOTION_TIME_MS` | **500** | 每次运动的等待时间（舵机动作余量） |
| `DOOR_LOCK_HOLD_TIME_MS` | **5000** | 保持开锁时长 |

LEDC 配置（`door_lock.c:13-20`, `137-165`）：

- `duty_resolution = LEDC_TIMER_14_BIT`（16384 级），`LEDC_LOW_SPEED_MODE`，
  `LEDC_TIMER_3` + `LEDC_CHANNEL_7`，`clk_cfg = LEDC_AUTO_CLK`（`:13-18`, `137-143`）。
- 通道初始 duty 直接设为**锁定脉冲**（`:156`），保证上电即处于锁定位置。

脉冲 → 占空比换算（`door_lock.c:36-41`）：

```c
duty = (pulse_us * 16384 + 20000/2) / 20000;    // 20000 us = 50 Hz 周期，四舍五入
```

据此：500 µs → duty 410；2500 µs → duty 2048。换算不夹紧（没有对非法脉宽的保护），
但本模块只使用两个编译期常量，因此不存在越界输入。
`set_servo_pulse`（`:43-59`）先 `ledc_set_duty` 再 `ledc_update_duty`，两步都检查错误并打日志。

### 7.2 时序（一次开门）

`door_lock_task`（`door_lock.c:84-126`）是唯一执行者：

```
等待命令队列 (阻塞)                      door_lock.c:90
  ↓ 收到 DOOR_LOCK_COMMAND_OPEN
清 command_pending                      :94-96
notify_state(OPEN_REQUEST) → OPENING    :103-104
set_servo_pulse(2500us) 失败 → FAILURE  :105-108
delay 500 ms   (MOTION_TIME)            :109
notify_state(MOTION_COMPLETE) → UNLOCKED:111-112
delay 5000 ms  (HOLD_TIME)              :113
notify_state(HOLD_EXPIRED) → CLOSING    :115-116
set_servo_pulse(500us) 失败 → FAILURE   :117-120
delay 500 ms   (MOTION_TIME)            :121
notify_state(MOTION_COMPLETE) → LOCKED  :123-124
循环
```

**总时长 ≈ 500 + 5000 + 500 = 6000 ms**（从收到命令到回到 LOCKED）。

### 7.3 状态机与安全限制

状态转移表（`door_lock_state.c:3-36`）：

| 当前 | 事件 | 下一状态 |
| --- | --- | --- |
| 任意 | `FAILURE` | `FAULT`（**优先级最高**，`:6-8`） |
| `LOCKED` | `OPEN_REQUEST` | `OPENING` |
| `OPENING` | `MOTION_COMPLETE` | `UNLOCKED` |
| `UNLOCKED` | `HOLD_EXPIRED` | `CLOSING` |
| `CLOSING` | `MOTION_COMPLETE` | `LOCKED` |
| `FAULT` / 其它 | 任意 | 保持（`:31-34`） |

安全与健壮性设计：

1. **命令与执行解耦**：`door_lock_request_open` 只做参数/状态检查并把命令放进长度 1 的队列
   （`door_lock.c:167`, `201-231`），真正的 6 秒动作在独立任务里，浏览器信令回调不会被阻塞。
2. **重复点击幂等**：若 `state != LOCKED || command_pending`，直接返回 `ESP_OK` 并打日志
   "Open request is already in progress"（`:214-219`）——不会叠加多次动作、不会重置计时。
3. **FAULT 拒绝服务**：一旦进入 `FAULT`，`door_lock_request_open` 永远返回 `ESP_FAIL`（`:210-213`）；
   `door_lock_state_transition` 也规定 `FAULT` 无法自行离开（`door_lock_state.c:31-34`）。
   **没有复位/恢复路径**，只能重启设备（这是有意的失效安全，但也是运维缺口）。
4. **临界区保护**：状态、`command_pending`、`initialized`、回调指针全部在 `portENTER_CRITICAL`
   下读写（`:29, 66-70, 94-96, 205-221, 234-238`）。回调在临界区之外调用（`:72-75`），避免死锁。
5. **未初始化保护**：`!initialized || command_queue == NULL` → `ESP_ERR_INVALID_STATE`（`:206-209`）。
6. **初始化幂等**：`door_lock_init` 重复调用直接返回 `ESP_OK`（`:130-135`）。
7. **失败路径**：任务里任何 PWM 设置失败、或收到未知命令，都调 `fail_operation()`
   → `door_lock_state_transition(state, FAILURE)` → `FAULT`（`:78-82, 98-101`）。
8. **状态回调**：`door_lock_set_state_callback`（`:193-199`）允许上层（`webrtc.c:407`）把状态同步到浏览器。

文档一致性：`docs/bringup/servo-power-and-safety.md:3-13`（MG90S/GX90S，50 Hz，0/90/180° = 500/1500/2500 µs，
堵转约 1 A，GPIO32）与 `:30-37`（上电 LOCKED、开 500 ms、保持 5 s、关 500 ms、重复命令幂等、错误进 FAULT）
与代码逐条吻合。

**文档强调的局限（必须写进汇报）**：舵机**没有位置/电流反馈**（`docs/bringup/servo-power-and-safety.md:39-42`），
所以 `UNLOCKED`/`LOCKED` 只表示"命令下发 + 等待时间结束"，**不证明机械连杆到位**；
堵转检测需要限位开关/位置传感器/电流传感器。接线必须用独立 5 V 电源、只共地
（`:15-28`），首测时先空载（`:44-55`）。

---

## 8. 执行器二：语音提示

### 8.1 三个音频资产与触发点

三个 AAC 文件由 `CMakeLists.txt:14` 以 `EMBED_TXTFILES` 嵌入，符号形如 `_binary_ring_aac_start/end`。

| 资产 | 大小 | 播放函数 | 触发点 | 用途 |
| --- | --- | --- | --- | --- |
| `ring.aac` | 10835 B | `play_tone(DOOR_BELL_TONE_RING)` | `webrtc.c:387`（响铃被接受时） | 板端来电铃声，时长参数 4000 ms（`webrtc.c:121`） |
| `join.aac` | 15502 B | `play_tone(DOOR_BELL_TONE_JOIN_SUCCESS)` | `webrtc.c:1384`（WebRTC 启动成功、worker 起来之后） | 接通/加入提示音，只放一遍（时长 0，`webrtc.c:123`） |
| `open.aac` | 22422 B | 非通话：`play_tone(DOOR_BELL_TONE_OPEN_DOOR)`；通话中：`play_open_door_prompt_in_call()` | `webrtc.c:584` / `webrtc.c:580` | 开门播报 |

`play_tone` 函数体：`webrtc.c:118-129`（按 tone 类型查表，前缀越界返回 0）。
符号声明：`webrtc.c:101-106`、`media_sys.c:72-73`。
浏览器侧还会直接请求 `/webrtc/ring.aac` 自行循环播放（`webrtc_http_server.c:362-371`，
网页引用 `webrtc_test.html:486`），只用于网页铃声——**板端 `join.aac` 不提供给网页**。

AAC 播放实现：`play_music`（`media_sys.c:544-562`）创建 `music_player` 线程；
`music_play_thread`（`media_sys.c:483-542`）把 AAC 按 ADTS 帧边界喂给 `av_render`，
`music_duration > 0` 时循环播放直到时长耗尽，`== 0` 时放完一遍等 FIFO 空后退出（`:514-529`），
每帧后 `sleep(1ms)` 让出 CPU（`:536`）。`stop_music`（`:564-573`）用标志位 + 轮询等待线程退出。

### 8.2 通话中的开门播报（PCM Overlay，本项目的技术亮点）

问题：`open.aac` 与 WebRTC 下行音频共用同一个 `av_render`，直接 add stream 会顶掉正在用的 Opus 解码器，
所以早期版本"通话中开门没声音、挂断后才播"
（`docs/superpowers/specs/2026-09-07-doorbell-round-two-fixes-design.md:7, 17`）。

实现分三段：

**(1) 启动时把 `open.aac` 预先解码成 PCM**
`media_sys_buildup`（`media_sys.c:400-415`）里先调 `prepare_open_door_prompt()`
（`media_sys.c:152-249`）：

- `parse_adts_format` 手写解析 ADTS 头，得到源采样率/声道（`media_sys.c:128-150`）。
- 用 `audio_resample_open` 指定输出 **16000 Hz / 单声道 / 16 bit**（`media_sys.c:161-171`）。
- 逐 ADTS 帧解析帧长、喂给 `adec_open` 的 AAC 解码器（`:200-235`），解码帧经重采样回调
  `prompt_decoded_frame → audio_resample_write → prompt_resampled_frame → append_prompt_pcm`
  累积到可增长的 `open_door_prompt` 缓冲（`:81-126`）。
- 失败只打警告"Open-door prompt will be unavailable"（`:407-409`），不影响通话和门锁。

**(2) 用"覆盖渲染器"包住 I2S 渲染器**
`av_render_alloc_prompt_overlay`（`doorbell_audio_render.c:223-250`）返回一个
`audio_render_handle_t`，其全部 ops 转发给内部 `av_render_alloc_i2s_render`
（`:111`, `:177-209`），只在 `write` 里做混音：

- `prompt_render_open`（`:123-145`）记录当前音频格式，只有 **16 kHz 且 1/2 声道且 16 bit** 才置
  `compatible = true`（`:134-136`），否则打印"Prompt waiting: current audio is ..."。
- `prompt_render_write`（`:147-175`）每次远端 PCM 到来时：
  更新 `last_remote_write_us`；若兼容则用 `pcm_overlay_mix_mono` 把提示音**叠加**到远端 PCM 上
  （`:158-161`），播完打 "Prompt playback completed"；然后照常写下游（`:170`）。
- 音频渲染器注册点：`media_sys.c:368-369`（`av_render_alloc_prompt_overlay(&i2s_cfg, &prompt_render)`）。

**(3) 兜底输出线程**
若音频还没进入 16 kHz 兼容格式（例如通话刚建立、远端暂无数据），
`doorbell_audio_prompt_start`（`:252-286`）会启动 `prompt_fallback` 线程；
`prompt_fallback_worker`（`:38-97`）每 5 ms 检查一次：
当 `pcm_overlay_fallback_due(active, now, last_remote_write_us, 40 ms)` 为真
（即 40 ms 内没有远端音频写入，`pcm_overlay.c:125-132`）时，
用 `pcm_overlay_copy_mono`（`pcm_overlay.c:98-123`，把单声道提示复制到所有声道）
直接写下游渲染器（`:63-84`），并在播完时打 "Prompt playback completed"。

`doorbell_audio_prompt_stop`（`:288-307`）置停止标志并轮询等待线程退出；
渲染器 close/deinit 也会先停播（`:203, :217`）。

**触发判据**（`webrtc.c:574-585`）：

| `door_bell_state` | 行为 |
| --- | --- |
| `CONNECTED` | `play_open_door_prompt_in_call()`（把 PCM 混进通话） |
| `< CONNECTING`（即 NONE / RINGING） | `play_tone(DOOR_BELL_TONE_OPEN_DOOR)`（走 AAC 播放路径） |
| `CONNECTING` / `DISCONNECTING` | 两个分支都不满足，**不播任何提示**（未在代码中确认这是有意为之） |

设计依据：`docs/superpowers/specs/2026-09-07-doorbell-round-two-fixes-design.md:25-29`
（通话外保留 AAC 路径，通话中改用 PCM 叠加；混音只改送扬声器的 PCM，不动回声消除参考）。

### 8.3 关于"中文语音提示"的重要更正

**代码中不存在任何 TTS / 中文语音合成**，也不存在按语言分支的提示逻辑：

- 三个提示音都是**预录 AAC 资源**（`CMakeLists.txt:14`），代码只负责解码播放。
- 中文只出现在**浏览器界面文本**与网页/文档中（例如 `webrtc_test.html` 的中文状态区、
  `docs/submission/README.md:21` 的"中文网页状态展示"）。
- 文档明确记录开门播报的台词是**英文**："Doors open, come in please"
  （`docs/submission/01_开发日志.md:112`、`docs/bringup/webrtc-test-matrix.md:62, 84`、
  `docs/submission/04_项目使用流程演示脚本.md:124`）。

因此：**"中文语音提示"未在代码或文档中确认**；若要作为汇报点，应改为
"预录音频播报（开门台词为英文）"，或后续替换 AAC 资源以支持中文。

---

## 9. 端到端时序：人靠近 → 雷达进区 → 视觉确认 → 融合决策 → 响铃/开锁

所有时间戳都来自 `esp_timer_get_time()/1000`（单调毫秒），
雷达侧 `main.c:147`、视觉侧 `pedestrain_detect.cpp:162` 取值方式一致，因此融合的时间窗比较是可靠的。

```
t=0ms      人进入雷达区域 (x∈[-1500,1500], y∈[300,3000])
           │
           ├─ radar_uart_task 每 ~100ms 收到一帧 (2.2)
           │   └─ ld2450_stream_feed → ld2450_parse_frame (帧头/尾校验通过)
           │       └─ radar_frame_received (main.c:141)
           │           ├─ presence_fusion_process  → CLEAR→ENTERING, state_since_ms=t0
           │           │                              in_zone_count=1, 方向=APPROACHING
           │           ├─ set_vision_radar_focus(true)          (main.c:152)
           │           │   └─ detect 任务此后允许"行人+人脸"同帧运行
           │           │      (pedestrain_detect.cpp:143-144)
           │           └─ 无事件（ENTERING 不打 radar_event 外的日志）
           │
t≈0..1000ms 每帧重复：仍 ENTERING；方向/代表目标刷新；日志只在 state_changed 时打
           │   （注意：<1s 的快速掠过会在目标消失时直接 CLEAR，永不产生事件）
           │
t≈1000ms   ┌─ presence_fusion: ENTERING→PRESENT（连续 ≥1000ms）
           │  event = ENTER_CONFIRMED, ring_requested = !cooldown_active
           │
           ├─ 日志: "Radar presence event=ENTER_CONFIRMED state=PRESENT ..."
           │        (main.c:160-177)
           ├─ SSE: {"type":"radar_event","event":"enter","track_id":<slot+1>,
           │        "x_mm":..,"y_mm":..,"speed":..,"zone":"door_approach",
           │        "confidence":null,"source":"ld2450"}   (main.c:179-196)
           │        → telemetry_queue(16×256B) → SSE → 网页传感器事件列表
           │
           └─ update_multimodal_radar(true, ring_requested, now)
               └─ multimodal_fusion_on_radar: radar_present=true,
                  radar_ring_eligible=ring_requested, ring_emitted=false
                  → 此刻若 2000ms 内已有视觉证据 ⇒ 立即响铃 (见下 B 路径)
                  → 否则返回 false，等视觉侧补上

（视觉侧并行，独立 2 FPS 节奏）
t=0..N      detect_filter 每 500ms 拿一帧 320×180 RGB565 (4.2)
            ├─ esp_cache_msync(M2C)          (pedestrain_detect.cpp:258)
            ├─ camera_motion_update_rgb565   → 只打日志 (4.5)
            └─ 帧拷入 data_queue（单帧缓冲，满则阻塞）
                │
                └─ detect 任务（优先级 8，低于音视频）:
                    ├─ 行人模型 run(img)                        (:114)
                    ├─ pedestrian_detection_is_plausible 过滤
                    │   （分数≥0.70、面积 5‰~850‰、框在画面内） (:125-129)
                    ├─ 无合理行人 或 radar_focus ⇒ 再跑人脸模型   (:143-144)
                    ├─ vision_tracker_update:
                    │     4 帧窗口内 ≥3 帧命中 ⇒ target.confirmed=true
                    │     (vision_tracker.c:182-188)
                    │     连续 3 帧丢失 ⇒ 清槽 (vision_tracker.c:191-203)
                    │     人脸落在行人框内 ⇒ 只算一个人 (:261-278)
                    ├─ 组装 vision_snapshot_t → cfg.snapshot
                    └─ 若 confirmed_present 发生跳变 ⇒ 打日志 (:192-198)

            cfg.snapshot = vision_snapshot_received (webrtc.c:469-474)
              └─ doorbell_report_vision_snapshot (main.c:86)
                  ├─ vision_snapshot_format_json → publish_latest_vision
                  │    → 单槽队列 xQueueOverwrite → SSE
                  │      {"type":"vision_state",...} (4.7)
                  └─ 取"已确认目标"证据（行人优先）(main.c:97-110)
                      └─ multimodal_fusion_on_vision_evidence(evidence, ts)
                          → has_vision_detection=true, last_vision_ms=ts

A) 视觉先到（视觉确认 t_v，雷达确认 t_r，t_r - t_v ≤ 2000ms）：
   t_r 时 multimodal_fusion_on_radar → request_ring_if_fused
        条件: radar_present ✓, radar_ring_eligible ✓, !emitted ✓, vision fresh ✓
   ⇒ 返回 true ⇒ main.c:205-207 request_sensor_ring("radar+vision")

B) 雷达先到（t_r 时视觉还没确认，融合返回 false）：
   稍后 t_v 时 multimodal_fusion_on_vision_evidence → request_ring_if_fused
        条件同上（radar_present 已为 true）⇒ 返回 true
   ⇒ main.c:125-128 request_sensor_ring("radar+vision")

C) 视觉证据超过 2000ms 未刷新 ⇒ 不响铃，等下一次视觉确认

响铃执行（request_sensor_ring → doorbell_request_ring("radar+vision"), main.c:46-53）
  ├─ 取 webrtc_api_mutex + door_bell_mutex
  ├─ doorbell_ring_policy_evaluate(webrtc != NULL, state == NONE)   (webrtc.c:355)
  │    ├─ WebRTC 未启动 ⇒ NOT_READY，只打警告，不死锁不崩溃
  │    └─ 正在响铃/连接/通话 ⇒ BUSY，抑制（防止 AI 打断通话）
  ├─ 接受 ⇒ state = RINGING，ring_generation++
  ├─ SEND_CMD("RING") → 浏览器开始响铃 + 弹来电界面      (webrtc.c:373)
  ├─ play_tone(DOOR_BELL_TONE_RING) → 板端 ring.aac 循环 4s (webrtc.c:387)
  └─ schedule_ring_stop：4s 后发 "RING_STOPPED"（代次校验）(webrtc.c:293-309)

用户接听 ⇒ 浏览器发 "ACCEPT_CALL" (webrtc.c:586-600) ⇒ CONNECTING ⇒ 使能 peer connection
          ⇒ 事件回调置 CONNECTED ⇒ 视频/双向音频建立

用户点"开门" ⇒ 浏览器发 "OPEN_DOOR" ⇒ door_bell_on_cmd (webrtc.c:557-585)
  ├─ door_lock_request_open()  (仅入队，立即返回)
  │    └─ door_lock 任务：OPENING(2500µs, 500ms) → UNLOCKED(保持 5000ms)
  │                       → CLOSING(500µs, 500ms) → LOCKED     (7.2)
  │       每步回调 door_lock_state_changed → 信令 DOOR_OPENING/OPENED/CLOSING/LOCKED
  └─ 语音：state == CONNECTED ⇒ PCM overlay 混入通话播 open.aac (8.2)
           state < CONNECTING ⇒ 直接播 open.aac

人离开 ⇒ 雷达 EXITING（无目标 ≥2000ms）⇒ LEAVE_CONFIRMED
  ├─ SSE: {"event":"leave",...}
  └─ multimodal_fusion_on_radar(false,...) ⇒ 清 radar_present/资格/已响标记/视觉证据
      ⇒ 开始 10s 冷却（presence_fusion.c:137-138）
      ⇒ 冷却内再进入：仍报 ENTER_CONFIRMED，但 ring_requested=false ⇒ 不响铃
```

**一次完整"靠近→响铃"的最短时间**：雷达 1 s 去抖 + 视觉 4 帧窗口内 3 次命中
（2 FPS 下最快约 3 帧 ≈ 1.0~1.5 s，但要与采集/推理节奏对齐）⇒ 实测量级约 2.5 s
（设计验收目标 `docs/superpowers/specs/2026-09-10-hybrid-person-detection-design.md:181-182`）。

---

## 10. 关键常量速查表

| 常量 | 值 | 位置 |
| --- | --- | --- |
| LD2450 波特率 / RX / TX | 256000 / GPIO4 / GPIO5 | `radar_ld2450.h:22-24` |
| LD2450 帧长 / 目标数 | 30 B / 3 | `radar_ld2450.h:11-17` |
| 雷达离线告警 | 2000 ms | `radar_ld2450.c:143` |
| UART 读超时 / 缓冲 / 栈 / 优先级 | 100 ms / 2048 / 4096 / 5 | `radar_ld2450.c:139-142, 178-179` |
| 关注区 X / Y | ±1500 mm / 300~3000 mm | `presence_fusion.c:8-9` |
| 进入 / 离开确认 | 1000 ms / 2000 ms | `presence_fusion.c:10-11` |
| 冷却 | 10000 ms | `presence_fusion.c:11` |
| 方向死区 | ±10 cm/s | `presence_fusion.c:12` |
| 视觉分辨率 / 频率 | 320×180 RGB565 / 2 FPS | `settings.h:42-44` |
| 视觉有效期（融合） | 2000 ms | `multimodal_fusion.c:8` |
| 行人分数 / 面积 | 0.70 / 5‰~850‰ | `pedestrian_filter.c:10-12` |
| 人脸分数 / 面积 | 0.75 / 2‰~600‰ | `vision_tracker.c:10-13` |
| 跟踪确认 / 清除 | 4 帧窗口内 3 命中 / 连续 3 帧丢失 | `vision_tracker.c:15-17` |
| IoU 关联阈值 | 250‰（25%） | `vision_tracker.c:14` |
| 运动采样步长 / 变化阈值 | 8 px / 80‰ | `camera_motion.c:9-11` |
| 降级门限 | >450 ms×3 降 1 FPS；<350 ms×30 恢复 | `pedestrain_detect.cpp:201-221` |
| 响铃时长 / 信令 | 4000 ms / `"RING"`→`"RING_STOPPED"` | `webrtc.c:46, 32-33` |
| 舵机 GPIO / 频率 | 32 / 50 Hz | `door_lock.h:10-11` |
| 锁定 / 开锁脉宽 | 500 µs / 2500 µs | `door_lock.h:12-13` |
| 运动等待 / 开锁保持 | 500 ms / 5000 ms | `door_lock.h:14-15` |
| LEDC 分辨率 / 定时器 / 通道 | 14 bit / TIMER_3 / CHANNEL_7 | `door_lock.c:14-18` |
| 播报采样率 / 兜底静音阈值 | 16000 Hz / 40 ms | `doorbell_audio_render.c:13-14` |
| SSE 队列 | 语义 16×256 B；视觉 1×1024 B | `webrtc_http_server.c:25-27` |

---

## 11. 已知缺口、风险与"文档-代码"不一致清单

按"汇报时可能被追问"的优先级排列：

1. **运动检测不参与融合**（代码事实）vs 文档称"近场运动 fallback 进入融合"
   （`docs/bringup/radar-test-matrix.md:119-124`）。
   运动结果仅打日志（`pedestrain_detect.cpp:267-278`），且 `multimodal_fusion` 显式拒绝 MOTION
   （`multimodal_fusion.c:73-77`）。汇报时建议表述为"运动只用于诊断与调度，不作为'有人'证据"。
2. **`pedestrian_format_probe` 未接入生产路径**，RGB565 字节序写死为 `RGB565LE`
   （`pedestrain_detect.cpp:107`）。若硬件/驱动字节序改变，需要手工改这一行。
3. **`cfg.detected` 为 NULL**（`webrtc.c:1392-1395`），使 `pedestrian_detected()`、
   `PEDESTRIAN_DETECTED` 信令、`doorbell_report_vision_detection()` 成为死代码；
   融合只走 snapshot 路径。
4. **`doorbell_status` 未接线**（只在主机测试中使用），显示屏接口尚未落地。
5. **AI 队列"阻塞"而非"丢帧"**：`data_queue_get_buffer` 会等待消费者释放
   （`components/media_lib_utils/src/data_queue.c:234-254`），队列容量实际只够 1 帧
   （`pedestrain_detect.cpp:322`），与设计文档"丢弃最旧检测帧"（设计 `:70-71`）表述不同。
6. **`filter_task` / `detecting_task` 一旦退出不会自愈**：`filter_task` 在 acquire 失败、cache 同步失败、
   队列返回 NULL 时 `break` 退出（`pedestrain_detect.cpp:255-256, 260-265, 295-298`），
   `detecting_task` 在模型全失败或 tracker 初始化失败时退出（`:76-88`）。
   只有 `stop_pedestrian_detection` + `start_pedestrian_detection`（下次 `start_webrtc`）才会重建。
7. **`detecting_task:80-88` 的失败处理偏严**：tracker 初始化失败会连带删掉两个已就绪的模型对象并退出，
   而不是保留模型、只关跟踪。
8. **门锁 FAULT 无恢复路径**（`door_lock.c:210-213`, `door_lock_state.c:31-34`），
   只能重启；且无位置/电流反馈，`UNLOCKED` 不代表机械到位（`docs/bringup/servo-power-and-safety.md:39-42`）。
9. **`CONNECTING` / `DISCONNECTING` 状态下开门不播提示音**（`webrtc.c:579-585` 的条件覆盖不全）。
10. **雷达解析无额外合理性校验**（无 CRC、不校验量程），只靠帧头/尾/长度；数据区出现 `0xAA`
    理论上可能造成一次误同步（会丢弃一帧）。
11. **"中文语音提示"不成立**：提示音是预录 AAC，文档记录开门台词为英文
    （`docs/submission/01_开发日志.md:112`）。中文只存在于网页 UI。
12. **`pedestrian_filter_update`（带状态确认）未被生产代码使用**，同一套"确认/清除"概念
    在 `pedestrian_filter` 与 `vision_tracker` 中各实现了一遍（存在重复概念的维护成本）。
13. **实机量化验收未完成**：`docs/bringup/radar-test-matrix.md:83-88, 134-136` 中
    "快速掠过""持续进入响铃一次""2 s 内丢帧不重复响铃""冷却后重访""30 分钟误触发计数""20 次进出统计"
    等行仍为 DEFERRED / PENDING MANUAL。

---

## 12. 明确"未在代码中确认"的条目

- 各 media_lib 默认线程的栈大小与优先级（只有 `detect`/`detect_filter` 被 `main.c:361-395` 显式设置）。
- `esp_capture_stream_frame_t` 的精确大小，以及采集侧在 AI 阻塞时的丢帧/覆盖计数（未在代码中确认）。
- `data`（数据通道）线程的循环周期与行为细节。
- `CONNECTING` / `DISCONNECTING` 期间不播开门提示是否为有意设计。
- 三个 AAC 文件的实际语音内容与语言（只能确认是预录 AAC + 文档记载的英文台词，
  未做音频内容解码分析）。
- 雷达 `distance_resolution_mm` 字段仅被解析与单测校验（`radar_ld2450.c:67`、
  `tests/radar_ld2450_parser_test.c:59`），**生产逻辑中未使用**。
