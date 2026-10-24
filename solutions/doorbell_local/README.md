> 本页保留上游例程说明与历史中文译文。WT99P4C5-S1 的实际配置、ESP-IDF v5.5.5 构建步骤和当前限制，请优先阅读 [仓库总览](../../README.md)、[使用指南](../../docs/submission/02_使用指南.md) 与 [项目状态](../../docs/PROJECT_STATUS.md)。


# 门铃本地演示

## 概述

本示例演示如何使用 `esp_webrtc` 以可选方式构建本地门铃应用程序 AI-有动力行人探测器. 一个 ESP32 系列董事会作为 HTTPS 信令服务器和提供实时视频流， 双向音频通信， 和智能运动探测能力。

## 硬件要求

默认硬件设置使用 [ESP32P4-函数 Ev- Board](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4-function-ev-board/user_guide.html), 包括 SC2336 摄影机。

## 构建指令

### ESP-IDF 版本

使用任意一个 **IDF 主分支** 或 **释放 v5.4**。

### 依赖项

此演示只取决于 **ESP-IDF**. 所有其他所需组件将自动从 [ESP-IDF 书记官处](https://components.espressif.com/)。

### 配置

1. **Wi-Fi 设置**
   准备好 Wi-Fi SSID 密码 [`settings.h`](main/settings.h)。

2. **相机配置**
   如果使用不同的相机模型或分辨率， 更新相应的设置 [`settings.h`](main/settings.h)。

3. **行人探测器 (可选)**
   实现行人检测功能：
   - 设定 `CONFIG_DOORBELL_SUPPORT_PEDESTRIAN_DETECT=y` 在配置中
   - 检测分辨率由 `DETECT_WIDTH` 联合国 `DETECT_HEIGHT` 输入 [`settings.h`](main/settings.h)
   - 检测帧率由 `DETECT_FPS` 设置
   - 需要额外的内存和处理功率

4. **USB-JTAG 下载 (可选)**
   如果使用的话 USB-JTAG, 解析下行 [`sdkconfig.defaults.esp32p4`](sdkconfig.defaults.esp32p4)：
   ```c
   CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
   ```

### 构建与烧录

```bash
idf.py -p YOUR_SERIAL_DEVICE flash monitor
```

## 测试

在板靴之后， 它会连接到配置的 Wi-Fi. 切换到不同的 Wi-Fi 动态网络， 使用 CLI：

```bash
wifi YOUR_SSID YOUR_PASSWORD
```

一旦连接， 棋盘将启动信令服务器并等待端点连接. 测试 URL 将在控制台上打印：

```
Webrtc_Test: Use browser to enter https://192.168.10.33/webrtc/test for test
```

您可以停止或重新启动信令：

```bash
stop
start
```

### 浏览器访问

打开 **Chrome** 或 **边缘** 并参观印刷品 URL. 由于网站使用自签证书， 您可能需要手动信任页面。

还有， 禁用 mDNS 数据 ICE 候选人确保适当 WebRTC 连通性：

- **Chrome**: `chrome://flags/#enable-webrtc-hide-local-ips-with-mdns`
- **边缘**: `edge://flags/#enable-webrtc-hide-local-ips-with-mdns`

禁用选项: **WebRTC 数字 ICE 候选人**
重新启动更改生效的浏览器。

### 互动

在浏览器中打开页面后：

1. **打开门**
   - 单击 **门** 图标。
   - 董事会会演奏"开门"的调子。
   - 浏览器将显示: `Receiving Door opened event`。

2. **通话 (环键)**
   - 按键 **启动键** 在董事会上。
   - 董事会演奏响铃音乐， 浏览器显示 **接受调用** 联合国 **拒绝呼叫** 图标。
   - 如获接受: 开始 **双向音频** 联合国 **单向视频** (董事会 → 浏览器)。
   - 如果被拒绝: 呼叫结束。
   - 要更改戒指按钮， 修改 `DOOR_BELL_RING_BUTTON` 输入 [`settings.h`](main/settings.h)。

3. **行人探测器** *(如果启用)*
   - 系统使用相机自动检测行人。
   - 当发现行人时， a `PEDESTRIAN_DETECTED` 事件发送到浏览器。
   - 浏览器在可滚动列表中显示检测事件， 并带有时间戳。
   - 单击 **检测事件** 按钮改为 view/clear 事件历史。
   - 事件按时间顺序显示 (最新的第一个) 具有动画效果。

4. **结束调用/ 清理**
   - 在浏览器中: 单击 **挂断** 图标。
   - 在董事会上: 运行 `stop` 命令。

## 技术细节

此演示使用 SSE (服务器事件) 向浏览器发送即时消息 HTTP POST 从浏览器接收即时消息。

### 增强 `esp_webrtc`

- **`no_auto_reconnect`**: 在信令连接上禁用自动端点连接设置。
- **`esp_webrtc_enable_peer_connection()`**: 明确 API 控制对等连接生命周期。

详细通话流程请参阅 [esp_webrtc 连接流程](../../components/esp_webrtc/README.md#典型调用序列-esp_webrtc)。

### 行人发现执行

启用时， 行人探测系统：

- **相机处理**: 按配置的分辨率和帧率从相机中抓取帧
- **AI 检测**: 用途 ESP-DL (深入学习) 用于实时行人检测的库
- **事件通知**: 发送 `PEDESTRIAN_DETECTED` 自定义命令通过 WebRTC 信令
- **浏览器 UI**: 在可滚动中显示带有时间戳的检测事件， 动画列表

检测系统独立于视频通话运行，并在启用时继续在背景运行。

### 限制

- 一次只有一个对等点可以连接。
- 执行5秒的心跳超时。
- 如果同行出乎意料的离开， 等待几秒钟后重新连接。
- 踏行者需要额外探测 CPU 和内存资源， 这可能会影响整体系统性能。
- 检测精度取决于光照条件， 相机质量， 以及行人定位。
