#!/system/bin/sh
# ============================================================
# adf_launcher.sh —— 启动器（对标 嗯嗯启动器.sh）
# 用法(root): sh adf_launcher.sh [卡密]
#   卡密参数保留是为了跟他们的用法一致；我们不校验、不联网。
# ============================================================
set -u
D=/data/adb/adf
SO=$D/adf_host.so

echo '[adf] start'
[ "$(id -u)" = 0 ] || { echo '[adf] need root'; exit 1; }
[ -f "$SO" ] || { echo "[adf] missing $SO"; exit 1; }

chown 0:0 "$SO" 2>/dev/null
chmod 0755 "$SO"

if [ -d /sys/module/adf_lkm ]; then
  echo 1 > /sys/module/adf_lkm/parameters/hide_proc 2>/dev/null
  echo 1 > /sys/module/adf_lkm/parameters/hide_adb  2>/dev/null
  echo '[adf] kernel module linked: hide_proc=1 hide_adb=1'
else
  echo '[adf] note: kernel module not loaded; ADB path hiding needs it'
fi

exec "$SO" status
