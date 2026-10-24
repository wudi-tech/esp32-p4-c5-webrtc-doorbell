# 2026-10-24 归档验证记录

## 本次实际执行

环境：Windows；Python 3.12.14；GCC 使用 `D:/mingw64/bin/gcc.exe`。Python 精确版本以同目录 [validation-results.json](validation-results.json) 的运行元数据为准。

| 检查 | 结果 | 能说明的范围 |
| --- | --- | --- |
| 两份源码逐文件 SHA-256 比对 | 487/487 相同 | 两个展开目录属于同一份源码内容 |
| 门铃 `verify_*.py` | 14/14 通过 | 静态接入/配置检查，以及独立 C 模块主机测试 |
| C 测试覆盖 | 13/13 个测试源文件被检查入口引用执行 | 解析、状态机、融合、过滤、追踪、混音等纯逻辑 |
| 结构设计 unittest | 17/17 通过 | 孔位、边界、净间隙、正反面参考、DXF/CSV、四页 PDF 和 ZIP |
| Wi-Fi 与常见令牌检查 | 见发布前扫描记录 | 常见令牌格式及本机私有 Wi-Fi 配置未进入发布内容 |

本次**没有执行** ESP-IDF 全量构建、设备烧录、串口运行、视频逐段审阅或硬件验收。历史构建、烧录和运行证据在原有文档中按日期保留。

## 门铃检查入口

```text
verify_door_lock.py
verify_doorbell_ring_policy.py
verify_doorbell_status.py
verify_hybrid_vision_ui.py
verify_pcm_overlay.py
verify_pedestrian_filter.py
verify_presence_fusion.py
verify_radar_ld2450.py
verify_round_two_ui.py
verify_task_8_3_8_4.py
verify_vision_event.py
verify_vision_runtime_safety.py
verify_vision_tracker.py
verify_wt99p4c5_config.py
```

检查通过不代表全系统无缺陷。例如 `verify_pcm_overlay.py` 验证纯 PCM 叠加逻辑，不能证明当前 8 kHz 通话路径兼容 16 kHz 提示音；`doorbell_status` 单元测试不能证明其存在生产调用方。

## 本地复跑

在仓库根目录的 PowerShell 执行；修改编译器路径以匹配本机。脚本会在缺目录、零入口、单项失败时退出，避免把“没跑到测试”当成通过。

```powershell
$ErrorActionPreference = 'Stop'
$env:CC = 'D:/mingw64/bin/gcc.exe'
$testDir = Join-Path (Get-Location) 'solutions/doorbell_local/tests'
if (-not (Test-Path -LiteralPath $testDir)) { throw '测试目录不存在' }
$checks = @(Get-ChildItem -LiteralPath $testDir -Filter 'verify_*.py' | Sort-Object Name)
if ($checks.Count -ne 14) { throw "预期 14 个入口，实际为 $($checks.Count)" }
$failed = 0
foreach ($check in $checks) {
    python $check.FullName
    if ($LASTEXITCODE -ne 0) { $failed++ }
}
Write-Host "VERIFY_TOTAL=$($checks.Count) VERIFY_FAILED=$failed"
if ($failed -ne 0) { throw '自动检查失败' }
```

结构设计测试和重新生成使用 `reportlab` / `pypdf`，可在独立 Python 环境安装后运行：

```powershell
python -m pip install -r requirements-cad.txt
python -m unittest cad.acrylic_plate.test_geometry -v
python cad/acrylic_plate/generate_plate.py
```

最后一条会更新 `output/acrylic`、`output/pdf` 的 V3 文件。PDF 二进制可能受生成时间与字体环境影响，几何与内容验证比逐字节比较更合适。

## 校验与追溯

- [source-snapshot-manifest.csv](source-snapshot-manifest.csv)：原 487 文件到公开路径的映射，含原始与公开 SHA-256。
- [project-file-inventory.csv](project-file-inventory.csv)：本次公开内容清单及文件 SHA-256；不包含该清单自身和 Git 元数据。
- [validation-results.json](validation-results.json)：检查入口返回码及输出。
- Release 的 `SHA256SUMS.txt`：大型视频、固件包、加工包校验。

日志中的 MAC、网络名称、可能出现的 Wi-Fi 命令参数和部分本机路径已按规则脱敏；这类文件不是原始日志的逐字节副本，差异已记录在来源清单中。
