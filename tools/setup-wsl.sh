#!/usr/bin/env bash
# 在 WSL2 的 Ubuntu 内运行：安装 Docker 引擎（子系统内），并准备 ESP32 构建环境。
# 用法：
#   sudo bash setup-wsl.sh
set -euo pipefail

echo "==> 更新 apt 并安装基础工具"
sudo apt-get update
sudo apt-get install -y curl git ca-certificates gnupg lsb-release

echo "==> 安装 Docker 引擎（官方一键脚本）"
if command -v docker >/dev/null 2>&1; then
  echo "    Docker 已安装，跳过。"
else
  curl -fsSL https://get.docker.com | sudo sh
fi

echo "==> 将当前用户加入 docker 组（免 sudo 跑 docker）"
sudo usermod -aG docker "$USER"

echo "==> 启动并启用 docker 服务"
sudo service docker start || sudo systemctl enable --now docker

echo "==> 验证 docker"
# 新组生效需要重新登录；这里用 sudo 先验证一次
sudo docker run --rm hello-world || true

echo ""
echo "✅ 子系统内 Docker 安装完成。"
echo "⚠️ 请退出并重新打开 Ubuntu 终端，让 docker 组生效（之后直接 'docker' 即可，无需 sudo）。"
echo ""
echo "项目在 Windows 盘的挂载路径通常为：/mnt/d/git/esp32"
echo "进入后构建固件示例："
echo "  cd /mnt/d/git/esp32"
echo "  bash tools/build-firmware.sh"
echo ""
echo "构建服务端（在 WSL 内）："
echo "  cd /mnt/d/git/esp32/server"
echo "  docker compose up --build"
