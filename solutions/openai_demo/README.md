# OpenAI 实时聊天演示

## 概述

本示例演示使用 `esp_webrtc` 建立实时聊天连接 OpenAI. 它还演示如何通过函数调用来使用语音命令来控制设备。

此演示使用 OpenAI 实时 GA WebRTC 电子符号流. 它首先创建了一个短命的客户秘密 `POST /v1/realtime/client_secrets`, 然后交换 SDP 通过 `POST /v1/realtime/calls` 使用该标志。

## 与 OpenAI 实时嵌入 SDK

此演示提供了类似于 [OpenAI 实时嵌入 SDK](https://github.com/openai/openai-realtime-embedded-sdk), 附有若干改进：

1. **综合解决方案**: 消除从零开始构建媒体系统组件的必要性， 提供现成的实施工作。
2. **优化代码c**: 积蓄 `esp_audio_codec` 高效率 OPUS 编码和解码。
3. **增强互动**: 包括音响回声取消 (AEC) 提高语音互动质量。
4. **改进对等连接**: 使用增强版 `esp_peer` 提高性能和可靠性。

## 硬件要求
默认设置使用 [ESP32-S3-科尔沃-2](https://docs.espressif.com/projects/esp-adf/en/latest/design-guide/dev-boards/user-guide-esp32-s3-korvo-2.html)。

## 如何构建

### IDF 版本
您可以选择其中任一 IDF 主树枝或树枝 IDF 释放 v5.4。

### 依赖项
此演示只取决于 **ESP-IDF**. 所有其他所需模块将自动从 [ESP-IDF 书记官处](https://components.espressif.com/)。

### 更改默认设置
1. 修改 Wi-Fi SSID 密码 [settings.h](main/settings.h)
2. 导出您的 OpenAI API 键
   ```bash
   export OPENAI_API_KEY=XXXXXXX
   ```
3. 支持其他董事会， 参考 [codec_board README](../../components/codec_board/README.md)

### 构建
```
idf.py -p YOUR_SERIAL_DEVICE flash monitor
```

## 测试

等板子拉起来， 它将尝试连接到配置的 Wi-Fi SSID. 
一旦 Wi-Fi 已成功连接， 它会尝试连接到 OpenAI 自动服务器.  
用户还可以按照控制台命令控制会话。
1. `start`: 开始聊天
2. `stop`: 停止聊天
3. `i`: 显示系统加载
4. `wifi`: 用密码连接到新的wifi sid
演示代码添加一些预定义的函数调用关闭光线， 浅色， 扬声器音量和开门控制。
用户可以使用语音命令触发函数调用。

## 技术细节
要连接到 OpenAI, 此示例添加了定制的信令实现 `esp_signaling_get_openai_signaling`. 它不使用一个 STUN/TURN 服务器， 因此， `on_ice_info` 将拥有 `stun_url` 设置为 `NULL`. 首先使用配置的信令 API 键来请求从 `/v1/realtime/client_secrets`; 联合国 SDP 然后与 `/v1/realtime/calls` 使用临时代号. 所有其他步骤都遵循典型的调用流程 `esp_webrtc`。

有关标准连接建立流程的更多信息，请参阅[连接建立流程](../../components/esp_webrtc/README.md#典型调用序列-esp_webrtc)。
