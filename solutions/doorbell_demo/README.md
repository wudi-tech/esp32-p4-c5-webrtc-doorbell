# 门贝尔示例

## 概述
本示例演示如何使用 `esp_webrtc` 用于构建门铃应用程序. 该代码基于Google [应用c](https://github.com/webrtc/apprtc) 项目， 带有自定义信令 WebSocket。

## 硬件要求
默认设置使用 [ESP32P4-函数 Ev- Board](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4-function-ev-board/user_guide.html), 包括一个 SC2336 摄影机。

## 如何构建

### IDF 版本
可选择 IDF 大师或 IDF 释放 v5.4。

### 依赖项
此演示只取决于 **ESP-IDF**. 所有其他所需模块将自动从 [ESP-IDF 书记官处](https://components.espressif.com/)。

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

一旦连接到 Wi-Fi, 棋盘会自动尝试加入从它的 MAC 地址. 房间的名字会在控制台出现：
```
W (9801) Webrtc_Test: Please use browser to join in espxxxxxx on https://webrtc.espressif.com/doorbell
```
用户也可以使用 `leave` 命令离开房间并使用 `join` 命令加入其他随机房间. 在进入前确保房间是空的：
```
join random_room_id
```
如果你是第一个加入的， 你会看到：
```
Initials set to 1
```

礛, 使用一个 Chrome/Edge 浏览器进入同一房间 [贝尔示例门](https://webrtc.espressif.com/doorbell)。

### 互动

1. **打开门:**
   - 在浏览器中， 单击 `Door` 图标. 这会把"开门"的命令发给董事会， 董事会将演奏"开门"的调子。
   - 董事会会用“ 打开门” 回复， 浏览器会显示信件: `Receiving Door opened event`。

2. **通话:**
   - 按键 `Ring` 按钮 (启动键) 在董事会上. 董事会会演奏戒指音乐
   - 浏览器会弹出 `Accept Call` 或 `Deny Call` 图标. 如获接受， 双向语音交流和单向视频交流 (跳转到浏览器) 将在棋盘和浏览器之间建立. 如果被拒绝， 电话会挂断的.  
   使用不同的按键作为环键， 更改 `DOOR_BELL_RING_BUTTON`  输入 [settings.h](main/settings.h)。

3. **清除测试:**
   - 在浏览器中， 单击 `Exit` 图标。
   - 在董事会上， 输入 `leave` 命令退出房间。

## 技术细节
  支持门铃功能， 此演示使用 [应用c](https://github.com/webrtc/apprtc) 并需要从对等连接中分离信令 构建逻辑. 端点连接仅在从端点收到特殊信令消息时建立。

### 关键更改 `esp_webrtc`：
- **`no_auto_reconnect` 配置**: 此配置在建立信令连接时会禁用对等连接的自动构建。
- **`esp_webrtc_enable_peer_connection` API**: 一个新的 API 用于手动控制对等连接的连接和断开。

所有其他步骤均遵循 `esp_webrtc` 的典型调用流程。标准连接建立流程详见[连接建立流程](../../components/esp_webrtc/README.md#典型调用序列-esp_webrtc)。

### QA
- 如果董事会出乎意料地离开房间， 无法重新进入同一个房间。
  服务器会在时间到来之前把房间保持1-2分钟. 用户在重试前必须等待超时到期。
  或者说 `leave` 房间首先 `join` 随机房间 ID。

- 如果用户想要更改 cn 服务器， 可以在输入前按照命令使用 `join`
   ```
   server 1
   ```
