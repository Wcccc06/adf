#!/system/bin/sh
# ============================================================
# adf_driver_load.sh —— 流程一：刷驱动
# 对标 LinYuDriverLoader.Ver.7.3.sh -> LinYuKernel.Ver.3.3.0.sh
# 用法(root): sh adf_driver_load.sh [模块路径]
# ============================================================
set -u

DIR=/data/adb/adf
KO="${1:-$DIR/adf_driver.ko}"
LOG=/data/local/tmp/adf_driver.log

log() { echo "[adf-driver] $*" | tee -a "$LOG"; }
die() { log "失败: $*"; exit 1; }

[ "$(id -u)" = "0" ] || die "需要 root: su -c 'sh $0'"

log "内核: $(uname -r)"
log "模块: $KO"
[ -f "$KO" ] || die "找不到模块文件 $KO"

# 校验 vermagic（防止"版本魔术字"不匹配导致加载失败）
VM=$(strings "$KO" 2>/dev/null | grep -m1 '^vermagic=')
REL=$(uname -r)
log "模块 vermagic: $VM"
log "运行内核     : $REL"
case "$VM" in
  *"$REL"*) log "vermagic 一致 OK" ;;
  *) log "警告: vermagic 与运行内核不一致，加载大概率报 version magic" ;;
esac

# 目录与权限（对标他们的 0777；我们建议 0700，下面两种都写出来）
mkdir -p "$DIR"
chown 0:0 "$DIR"
chmod 0700 "$DIR"                 # 建议值：只有 root 能进
chmod 0644 "$KO"

# 加载（第三条路：KernelSU 自带通道，实测设备上存在）
if command -v ksud >/dev/null 2>&1; then
  log "用 ksud insmod 加载"
  ksud insmod "$KO" && log "加载成功" || die "ksud insmod 失败"
else
  log "没有 ksud，退回 insmod"
  insmod "$KO" && log "加载成功" || die "insmod 失败"
fi

# 确认
if [ -d /sys/module/adf_driver ]; then
  log "模块已在内核中: /sys/module/adf_driver"
  log "参数: hide_proc=$(cat /sys/module/adf_driver/parameters/hide_proc 2>/dev/null)"
  log "      hide_adb=$(cat /sys/module/adf_driver/parameters/hide_adb 2>/dev/null)"
  log "驱动加载完成"
else
  die "加载后没看到 /sys/module/adf_driver，dmesg 里找 adf: 开头的行"
fi
