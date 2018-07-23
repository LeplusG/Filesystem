.PHONY: all clean-all

ifneq ($(KERNELRELEASE),)

  obj-m += pnlfs.o
  pnlfs-objs := super.o inode.o file.o
else

 KERNELDIR ?= ../../projet/linux-4.9.83
 PWD := $(shell pwd)

all:mkfs-pnlfs
	make -C $(KERNELDIR) M=$$PWD modules
	dd if=/dev/zero of=disk.img bs=1M count=30
	./mkfs-pnlfs disk.img


mkfs-pnlfs: mkfs-pnlfs.o 
	gcc -o $@ $<

clean:
	make -C $(KERNELDIR) M=$$PWD clean
	rm disk.img mkfs-pnlfs
endif
