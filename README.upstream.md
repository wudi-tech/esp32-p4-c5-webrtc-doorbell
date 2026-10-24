# ESP WebRTC 解决方案

## 项目简介

本仓库提供构建 WebRTC 应用所需的完整内容，其中包括 `esp_webrtc` 核心代码及其依赖组件：

- **`esp_peer`**：用于实现 WebRTC 对等连接
- **`esp_capture`**：用于采集媒体数据，详情请参阅组件库中的 [esp_capture](https://components.espressif.com/components/espressif/esp_capture/)
- **`av_render`**：用于播放媒体数据

此外，本仓库在 `solutions/` 目录中提供了多个示例应用，用于演示如何使用 `esp_webrtc` 及相关媒体技术栈。每个解决方案均配有独立的说明文档，其中包含硬件要求和构建步骤。

## 解决方案

### 入门学习与接口示例

| 目录 | 说明 |
|------|------|
| [`peer_demo`](solutions/peer_demo/) | 使用 `esp_peer` 接口从零构建 WebRTC 应用的最小示例。 |

### 云服务、流媒体与 SFU 集成

| 目录 | 说明 |
|------|------|
| [`openai_demo`](solutions/openai_demo/) | 通过自定义信令路径与 OpenAI 实时服务建立 WebRTC 实时会话。 |
| [`whip_demo`](solutions/whip_demo/) | 使用 WHIP（`esp_webrtc`）向服务器发布音视频流。 |
| [`kvs_master`](solutions/kvs_master/) | Amazon Kinesis 视频流主节点：接收观看端的 SDP 提议，并通过 KVS 信令返回应答。 |
| [`kms_demo`](solutions/kms_demo/) | 向 Kurento 媒体服务器发布媒体流，并提供用于观看该媒体流的浏览器页面。 |
| [`janus_demo`](solutions/janus_demo/) | 通过 Janus HTTP 信令向 Janus 视频房间发布媒体流。 |

### 产品化示例

| 目录 | 说明 |
|------|------|
| [`doorbell_demo`](solutions/doorbell_demo/) | 使用 AppRTC 风格的 WebSocket 信令实现门铃功能，支持远程控制、实时视频和双向音频。 |
| [`doorbell_local`](solutions/doorbell_local/) | 无需外部信令服务器的本地门铃方案，由 ESP 设备提供信令服务，并支持实时行人检测。 |
| [`videocall_demo`](solutions/videocall_demo/) | 使用 `esp_webrtc` 数据通道实现的视频通话应用。 |

### 协议桥接与 RTSP

| 目录 | 说明 |
|------|------|
| [`webrtc_usb_camera`](solutions/webrtc_usb_camera/) | WebRTC 到 USB UVC 的桥接方案：浏览器通过 WebRTC 发送媒体数据，主机将设备识别为 USB 摄像头。当前支持视频链路，详情请参阅该示例的说明文档。 |
| [`rtsp_demo`](solutions/rtsp_demo/) | 在设备上运行 RTSP 服务器或推流端，用于局域网媒体传输，并作为 WebRTC 与媒体示例的补充。 |
| [`rtmp_demo`](solutions/rtmp_demo/) | 采集设备的音视频数据并发布到 RTMP 服务器的推流示例。 |
