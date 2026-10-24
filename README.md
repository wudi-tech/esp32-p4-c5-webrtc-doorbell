# 基于 ESP32-P4/C5 的多模态 WebRTC 智能门铃

基于 WT99P4C5-S1 的嵌入式音视频与多模态感知项目。在 Espressif `esp-webrtc-solution` 的本地门铃例程上完成板级适配，接入 OV5647 摄像头、ES8311 音频、HLK-LD2450 毫米波雷达和 PWM 舵机，通过浏览器完成来电、接听、视频查看、双向语音、模拟开门和状态显示。

开发者：吴迪 · 开发记录：2026.07—2026.09 · 本次归档：2026-10-24

**项目定位：局域网、单浏览器客户端的原型系统。视觉判断人员/人脸是否存在；舵机模拟门锁。**

[项目演示视频](https://github.com/wudi-tech/esp32-p4-c5-webrtc-doorbell/releases/download/archive-2026-10-24/doorbell-demo.mp4) · [代码讲解视频](https://github.com/wudi-tech/esp32-p4-c5-webrtc-doorbell/releases/download/archive-2026-10-24/code-walkthrough.mp4) · [固件与下载附件](https://github.com/wudi-tech/esp32-p4-c5-webrtc-doorbell/releases/tag/archive-2026-10-24) · [完整资料导航](docs/MATERIALS.md)

## 主要能力

| 模块 | 具体内容 | 代码入口 |
| --- | --- | --- |
| 嵌入式音视频 | OV5647 视频采集与 H.264 视频链路、ES8311 双向语音、AAC 提示音资源 | [`media_sys.c`](solutions/doorbell_local/main/media_sys.c)、[`webrtc.c`](solutions/doorbell_local/main/webrtc.c) |
| 本地通信 | 设备提供 HTTPS 页面，通过 SSE / POST 交换信令，建立 WebRTC 媒体连接 | [`webrtc_http_server.c`](solutions/doorbell_local/main/webrtc_http_server.c) |
| 来电与恢复 | BOOT 来电、网页接听/拒绝/挂断、刷新会话替换、媒体资源释放和重复来电抑制 | [`webrtc.c`](solutions/doorbell_local/main/webrtc.c)、[`doorbell_ring_policy.c`](solutions/doorbell_local/main/doorbell_ring_policy.c) |
| 毫米波感知 | LD2450 UART 帧解析、最多三个目标的二维坐标、门前区域与进出状态去抖 | [`radar_ld2450.c`](solutions/doorbell_local/main/radar_ld2450.c)、[`presence_fusion.c`](solutions/doorbell_local/main/presence_fusion.c) |
| 视觉存在检测 | ESP-DL 行人/人脸存在模型、候选框过滤、连续帧确认、目标关联及视觉事件 | [`pedestrain_detect.cpp`](solutions/doorbell_local/main/pedestrain_detect.cpp)、[`vision_tracker.c`](solutions/doorbell_local/main/vision_tracker.c) |
| 多模态触发 | 汇合雷达与视觉证据，判断来访并限制同一次来访重复响铃 | [`multimodal_fusion.c`](solutions/doorbell_local/main/multimodal_fusion.c) |
| 舵机执行 | 异步动作队列、GPIO32 / 50 Hz PWM、开启后保持与复位 | [`door_lock.c`](solutions/doorbell_local/main/door_lock.c) |
| 中文网页 | 呼叫交互、视频检测框、雷达目标文字和事件显示 | [`webrtc_test.html`](solutions/doorbell_local/main/webrtc_test.html) |
| 结构设计 | 亚克力背板几何生成、孔位表、正反面装配图、DXF 和四页工程图 | [`cad/acrylic_plate`](cad/acrylic_plate/)、[`加工文件`](output/acrylic/) |

历史开发日志记载：取消局域网中不需要的公网 STUN 等待、减少浏览器额外媒体采集后，三轮呼叫建立约为 **4 秒、6 秒、4 秒**，初版约需 30 秒。这是当时的小样本实测记录，本次归档未重新测量，也不作为长期稳定性或公网性能结论。见[开发日志](docs/submission/01_开发日志.md)。

## 系统组成

```text
OV5647 摄像头 ── 视频采集/编码 ─────────────────────┐
ES8311 麦克风/扬声器 ── 音频采集/播放 ─────────────┤ WebRTC
                                                  ├──────── 浏览器
ESP32-C5 ── SDIO / Wi-Fi ── ESP32-P4 ── HTTPS/SSE/POST ┘
                              │
LD2450 ── UART ── 区域/存在状态 ─┤
摄像头检测支路 ── 过滤/追踪 ─────┼── 多模态触发 ── 来电策略
                              │
网页开门命令 ── 执行队列 ── GPIO32 PWM ── 舵机模拟门锁
```

ESP32-P4 负责媒体、推理、信令和业务；板载 ESP32-C5 提供 Wi-Fi。本项目保留开发板配套 C5 固件，发布附件只包含 P4 的启动程序、分区表和应用。

## 硬件与软件

| 项目 | 配置 |
| --- | --- |
| 主板 | WT99P4C5-S1 1V1；实机记录为 P4 rev 1.3、16 MB Flash、32 MB PSRAM |
| 摄像头 / 音频 | OV5647 MIPI-CSI；ES8311、板载麦克风、4 Ω / 3 W 扬声器 |
| 感知 / 执行 | HLK-LD2450；MG90S/GX90S 类舵机模拟门锁 |
| 视频 / 检测 | 640×480 / 15 FPS；检测支路 320×180 / 2 FPS，均为配置值 |
| 开发环境 | ESP-IDF v5.5.5；历史构建使用 Python 3.11、CMake、Ninja |
| 关键传输依赖 | `esp_hosted 2.12.12`、`esp_wifi_remote 1.6.3` 在组件清单中精确固定 |

其余依赖由 `idf_component.yml` 的版本范围解析；仓库未带 `dependencies.lock`，不能保证所有传递依赖与历史环境逐项相同。接线、供电和环境配置见[使用指南](docs/submission/02_使用指南.md)。

## 从源码运行

在已初始化的 ESP-IDF v5.5.5 PowerShell 中执行：

```powershell
git clone https://github.com/wudi-tech/esp32-p4-c5-webrtc-doorbell.git
Set-Location esp32-p4-c5-webrtc-doorbell
Copy-Item solutions/doorbell_local/main/settings.local.h.example `
          solutions/doorbell_local/main/settings.local.h
```

编辑新建的 `settings.local.h`，填写自己的 2.4 GHz Wi-Fi；该文件已被忽略。然后执行：

```powershell
idf.py -C solutions/doorbell_local set-target esp32p4
idf.py -C solutions/doorbell_local build
idf.py -C solutions/doorbell_local -p COM5 flash monitor
```

把 `COM5` 改为实际串口。电脑与设备接入同一局域网后，打开串口打印的 `https://设备IP/webrtc/test`，按 BOOT 发起来电并在网页接听。首次访问自签名证书需要在浏览器中确认继续访问。

已编译的历史公开固件见 [`firmware/2026-09-14`](firmware/2026-09-14/) 和 [Release](https://github.com/wudi-tech/esp32-p4-c5-webrtc-doorbell/releases/tag/archive-2026-10-24)。其与当前快照的逐字节构建对应关系未在本次重建验证。

## 代码与资料导航

| 内容 | 位置 |
| --- | --- |
| 项目应用 / 共用媒体网络层 | [`solutions/doorbell_local`](solutions/doorbell_local/)、[`solutions/common`](solutions/common/) |
| WebRTC、媒体渲染、板级和预编译 Peer 库 | [`components`](components/) |
| 模块职责与业务链路 | [架构说明](docs/ARCHITECTURE.md)、[从零讲清代码结构](docs/submission/10_代码结构从0讲清.md) |
| 开发日志、使用指南、录屏脚本、源码审计 | [`docs/submission`](docs/submission/) |
| 调试记录与原始日志 | [`docs/bringup`](docs/bringup/)；部分网络标识已脱敏 |
| 亚克力设计与 V1/V2/V3 加工资料 | [`cad`](cad/)、[`output/acrylic`](output/acrylic/)、[`output/pdf`](output/pdf/) |
| 原理图、尺寸图、主板指南、屏幕规格书 | [`hardware/reference`](hardware/reference/) |
| 当前限制与文档差异 | [项目状态](docs/PROJECT_STATUS.md) |
| 本次检查及复跑命令 | [验证记录](docs/VALIDATION.md) |
| 来源、去重与归档范围 | [整理说明](docs/PROVENANCE.md)、[487 文件来源清单](docs/source-snapshot-manifest.csv) |

其他上游示例也整体保留在 `solutions/`，便于学习与依赖追溯；它们不代表本项目分别完成了公网门铃、云服务和全部开发板适配。

## 验证与已知限制

2026-10-24 本次整理复跑：**14/14 个门铃检查入口通过，17/17 个结构设计测试通过**。这些是主机侧 C 测试、静态检查和几何/文件生成检查；本次未重新构建 ESP-IDF 固件、烧录设备或执行实机验收。

- 默认 G.711A 通话为 8 kHz，开门提示音混音路径要求 16 kHz；通话中提示音是否输出存在兼容问题，保留为待修复项。
- `doorbell_status` 模块虽参与编译且有测试，未接入生产调用链；实际网页状态来自现有事件路径。不能用测试通过推定该模块已经集成。
- 雷达界面为目标/距离文字与事件；当前网页没有雷达俯视轨迹图。
- HTTPS 使用上游公开的演示证书及密钥；开门控制没有用户鉴权，适用范围为可信局域网演示。
- 未实现人脸身份识别、真实锁体与锁舌反馈、配套屏幕交互；长期运行、误触发率和负载能力未在本次量化。

## 来源与许可

上游：[Espressif esp-webrtc-solution](https://github.com/espressif/esp-webrtc-solution)。本项目贡献集中在 WT99P4C5-S1 适配、感知与舵机扩展、中文交互、状态与异常恢复、系统集成及项目资料整理；上游 WebRTC / 媒体基础和第三方库的权利声明予以保留。

各组件与文件按原有许可证使用。硬件参考文档保留原厂商权利；本次公开归档未新增覆盖全部代码与资料的统一许可证，详见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
