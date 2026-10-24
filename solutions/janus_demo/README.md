# Janus VideoRoom 发布端示例

## 概述

此演示展示了如何使用 `esp_webrtc` 作为 Janus VideoRoom **发布端** 通过 Janus HTTP 信令。

## Janus 设置

- 启用 Janus HTTP 传输插件 (默认端口 `8088`)。
- 启用 `janus.plugin.videoroom` 并创建一个房间。
- 在以下位置使用房间ID `main/settings.h` (`JANUS_ROOM_ID`)。

示例 API 端点：

```text
http://<janus-ip>:8088/janus
```

## 配置

编辑 [`main/settings.h`](main/settings.h)：

- `WIFI_SSID`, `WIFI_PASSWORD`
- `JANUS_SERVER`
- `JANUS_ROOM_ID`
- 可选 `JANUS_PIN`, `JANUS_TOKEN`, `JANUS_API_SECRET`, `JANUS_DISPLAY`

## 使用方法

启动后， 控制台命令：

- `start` : 使用默认设置发布。
- `start <janus_url> <room_id>` : 使用运行时发布 URL/房间。
- `stop` : 停止发布。
