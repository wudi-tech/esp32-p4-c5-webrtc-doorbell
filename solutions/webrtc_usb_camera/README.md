# WebRTC USB 相机桥

## 概述

此示例显示如何转动 ESP32 设备变成一个 **WebRTC-改为-USB UVC 桥**：

- 浏览器抓取网络摄像头和麦克风媒体。
- 浏览器发送媒体到 ESP32 通过 WebRTC (AppRTC 信令)。
- ESP32 接收流经 `esp_peer`。
- ESP32 向它转发视频帧 USB UVC 设备接口。
- 一个主机 PC 看见 ESP32 作为标准 USB 摄影机。

简而言之， 此演示转换 a WebRTC 视频流进入插件和播放 USB 网络摄像头种子。

> 目前情况: 仅限 **视频** 转至 UVC 产出。
> 音频在商谈中 WebRTC 但当前忽略。

---

## 系统架构

```text
+--------------------+
|  Browser (Sender)  |
|--------------------|
| Webcam + Mic       |
| WebRTC Sender      |
+---------+----------+
          |
          | WebRTC (SRTP)
          v
+--------------------+
|       ESP32        |
|--------------------|
| esp_peer (WebRTC)  |
| Video Frame Queue  |
| UVC Device Driver  |
+---------+----------+
          |
          | USB UVC
          v
+--------------------+
|   Host Computer    |
|--------------------|
| Camera Application |
| (OBS / VLC / etc.) |
+--------------------+
```

---

## 要求

### 硬件

支助目标：

- ESP32-S3
- ESP32-P4

所需能力：

- Wi-Fi 连通性
- USB 设备模式支持

### 软件与服务

- ESP-IDF 构建环境
- AppRTC-兼容的信令服务器：
  - <https://webrtc.espressif.com>

信令服务器用于 SDP 联合国 ICE 交换， 然后对等连接设置。

---

## 构建与烧录

### 1) 配置 Wi-Fi

编辑 `main/settings.h`：

```c
#define WIFI_SSID "your_ssid"
#define WIFI_PASS "your_password"
```

### 2）构建、烧录与串口监视

```bash
idf.py -p YOUR_SERIAL_PORT flash monitor
```

启动后， 控制台显示系统日志和 WebRTC 状态。

---

## 运行演示

### 步骤1: 开始 ESP32 接收器

在那个 ESP 控制台：

```bash
start <room_id>
```

此命令连接到信令， 加入房间， 并等待浏览器端点。

停下来：

```bash
stop
```

### 步骤2: 启动浏览器发送器

打开 `main/webrtc_usb_sender.html` 设置：

- **服务器**: `https://webrtc.espressif.com`
- **会议室 ID**: 使用的数值与 `start <room_id>`

单击 **开始**. 浏览器抓取本地媒体并将其发送给 ESP32。

### 步骤3: 打开 USB 主机上的相机

连接 ESP32 USB 设备移植到主机 PC。
之后 `uvc_device_init()` 完成， 一个新的 UVC 相机应在 OS。

你可以测试：

- OBS 工作室
- VLC
- 缩放
- 系统相机应用

---

## 当地 AppRTC 代理服务器 (CORS/来源)

`webrtc.espressif.com` 可能拒绝当地人的直接请求 HTML 原产地检查造成的页数。
地方发展， 使用包含在 `main/apprtc_proxy.js`。

### 开始代理

```bash
cd solutions/webrtc_usb_camera/main
npm install ws
node apprtc_proxy.js --port 8080
```

### 发件人页面配置

设定 **服务器** 改为：

```text
http://<proxy-ip>:8080
```

代理重写 AppRTC HTTP 端点和继电器 WebSocket 信令。

---

## 框架流和缓冲

```text
WebRTC Decoder
      |
      v
Video Frame Buffer Pool
      |
      v
UVC Device Driver
      |
      v
USB Host Camera Client
```

框架召回行为：

| 回电 | 说明 |
| --- | --- |
| `fb_get_cb` | UVC 驱动程序请求下一个视频帧 |
| `fb_return_cb` | UVC 驱动程序释放上一个框架 |

使用一个小的可重复使用的框架池来减少分配间接费用。

---

## 示例使用大小写

1. **远程相机桥**
   远程相机 -> WebRTC -> ESP32 -> USB 网络相机 PC。

2. **浏览器相机虚拟化**
   浏览器 (camera/screen 来源) 显示为 USB 相机到桌面应用程序。

3. **无线摄像头适应器**
   转换 Wi-Fi 流到 USB 无自定义主机驱动的 Webcam。

4. **云相机网关**
   跨网络转发远程流并在当地显示为 UVC 摄影机。
