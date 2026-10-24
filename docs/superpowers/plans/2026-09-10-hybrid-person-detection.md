# 混合探测执行计划

> **代理工人:** REQUIRED SUB-SKILL: 使用超能力:亚剂驱动开发 (建议) 或超级大国:逐项任务执行计划. 步骤使用复选框 (`- []`) 跟踪的语法。

**目标:** 添加 ESP32-P4 以诚实信任标签检测行人加面孔， 雷达振铃， 直播中国浏览器覆盖， 优雅的退化， 未来 screen/identity 扩展连接。

**架构:** 保留现有的320x180 RGB565 探测槽，用统一的视线管道取代运动即时存在. 步行者检测仍是首要的; `HumanFaceDetect` 是近地倒计时， 输入主机可测试时间跟踪器和结构化遥测快照. WebRTC/control 交通量仍然处于较高优先地位，并丢弃陈旧的探测器框架。

**技术堆栈:** ESP-IDF 5.5.5, ESP-DL 3.3.x, `espressif/pedestrian_detect` 0.3.x, `espressif/human_face_detect` 0.2.3, C/C++, 浏览器 JavaScript/SSE, Python 主机验证， COM5。

---

### 任务1: 可测试的主机统一视觉跟踪器

**文件:**
- 创建: `solutions/doorbell_local/main/vision_tracker.h`
- 创建: `solutions/doorbell_local/main/vision_tracker.c`
- 创建: `solutions/doorbell_local/tests/vision_tracker_test.c`
- 创建: `solutions/doorbell_local/tests/verify_vision_tracker.py`
- 修改: `solutions/doorbell_local/main/CMakeLists.txt`

- [ ] **步骤1: 写入失败的跟踪测试**

定义 `vision_detection_t`, `vision_track_t`, `vision_tracker_config_t`, 和分数阈值测试， 三者确认， 3 个未清除， invalid/full-frame 拒绝， 和面部阴茎减震。

- [ ] **步骤2: 运行测试并验证失败**

运行: `python solutions/doorbell_local/tests/verify_vision_tracker.py`

预期: 编译失败， 因为 `vision_tracker.c` 联 合 国 API 不存在。

- [ ] **步骤3: 执行最小纯 C 跟踪器**

展示这些稳定的切入点：

```c
vision_tracker_config_t vision_tracker_default_config(void);
bool vision_tracker_init(vision_tracker_t *tracker,
                         const vision_tracker_config_t *config);
vision_update_t vision_tracker_update(vision_tracker_t *tracker,
                                      const vision_detection_t *detections,
                                      size_t detection_count,
                                      uint16_t frame_width,
                                      uint16_t frame_height,
                                      uint32_t now_ms);
```

使用每类四位点击历史， 基于IoU的短期匹配， 类型特定阈值 0.70/0.75, 和最多三个产出目标. `VISION_KIND_MOTION` 永远不能确认。

- [ ] **步骤4: 运行跟踪器测试**

运行: `python solutions/doorbell_local/tests/verify_vision_tracker.py`

预期: `PASS: vision tracker`。

### 任务2: 结构化视觉状态和聚变合同

**文件:**
- 创建: `solutions/doorbell_local/main/vision_event.h`
- 创建: `solutions/doorbell_local/main/vision_event.c`
- 创建: `solutions/doorbell_local/tests/vision_event_test.c`
- 修改: `solutions/doorbell_local/main/multimodal_fusion.h`
- 修改: `solutions/doorbell_local/main/multimodal_fusion.c`
- 修改: `solutions/doorbell_local/tests/multimodal_fusion_test.c`
- 修改: `solutions/doorbell_local/main/CMakeLists.txt`

- [ ] **步骤1: 写入失败的序列器和核聚变测试**

要求范围 JSON 这种形状和明确的模型状态：

```json
{"type":"vision_state","timestamp_ms":123,"state":"confirmed","source_width":320,"source_height":180,"targets":[{"track_id":1,"kind":"face","score":0.86,"x":10,"y":20,"width":40,"height":50,"confirmed":true}],"pedestrian_model":"ready","face_model":"ready"}
```

测试运动不能满足视觉， 确认 face/pedestrian 能满足视觉， 和雷达离开 清除访问。

- [ ] **步骤2: 校验测试失败**

运行新主机测试加 `python solutions/doorbell_local/tests/verify_task_8_3_8_4.py`。

预期: 缺少 serializer/types 在当前测试保持绿色的情况下失败。

- [ ] **步骤3: 执行序列器和打印聚变证据**

使用 `snprintf` 拒绝调校. 以 `VISION_EVIDENCE_FACE` 联合国 `VISION_EVIDENCE_PEDESTRIAN`; 不接受动议证据。

- [ ] **步骤4: 运行所有受影响的主机测试**

预期: 序列器， 聚变， 雷达， 门锁， 音频覆盖， 和现有的照相机测试合格证。

### 任务3: 整合 ESP32-P4 面额模型

**文件:**
- 修改: `solutions/doorbell_local/main/idf_component.yml`
- 修改: `solutions/doorbell_local/sdkconfig.defaults.esp32p4`
- 修改: `solutions/doorbell_local/partitions.csv`
- 修改: `solutions/doorbell_local/main/common.h`
- 修改: `solutions/doorbell_local/main/pedestrain_detect.cpp`
- 修改: `solutions/doorbell_local/main/CMakeLists.txt`

- [ ] **步骤1: 添加版本兼容组件依赖性**

添加 `espressif/human_face_detect: "^0.2.3"` 依据现行行人特征规则， ESP-DL 3.3.x. 将模型存储在烧录棒中， 除非解析的组件需要专用 `human_face_det` 分区; 保存当前行人分区。

- [ ] **步骤2: 重新配置一次以获取和检查组件 API**

运行从一个 ESP-IDF 5.5.5 环境：

```powershell
idf.py -C solutions/doorbell_local reconfigure
```

预期: `human_face_detect` 解决而不改变 ESP-DL 外部 3.3.x。

- [ ] **步骤3: 以统一的探测器排程器取代现时运动**

证据 `PedestrianDetect` 联合国 `HumanFaceDetect` 一次. 先跑个行人， 当行人没有合理结果或雷达焦点要求时， 运行面部， 将所有模型结果转换为 `vision_detection_t`, 并输入跟踪器. 动议可能唤醒管道，但不得召回。

- [ ] **步骤4: 添加非屏蔽降解和计时日志**

报告模型初始化状态， 每个模型推断时间， 帧跳动， 2对1 FPS 退化， 恢复， 证实和清除. 一个失败的表情模型让行人操作活动。

### 任务4: 将视觉快照连接到应用程序

**文件:**
- 修改: `solutions/doorbell_local/main/common.h`
- 修改: `solutions/doorbell_local/main/webrtc.c`
- 修改: `solutions/doorbell_local/main/main.c`
- 修改: `solutions/doorbell_local/main/webrtc_http_server.h`
- 修改: `solutions/doorbell_local/main/webrtc_http_server.c`

- [ ] **步骤1: 定义一个应用程序召回**

用带有目标阵列的快照回调替换仅区域回调， 保健模式， 确认状态和时间戳. 旧的符号只保存为兼容性包装， 如果使用其他的解决方案。

- [ ] **步骤2: 发布最新值的遥测**

序列化和发布 `vision_state` 不超过2赫兹. 摩擦的快照如此生动 AI 无法删除数据 WebRTC 信令或雷达 enter/leave 事件。

- [ ] **步骤3: 仅从确认的语义结果驱动多式聚变**

使用面部或行人证据进行调用. 仅在视觉存在改变时才释放过渡事件; 不附加每帧一个事件。

- [ ] **步骤4: 添加未来状态快照连接**

显示一个小 `doorbell_status_snapshot_t` 有人员在场， 电话， 锁定， AI 和网络状态. 不初始化 LCD 硬件或分配显示缓冲器。

### 任务5: 添加中国浏览器覆盖和状态面板

**文件:**
- 修改: `solutions/doorbell_local/main/webrtc_test.html`
- 修改: `solutions/doorbell_local/tests/verify_round_two_ui.py`
- 创建: `solutions/doorbell_local/tests/verify_hybrid_vision_ui.py`

- [ ] **步骤1: 写入失败的静态 UI 合同检查**

要求画布与视频对齐，使用中文标签，处理 `vision_state`，在数据超过 3000 毫秒未更新时清除显示，格式化分数并显示模型健康状态；使用安全的 `textContent`，且不得出现 `近距离人员` 字样。

- [ ] **步骤2: 校验 UI 测试失败**

运行: `python solutions/doorbell_local/tests/verify_hybrid_vision_ui.py`

预期: 缺少 canvas/message 处理断言失败。

- [ ] **步骤3: 实施响应性覆盖绘图**

绘制绿色行人， 黄脸， 和灰色候选框. 地图320x180 通过显示的视频源坐标 `object-fit: contain` 矩形， 页:1. 在3000毫秒后清除箱子，没有快照。

- [ ] **步骤4: 执行仅覆盖的活状态和仅过渡历史**

分别显示 `人脸 86%`、雷达距离和 `已确认有人`。SSE 重连时替换实时状态，不得添加重复的历史记录。

- [ ] **步骤5: 运行浏览器静态检查**

预期: 现有中文 UI 试验和新的混合愿景 UI 测试合格。

### 任务6: 全面核查和 ESP32-P4 构建

**文件:**
- 修改: `solutions/doorbell_local/tests/verify_wt99p4c5_config.py`
- 修改: `docs/bringup/webrtc-test-matrix.md`

- [ ] **步骤1: 运行所有门铃主机校验脚本**

运行 `verify_*.py` 下级 `solutions/doorbell_local/tests`. 预期: 全部返回零。

- [ ] **步骤2: 重新配置和构建**

运行：

```powershell
idf.py -C solutions/doorbell_local reconfigure
idf.py -C solutions/doorbell_local build
```

预期: ESP32-P4 应用程序， 装填器， 分区表格， 行人模型和面部模型都成功建造，没有分区溢出。

- [ ] **步骤3: 记录构建证据**

用依赖性版本更新测试矩阵， 二进制大小， 自动测试命令和剩余真设备检查。

### 任务7: COM5 闪存和未处理的启动日志抓取

**文件:**
- 创建: `docs/bringup/logs/2026-09-10-hybrid-person-detection-runtime.log`
- 修改: `docs/bringup/webrtc-test-matrix.md`

- [ ] **步骤1: 烧录 COM5 以保守速度进行**

运行：

```powershell
idf.py -C solutions/doorbell_local -p COM5 -b 115200 flash
```

预期: 所有应用程序和模型分区都成功验证，棋盘硬重置。

- [ ] **步骤2: 抓取启动日志**

捕获 COM5 在115200baud 足够观察靴子， C5/Wi-Fi, HTTPS, 摄影机， 雷达， 行人型号， 脸型和 AI 调度器状态。

- [ ] **步骤3: 如果自动重置失败， 重试一次**

重试 COM5 重建闪存参数后一次. 如果端口仍然无法输入下载模式， 记录确切的要求: 拿着 BOOT, 简短按下 RST, 释放 BOOT, 然后重运行闪存命令。

- [ ] **步骤4: 更新证据和移交**

记录准确的闪存结果， 启动结果， 当前 IP 可用时， 自动测试状态， 和手册 five-minute/near-field/three-call/fifteen-minute 剩余支票。
