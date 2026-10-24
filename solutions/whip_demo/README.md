# WHIP 发布客户端Demo

## 概述

本示例演示如何使用 `esp_webrtc` 作为 WHIP 发布客户端以流媒体到 a WHIP 服务器。

## 硬件要求

- 一个 [ESP32P4-函数 Ev- Board](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32p4-function-ev-board/user_guide.html) (包括a SC2336 摄影机)。

## WHIP 服务器设置

测试， 您可以使用 [介质MTX](https://github.com/bluenviron/mediamtx) 作为你的 WHIP 服务器。

### 配置介质MTX

1. **用户认证**
   在你身边 `mediamtx.yml` 文件， 添加用户账户：
   ```yaml
   authInternalUsers:
     - user: username
       pass: password
   ```

2. **STUN/中继服务器**
   添加一个 STUN (或继电器) 服务器：
   ```yaml
   webrtcICEServers2:
     - url: stun:stun_server:3478
   ```

## 如何构建

### IDF 版本

你可以使用两个 IDF 主树枝或树枝 IDF 释放 v5.4。

### 配置步骤

1. **Wi-Fi 设置**
   更新 Wi-Fi SSID 密码 [main/settings.h](main/settings.h)。

2. **WHIP 服务器设置**
   修改 WHIP 服务器 URL 和访问符 [main/settings.h](main/settings.h)。

3. **ICE 语句支持**
   有点 WHIP 服务器只能支持 ICE 字节模式. 确保启用 ICE 输入 [main/settings.h](main/settings.h) 必要时。

4. **对其他理事会的支助**
   关于支持其他董事会的指示， 见 [codec_board README](../../components/codec_board/README.md)。

### 架构和闪耀

构建， 烧录， 并用：
```bash
idf.py -p YOUR_SERIAL_DEVICE flash monitor
```

## 测试

启动后， 董事会会：
1. 连接到配置 Wi-Fi 网络。
2. 自动将媒体流推向 WHIP 成功连接时的服务器。

也可以控制流经 CLI 命令：
- `start server_url token` : 开始流到 WHIP 服务器。
- `stop` : 停止流线。
- `i` : 显示系统信息。
- `wifi` : 连接到新 Wi-Fi 通过指定 SSID 密码。

### 播放已发布的流

要查看溪流， 使用情况 `ffplay` 与：
```bash
ffplay rtsp://username:password@whip_server_ip:rtsp_port/push_stream_name
```
## 技术细节

此演示使用自定义信令执行 `esp_signaling_get_whip_impl`, 切换 SDP 与 WHIP 服务器. 详细过程如下：：
- 客户端发送一个初始 SDP 页:1 WHIP 服务器。
- 如果答复包括： STUN 服务器 URL, 客户端会打电话 `esp_peer_update_ice_info` 然后发送一个 PATCH 有新候选人通知服务器。
- 当信令关闭时，它会发出 `DELETE` 删除会话

连接过程的完整说明请参阅[连接建立流程](../../components/esp_webrtc/README.md#典型调用序列-esp_webrtc)。
