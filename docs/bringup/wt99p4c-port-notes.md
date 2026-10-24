# WT99P4C5-S1 WebRTC 端口笔记

## 核实的硬件基线

- ESP32-P4 订正 v1.3 有32个 MB PSRAM 第16条 MB 烧录。
- OV5647 MIPI-CSI 摄影机， 检测为 PID `0x5647` 1280x960时。
- ESP32-C5 连接到 P4 通过四位元 SDIO。
- ES8311 音频路径匹配现有的 `ESP32_P4_DEV_V14` 代码配置文件在
  `components/codec_board/board_cfg.txt`: I2C SDA/SCL 7/8, I2S
  MCLK/BCLK/WS/DOUT/DIN 13/12/10/9/11, 联合国 PA 启用 53。
- 卖家演示经过15分钟的连续稳定运行，没有中断或恐慌。

## 任务4配置

- 相机传感器模式: OV5647 RAW10 1280 x 960 在45英尺宽处进行宾装。
- 初次 WebRTC 流线: 1280 x 960， 在 20 fps 时。
- 烧录大小: 16 MB, QIO 保留。
- C5 SDIO 针头: CLK 18, CMD 19, D0-D3 14-17, 重设 54。
- P4 修订兼容性被固定在 v1.x 因为安装的芯片是 v1.3。
- 首选东道运输缓冲器 PSRAM, 匹配稳定的供应商演示设置。
- 以太网和行人检测仍然无法进行; GPIO35 继续按键。
- 现有 `ESP32_P4_DEV_V14` 代码配置文件仍然用于任务4. 任务6
  将引入明确的 `WT99P4C5_S1` 配置文件之后 WebRTC 验证。

已解析管理组件使用当前 Kconfig 名称
`CONFIG_ESP_HOSTED_IDF_SLAVE_TARGET` 大写 `1280X960` 相机模式
符号. 旧计划名称 (`CONFIG_IDF_SLAVE_TARGET`, 小写 `x`, 联合国
`CONFIG_ESP_HOSTED_CUSTOM_SDIO_PINS`) 未由这些组件定义
版本; 个人 SDIO 上面的针设置是有效的控件。

## 当地 Wi-Fi 设置

复制 `solutions/doorbell_local/main/settings.local.h.example` 改为
`settings.local.h`, 然后用测试替换占位符 2.4 千兆赫 SSID 联合国
密码. `settings.local.h` 被基特忽略了，不能犯错。

## 已知观察

供应商演示报告 ESP-主机版本 2.12.0 和协处理器版本
0.0.0, 时 C5 尚未完成 SDIO 初始化和 Wi-Fi 启动. 这样做
不覆盖 C5 任务4期间; 仅在任务5显示时重新评估 RPC 超时。
