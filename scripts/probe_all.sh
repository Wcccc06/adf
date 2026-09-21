#!/system/bin/sh
echo '=== 设备 ==='
getprop ro.product.model; uname -r; getenforce
echo '=== root ==='
su -c 'id'
echo '=== KPM 能力（我们的 driver 要靠它加载，或走 insmod）==='
su -c 'ksud kpm version' 2>&1 | head -2
echo '=== 当前可见性基线（三对目标探测点）==='
echo -n 'adbd in ps: '; ps -A -o NAME 2>/dev/null | grep -c '^adbd$'
echo -n '/data/adb readable by shell: '; (ls /data/adb >/dev/null 2>&1 && echo yes || echo no)
for f in /system/bin/adb /system/bin/adbd /data/misc/adb /dev/usb-ffs/adb; do
  [ -e "$f" ] && echo "  visible: $f" || echo "  absent : $f"
done
echo -n 'init.svc.adbd = '; getprop init.svc.adbd
echo '=== 他们的四条线（对照）==='
ls -l /data/adb/LinYu* /data/adb/lib2026*.so "/data/adb/嗯嗯启动器.sh" 2>/dev/null | awk '{print "  "$1, $5, $9}'
echo '=== 我们的目录（还没铺）==='
ls -l /data/adb/adf 2>/dev/null || echo '  (不存在，等编译产物) '
echo '=== 手机能不能编译 C（决定走哪条编译路线）==='
for c in clang gcc cc; do printf '  %s: ' $c; command -v $c 2>/dev/null || echo '(none)'; done
ls -d /data/data/com.termux 2>/dev/null && echo '  Termux 已安装' || echo '  Termux 未安装'
