#!/usr/bin/env bash
# 用乐鑫官方 ESP-IDF Docker 镜像编译/烧录固件，无需在本机装 ESP-IDF 工具链。
# 在 WSL2 的 Ubuntu 里运行（项目挂在 /mnt/d/git/esp32）。
#
# 用法：
#   bash tools/build-firmware.sh build          # 仅编译，产物在 build/
#   bash tools/build-firmware.sh flash          # 编译并烧录到 ota_0（首次）
#   bash tools/build-firmware.sh monitor        # 打开串口监视
#   bash tools/build-firmware.sh flash-monitor  # 烧录 + 监视
#
# 烧录前（在 Windows 管理员 PowerShell 里把 ESP32 串口透传进 WSL）：
#   usbipd list                      # 找到 ESP32 的 BUSID（如 2-3）
#   usbipd bind --busid 2-3
#   usbipd attach --wsl --busid 2-3  # 之后 WSL 内会出现 /dev/ttyUSB0
# 若 WSL 内看不到设备，重跑上面 attach；设备拔插后需重新 attach。

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IDF_IMAGE="espressif/idf:idf-v5.5"

# 在容器内执行 idf.py 的通用封装：挂载仓库、映射串口、复用 cargo/idl 缓存
run_idf() {
  local cmd="$1"
  docker run --rm -it \
    -v "${REPO_ROOT}:/project" \
    -w /project \
    -e "IDF_TARGET=esp32s3" \
    --device=/dev/ttyUSB0:/dev/ttyUSB0 \
    "${IDF_IMAGE}" bash -lc "idf.py ${cmd}"
}

case "${1:-build}" in
  build)
    docker run --rm -it \
      -v "${REPO_ROOT}:/project" -w /project \
      -e "IDF_TARGET=esp32s3" \
      "${IDF_IMAGE}" bash -lc "idf.py build"
    echo "✅ 编译完成，app 镜像：build/*.bin"
    ;;
  flash)
    run_idf "flash -p /dev/ttyUSB0"
    ;;
  monitor)
    run_idf "monitor -p /dev/ttyUSB0"
    ;;
  flash-monitor)
    run_idf "flash -p /dev/ttyUSB0 monitor"
    ;;
  *)
    echo "用法: $0 {build|flash|monitor|flash-monitor}"
    exit 1
    ;;
esac
