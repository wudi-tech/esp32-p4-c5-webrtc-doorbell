# ESP 同行 – WebRTC PeerConnection 构成部分

`esp_peer` 是一个全天候 WebRTC PeerConnection Espressif 平台的优化执行. 它能与音频进行对等通信， 视频， 和数据通道， 遵守标准 WebRTC 协议堆栈。

---

## 🔍 概述

来源 [解析器](https://github.com/sepfy/libpeer.git), `esp_peer` 添加平台专用增强和优化 ESP32 序列. 它提供生产级 WebRTC 堆栈支持：

- **ICE** – 互动连接的建立
- **DTLS** – 数据报 传输层安全
- **SCTP** – 流控传输协议
- **RTP/SRTP** – 带有安全变体的实时传输协议

---

## ✨ 主要功能

### ✅ WebRTC 协议堆栈

- **TURN 支助**: 执行 [RFC5766](https://datatracker.ietf.org/doc/html/rfc5766) 联合国 [RFC8656](https://datatracker.ietf.org/doc/html/rfc8656)
- **双 ICE 角色**: 运行为 * 控制 * 或 * 控制 *
- **优化 ICE 配对**: 快速高效地甄选候选人
- **RTP 可靠性**: 执行 NACK, 抖动缓冲， 和再发送
- **强壮 SCTP**: 支持多流， 不成体系， 联合国 SACK
- **线索操作**: 非封锁 send/receive 有专门任务

### 🎹 媒体支助

- **音频编解码器**: G711A (PCMA), G711U (PCMU), OPUS
- **视频解码器**: H.264, MJPEG
- **数据通道**: Reliable/unreliable; ordered/unordered 模式
- **灵活媒体模式**: 只发送， 仅接收， 或全复数

### 🧹 模块配置( C)

- **可插件接口**: 清洁抽象通过 `esp_peer_ops_t`
- **默认执行**: 准备通过 `esp_peer_get_default_impl()`
- **资源控制**: Tune 内存使用方法， 超时， 缓冲大小

---

## 🚀 快速开始

### 1+++ 对等配置

```c
esp_peer_cfg_t cfg = {
    .role = ESP_PEER_ROLE_CONTROLLING,
    .ice_trans_policy = ESP_PEER_ICE_TRANS_POLICY_ALL,
    .audio_info = {
        .codec = ESP_PEER_AUDIO_CODEC_OPUS,
        .sample_rate = 48000,
        .channel = 1
    },
    .video_info = {
        .codec = ESP_PEER_VIDEO_CODEC_H264,
        .width = 640,
        .height = 480,
        .fps = 30
    },
    .audio_dir = ESP_PEER_MEDIA_DIR_SEND_RECV,
    .video_dir = ESP_PEER_MEDIA_DIR_SEND_RECV,
    .enable_data_channel = true,
    .ctx = user_context,
    .on_state = state_callback,
    .on_msg = message_callback,
    .on_audio_data = audio_callback,
    .on_video_data = video_callback,
    .on_data = data_callback
};
```

---

### 2。 移徙: 浏览器 WebRTC → ESP 同行

从浏览器样式轻松迁移 WebRTC 嵌入式编码 `esp_peer` API. 参见 [`peer_demo.c`](examples/peer_demo/main/peer_demo.c) 完全走过。

#### 🔄 API 绘图

| 浏览器 WebRTC                                     | ESP 同行                            | 页:1                             |
| -------------------------------------------------- | ----------------------------------- | --------------------------------- |
| `new RTCPeerConnection()`                          | `esp_peer_open()`                   | 基于配置的即时 |
| `pc.onicecandidate`                                | `.on_msg` 回调                  | Candidate/SDP 交换            |
| `pc.onconnectionstatechange`                       | `.on_state` 回调                | 连接寿命周期              |
| `pc.ontrack`                                       | `.on_audio_data` / `.on_video_data` | 媒体招待会                   |
| `pc.ondatachannel`                                 | `.on_data` 回调                 | 数据通道消息             |
| `createOffer()` / `createAnswer()`                 | `esp_peer_new_connection()`         | 自动生成 SDP                |
| `setLocalDescription()` / `setRemoteDescription()` | `esp_peer_send_msg()`               | 交换 SDP 手动             |
| `dataChannel.send()`                               | `esp_peer_send_data()`              | 通过 SCTP                |
| 手动事件循环                                  | `esp_peer_main_loop()`              | 处理背景任务        |

---

### 3️⃣ 数据通道使用

#### ✔QQ 自动创建

若为 `.enable_data_channel = true`：

- **客户角色**: 自动创建默认频道
- **服务器角色**: 等待对等端创建并配对频道

#### 🛠 手动创建

高级使用案件 (e.g., 多个或自定义配置的频道)：

```c
esp_peer_cfg_t cfg = {
    .enable_data_channel = true,
    .manual_ch_create = true,
    // ...
};
esp_peer_open(&cfg, esp_peer_get_default_impl(), &peer);

// After ESP_PEER_STATE_DATA_CHANNEL_CONNECTED
esp_peer_data_channel_cfg_t ch_cfg = {
    .type = ESP_PEER_DATA_CHANNEL_RELIABLE,
    .ordered = true,
    .label = "my_channel"
};
esp_peer_create_data_channel(peer, &ch_cfg);

// Once open:
esp_peer_data_frame_t data_frame = {
    .type = ESP_PEER_DATA_CHANNEL_STRING,
    .stream_id = 0,
    .data = (uint8_t*)"Hello WebRTC!",
    .size = 13
};
esp_peer_send_data(peer, &data_frame);
```

## 4️⃣ 信令解释

WebRTC 需要一个交换的信令机制 SDP 联合国 ICE 同行候选人. 本单元 **不提供内置信令传输**。

你可以指 完整的 [esp-webrtc-溶液](https://github.com/espressif/esp-webrtc-solution) 包括实用的信令执行，如手动 OpenAI, WHIP, 联合国 AppRTC, 或者您可以开发适合您的应用程序的自定义信令系统。

---

## ⚙精细的图宁 (可选)

内存或性能权衡表 `esp_peer_default_cfg_t`：

```c
esp_peer_default_cfg_t default_cfg = {
    .agent_recv_timeout = 100,
    .data_ch_cfg = {
        .cache_timeout = 5000,
        .send_cache_size = 102400,
        .recv_cache_size = 102400,
    },
    .rtp_cfg = {
        .audio_recv_jitter = {
            .cache_timeout = 100,
            .resend_delay = 20,
            .cache_size = 102400,
        },
        .video_recv_jitter = {
            .cache_timeout = 100,
            .resend_delay = 20,
            .cache_size = 409600,
        },
        .send_pool_size = 409600,
        .send_queue_num = 256,
        .max_resend_count = 3
    }
};

cfg.extra_cfg = &default_cfg;
cfg.extra_size = sizeof(default_cfg);
esp_peer_open(&cfg, esp_peer_get_default_impl(), &peer);
```

---

## 📉 最低所需资源

`esp_peer` 高度可配置以支持低模环境. 甚至在平台上 **无 PSRAM**, 最小设置用途 **< 60 KB** RAM。

见 [`peer_demo`](examples/peer_demo) 例如，两个对等方可以同时运行于一个 **ESP32-S3** 没有外部内存。

---

## 📦 依赖项

### 库

- **自由软件** – 安全 RTP (捆绑)
- **密布TLS** – 所需经费 DTLS (捆绑在一起 ESP-IDF)

### ESP-IDF 设置

确保启用这些配置选项：

```ini
CONFIG_MBEDTLS_SSL_PROTO_DTLS=y
CONFIG_MBEDTLS_SSL_DTLS_SRTP=y
For IDFv6.0 need turn on following option also
CONFIG_MBEDTLS_X509_CREATE_C=y
```

---

## 🔀 PeerConnection 状态机

```mermaid
stateDiagram-v2
direction LR
[*] --> NEW_CONNECTION
NEW_CONNECTION --> PAIRING
PAIRING --> PAIRED
PAIRED --> CONNECTING
CONNECTING --> CONNECTED
CONNECTED --> DATA_CHANNEL_CONNECTED
DATA_CHANNEL_CONNECTED --> DATA_CHANNEL_OPENED
DATA_CHANNEL_OPENED --> DISCONNECTED
DISCONNECTED --> CLOSED
CLOSED --> [*]
```

---

## 🧠 性能建议

- **缓冲大小**: 基于使用案例的记忆交易延迟性
- **调整超时**: 适应高相关性或亏损的网络
- **使用指定任务**: 运行 `esp_peer_main_loop()` 在它自己的线程中
- **配置资源使用**: 用于优化的监视堆栈和堆栈

---

## 📬 联系人支持( S)

本构成部分是 [esp-webrtc-溶液](https://github.com/espressif/esp-webrtc-solution), 提供完整 WebRTC 包括媒体流在内的能力 (esp_capture, av_render), 信令种类 (OpenAI, WHIP, AppRTC, 类.) 和许多实例。

🔧 找到一个错误? 有个建议?\
打开一个问题 GitHub: [esp-webrtc-solution/issues](https://github.com/espressif/esp-webrtc-solution/issues)

我们是来帮忙的!

---
