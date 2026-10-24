# Codec_board

## 概述
`codec_board` 是一种简单的执行 董事会支持各种 ESP32 系列板. 
它的主要目的是：
- 从用户收集特定局外设置。
- 为支持的外围添加驱动程序。
- 提供高级API，用于与外围互动或将外围抽象为可控控控件。

## 支持的外设

- **编码c**  
  目前支助 `ES8311`, `ES8388`, `ES7210`, `ES7243`, 用户也可以添加其他编码器.  
  之后 `init_codec` 名为， 解码器被抽象为 `esp_codec_dev` 控件.  
  然后可以使用 `esp_codec_dev` API 用于播放和记录.  
  简单的编解码器配置为：
```
out: {codec: ES8311, pa: 38, use_mclk: 0, pa_gain:6}
in: {codec: ES7210, i2s_port: 1}
```

- **SD卡**  
  用户可以配置相关的 SDCard GPIO 然后使用 API `mount_sdcard` 以挂载此卡片以使用.  
  SDcard 配置为：
```
sdcard: {clk: 43, cmd: 44, d0: 39, d1: 40, d2: 41, d3: 42}
```

- **摄影机**  
  目前支助 DVP 联合国 MIPI 摄影机， 用户可以使用相机设置 API `get_camera_cfg`

- **LCD**  
   目前仅支持 LCD 打开 `S3_Korvo_V2` 联合国 `ESP32_P4_FUNCTION_EV`。
   用户可以得到 LCD 操作通过 API `board_get_lcd_handle` 之后 `board_lcd_init`

## 自定义新局
要重用预定义的设备， 遵循这些步骤：
1. 在 [board_cfg.txt](board_cfg.txt) 文件中添加一个新章节，并按需修改相应的 GPIO 设置。
2. 调用 `set_codec_board_type` 与新添加的章节名称一起.  
新设备， 您需要手动修改代码。

## 与esp-bsp的比较

[页:1](https://github.com/espressif/esp-bsp) 支持几乎所有随身携带的设备， 而此模块只支持媒体相关设备. 如果将同一设备与不同设备重用，则提供快速核查选项。 GPIO 设置. 用户可以轻松替换此模块 `esp-bsp` 通过下列替代：

- `bsp_audio_codec_speaker_init`: 替换 `get_playback_handle`
- `bsp_audio_codec_microphone_init`: 替换 `get_record_handle`
- `bsp_display_new`: 替换 `board_get_lcd_handle`
