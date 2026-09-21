# adf KPM 构建（改自 Wcccc06/KPM-Build-Anywhere 的模板）
# 只要 NDK + KernelPatch 头文件，不需要完整内核源码树
# 用法: make -f kpm.mk NDK_PATH=/path/ndk KP_DIR=/path/KernelPatch

ifeq ($(OS), Windows_NT)
    PLATFORM := windows-x86_64
else
    PLATFORM := linux-x86_64
endif

ifndef TARGET_COMPILE
    export TARGET_COMPILE=$(NDK_PATH)/toolchains/llvm/prebuilt/$(PLATFORM)/bin/
endif

ifndef KP_DIR
    KP_DIR = ../KernelPatch
endif

CC      = $(TARGET_COMPILE)aarch64-linux-android31-clang
LD      = $(TARGET_COMPILE)ld.lld
STRIP   = $(TARGET_COMPILE)llvm-strip

INCLUDE_DIRS := . include patch/include linux/include linux/arch/arm64/include linux/tools/arch/arm64/include
INCLUDE_FLAGS := $(foreach dir,$(INCLUDE_DIRS),-I$(KP_DIR)/kernel/$(dir))

CFLAGS = $(INCLUDE_FLAGS) -Wall -Ofast -fno-PIC -fno-asynchronous-unwind-tables \
         -fno-stack-protector -fno-unwind-tables -fno-semantic-interposition \
         -U_FORTIFY_SOURCE -fno-common -fvisibility=hidden

LDFLAGS += -s
objs := adf_kpm.o

all: adf_kpm.kpm

adf_kpm.kpm: ${objs}
	${CC} $(LDFLAGS) -r -o $@ $^
	${STRIP} -g --strip-unneeded --strip-debug --remove-section=.comment --remove-section=.note.GNU-stack $@

%.o: %.c
	${CC} $(CFLAGS) -c -O2 -o $@ $<

.PHONY: clean
clean:
	rm -f *.o *.kpm
