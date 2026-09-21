#!/system/bin/sh
# ============================================================
# adf_launcher.sh —— 流程二：下载 lib 然后点启动器
# 对标 嗯嗯启动器.sh（他们的会读卡密 + 跑同目录 .so）
# 我们这版：不读卡密、不联网，直接起宿主
# 用法(root): sh adf_launcher.sh
# ============================================================
set -u

DIR=/data/adb/adf
HOST="$DIR/adf_host"

echo "[adf] 启动器"
[ "$(id -u)" = "0" ] || { echo "[adf] 需要 root"; exit 1; }

# 1) 驱动在不在
if [ ! -d /sys/module/adf_driver ]; then
  echo "[adf] 驱动没加载，先跑 adf_driver_load.sh"
  exit 1
fi

# 2) 宿主存在性与权限（他们给 0777，我们给 0755；可执行就够了）
[ -x "$HOST" ] || { echo "[adf] 缺少宿主 $HOST"; exit 1; }
chmod 0755 "$HOST"
chown 0:0 "$HOST"

# 3) 下发配置
echo 1 > /sys/module/adf_driver/parameters/hide_proc 2>/dev/null
echo 1 > /sys/module/adf_driver/parameters/hide_adb  2>/dev/null
echo 0 > /sys/module/adf_driver/parameters/filter_tgid 2>/dev/null

# 4) 起宿主
echo "[adf] 宿主启动"
exec "$HOST" status
