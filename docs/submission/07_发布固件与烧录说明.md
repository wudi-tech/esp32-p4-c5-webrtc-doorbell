# 发布固件与烧录说明

## 1. 固件组成

公开提交包中的 ESP32-P4 固件由以下三个文件组成：

| 文件 | 烧录地址 | 作用 |
| --- | ---: | --- |
| `bootloader.bin` | `0x2000` | ESP32-P4 启动程序 |
| `partition-table.bin` | `0x9000` | 16 MB Flash 分区表 |
| `doorbell_demo.bin` | `0x10000` | WebRTC 智能门铃主程序 |

发布固件面向 WT99P4C5-S1（ESP32-P4，16 MB Flash）。ESP32-C5 使用开发板原有配套固件，本提交包不重复烧录 C5。

## 2. 隐私说明

公开固件和公开源码均不包含个人 Wi-Fi 名称或密码。首次使用时有两种配置方法：

1. 推荐：复制 `settings.local.h.example` 为 `settings.local.h`，填写 2.4 GHz Wi-Fi 后重新编译；该本地文件已被 Git 忽略。
2. 临时调试：在 ESP-IDF 串口终端输入 `wifi <Wi-Fi名称> <Wi-Fi密码>`。配置会写入 NVS，重启后继续使用。

不要把填有真实密码的 `settings.local.h`、串口截图或私人固件上传到公开平台。

## 3. 烧录环境

- Windows 10/11
- ESP-IDF v5.5.5
- Python 环境中已安装 `esptool`
- 开发板 USB-JTAG/串口连接到电脑（本机测试端口曾为 COM5，换电脑后可能不同）

## 4. 自动烧录

在 ESP-IDF PowerShell 中进入固件目录，执行：

```powershell
.\flash_windows.ps1 -Port COM5
```

如果自动复位无法进入下载模式：按住 BOOT，短按一次 RST，松开 BOOT，然后重新执行命令。

## 5. 手动烧录命令

```powershell
python -m esptool --chip esp32p4 -p COM5 -b 460800 `
  --before default_reset --after hard_reset write_flash `
  --flash_mode dio --flash_freq 80m --flash_size 16MB `
  0x2000 bootloader.bin `
  0x9000 partition-table.bin `
  0x10000 doorbell_demo.bin
```

烧录成功后，串口终端会输出局域网 HTTPS 地址。浏览器首次访问自签名证书页面时，需要手动选择继续访问。

## 6. 从源码构建

```powershell
Set-Location .\solutions\doorbell_local
idf.py set-target esp32p4
idf.py build
idf.py -p COM5 flash monitor
```

只有在切换芯片目标或清理工程时才需要重复执行 `set-target`。正常修改代码后直接 `idf.py build` 即可。

## 7. 验收要点

- 串口能看到 ESP32-P4 启动、摄像头、音频、Wi-Fi 和 HTTPS 服务初始化日志。
- 浏览器能打开门铃测试页并显示已连接。
- BOOT 能触发来电；能接听、双向通话、开门、挂断。
- 雷达有人时显示目标/事件，无人时不误触发来电。
- 舵机只用于模拟门锁动作，尚未连接真实门锁。
