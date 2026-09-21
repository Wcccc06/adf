all:
	$(MAKE) -C $(KDIR) M=$(PWD) ARCH=arm64 LLVM=1 CC=clang modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
