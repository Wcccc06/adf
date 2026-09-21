# adf 宿主层构建（Android NDK）
# 用法: make NDK=$ANDROID_NDK_HOME
NDK    ?=
CC     := $(NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android29-clang
CFLAGS := -O2 -Wall -static
OUT    := build

all: $(OUT)/adf_host

$(OUT)/adf_host: host/adf_host.c
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -o $@ $< -ldl

clean:
	rm -rf $(OUT)

.PHONY: all clean
