# 门铃圆形两轮可靠性修复实施计划

> **代理工人:** REQUIRED SUB-SKILL: 使用超能力:亚剂驱动开发 (建议) 或超级大国:逐项任务执行计划. 步骤使用复选框 (`- []`) 跟踪的语法。

**目标:** 修复呼叫中的开门音频， 传感器事件介绍， 同步铃声， 并承认挂断 而不倒退现有的 WT99P4C5-S1 门铃功能。

**架构:** 当地项目 PCM 覆盖已存在的 I2S 音频渲染器，所以提示可以混合而不替换 WebRTC 解码器. 董事会生成的信令事件成为完成环并呼叫拆掉的真相来源， 而浏览器将视线检测连续成片段，并在确认拆解后明确清除介质。

**技术堆栈:** ESP-IDF 5.5.5, ESP WebRTC 解决方案， Espressif 音频 decoder/resampler, C, 内嵌 HTML/Java 脚本， Python 源验证器， 本地C单元测试。

---

### 任务1: PCM 提示覆盖

**文件:**
- 创建: `solutions/doorbell_local/main/pcm_overlay.h`
- 创建: `solutions/doorbell_local/main/pcm_overlay.c`
- 创建: `solutions/doorbell_local/main/doorbell_audio_render.h`
- 创建: `solutions/doorbell_local/main/doorbell_audio_render.c`
- 创建: `solutions/doorbell_local/tests/pcm_overlay_test.c`
- 修改: `solutions/doorbell_local/main/CMakeLists.txt`

- [x] **步骤1: 写入失败的本地测试**

添加以下断言: 启动叠加混合将快速样本混合到目的地 PCM, 上面的夹 `INT16_MAX` 及以下各项 `INT16_MIN`, 标记完成， 从样本零重新开始。

- [x] **步骤2: 运行测试以验证它失败**

运行主机 C 编译器对 `pcm_overlay_test.c` 联合国 `pcm_overlay.c`. 预期: 编译失败， 因为覆盖 API 不存在。

- [x] **步骤3: 执行最小覆盖和渲染器包装**

定义 `pcm_overlay_start`, `pcm_overlay_stop`, `pcm_overlay_active`, 联合国 `pcm_overlay_mix`. 执行一个 `audio_render` 包装器 `write` 回调锁定其快捷状态， 混合兼容 16 kHz 单位 16 位框架， 然后将框架转发到现有的 I2S 渲染器。

- [x] **步骤4: 运行测试来验证它是否通过**

预期: 所有重叠的断言通过，带有退出代码 0。

### 任务2: 准备并启动开门提示

**文件:**
- 修改: `solutions/doorbell_local/main/media_sys.c`
- 修改: `solutions/common/media_sys.h`
- 修改: `solutions/doorbell_local/main/webrtc.c`
- 修改: `solutions/doorbell_local/tests/verify_task_8_3_8_4.py`

- [x] **步骤1: 添加失败的源检查**

需要启动 AAC decode/resample 准备， 覆盖渲染器配置， a `play_open_door_prompt_in_call()` API, 和一个不引用 `play_music`。

- [x] **步骤2: 运行验证器确认失败**

预期: 缺少呼叫提示 preparation/trigger 检查失败。

- [x] **步骤3: 实施快速准备和连接状态路由**

分析 ADTS 格式， 解码嵌入 `open.aac`, 复制到 16 kHz 单位签名 16 位 PCM, 保留缓冲， 然后从 `OPEN_DOOR` 何时 `DOOR_BELL_STATE_CONNECTED`. 保留现有的 AAC 非呼叫状态的路径。

- [x] **步骤4: 运行本地和源代码测试**

预期: 覆盖测试和更新的核查员通行证。

### 任务3: UTF-8次和分组传感器事件

**文件:**
- 修改: `solutions/doorbell_local/main/webrtc_http_server.c`
- 修改: `solutions/doorbell_local/main/webrtc_test.html`
- 创建: `solutions/doorbell_local/tests/verify_round_two_ui.py`

- [x] **步骤1: 写入失败 UI 检查**

需求 `text/html; charset=utf-8`, a 是一个 ASCII 雷达分隔符， 标签 `Sensor Events`, 解释性副本， 行人活动旗， 和一个不附加重复条目而刷新的计时器。

- [x] **步骤2: 运行验证器确认失败**

预期: UTF-8项声明， 更名 UI, 和分集检查失败。

- [x] **步骤3: 执行页面更改**

设定 HTTP 内容类型， 替换分隔符， rename/explain 列表， 并使用三秒钟的无活动窗口进行连续人员检测，同时保留100进封顶。

- [x] **步骤4: 运行验证器**

预期: 所有文本和传感器活动检查通过。

### 任务4: 板式同步环和确认挂机

**文件:**
- 修改: `solutions/doorbell_local/main/webrtc.c`
- 修改: `solutions/doorbell_local/main/webrtc_test.html`
- 修改: `solutions/doorbell_local/tests/verify_round_two_ui.py`

- [x] **步骤1: 扩展失败的检查**

需求 `RING_STOPPED`, 环形护卫， `CALL_ENDED`, `CALL_END_FAILED`, a 是一个 `Ending call...` 状态， 延迟清理， 确认超时， 和清晰的视频 `load()` 清理。

- [x] **步骤2: 运行验证器确认失败**

预期: 环同步和挂载确认检查失败。

- [x] **步骤3: 执行棋盘信令**

安排一个四秒钟的戒具工人 并进行发电验证. 发送 `CALL_ENDED` 仅在对等方禁用成功后， `CALL_END_FAILED` 禁用或排程失败后。

- [x] **步骤4: 执行浏览器状态处理**

停止浏览器环 `RING_STOPPED` 带有 4.5-second 倒转. 分开 `requestHangup()` 从 `finalizeHangup()`, 等待时 POST 错误， 最后敲定 acknowledgment/remote 终端状态， 恢复重试 timeout/failure。

- [x] **步骤5: 运行验证器**

预期: 所有铃声和挂机检查通过。

### 任务5: 回归， 构建， 烧录， 以及文档

**文件:**
- 修改: `docs/bringup/webrtc-test-matrix.md`
- 创建: `docs/bringup/logs/2026-09-07-round-two-build-flash.md`

- [x] **步骤 1: 运行所有门铃本地测试**

运行本州的测试以及每一个 `verify_*.py` 剧本. 预期: 全部通过。

- [x] **步骤2: 建立 ESP32-P4 应用**

激活 `E:\Espressif\frameworks\esp-idf-v5.5.5\export.bat`, 构建 `solutions/doorbell_local`, 并要求退出代码为0，并报告分区余量。

- [x] **步骤 3: 烧录 COM5 并捕获启动日志**

如果自动重置不可靠，请以保守的波特率进行闪存， 然后监控足够长的时间以确认启动， Wi-Fi, HTTP 信令， 雷达初始化， 以及提示准备。

- [x] **步骤 4: 更新硬件测试矩阵和日志**

记录通话中提示的预期通过标准， 四秒同步响铃， 分组传感器事件， 已确认挂断， 以及拆除后的第二次调用。

- [x] **步骤5: 查看差异并报告仅硬件检查**

确认差异中不包含无关的回退，并仅报告用户需要执行的剩余实际操作。
