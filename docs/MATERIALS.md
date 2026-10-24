# 完整资料导航

## 推荐阅读顺序

1. [仓库总览](../README.md)：目标、组成、个人开发范围、快速启动。
2. [项目状态](PROJECT_STATUS.md)：当前限制与旧资料差异。
3. [使用指南](submission/02_使用指南.md)：环境、接线、供电、构建和浏览器操作。
4. [架构说明](ARCHITECTURE.md) 与 [代码结构手册](submission/10_代码结构从0讲清.md)：业务链路与模块职责。
5. [开发日志](submission/01_开发日志.md)、[实机调试资料](bringup/) 和 [验证记录](VALIDATION.md)：问题定位与证据。

## 源码、开发与讲解

| 类别 | 路径 |
| --- | --- |
| 完整门铃应用与 14 个检查入口 | [`../solutions/doorbell_local`](../solutions/doorbell_local/) |
| 本地核心组件与原有其他示例 | [`../components`](../components/)、[`../solutions`](../solutions/) |
| 原始快照说明 | [`../SOURCE_SNAPSHOT.md`](../SOURCE_SNAPSHOT.md)；涉及最新状态请交叉阅读项目状态 |
| 原上游目录总览 | [`../README.upstream.md`](../README.upstream.md) |
| 开发日志、使用指南、发布说明、提交检查表 | [`submission`](submission/) |
| 10 分钟与 20 分钟代码录屏脚本 | [`submission/03_代码录屏讲解脚本.md`](submission/03_代码录屏讲解脚本.md)、[`submission/11_代码录屏讲解脚本_20分钟版.md`](submission/11_代码录屏讲解脚本_20分钟版.md) |
| 四层技术分析底稿 | [`submission/_analysis`](submission/_analysis/) |
| 单独整理的讲解目录版本 | [`code-walkthrough`](code-walkthrough/) |
| 雷达/WebRTC 测试矩阵、供电、板级适配、历史日志 | [`bringup`](bringup/) |
| 原源码中的设计计划 | [`superpowers`](superpowers/)；作为历史设计材料阅读 |
| 原始外层提交文档、检查表、录像脚本 | [`archive/submission-2026-09-14`](archive/submission-2026-09-14/) |

部分讲解副本与快照中的同名文件内容不同，故保留两者。两份完整源码内容相同，保留一份；旧目录名与说明中的“待录制”保留在历史文档里，实际视频见发布附件。

## 硬件与结构

| 资料 | 路径 |
| --- | --- |
| 主板原理图 | [`SCH_WT99P4C5-S1-1V1.pdf`](../hardware/reference/SCH_WT99P4C5-S1-1V1.pdf) |
| 主板尺寸图 | [`WT99P4C5-S1_OutlineSize_V1.0.pdf`](../hardware/reference/WT99P4C5-S1_OutlineSize_V1.0.pdf) |
| 主板使用指南 | [`WT99P4C5-S1开发板使用指南v1.5.pdf`](../hardware/reference/WT99P4C5-S1开发板使用指南v1.5.pdf) |
| 配套屏幕规格书 | [`WT99P4C5-S1开发板配套屏幕规格书.pdf`](../hardware/reference/WT99P4C5-S1开发板配套屏幕规格书.pdf)；只作参考，屏幕未集成 |
| 结构生成参数、程序和测试 | [`../cad/acrylic_plate`](../cad/acrylic_plate/) |
| V3 加工主文件 | [`acrylic_plate_cut_only_v3.dxf`](../output/acrylic/acrylic_plate_cut_only_v3.dxf) |
| V3 分层工程文件 | [`acrylic_plate_layered_v3.dxf`](../output/acrylic/acrylic_plate_layered_v3.dxf) |
| 正面 / 背面装配 | [`正面 SVG`](../output/acrylic/acrylic_plate_front_assembly_v3.svg)、[`背面 SVG`](../output/acrylic/acrylic_plate_rear_assembly_v3.svg) |
| V3 孔位表 | [`acrylic_plate_hole_table_v3.csv`](../output/acrylic/acrylic_plate_hole_table_v3.csv) |
| 四页工程图与 V1/V2 历史图 | [`../output/pdf`](../output/pdf/) |
| V1/V2/V3 加工包和历史向量图 | [`../output/acrylic`](../output/acrylic/) |
| 背板设计与加工计划 | [`archive/acrylic-design`](archive/acrylic-design/) |

V3 结构为 220×190×5 mm 亚克力背板、R10 外角、14 个安装孔和一个圆角过线孔。切割坐标以正面为准；装配图不代替切割图。旧版本按历史资料保留，本次几何测试不等于实物装配或厂家验收。

## 视频与固件

[归档发布页](https://github.com/wudi-tech/esp32-p4-c5-webrtc-doorbell/releases/tag/archive-2026-10-24) 提供：

- `doorbell-demo.mp4`：项目使用流程演示，约 439 MB。
- `code-walkthrough.mp4`：代码讲解，约 445 MB。
- `wt99p4c5-doorbell-firmware.zip`：历史公开 P4 固件、烧录脚本与说明。
- `acrylic-manufacturing-v3.zip`：V3 加工交付包。
- `SHA256SUMS.txt`：发布附件的 SHA-256 校验值。

固件也展开在 [`../firmware/2026-09-14`](../firmware/2026-09-14/)；GitHub 还会按标签自动生成源码 ZIP / TAR 下载。视频只保存一份于 Release，避免在 Git 历史中反复复制大型二进制。

## 归档边界

项目无关的招新签到、人员名单、它们的预览与处理脚本未纳入。构建缓存、依赖缓存、编辑器临时文件、工作树及原始第三方参考视频也未纳入。旧全量压缩包由现有源码、文档、固件与视频拆分归档替代，完整范围见 [整理说明](PROVENANCE.md)。
