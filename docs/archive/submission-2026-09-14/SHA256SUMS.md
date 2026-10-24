# 发布文件 SHA-256 校验值

生成日期：2026-09-16

| 文件 | 字节数 | SHA-256 |
| --- | ---: | --- |
| `01_可运行作品固件/bootloader.bin` | 24,032 | `119be19297f7e9c6c8063ca553f1db0322ded93aa9d0d8c8a7d3249565614a36` |
| `01_可运行作品固件/partition-table.bin` | 3,072 | `a555ceef0550f6fb640e90b337e5cb8d98e2453b13022062eefb288c6d21aa24` |
| `01_可运行作品固件/doorbell_demo.bin` | 6,894,656 | `db52adea304232c50d07179824195dd79b94a6d96f55856231823ebf50f8501e` |
| `02_项目完整源码_WT99P4C5_WebRTC_Doorbell.zip` | 4,559,529 | `5d6f00b9499e1f54bd5113bb24b02489731f38a872657a9e5dba5ff933ab7e7b` |
| `03_提交文档/01_开发日志.md` | 14,001 | `fb4400ec0bac2d193d89219e874ec4770145104d82f1f8e9638f47289fe22b9f` |
| `03_提交文档/02_使用指南.md` | 12,719 | `a02a3bc5c3bae27c1cc7f380863a397a4565327fce4ae0e4ec3ea78996a0cefa` |
| `03_提交文档/03_提交源码完备性审计.md` | 15,967 | `0019d3b476b384a269589663271c91e1ad69335b9297fe8e1a37242cbbdfd6cb` |
| `05_代码录屏讲解_待录制/01_代码录屏讲解脚本.md` | 28,508 | `4d5db7f31ccb24e05fee75772fce9a441efa3955f15459f11b68115f867f2303` |
| `06_项目使用流程演示_待录制/02_项目使用流程演示脚本.md` | 12,369 | `77967f8f966f8d21054d900a8c890f200586fb3fcbe696f924bb9095a55ccf3d` |

PowerShell 校验示例：

```powershell
Get-FileHash -Algorithm SHA256 .\01_可运行作品固件\doorbell_demo.bin
Get-FileHash -Algorithm SHA256 .\02_项目完整源码_WT99P4C5_WebRTC_Doorbell.zip
```

输出应与上表一致。视频录制完成后会产生新文件，需在最终提交前另行计算其校验值。
