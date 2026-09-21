# SPDX-License-Identifier: GPL-2.0
# adf 驱动层构建（GKI android14-6.1 / arm64）
#
# 关键参数来自设备实测:
#   内核 release : 6.1.118-android14-11-o-g64180ab070e5
#   vermagic 必须与运行内核一致，否则 insmod 报 version magic
#
# 用法:
#   make KDIR=/path/to/android14-6.1/common CC=/path/to/clang \
#        CROSS_COMPILE=aarch64-linux-gnu- \
#        KBUILD_EXTRA_SYMBOLS=/path/to/Module.symvers
#
obj-m := adf_driver.o

KVER    := 6.1.118-android14-11-o-g64180ab070e5
# 设备上厂商模块的 vermagic（对齐用；含 -o- 变体）
VMAGIC  := 6.1.118-android14-11-o-geb5e030f21ba SMP preempt mod_unload modversions aarch64

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

# 对齐 vermagic：把编出来的 .ko 里的版本串替换成运行内核的
# （两个字符串必须等长，否则用 scripts/setvermagic.py 做等长填充）
align:
	@python3 ../scripts/setvermagic.py adf_driver.ko "$(KVER)"

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f *.ko *.o *.mod *.mod.c modules.order Module.symvers

.PHONY: all align clean
