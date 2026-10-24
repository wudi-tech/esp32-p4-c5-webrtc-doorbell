# RTSP 服务器推器示例

此示例开始 RTSP 服务器或推器 ESP 设备之后 Wi-Fi 连接。
最重要的设置是在 `main/settings.h`。

## 什麽? `main.c` 已经

- 初始化板+媒体管道 (`init_board()`, `media_sys_buildup()`)。
- 启动控制台 REPL (`esp>` 提示)。
- 连接到 Wi-Fi 通过 `network_init(WIFI_SSID, WIFI_PASSWORD, ...)`。
- 自动启动 RTSP 网络打开时的服务器：
  - 模式: `RTSP_SERVER` (`0`), `RTSP_PUSHER` (`1`)
  - URI 路径: `rtsp://127.0.0.1:8554/live` (路径被使用; 服务器在设备上监听 IP)
  - 端口: `8554` (设置在 `main/rtsp.c`)

所以说 PC 编号 LAN, 溪流 URL 通常为：

`rtsp://<ESP_DEVICE_IP>:8554/live`

---

## 1) 所需配置

### A. 设定 Wi-Fi 全权证书

编辑 `main/settings.h`：

- `WIFI_SSID`
- `WIFI_PASSWORD`

没有这个， `start` 命令和自动启动将等待网络而无所作为。

### B. 确认棋盘和媒体设置

也进入 `main/settings.h`：

- `TEST_BOARD_NAME`
  - `ESP32_P4_DEV_V14` (单位:千美元) `esp32p4`
  - `S3_Korvo_V2` 否则
- 视频：
  - ESP32-P4 默认: `1920x1080 @ 25fps`, `H264`
  - 其他默认: `320x240 @ 10fps`, `MJPEG`
- 音频：
  - `AAC`, `16kHz`, 单

狦 camera/board 不同， 首先更新这些值。

### C. (ESP32-P4) 相机相关默认值

`sdkconfig.defaults.esp32p4` 启用 SC2336 相机和相关选项。
如果你的硬件使用不同的传感器， 更新相机选项 `menuconfig`。

### D. 控制台运输 (可选但有用)

应用程序使用 ESP-IDF 控制台 REPL. 如果控制台输入无效， 校验其中之一：

- `CONFIG_ESP_CONSOLE_UART`
- `CONFIG_ESP_CONSOLE_USB_CDC`
- `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG`

---

## 2) 构建与烧录

从 `solutions/rtsp_server`：

```bash
idf.py set-target esp32p4
idf.py build
idf.py -p <PORT> flash monitor
```

用于非-P4 目标， 设定正确的目标 (举例来说， `esp32s3`)。

---

## 3) 运行和校验

启动后：

1. 等着 Wi-Fi 连接日志。
2. RTSP 服务器在连接时自动启动。
3. 获取设备 IP 从 logs/router。
4. 在主机上打开流流：

```bash
ffplay rtsp://<ESP_DEVICE_IP>:8554/live
```

---

## 4) 主控台命令

时 `esp>` 提示：

- `start <mode> [uri]`
  - 模式：
    - `0` = RTSP 服务器
    - `1` = RTSP 推动客户端
  - 示例 (服务器模式)：
    - `start 0 rtsp://127.0.0.1:8554/live`
- `stop`
  停下来 RTSP 服务。
- `i`
  显示系统加载信息。

说明: 用于正常服务器使用， 自动启动 Wi-Fi 连接已经启用于 `main.c`。

---
