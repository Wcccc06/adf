#!/sbin/sh
# ============================================================
# /data/adb/adf/install.sh —— 一次装完（两对四条线铺到 /data/adb/adf）
#   对1-驱动: adf_driver.ko + adf_driver_load.sh   (对标 LinYuDriverLoader + LinYuKernel)
#   对2-客户端: adf_host + adf_launcher.sh          (对标 lib20260914.so + 嗯嗯启动器.sh)
# 用法(root): sh install.sh [源目录，默认 /data/local/tmp]
# ============================================================
set -u
SRC=${1:-/data/local/tmp}
D=/data/adb/adf

[ "$(id -u)" = 0 ] || { echo '[adf] need root'; exit 1; }

echo '[adf] 建目录'
mkdir -p $D

echo '[adf] 铺文件'
for f in adf_driver.ko adf_host; do
  if [ -f "$SRC/$f" ]; then
    cp -f "$SRC/$f" $D/$f && echo "  copied $f"
  else
    echo "  MISSING $SRC/$f  (还没编译出来)"
  fi
done
for f in adf_driver_load.sh adf_launcher.sh adf_verify.sh; do
  if [ -f "$SRC/$f" ]; then cp -f "$SRC/$f" $D/$f && echo "  copied $f"; fi
done

echo '[adf] 权限（不用 777，用最小可用）'
chown 0:0 $D
chmod 0700 $D
chmod 0644 $D/adf_driver.ko 2>/dev/null
chmod 0755 $D/adf_host 2>/dev/null
chmod 0755 $D/*.sh 2>/dev/null
ls -l $D

echo '[adf] 装完。下一步:'
echo '  su -c "sh /data/adb/adf/adf_driver_load.sh"   # 对1: 刷驱动'
echo '  su -c "sh /data/adb/adf/adf_launcher.sh"      # 对2: 起客户端'
echo '  su -c "sh /data/adb/adf/adf_verify.sh"        # 验收'
