# KVS 示例大师

## 概述
本示例演示如何使用 `esp_webrtc` 与 Amazon Kinesis 视频流 (KVS) 表示要构建 WebRTC 应用程序. 那个 ESP32 担任 KVS **MASTER**, 收到 SDP VEWERs的报价 (浏览器) 发送答案。

## 硬件要求
默认设置使用 [ESP32P4-函数 Ev- Board](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4-function-ev-board/user_guide.html), 包括一个 SC2336 摄影机。

## 如何构建

### IDF 版本
需求 IDF v5.4 或稍后时间。

### 依赖项
此演示只取决于 **ESP-IDF**. 所有其他所需模块将自动从 [ESP-IDF 书记官处](https://components.espressif.com/)。

### KVS 配置

配置 AWS KVS 证书和频道设置 `idf.py menuconfig` 下级 **KVS 信令配置**：

- **AWS 访问密钥 ID** -  AWS 访问密钥
- **AWS 秘密访问密钥** -  AWS 密钥
- **AWS 会话** - 会话符号 (临时全权证书)
- **AWS 地区** - AWS 区域 (默认: `us-east-1`)
- **KVS 频道名称** - 信令频道名称 (默认: `demo-channel`)

### 更改默认设置
1. 修改 Wi-Fi SSID 密码 [settings.h](main/settings.h)
2. 如果您使用不同的相机类型或分辨率， 更新设置 [settings.h](main/settings.h)
3. 使用时 USB-JTAG 要下载， 在 [sdkconfig.defaults.esp32p4](sdkconfig.defaults.esp32p4)
```
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
```

### 构建
```
idf.py -p YOUR_SERIAL_DEVICE flash monitor
```

## 测试

启动后， 连接到配置的棋盘 Wi-Fi 自动加入 KVS 频道设置在 menuconfig。

如果你想连接到不同的 Wi-Fi STA 运行时：
```
wifi ssid psw
```

### 命令行方式

| 命令 | 说明 |
|---------|-------------|
| `join` | 加入到 KVS 频道 (配置为 menuconfig) |
| `leave` | 离开当前频道 |
| `wifi ssid psw` | 连接到不同的 Wi-Fi 网络 |
| `i` | 显示系统状态 |
| `rec2play` | 测试抓取和播放 |
| `m` | 测量系统加载 |

### QA
- 如果董事会出乎意料地离开频道， 服务器在计时截止前将频道保留了1-2分钟. 等待超时后再重试。
  或者说 `leave` 先说吧 `join` 再来一次。

## 技术细节

- 那个 ESP32 担任 **MASTER** 输入 KVS 术语
- 那个 MASTER 接收 SDP VEWERs的报价 (浏览器) 自动生成答案
- 当信令连接和 ICE 收到信息
- 支持双向音频和视频

有关连接流程的更多信息，请参阅[连接建立流程](../../components/esp_webrtc/README.md#典型调用序列-esp_webrtc)。
