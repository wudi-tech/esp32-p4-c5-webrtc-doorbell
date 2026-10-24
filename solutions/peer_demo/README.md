# 端点

## 概述

本示例演示如何使用 `esp_peer` API 创建简单的聊天程序 ESP32 系列板.  
应用程序在建立连接后定期通过数据通道发送假音频数据和聊天字符串。

## 硬件要求

此演示需要最小的硬件: 任何支持 Wi-Fi 连接就足够了。
它需要两个板子来充当聊天同伴。

## 如何构建

### IDF 版本

你可以用两个 IDF 主树枝或树枝 IDF 释放 v5.4。

### 依赖项

此演示只取决于 **ESP-IDF**. 所有其他所需模块将自动从 [ESP-IDF 书记官处](https://components.espressif.com/)。

### 更改默认设置

更新 Wi-Fi SSID 密码 [settings.h](main/settings.h) 文件。

### 构建与烧录

运行以下命令以构建， 演示的烧录：
```bash
idf.py -p YOUR_SERIAL_DEVICE flash monitor
```

## 测试

等板子拉起来， 它将尝试连接到配置的 Wi-Fi 网络。
一旦 Wi-Fi 连接已成功建立， 您可以使用下列控制台命令来测试演示：

1. `start <roomid>`: 输入指定的房间并开始聊天。
2. `stop`: 停止聊天。
3. `i`: 显示系统加载信息。
4. `wifi <ssid> <password>`: 连接到新 Wi-Fi SSID 带有密码。

## 技术细节

  此演示使用 `apprtc` 指示执行 (`esp_signaling_get_apprtc_signaling`) 作为默认信令.  
  信令流程遵循[连接建立流程](../../components/esp_webrtc/README.md#典型调用序列-esp_webrtc)。  
  它详细演示了如何使用这种技术。 `esp_peer` 协议 API 设立 WebRTC 应用程序。
