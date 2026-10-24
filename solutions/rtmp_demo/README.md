# RTMP 按下示例

此示例抓取 audio/video 从 ESP 设备并推到一个 RTMP 使用 `esp_media_protocols`。
最重要的设置是在 `main/settings.h`。

## 什麽? `main.c` 已经

- 初始化板+媒体管道 (`init_board()`, `media_sys_buildup()`)。
- 启动控制台 REPL (`esp>` 提示)。
- 连接到 Wi-Fi 通过 `network_init(WIFI_SSID, WIFI_PASSWORD, ...)`。
- 自动启动 RTMP 后按键 Wi-Fi 连接时 `RTMP_PUSH_URL` 设置在构建时间。

若为 `RTMP_PUSH_URL` 为空， 应用程序等待控制台命令：

```bash
start rtmp://<server>/<app>/<stream>
```

## 1) 所需配置

### A. 设定 Wi-Fi 全权证书

编辑 `main/settings.h`：

- `WIFI_SSID`
- `WIFI_PASSWORD`

没有这个， `start` 命令和自动启动将等待网络而无所作为。

### B. 设定 RTMP URL

使用其中之一选项：

- 构建时环境变量：

```bash
RTMP_PUSH_URL=rtmp://<server>/<app>/<stream> idf.py build
```

- 运行时控制台命令：

```bash
start rtmp://<server>/<app>/<stream>
```

保存中 `RTMP_PUSH_URL` 调出 `main/settings.h` 避免执行服务器 URL 到源代码。

### C. 确认媒体设置

也进入 `main/settings.h`：

- `TEST_BOARD_NAME`
  - `ESP32_P4_DEV_V14` (单位:千美元) `esp32p4`
  - `S3_Korvo_V2` 否则
- 视频：
  - ESP32-P4 默认: `1920x1080 @ 25fps`, `H264`
  - 其他默认: `320x240 @ 10fps`, `MJPEG`
- 音频：
  - `AAC`, `8kHz`, 单

RTMP 支持取决于服务器. H264 + AAC 通常对公众来说是最好的选择 RTMP 服务。

### D. (ESP32-P4) 相机相关默认值

`sdkconfig.defaults.esp32p4` 启用 SC2336 相机和相关选项。
如果你的硬件使用不同的传感器， 更新相机选项 `menuconfig`。

## 2) 构建与烧录

从 `solutions/rtmp_demo`：

```bash
. ~/esp/idf_v55/esp-idf/export.sh
idf.py set-target esp32p4
RTMP_PUSH_URL=rtmp://<server>/<app>/<stream> idf.py build
idf.py -p <PORT> flash monitor
```

用于非-P4 目标， 设定正确的目标， 举例来说， `esp32s3`。

## 3) 运行和校验

启动后：

1. 等着 Wi-Fi 连接日志。
2. 若为 `RTMP_PUSH_URL` 构建时设置， 自动启动。
3. 否则就跑 `start rtmp://<server>/<app>/<stream>` 现场 `esp>` 提示。
4. 打开你的溪流 RTMP 服务器或播放器。

## 4) 主控台命令

时 `esp>` 提示：

- `start [url]`
  开始 RTMP 推动. 若为 `url` 删除， `RTMP_PUSH_URL` 用于。
- `stop`
  停下来 RTMP 推动。
- `wifi <ssid> [password]`
  重新配置和重新连接 Wi-Fi, 匹配其他演示。
- `i`
  显示系统加载信息。
