param(
    [string]$Port = "COM5"
)

$ErrorActionPreference = "Stop"
$firmwareDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$requiredFiles = @(
    "bootloader.bin",
    "partition-table.bin",
    "doorbell_demo.bin"
)

foreach ($fileName in $requiredFiles) {
    $filePath = Join-Path $firmwareDirectory $fileName
    if (-not (Test-Path -LiteralPath $filePath -PathType Leaf)) {
        throw "缺少固件文件：$fileName"
    }
}

Push-Location $firmwareDirectory
try {
    python -m esptool --chip esp32p4 -p $Port -b 460800 `
        --before default_reset --after hard_reset write_flash `
        --flash_mode dio --flash_freq 80m --flash_size 16MB `
        0x2000 bootloader.bin `
        0x9000 partition-table.bin `
        0x10000 doorbell_demo.bin

    if ($LASTEXITCODE -ne 0) {
        throw "烧录失败，退出代码：$LASTEXITCODE。若无法连接，请按住 BOOT、短按 RST、松开 BOOT 后重试。"
    }

    Write-Host "烧录完成。请打开串口终端查看启动日志和设备 IP 地址。"
}
finally {
    Pop-Location
}
