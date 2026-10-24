# AV 渲染测试应用程序

这是一个全面的音频视频渲染测试应用程序，用于 **ESP32P4** 联合国 **ESP32S3** 平台， 显示实时媒体渲染能力。

## 概述

应用程序从硬件来源获取音频和视频 (摄像机和麦克风) 并使他们通过 LCD 显示和 I2S 音频输出， 形成完整的端到端介质管道测试。

## 架构

### 系统组件

测试应用分为两大子系统：

1. **捕获系统** — 获得媒体投入 (摄影机， 麦克风)
2. **渲染系统** — 处理媒体输出 (本文件的重点)

### 渲染系统组件

```c
typedef struct {
    audio_render_handle_t audio_render;  // I2S audio renderer
    video_render_handle_t video_render;  // LCD video renderer
    av_render_handle_t    player;        // Combined AV renderer
} player_system_t;
```

- **音频渲染**: I2S-基于音频输出， 固定时钟模式， 6KB缓冲器
- **视频渲染**: LCD-基于视频输出， 500KB 缓冲器， 支持 MJPEG
- **玩家 (av_render)**: 组合音频和视频渲染器， 管理同步和缓冲使用

## 关键函数

### `build_player_system()`

初始化渲染系统组件：

```c
static int build_player_system(void)
{
    // Create I2S audio renderer
    i2s_render_cfg_t i2s_cfg = {
        .fixed_clock = true,
        .play_handle = get_playback_handle(),
    };
    player_sys.audio_render = av_render_alloc_i2s_render(&i2s_cfg);

    // Create LCD video renderer
    lcd_render_cfg_t lcd_cfg = {
        .lcd_handle = board_get_lcd_handle(),
    };
    player_sys.video_render = av_render_alloc_lcd_render(&lcd_cfg);

    // Create combined AV renderer
    av_render_cfg_t render_cfg = {
        .audio_render = player_sys.audio_render,
        .video_render = player_sys.video_render,
        .audio_raw_fifo_size = 4096,
        .audio_render_fifo_size = 6 * 1024,
        .video_raw_fifo_size = 500 * 1024,
        .allow_drop_data = false,
    };
    player_sys.player = av_render_open(&render_cfg);
}
```

### `test_capture_to_player()`

演示捕获至移交管道：

- **音频流**：
  G.711A 边框 (8千赫兹， 单) → `av_render_add_audio_data()` → I2S speakers/headphones

- **视频流**：
  MJPEG 边框 → `av_render_add_video_data()` → LCD 显示

## 渲染配置

### 音频
- 编码c: G.711A
- 样本率: 8千赫兹
- 频道: 单曲
- 缓冲: 4KB 生 + 6KB 制导 FIFO

### 视频
- 编码c: MJPEG
- 决议: 可配置 (`VIDEO_WIDTH`, `VIDEO_HEIGHT`)
- 帧率: 可配置 (`VIDEO_FPS`)
- 缓冲: 500KB 未加工 FIFO

## 性能特征

### 线索排程

```c
static void thread_scheduler(const char *thread_name, media_lib_thread_cfg_t *schedule_cfg)
{
    if (strcmp(thread_name, "venc_0") == 0) {
        schedule_cfg->priority = 10;
        #if CONFIG_IDF_TARGET_ESP32S3
        schedule_cfg->stack_size = 20 * 1024;
        #endif
    }
    else if (strcmp(thread_name, "AUD_SRC") == 0) {
        schedule_cfg->priority = 15;  // Higher priority for audio
    }
}
```

- 低空音频优先排序
- 视频分配更大的堆栈进行处理

### 缓冲管理
- **音频**: 双-FIFO (原始 + 制成) 防止辍学
- **视频**: 500KB 缓冲器 MJPEG 框架变化
- **放下保护**: `allow_drop_data = false` 保证框架完整性

## 支持的平台

### ESP32P4
- 视频输入: V4L2 (`/dev/video0`)
- 摄影机: MIPI CSI / DVP
- 处理: 硬件加速

### ESP32S3
- 视频输入: DVP
- 摄影机: DVP-基于
- 处理: 仅限软件

## 使用方法

1. **初始化**: `media_sys_buildup()`
2. **运行测试**: `test_capture_to_player()`
3. **连续操作**: 实时抓取 → 解码 → 提交

## 质量设置

编辑 `settings.h` 以调整播放质量：

- `VIDEO_WIDTH`
- `VIDEO_HEIGHT`
- `VIDEO_FPS`

## 依赖项

- `av_render` — 核心渲染库
- `esp_capture` — 抓取库
- `codec_board` — 董事会专用代码支持
- `media_lib_sal` — 媒体库调整层
