<#
.SYNOPSIS
  一键启用 WSL2 + 安装 Ubuntu + 安装 usbipd-win（首次烧录 ESP32 用）。
  必须以「管理员」身份运行此脚本（右键 PowerShell → 以管理员身份运行）。

.DESCRIPTION
  1. 启用 Windows 可选功能：VirtualMachinePlatform + Microsoft-Windows-Subsystem-Linux
  2. 设置 WSL 默认版本为 2
  3. 安装 Ubuntu 发行版（wsl --install -d Ubuntu）
  4. 通过 winget 安装 usbipd-win（把 USB 串口透传进 WSL 用）
  5. 提示重启（WSL 内核生效通常需要重启）

  重启后：打开 Ubuntu 终端完成初始用户创建，然后运行仓库里的 tools/setup-wsl.sh
  把 Docker 装进子系统。
#>

$ErrorActionPreference = "Stop"

function Write-Step($msg) { Write-Host "`n==> $msg" -ForegroundColor Cyan }
function Test-Admin {
    $id = [System.Security.Principal.WindowsIdentity]::GetCurrent()
    $p  = New-Object System.Security.Principal.WindowsPrincipal($id)
    return $p.IsInRole([System.Security.Principal.WindowsBuiltInRole]::Administrator)
}

if (-not (Test-Admin)) {
    Write-Host "❌ 请以「管理员」身份运行此脚本（右键 PowerShell → 以管理员身份运行）。" -ForegroundColor Red
    exit 1
}

# 1. 启用 WSL 功能
Write-Step "启用 WSL 可选功能（VirtualMachinePlatform + WSL）"
dism.exe /online /enable-feature /featurename:VirtualMachinePlatform /all /norestart | Out-Null
dism.exe /online /enable-feature /featurename:Microsoft-Windows-Subsystem-Linux /all /norestart | Out-Null

# 2. 设默认版本 2
Write-Step "设置 WSL 默认版本为 2"
wsl --set-default-version 2

# 3. 安装 Ubuntu（若已装则跳过）
Write-Step "安装 Ubuntu 发行版"
$installed = wsl --list --quiet 2>$null
if ($installed -match "Ubuntu") {
    Write-Host "    Ubuntu 已存在，跳过安装。"
} else {
    wsl --install -d Ubuntu
}

# 4. 安装 usbipd-win
Write-Step "安装 usbipd-win（串口透传用）"
$hasUsbipd = Get-Command usbipd -ErrorAction SilentlyContinue
if ($hasUsbipd) {
    Write-Host "    usbipd-win 已安装，跳过。"
} else {
    if (Get-Command winget -ErrorAction SilentlyContinue) {
        winget install --exact --accept-package-agreements --accept-source-agreements usbipd.usbipd-win
    } else {
        Write-Host "    ⚠️ 未找到 winget，请手动到 https://github.com/dorssel/usbipd-win/releases 下载安装 usbipd-win。" -ForegroundColor Yellow
    }
}

# 5. 提示重启
Write-Step "完成"
Write-Host "✅ WSL2 / Ubuntu / usbipd-win 已配置。" -ForegroundColor Green
Write-Host "⚠️ 建议现在重启一次 Windows 让 WSL2 内核生效。" -ForegroundColor Yellow
Write-Host "重启后请：" -ForegroundColor White
Write-Host "  1) 打开 'Ubuntu' 终端，按提示创建 Linux 用户名/密码；" -ForegroundColor White
Write-Host "  2) 在 Ubuntu 里执行： bash <(curl -fsSL https://raw.githubusercontent.com/.../setup-wsl.sh)" -ForegroundColor White
Write-Host "     （或直接把本仓库 tools/setup-wsl.sh 拷进 Ubuntu 运行）。" -ForegroundColor White

$reboot = Read-Host "是否立即重启？(Y/N)"
if ($reboot -match '^[Yy]') { Restart-Computer -Force }
