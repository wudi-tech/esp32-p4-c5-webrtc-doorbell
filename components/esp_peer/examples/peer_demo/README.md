# ESP 同行 WebRTC 示例

这个例子显示，使用 **ESP 同行 WebRTC** 库. 它模拟了猫之间的聊天 (🐱) 和绵羊 (🐑), 播放音频流和信件 WebRTC 数据通道。

---

## 🧩 概述

本示例演示核心 WebRTC 能力构建 ESP32, 包括：

- **同行沟通**: 介于两者之间的直接媒体和消息传输 ESP-基于同行。
- **音频流**: 实时音频使用 G.711A 代码c。
- **数据通道**: 双向信息传递和低相关性发送。
- **SDP 谈判**: 模拟 offer/answer 同行之间的交换。

---

## ✨ 功能

### 🎵 音频通信

- 本地同行之间的实时音频流。
- 用途 G.711A 语音传输的编码器。
- 方向音频支持 (发送， 实收， 或两者兼有)。
- PTS-基于 (演示文稿时间戳) 音频帧的校验。

### 💬 数据通道消息

- 模拟猫之间的 QQA 交互 🐱 与绵羊， 🐑。
- 基于简单关键字匹配的自动生成的回复。
- 通过数据渠道进行可靠的信息交换。

### 🌐 网络设置

- 运行于 **SoftAP 模式** 用于对等连接。
- 演示操作不需要外部路由器或互联网。

### ⚙配置选项

- 代理超时配置
- RTP jitter 缓冲参数
- 数据通道缓存
- RTP 向外包滚动缓冲设置

---

## 🚀 构建和运行

### 前置条件

- [ESP-IDF](https://github.com/espressif/esp-idf) v5.0 或稍后时间
- 妥善设置 ESP-IDF 发展环境
- 通过 `idf.py add-dependency` (或 `idf_component.yml`)

### 构建和烧录的步骤

```bash
# 设置目标芯片（例如 ESP32-S3）
idf.py set-target esp32s3

# 构建并烧录示例
idf.py -p <SerialDevice> flash monitor
```

---

## 🧪 与浏览器的比较

比较一下 ESP 基于浏览器的执行 WebRTC：

- 很简单 HTML 文件 [`animal_chat.html`](main/animal_chat.html) 包含。
- 它模仿了人类的行为 ESP 演示以方便测试和 API 对齐。

---

## 📌 页:1

- 此演示会运行本地的对等者。 ESP 方便设备。
- 实际部署：
  - 配置无效 **STUN/TURN 服务器** (单位:千美元) NAT 转弯。
  - 执行a **信令机制** (e.g., MQTT, WebSocket, HTTP) 切换 SDP 联合国 ICE 跨设备候选人。
