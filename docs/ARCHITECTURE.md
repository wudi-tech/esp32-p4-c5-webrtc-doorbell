# 架构与代码阅读路线

本说明面向本次归档源码。主应用是 `solutions/doorbell_local`，上游媒体与 WebRTC 组件整体保留。更详细的历史逐行讲解位于 [代码手册](submission/10_代码结构从0讲清.md)；涉及现状的判断以 [项目状态](PROJECT_STATUS.md) 和实际代码为准。

## 启动与通信

`main/main.c` 的 `app_main()` 负责板级、网络及业务装配。`board.c` 结合 `components/codec_board/board_cfg.txt` 初始化 WT99P4C5-S1，`solutions/common/network.c` 处理 Wi-Fi 与 NVS 配置。网络就绪后启动媒体系统、HTTPS 信令与 WebRTC；雷达和视觉事件汇合到门铃业务。

设备向浏览器提供 `webrtc_test.html`，SSE 发送设备事件，HTTP POST 接收网页命令与信令；WebRTC 承载音视频。视频主要从板端发往浏览器，音频双向传输。SSE、POST、WebRTC 是不同职责的路径，不能将网页状态更新等同于视觉算法。

## 应用模块

以下路径均相对于 `solutions/doorbell_local/main/`。

| 文件或文件组 | 职责与关联 |
| --- | --- |
| `main.c`、`common.h` | 系统装配、网络回调、事件汇合与公共接口 |
| `board.c` | 音频 Codec、功放与板级外设初始化 |
| `media_sys.c` | 摄像头与音频采集/播放、媒体流和提示音资源 |
| `webrtc.c` | 通话生命周期、按键来电、接听/挂断、媒体与检测任务的连接 |
| `webrtc_http_server.c/.h` | HTTPS、SSE、POST、网页会话与刷新替换 |
| `webrtc_test.html` | 中文界面、浏览器媒体、呼叫交互、检测框及雷达文字 |
| `doorbell_ring_policy.c/.h` | 来电状态组合与重复触发策略 |
| `doorbell_event.c/.h` | 雷达/门铃事件 JSON 序列化 |
| `radar_ld2450.c/.h` | UART 字节流、30 字节协议帧、重同步、最多三目标解析 |
| `presence_fusion.c/.h` | 门前区域、进入/离开去抖、冷却与方向 |
| `pedestrain_detect.cpp` | 行人/人脸存在模型与检测图像支路；保留历史文件名拼写 |
| `pedestrian_filter.c/.h` | 分数、尺寸、位置与连续帧过滤 |
| `pedestrian_format_probe.c/.h` | 检测输入像素格式及 RGB565 字节序探测 |
| `camera_motion.c/.h` | 图像运动辅助证据与亮度变化过滤 |
| `vision_tracker.c/.h` | 候选关联、连续帧确认、跟踪与丢失清除 |
| `vision_event.c/.h` | 视觉追踪快照与网页事件序列化 |
| `multimodal_fusion.c/.h` | 雷达存在、视觉证据有效期与单次来访触发 |
| `door_lock.c/.h` | 非阻塞动作队列、LEDC PWM、开启保持与复位 |
| `door_lock_state.c/.h` | 舵机动作状态机，可独立主机测试 |
| `doorbell_audio_render.c/.h` | 提示音渲染、PCM 叠加与后备路径；有采样率兼容限制 |
| `pcm_overlay.c/.h` | 16 位单声道 PCM 混音、限幅及缓冲 |
| `doorbell_status.c/.h` | 可测试的状态模型；目前没有生产调用方 |
| `settings.h`、`settings.local.h.example` | 板型、视频/检测配置、私有 Wi-Fi 模板 |
| `ring.aac`、`open.aac`、`join.aac`、`certs/` | 构建嵌入的提示音、演示 HTTPS 证书和密钥 |

## 五条业务链路

1. **按键呼叫**：BOOT → 来电策略 → 板端铃声 / 浏览器来电事件 → 接听 → WebRTC 媒体连接 → 挂断与释放。
2. **网页开门**：POST 命令 → 业务处理 → 舵机队列 → PWM 开启 / 保持 / 复位；提示音路径单独受采样率兼容性约束。
3. **雷达感知**：UART → 帧解析 → 目标二维坐标 → 区域与存在去抖 → 网页事件 / 融合输入。
4. **视觉感知**：摄像头检测支路 → 模型候选 → 过滤与连续帧确认 → 跟踪快照 → 视觉框 / 融合输入。
5. **多模态触发**：雷达存在与视觉证据 → 有效期判断 → 单次来访策略 → 门铃来电。

## 组件与构建

`solutions/doorbell_local/CMakeLists.txt` 是 ESP-IDF 工程入口，`main/CMakeLists.txt` 登记生产模块与嵌入资源；视觉 C++ 模块由配置开关启用。`main/idf_component.yml` 使用本地覆盖路径引用仓库内组件，并声明需要在线获取的模型、H.264 与 P4/C5 传输依赖。

| 目录 | 作用 |
| --- | --- |
| `components/esp_webrtc` | 会话、媒体与不同信令实现 |
| `components/esp_peer` | Peer 接口和各芯片预编译库；发布时保留 `.a` 文件 |
| `components/av_render` | 音视频渲染 |
| `components/codec_board` | 板级硬件与 Codec 配置 |
| `components/media_lib_utils`、`components/webrtc_utils` | 媒体 / WebRTC 辅助层 |
| `solutions/common` | 示例共用网络、媒体接口及资源 |

`managed_components`、本机 `sdkconfig` 和构建目录由开发环境生成。使用指南中的组件版本表是历史环境记录；只有清单中的精确版本约束构成本次源码的固定要求。

## 测试和结构设计

`tests/verify_*.py` 检查生产配置/接入及网页行为，并编译执行独立的 C 单元测试，覆盖帧解析、状态机、融合、追踪和混音。它们不代替 ESP-IDF 链接、实时调度及硬件测试。

`cad/acrylic_plate/layout_spec.py` 保存结构参数，`generate_plate.py` 输出 DXF、SVG、CSV、PDF 和加工 ZIP；`test_geometry.py` 检查孔位、间隙、边界、文件结构与交付包。
