# 视频呼叫演示

## 概述
本示例演示如何使用 `esp_webrtc` 以建立设备来安装视频呼叫应用程序. 该代码使用修改后的版本 [应用c](https://github.com/webrtc/apprtc) 作为信令服务器和定制命令通道。


## 硬件要求
默认设置使用 [ESP32P4-函数 Ev- Board](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4-function-ev-board/user_guide.html), 包括a SC2336 摄影机. 一个视频通话， 两个 `ESP32P4-Function-Ev-Board` 需要设备， 一个是来电，另一个是来电。

## 如何构建

### IDF 版本
可选择 IDF 大师或 IDF 释放 v5.4。

### 更改默认设置
1. 修改 Wi-Fi SSID 密码 [settings.h](main/settings.h)
2. 如果您使用不同的相机类型或分辨率， 更新相机类型和分辨率的设置 [settings.h](main/settings.h)
3. 使用时 USB-JTAG 要下载， 在 [sdkconfig.defaults.esp32p4](sdkconfig.defaults.esp32p4)
```
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
```

### 构建
```
idf.py -p YOUR_SERIAL_DEVICE flash monitor
```

## 测试

等板子拉起来， 它将尝试连接到配置的 Wi-Fi SSID. 如果你想连接到不同的 Wi-Fi STA, 您可以使用以下选项： CLI 命令：
```
wifi ssid psw
```

成功过后 Wi-Fi 连接， 使用联合命令加入随机房间. 两块板子都要进同一间房：
```
join mytestroom
```

### 互动

1. **环**
   - 一块板点击启动按钮或输入 `b`` 命令在控制台中按住对等板。
   - 同行委员会收到 `Ring` 命令和播放环形音乐。

2. **接受:**
   - 对等板按下启动按钮或输入 `b` 命令接受调用。
   - 如果无法接受， 超时后将自动重设。
  
3. **挂断:**
   - 对等板按下启动按钮或输入 `b` 命令结束当前调用。

4. **清除测试:**
   - 在两个板上， 输入 `leave` 命令退出房间。

## 技术细节
  支持视频通话功能， 此演示使用 [应用c](https://github.com/webrtc/apprtc) 并需要从对等连接中分离信令 构建逻辑. 端点连接仅在从端点收到特殊信令消息时建立。

### 关键更改 `esp_webrtc`：
- **`no_auto_reconnect` 配置**: 此配置在建立信令连接时会禁用对等连接的自动构建。
- **`esp_webrtc_enable_peer_connection` API**: 一个新的 API 用于手动控制对等连接的连接和断开。
- **数据频道视频**: 启用视频通过数据通道， 使用下列配置：
```
   .enable_data_channel = true,
   .video_over_data_channel = true,
```
所有其他步骤均遵循 `esp_webrtc` 的典型调用流程。标准连接建立流程详见[连接建立流程](../../components/esp_webrtc/README.md#典型调用序列-esp_webrtc)。
