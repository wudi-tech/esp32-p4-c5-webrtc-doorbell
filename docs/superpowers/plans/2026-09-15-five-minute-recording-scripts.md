# 五分钟录音脚本实施计划

> **针对自主工作者:** REQUIRED SUB-SKILL: 使用超能力:子代理驱动开发 (推荐) 或超能力:执行计划以逐任务实施该计划. 步骤使用复选框 (`- []`) 跟踪的语法。

**Goal:** 生成两份不超过五分钟、可逐句照读照拍的项目演示和代码讲解脚本，并同步到最终提交包。

**Architecture:** 以源码仓库 `docs/submission` 中的脚本作为单一来源。项目演示脚本按完整使用闭环组织；代码脚本按工程地图和五条调用链组织，并使用当前源码的函数名及行号。完成后同步到提交目录与源码快照，更新源码 ZIP 和 SHA-256 清单。

**技术栈：** Markdown、ESP-IDF C/C++/HTML 源码索引、PowerShell、Git、ZIP、SHA-256

---

### 任务 1：核对代码讲解定位点

**文件:**
- 阅读: `solutions/doorbell_local/main/CMakeLists.txt`
- 阅读: `solutions/doorbell_local/main/main.c`
- 阅读: `solutions/doorbell_local/main/webrtc.c`
- 阅读: `solutions/doorbell_local/main/webrtc_http_server.c`
- 阅读: `solutions/doorbell_local/main/door_lock.c`
- 阅读: `solutions/doorbell_local/main/doorbell_audio_render.c`
- 阅读: `solutions/doorbell_local/main/radar_ld2450.c`
- 阅读: `solutions/doorbell_local/main/presence_fusion.c`
- 阅读: `solutions/doorbell_local/main/multimodal_fusion.c`
- 阅读: `solutions/doorbell_local/main/pedestrain_detect.cpp`
- 阅读: `solutions/doorbell_local/main/vision_tracker.c`
- 阅读: `solutions/doorbell_local/main/webrtc_test.html`

- [ ] **Step 1: 提取关键函数的当前行号**

运行：

```powershell
$main = 'solutions/doorbell_local/main'
rg -n 'app_main|thread_scheduler|radar_frame_received|request_sensor_ring' "$main/main.c"
rg -n 'doorbell_request_ring|door_lock_request_open|start_webrtc|stop_webrtc' "$main/webrtc.c"
rg -n 'ld2450_parse_frame|radar_ld2450_init' "$main/radar_ld2450.c"
rg -n 'presence_fusion_process|multimodal_fusion_on_radar|multimodal_fusion_on_vision' `
  "$main/presence_fusion.c" "$main/multimodal_fusion.c"
rg -n 'start_pedestrian_detection|vision_tracker_update' `
  "$main/pedestrain_detect.cpp" "$main/vision_tracker.c"
```

Expected: 每个讲解入口至少命中一次，且不读取 `settings.local.h`。

- [ ] **Step 2: 确认网页和音频定位点**

运行：

```powershell
$main = 'solutions/doorbell_local/main'
rg -n 'EventSource|RTCPeerConnection|openDoorButton|hangupButton' "$main/webrtc_test.html"
rg -n 'prompt_render_write|pcm_overlay_mix_mono' `
  "$main/doorbell_audio_render.c" "$main/pcm_overlay.c"
```

Expected: SSE、PeerConnection、开门、挂断和 PCM 混音入口均存在。

### 任务 2：编写五分钟项目流程演示脚本

**文件:**
- Create: `docs/submission/04_项目使用流程演示脚本.md`

- [ ] **Step 1: 写入录制准备和画面布局**

脚本开头固定写明：1920×1080 横屏、30 FPS、电脑录屏为主画面、手机硬件画面位于右下角、耳机避免回授、关闭通知、舵机标注“模拟门锁”、画面不得出现 Wi-Fi 密码。

- [ ] **Step 2: 写入逐句分镜表**

按照以下时间段写入完整台词、电脑画面、手机画面、操作、成功标志和补拍方法：

```text
0:00～0:25 作品与硬件
0:25～0:50 上电与中文页面
0:50～1:25 雷达与摄像头检测
1:25～1:55 BOOT 来电
1:55～2:50 接听和双向音视频
2:50～3:35 通话中开门
3:35～4:05 挂断与再次来电
4:05～4:30 总结
```

- [ ] **Step 3: 增加失败重拍与验收清单**

必须覆盖：检测框暂未出现、来电未触发、浏览器麦克风未授权、音频回授、舵机动作未入镜和接通超过预定时间时的处理方式。明确不通过剪辑伪造一次不存在的连续成功操作。

### 任务 3：编写五分钟代码录屏讲解脚本与代码地图

**文件:**
- Create: `docs/submission/03_代码录屏讲解脚本.md`

- [ ] **Step 1: 写入录制准备和标签页顺序**

列出应预先打开的所有文件、编辑器字号、函数搜索词、终端测试命令和隐私要求。明确禁止打开 `settings.local.h`。

- [ ] **Step 2: 写入逐句代码讲解表**

按照以下时间段，为每句话标明文件、当前行号、函数/常量、选中范围、功能、上游调用、下游调用和画面停留时间：

```text
0:00～0:25 工程地图
0:25～0:55 板级配置
0:55～1:30 app_main 启动链
1:30～2:15 WebRTC 与门铃状态
2:15～2:50 舵机与 PCM Overlay
2:50～3:35 雷达与多模态融合
3:35～4:20 视觉检测链
4:20～4:40 中文网页
4:40～4:50 自动检查与总结
```

- [ ] **Step 3: 增加详细代码结构附录**

附录按构建配置、入口、WebRTC、状态、音频、舵机、雷达、视觉、融合、网页和测试十一层说明职责与依赖，并给出以下调用链：

```text
app_main → start_webrtc → doorbell_webrtc_init
BOOT GPIO → doorbell_request_ring → SEND_CMD(RING) → SSE → 浏览器来电
浏览器 POST → webrtc_http_server → PeerConnection → 音视频通道
open door → door_lock_request_open → 舵机状态机 + audio prompt → PCM overlay
LD2450/摄像头 → presence/vision filter → multimodal_fusion → request_sensor_ring
```

- [ ] **Step 4: 增加超时删减顺序**

超过五分钟时依次删减：组件文件逐个念名、次要常量、测试名称、未来扩展；不得删去官方来源、主要新增功能、开门语音原理和多模态调用链。

### 任务 4：验证并提交脚本源文件

**文件:**
- Test: `docs/submission/03_代码录屏讲解脚本.md`
- Test: `docs/submission/04_项目使用流程演示脚本.md`

- [ ] **Step 1: 检查脚本结构和时长**

运行：

```powershell
rg -n '完整台词|打开文件|定位位置|功能说明|调用关系|代码结构|调用链|4:50' `
  docs/submission/03_代码录屏讲解脚本.md
rg -n '完整台词|电脑画面|手机画面|操作动作|成功标志|补拍|4:30' `
  docs/submission/04_项目使用流程演示脚本.md
```

Expected: 两份脚本均覆盖固定字段，并分别在 4:50 和 4:30 内结束。

- [ ] **Step 2: 检查源码定位仍然存在**

运行：

```powershell
$script = Get-Content docs/submission/03_代码录屏讲解脚本.md -Raw
$required = @('app_main','doorbell_request_ring','door_lock_request_open',
  'ld2450_parse_frame','presence_fusion_process','multimodal_fusion_on_radar',
  'start_pedestrian_detection','vision_tracker_update','EventSource','RTCPeerConnection')
$missing = @($required | Where-Object { -not $script.Contains($_) })
"MISSING_CODE_ANCHORS=$($missing.Count)"
```

预期: `MISSING_CODE_ANCHORS=0`。

- [ ] **Step 3: 检查占位符和隐私**

运行：

```powershell
rg -n 'TODO|TBD|xxx|YOUR_WIFI_PASSWORD' `
  docs/submission/03_代码录屏讲解脚本.md `
  docs/submission/04_项目使用流程演示脚本.md
```

Expected: 无输出，退出码 1。

- [ ] **Step 4: 提交脚本源文件**

运行：

```powershell
git add docs/submission/03_代码录屏讲解脚本.md `
        docs/submission/04_项目使用流程演示脚本.md
git commit -m "docs: add detailed five-minute recording scripts"
```

Expected: 只提交两份脚本源文件。

### 任务 5：同步提交包并更新校验值

**文件:**
- Modify: `output/submission/ESP32-P4-C5_WebRTC_Doorbell_2026-09-14/05_代码录屏讲解_待录制/01_代码录屏讲解脚本.md`
- Modify: `output/submission/ESP32-P4-C5_WebRTC_Doorbell_2026-09-14/06_项目使用流程演示_待录制/02_项目使用流程演示脚本.md`
- Create: `output/submission/ESP32-P4-C5_WebRTC_Doorbell_2026-09-14/02_项目完整源码/docs/submission/03_代码录屏讲解脚本.md`
- Create: `output/submission/ESP32-P4-C5_WebRTC_Doorbell_2026-09-14/02_项目完整源码/docs/submission/04_项目使用流程演示脚本.md`
- Rebuild: `output/submission/ESP32-P4-C5_WebRTC_Doorbell_2026-09-14/02_项目完整源码_WT99P4C5_WebRTC_Doorbell.zip`
- 修改: `output/submission/ESP32-P4-C5_WebRTC_Doorbell_2026-09-14/SHA256SUMS.md`

- [ ] **Step 1: 同步脚本到最终目录和源码快照**

使用 `Copy-Item -LiteralPath` 覆盖两个最终脚本，并复制两份源码脚本到源码快照。不得删除、覆盖或移动任何 MP4 文件。

- [ ] **Step 2: 更新源码 ZIP**

运行：

```powershell
Compress-Archive -LiteralPath .\02_项目完整源码 `
  -DestinationPath .\02_项目完整源码_WT99P4C5_WebRTC_Doorbell.zip `
  -CompressionLevel Optimal -Update
```

Expected: ZIP 中 `docs/submission` 包含两份新脚本，且无重复路径。

- [ ] **Step 3: 更新 SHA-256 清单**

重新计算源码 ZIP、两份脚本和已有提交文档的 SHA-256 与字节数，再更新 `SHA256SUMS.md`。

- [ ] **Step 4: 最终完整性和隐私检查**

预期：

```text
PRIVATE_DATA_HITS=0
FORBIDDEN_LOCAL_SETTINGS_FILES=0
ZIP_PRIVATE_OR_FORBIDDEN_HITS=0
ZIP_CODE_SCRIPT_COUNT=1
ZIP_DEMO_SCRIPT_COUNT=1
MP4_FILES_PRESERVED=true
```
