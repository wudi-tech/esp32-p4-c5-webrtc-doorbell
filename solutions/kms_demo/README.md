# Kurento 媒体服务器 (KMS) 发布客户端Demo

## 概述

本示例演示如何使用 `esp_webrtc` 将发布 video/audio 流到 Kurento 媒体服务器 (KMS). ESP32 行为 WebRTC 发布端， 浏览器可以连接到浏览流 KMS。

### 架构

```
┌─────────────┐         WebRTC          ┌─────────────┐         WebRTC         ┌─────────────┐
│   ESP32     │ ───────────────────────>│     KMS     │<───────────────────────│   Browser   │
│  (Publisher)│                         │   (Server)  │                        │ (Subscriber)│
└─────────────┘                         └─────────────┘                        └─────────────┘
     │                                          │                                       │
     └─────────── WebSocket Signaling ──────────┴─────────── WebSocket Signaling ───────┘
```

**流动:**
1. ESP32 连接到 KMS 通过 WebSocket 并创建媒体管道
2. ESP32 创建两个 WebRTC 端点：
   - 端点1: ESP32's 自己的发布端点
   - 端点2: 用户浏览器显示端点
3. ESP32 连接端点1 → 媒体路线的端点2
4. ESP32 日志浏览器端点 ID
5. 浏览器使用端点 ID 连接并接收溪流

## 硬件要求

- **ESP32P4-函数 Ev- Board** (建议)
  - 包括 SC2336 摄影机
  - 支持到 1920x1080@25fps
  - [文档](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4-function-ev-board/index.html)

- **ESP32-S3-科尔沃-V2** (也获得支持)
  - 最多支持 320x240@ 10fps

## 前置条件

- **ESP-IDF**: v5.4 或 v5.5
- **Kurento 媒体服务器**: v7.2.0 或稍后时间
- **现代浏览器**: Chrome, Firefox, 边缘 (与 WebRTC 支助)
- **网络**: ESP32 浏览器必须能够到达 KMS 服务器

## Kurento 服务器设置

### 安装

详细安装指令， 参考 [公务 Kurento 文档](https://doc-kurento.readthedocs.io/en/latest/user/installation.html)。

### 快速从 Docker 开始

最简单的跑法 KMS 正在使用 Docker 选项：

```bash
docker run --rm \
     --network host \
     -p 8888:8888/tcp \
     -p 5000-5050:5000-5050/udp \
     -e KMS_MIN_PORT=5000 \
     -e KMS_MAX_PORT=5050 \
     kurento/kurento-media-server:7.2.0
```

**重要说明:**
- 使用 `--network host` 直接网络访问
- 确保港口8888 (WebSocket) 可访问
- UDP 5000-5050为 RTP 媒体 (视需要调整)
- 远程访问， 确保 KMS 约束 `0.0.0.0`, 没有 `127.0.0.1`

### 校验 KMS 正在运行

检查是否 KMS 可访问：
```bash
curl http://YOUR_SERVER_IP:8888/kurento
```

你应该看看 JSON 回复方式 KMS 版本信息。

## 配置

### 1. Wi-Fi 设置

编辑 [main/settings.h](main/settings.h)：

```c
#define WIFI_SSID "YourWiFiSSID"
#define WIFI_PASSWORD "YourWiFiPassword"
```

### 2. Kurento 服务器 URL

设定 KMS WebSocket URL 输入 [main/settings.h](main/settings.h)：

```c
#define KURENTO_SERVER "ws://YOUR_SERVER_IP:8888/kurento"
```

**实例:**
- 本地服务器: `ws://192.168.1.100:8888/kurento`
- 远程服务器: `ws://example.com:8888/kurento`
- 安全 (WSS): `wss://example.com:8888/kurento`

### 3. 视频设置

视频分辨率和帧率配置于 [main/settings.h](main/settings.h)：

**ESP32P4:**
```c
#define VIDEO_WIDTH  1920
#define VIDEO_HEIGHT 1080
#define VIDEO_FPS    25
```

**ESP32-S3:**
```c
#define VIDEO_WIDTH  320
#define VIDEO_HEIGHT 240
#define VIDEO_FPS    10
```

### 4. 董事会配置

根据目标芯片自动检测棋盘类型. 支持其他董事会， 见 [codec_board README](../../components/codec_board/README.md)。

## 架构和闪耀

### 构建

```bash
cd solutions/kms_demo
idf.py build
```

### 烧录和监视器

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

替换 `/dev/ttyUSB0` 与您的序列端口：
- Linux: `/dev/ttyUSB0` 或 `/dev/ttyACM0`
- 马科斯: `/dev/cu.usbserial-*` 或 `/dev/cu.usbmodem*`
- Windows: `COM3` 或类似

### 只监视器

```bash
idf.py -p /dev/ttyUSB0 monitor
```

新闻 `Ctrl+]` 退出监视器。

## 使用方法

### 自动流线

启动后， ESP32 将：
1. 连接到配置 Wi-Fi 网络
2. 连接到 KMS 服务器
3. 创建媒体管道和端点
4. 开始发布 video/audio 流线
5. 登录浏览器端点 ID

### 命令行方式

您可以通过串行控制台控制流：

| 命令 | 说明 |
|---------|-------------|
| `start [server_url]` | 开始流到 KMS 服务器 (选项 URL 覆盖) |
| `stop` | 停止流线和断开 KMS |
| `i` | 显示系统信息 (内存， 升学时间， 类.) |
| `wifi <ssid> <password>` | 连接到新 Wi-Fi 网络 |

**示例:**
```
esp32> start ws://192.168.1.100:8888/kurento
esp32> stop
esp32> wifi MyNetwork MyPassword
```

### 获取浏览器端点 ID

何时 ESP32 成功发布到 KMS, 它会登录浏览器端点 ID：

```
I (417491) KWS_SIG: ========================================
I (417492) KWS_SIG: BROWSER_ENDPOINT_ID: 6e22da0f-d509-40bd-ae42-00a3c8b28244_kurento.MediaPipeline/0aad6cb2-75d9-411e-9658-0d7ab5420db8_kurento.WebRtcEndpoint
I (417493) KWS_SIG: ========================================
```

**复制此端点 ID** - 浏览器客户端需要它。

## 浏览器客户端

### 打开客户端

打开 [main/kurento_client.html](main/kurento_client.html) 在现代浏览器中。

### 配置

1. **Kurento WebSocket URL**: 输入您的 KMS 服务器 URL
   - 示例: `ws://192.168.1.100:8888/kurento`
   - 必须匹配服务器 ESP32 连接到

2. **ESP32 浏览器端点 ID**: 粘贴端点 ID 从 ESP32 日志
   - 格式: `xxx_kurento.MediaPipeline/yyy_kurento.WebRtcEndpoint`
   - 这是端点2 ID, NOT 端点1

3. **只接收**: 选中此框 (浏览器只接收， 不发送)

4. **STUN 服务器**: 可选， 默认为谷歌 STUN 服务器

### 连接中

1. 单击 **连接** 按钮
2. 等着 WebRTC 连接以建立
3. 视频应该出现在“远程视频”中 (ESP32)" 节次

### 解析浏览器连接

**问题: "找不到端点"**
- 校验端点 ID 正确的 (从 ESP32 日志)
- 确保 ESP32 仍然连接到 KMS
- 检查端点 ID 还没有过期 (ESP32 当浏览器断开时重新创建它)

**问题: "已谈判的端点"**
- ESP32 当浏览器断开时自动重现端点
- 等一等，拿到新的端点 ID 从 ESP32 日志
- 使用新生成的端点重新连接 ID

**问题: 未显示视频**
- 检查浏览器控制台出错
- 校验 WebRTC 连接状态显示“ 已连接 ”"
- 检查浏览器和网络连接 KMS 服务器


## 技术细节

### 信令执行

此演示使用自定义 Kurento WebSocket 指示执行 (`esp_signaling_get_kms_impl`). 信令流：

1. **WebSocket 连接**: ESP32 连接到 KMS WebSocket 端点
2. **管道创建**: 创建媒体管道 KMS
3. **端点创建**: 创建两个 WebRtcend 点：
   - 端点1: 为： ESP32'自动 WebRTC 连接
   - 端点2: 对于浏览器连接
4. **媒体路线**: 连接端点1 → 端点2
5. **SDP 交换**: 交换 SDP offer/answer 与 KMS
6. **ICE 候选人**: 交换 ICE 候选人 NAT 转弯
7. **媒体流**: 开始流线 video/audio

### 连接流程

关于……的完整解释 WebRTC 连接过程， 参考 [连接构建流程](../../components/esp_webrtc/README.md#典型调用序列-esp_webrtc)。


## 限制

- **会话隔离**: Kurento 对象的作用域为 WebSocket 会议. ESP32 而且浏览器必须使用不同的会话， 这就是为什么需要 endpoint2。
- **单一浏览器**: 每个浏览器连接都需要一个新的端点2. ESP32 当浏览器断开连接时自动重新创建 endpoint2。
- **网络要求**: 需要 STUN 服务器 NAT 遍历. TURN 受限网络所需的服务器。
- **演示用途**: 测试代码仅用于演示; 部署定制信令以使用更高级的功能。

## 附加资源

- [Kurento 文档](https://doc-kurento.readthedocs.io/)
- [WebRTC 规格](https://www.w3.org/TR/webrtc/)
- [ESP32 WebRTC 组件](../../components/esp_webrtc/README.md)

## 许可证

此示例代码属于公共领域 (CC0 有执照的)。
