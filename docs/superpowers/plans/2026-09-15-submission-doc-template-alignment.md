# 提交文档模板对齐实施计划

> **针对自主工作者:** REQUIRED SUB-SKILL: 使用超级能力:子代理驱动开发 (推荐) 或超级能力:逐任务执行计划以实施该计划. 步骤使用复选框 (`- []`) 跟踪语法。

**Goal:** 将开发日志和 ESP-IDF 使用指南完整对齐参考样例，同时保留项目真实技术内容并更新最终提交包。

**Architecture:** 以源码仓库中的 `docs/submission` 为文档单一来源，先补齐模板要求，再同步到最终提交目录及源码快照。重新生成源码 ZIP 和 SHA-256 清单，确保文档、归档和校验值一致。

**技术栈:** Markdown、PowerShell、Git、SHA-256、ZIP

---

### 任务 1：核对项目元数据与依赖

**文件:**
- 读取: `solutions/doorbell_local/dependencies.lock`
- 读取: `solutions/doorbell_local/sdkconfig`
- Read: `docs/submission/01_开发日志.md`
- Read: `docs/submission/02_使用指南.md`

- [ ] **Step 1: 提取已锁定组件版本**

运行：

```powershell
rg -n '^(  )?(espressif/|version:|source:)' solutions/doorbell_local/dependencies.lock
```

Expected: 输出 WebRTC、媒体、摄像头、ESP-DL 和人脸检测相关组件的锁定版本，不输出 Wi-Fi 私有配置。

- [ ] **Step 2: 核对芯片、Flash、PSRAM和视觉配置**

运行：

```powershell
rg -n 'ESP32P4_REV|FLASHSIZE|SPIRAM|PEDESTRIAN|HUMAN_FACE' solutions/doorbell_local/sdkconfig
```

Expected: ESP32-P4 v1.x、16 MB Flash、32 MB PSRAM及视觉模型配置与实机记录一致。

### 任务 2：按参考模板补齐开发日志

**文件:**
- Modify: `docs/submission/01_开发日志.md`

- [ ] **Step 1: 重组开发日志章节**

使用以下固定结构，并保留现有阶段记录：

```text
项目信息
选题与可行性分析
硬件搭建
软件移植
整合调试与问题汇总
最终效果与原项目对比
验证与性能数据
总结与反思
参考资料
提交前照片清单
```

项目信息固定为：开发者“吴迪”，开发周期“2026-07-24 ～ 2026-09-15”，原项目为 `https://github.com/espressif/esp-webrtc-solution`，复刻基准为 `solutions/doorbell_local`。

- [ ] **Step 2: 增加完整硬件与接线记录**

写入 WT99P4C5-S1、OV5647、ES8311、4 Ω 3 W 扬声器、MG90S/GX90S、HLK-LD2450、三路降压板和9 V/2.22 A适配器；明确 GPIO4、GPIO5、GPIO32、共地及5 V供电关系。

- [ ] **Step 3: 增加问题汇总和原项目对比表**

问题表至少覆盖 ESP-IDF 环境、P4 revision、刷新后不来电、呼叫建立慢、通话中开门语音、舵机命令失败、雷达乱码、挂断残留、AI线程栈溢出。对比表明确“官方核心功能已复刻”和雷达、舵机、多模态融合、中文界面等增强项。

- [ ] **Step 4: 增加反思、参考资料和照片清单**

照片清单只描述需拍摄的真实画面：整机全景、主板与摄像头、雷达方向、舵机与电源、网页通话、雷达/视觉检测；不嵌入虚构图片。

### 任务 3：按 ESP-IDF 模板补齐使用指南

**文件:**
- Modify: `docs/submission/02_使用指南.md`

- [ ] **Step 1: 增加项目简介和环境版本表**

明确项目基于 `doorbell_local`，环境为 Windows 11、ESP-IDF v5.5.5、ESP32-P4 rev 1.3、16 MB Flash、32 MB PSRAM、Python 3.11、Chrome/Edge。

- [ ] **Step 2: 增加组件版本与硬件清单**

组件版本只使用 `dependencies.lock` 中实际锁定值。硬件清单包含数量、规格和用途，不填写未经验证的型号。

- [ ] **Step 3: 完善引脚和电源板连接**

写明：9 V/2.22 A接降压板输入，负载连接前用万用表将输出调到5.0 V；雷达与舵机分路供电；雷达 TX→GPIO4、RX→GPIO5；舵机PWM→GPIO32；所有地线共地；5 V/3 A适配器使用时绕过6～24 V输入降压板。

- [ ] **Step 4: 补充配置、构建、烧录、使用、FAQ和目录结构**

保留 Wi-Fi 隐私配置、COM5、BOOT+RST下载模式、HTTPS证书、呼叫流程和现有FAQ；增加源码目录树、参考资料及许可证说明。

### 任务 4：验证文档内容

**文件:**
- Test: `docs/submission/01_开发日志.md`
- Test: `docs/submission/02_使用指南.md`

- [ ] **Step 1: 检查模板章节**

运行：

```powershell
rg -n '项目信息|可行性|硬件搭建|问题汇总|原项目对比|总结.*反思|参考资料|照片清单' docs/submission/01_开发日志.md
rg -n '项目简介|开发环境版本|组件.*版本|硬件清单|引脚连接|项目配置|编译与烧录|项目目录结构|常见问题|参考资料|许可证' docs/submission/02_使用指南.md
```

Expected: 每个要求的章节至少命中一次。

- [ ] **Step 2: 检查模板残留和隐私**

运行：

```powershell
rg -n '模板结束|提交前请删除本行|\[项目名称\]|xxx|YOUR_WIFI_PASSWORD' docs/submission/01_开发日志.md docs/submission/02_使用指南.md
```

Expected: 无输出，退出码1。

- [ ] **Step 3: 提交文档源文件**

运行：

```powershell
git add docs/submission/01_开发日志.md docs/submission/02_使用指南.md
git commit -m "docs: align submission guides with reference templates"
```

Expected: 只提交两份目标文档，不包含其他工作树改动。

### 任务 5：同步最终提交包并重新校验

**文件:**
- Modify: `D:/Documents/ChatGPT/WebRTC复刻挑战/output/submission/ESP32-P4-C5_WebRTC_Doorbell_2026-09-14/03_提交文档/01_开发日志.md`
- Modify: `D:/Documents/ChatGPT/WebRTC复刻挑战/output/submission/ESP32-P4-C5_WebRTC_Doorbell_2026-09-14/03_提交文档/02_使用指南.md`
- Modify: `D:/Documents/ChatGPT/WebRTC复刻挑战/output/submission/ESP32-P4-C5_WebRTC_Doorbell_2026-09-14/02_项目完整源码/docs/submission/01_开发日志.md`
- Modify: `D:/Documents/ChatGPT/WebRTC复刻挑战/output/submission/ESP32-P4-C5_WebRTC_Doorbell_2026-09-14/02_项目完整源码/docs/submission/02_使用指南.md`
- Rebuild: `D:/Documents/ChatGPT/WebRTC复刻挑战/output/submission/ESP32-P4-C5_WebRTC_Doorbell_2026-09-14/02_项目完整源码_WT99P4C5_WebRTC_Doorbell.zip`
- Modify: `D:/Documents/ChatGPT/WebRTC复刻挑战/output/submission/ESP32-P4-C5_WebRTC_Doorbell_2026-09-14/SHA256SUMS.md`

- [ ] **Step 1: 同步两份文档到提交目录和源码快照**

使用 `Copy-Item -LiteralPath` 覆盖四个明确目标文件，不删除其他材料。

- [ ] **Step 2: 更新源码ZIP**

运行：

```powershell
Compress-Archive -LiteralPath .\02_项目完整源码 `
  -DestinationPath .\02_项目完整源码_WT99P4C5_WebRTC_Doorbell.zip `
  -CompressionLevel Optimal -Update
```

Expected: ZIP可以使用 `System.IO.Compression.ZipFile.OpenRead()` 打开，并包含两份更新后的文档。

- [ ] **Step 3: 重新计算SHA-256**

运行：

```powershell
Get-FileHash -Algorithm SHA256 .\02_项目完整源码_WT99P4C5_WebRTC_Doorbell.zip
Get-FileHash -Algorithm SHA256 .\03_提交文档\01_开发日志.md
Get-FileHash -Algorithm SHA256 .\03_提交文档\02_使用指南.md
```

Expected: 将新哈希和字节数写入 `SHA256SUMS.md`。

- [ ] **Step 4: 执行最终隐私与完整性检查**

从被Git忽略的 `settings.local.h` 读取本机SSID与密码，仅在内存中比较提交包文件内容，输出命中数量而不输出凭据。

预期：

```text
PRIVATE_DATA_HITS=0
FORBIDDEN_LOCAL_SETTINGS_FILES=0
ZIP_PRIVATE_OR_FORBIDDEN_HITS=0
```

同时确认两份视频目录仍保留录制脚本和README，等待参赛者放入实际MP4。
