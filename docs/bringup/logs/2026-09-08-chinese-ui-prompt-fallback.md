# 中文 UI 和即时倒计时核查

日期: 2026-09-08  
目标: WT99P4C5-S1 ESP32-P4, COM5  
页次: `https://192.168.0.103/webrtc/test`

## 已核实更改

- 替换所有用户可见的英文页面标签， 状态， 对话框， 通知， 联合国
  带有中文文本的传感器事件描述。
- 将事件历史扩展为 240–360 px 可滚动区域并启用
  长雷达记录的包装。
- 添加了写作 16 kHz 单曲的快速折返工人 PCM 独立
  收到 WebRTC 音频框架。
- 连续倒计时和正常 WebRTC 渲染器写入， 并保持正常状态
  远程音频流动时在框中混合。
- 排出一个早期的打开门提示， 直至 16 kHz 调制器变成
  可用。

## 自动证据

- 土著 PCM 覆盖单元测试: 通过。
- 门锁， 环形政策， 存在聚变， LD2450 解析器， 序列化环,
  回合二 UI/信令， 结构化雷达计划， 任务 8.3/8.4 融合,
  和棋盘配置检查: 通过。
- 嵌入式页面 JavaScript 语法检查: 通过。
- Whitespace/error 检查: 通过 (只有行尾转换警告)。
- 现场页面回复: HTTP 200, `text/html; charset=utf-8`; 中国的管制,
  中国事件渲染， 扩大面板 CSS, 包装 CSS 中文 RTP
  文本全部显示。

## 固件证据

- 应用程序大小: `0x65dd20` 字节; 20%的应用程序分区免费。
- COM5 烧录: 6,675,写入 744 字节; 图像散列已校验; 硬重置
  已完成。
- 启动: ESP32-P4 订正 v1.3, 32 MB PSRAM 测试 OK, ES8311 已初始化,
  GPIO32 门锁已初始化， LD2450 UART1 已初始化， OV5647 检测， C5
  四位数 SDIO 活动， 联合国 HTTPS 信令开始于 `192.168.0.103`。
- 启动时迅速解码为95的资产,574 PCM 16 kHz 单位的字节。

## 其余实际接受

通话建立后，单击一次 `开门`，并监听开发板的声音。预期序列如下：

1. `Prompt playback armed`
2. 无论是 `Prompt mixed with WebRTC remote audio` 或
   `Prompt fallback output started after remote-audio silence`
3. `Prompt playback completed`

用户必须确认，在电话未打完的情况下，口头提示可以发出。
已连接。
