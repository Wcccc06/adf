# 标准 LKM 构建（在 GKI 内核树里编外部模块）
# 依赖 NDK 里的 llvm 工具链（runner 上没装 llvm，必须显式指定）
# 用法: make -f lkm.mk KDIR=/opt/gki TOOLCHAIN=$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64

KDIR      ?= /opt/gki
TOOLCHAIN ?=
obj-m := adf_lkm.o

ifneq ($(TOOLCHAIN),)
  MAKEVARS := LLVM=1 CC=clang LD=ld.lld AR=llvm-ar NM=llvm-nm \
              OBJCOPY=llvm-objcopy OBJDUMP=llvm-objdump READELF=llvm-readelf \
              STRIP=llvm-strip HOSTCC=clang HOSTLD=ld.lld HOSTAR=llvm-ar
  PATH := $(TOOLCHAIN)/bin:$(PATH)
  export PATH
else
  MAKEVARS := LLVM=1 CC=clang
endif

all:
	$(MAKE) -C $(KDIR) M=$(PWD) ARCH=arm64 $(MAKEVARS) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f *.ko *.o *.mod *.mod.c modules.order Module.symvers

.PHONY: all clean