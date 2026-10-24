# 变更记录

## v2.0.2

- 已修复 LCD 初始化失败，原因是时钟错误 IDFv6.x
- 已添加对…的支持 `esp32s31_korvo_1` 板

## v2.0.1

- 支持获取 LCD IO 处理 `board_get_lcd_io_handle`

## v2.0.0

- 已修复 `fb_num` 配置无效
- 已添加 `Kconfig` 选择默认板使用 API `set_default_codec_board`

## v1.0.2

- 固定引脚解析错误

## v1.0.1

- 升级依赖项 `esp_codev_dev` 到 v1.5.x

## v1.0.0

- 升级依赖项 `esp_codev_dev` 到 v1.4.0
- 已添加 API `codec_board_parse_all_config` 用于不在其中的易于定制的板 `board_cfg.txt`
- 已添加 API `get_playback/record_i2s_channel` 获取低级别 I2S 通道
- 已修复构建问题于 IDFv6.0

## v0.5.5

- 添加板级支持 `ESP32_S3_KORVO_2L`, `ESP32_S3_EchoEar`, `ATOMS3_ECHO_BASE`
- 在 esp32s3 和 esp32p4 上挂载 SD 卡时使用高速以获得更好的性能

## v0.5.4

- 添加 PDM 支持
- 添加播放和录音支持 `XD_AIOT_C3` 和 `ESP_SPOT`

## v0.5.3

- 为 esp32 添加挂载 SD 卡的支持
- 添加对…的支持 I2C 手动控制使用 `init_i2c` 和 `deinit_i2c`
- 已修复 I2S 如果超过一个，驱动程序初始化失败

## v0.5.0

- 初始版本 `codec_board`
