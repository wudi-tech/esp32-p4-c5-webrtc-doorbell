# 源码快照说明

- 项目：WT99P4C5-S1 本地 WebRTC 智能门铃
- 导出日期：2026-09-16
- 开发分支：`feat/wt99p4c5-doorbell`
- 上游基础：Espressif `esp-webrtc-solution`
- ESP-IDF：v5.5.5
- 目标芯片：ESP32-P4

本目录是最终实机测试版本的源码快照，包含尚未提交到 Git 的本项目修改，因而比单纯下载上游仓库或导出最后一次 Git 提交更完整。

为控制体积并保护隐私，快照没有包含：

- `.git` 历史和工作树元数据；
- `build`、`managed_components` 等可重新生成的构建缓存；
- `settings.local.h` 等私人 Wi-Fi 配置；
- 临时 PID、编辑器缓存和本机工具状态。

依赖会在 ESP-IDF 首次配置或构建时根据各组件的 `idf_component.yml` 自动获取。首次构建前，请阅读 `docs/submission/02_使用指南.md` 并由 `settings.local.h.example` 创建自己的本地配置。

本快照已在 ESP-IDF v5.5.5 下从空构建目录验证：1850 个步骤全部完成，生成的应用镜像大小为 `0x68fd20` 字节，8 MiB 应用分区剩余 18%。详细逐文件结论见 `docs/submission/09_提交源码完备性审计.md`。

项目范围说明：摄像头只判断人员/人脸是否存在，不做人脸身份识别；雷达提供二维目标坐标；舵机模拟门锁；配套显示屏未纳入当前版本。
