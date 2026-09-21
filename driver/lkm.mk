# 标准 LKM 构建（在 GKI 内核树里编外部模块）
# 用法: make -f lkm.mk KDIR=/opt/gki

KDIR ?= /opt/gki
obj-m := adf_lkm.o

all:
	$(MAKE) -C $(KDIR) M=$(PWD) ARCH=arm64 LLVM=1 CC=clang modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f *.ko *.o *.mod *.mod.c modules.order Module.symvers

.PHONY: all clean