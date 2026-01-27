#  /* clang-format off */

DD = dd
PARTED = parted
LOSETUP = losetup
MKFS = mkfs.ext2
GRUB_INSTALL = grub-install
MOUNT = mount
UMOUNT = umount

KERNEL_ELF = build/kernel.elf
ROOTFS = build/rootfs
LOOP_DEV = /dev/loop5
PARTED_DEV = $(LOOP_DEV)p1
MOUNT_POINT = /mnt/disk
ZERO_FILL_SIZE = 32

QEMU = qemu-system-x86_64
IMG = build/disk.img
QEMU_OPTS = -m 512M -cpu core2duo -hda $(IMG) -serial mon:stdio -boot c -d cpu_reset -D build/qemu.log

ifeq ($(SERIAL), true)
	QEMU_OPTS += -nographic
endif

ifeq ($(DEBUG), true)
	CPPFLAGS += -O0 -g -D DEBUG
	QEMU_OPTS += -s -S 
endif

.PHONY: all lib clean makeimg cpfiles run runall

mkdirs:
	@mkdir -p $(ROOTFS)/dev
	@mkdir -p $(ROOTFS)/mnt
	@mkdir -p $(ROOTFS)/media

	@mkdir -p $(ROOTFS)/root
	@mkdir -p $(ROOTFS)/sbin
	@mkdir -p $(ROOTFS)/bin
	@mkdir -p $(ROOTFS)/lib

	@mkdir -p $(ROOTFS)/home
	@mkdir -p $(ROOTFS)/usr
	@mkdir -p $(ROOTFS)/usr/bin
	@mkdir -p $(ROOTFS)/usr/lib
	@mkdir -p $(ROOTFS)/usr/sbin
	@mkdir -p $(ROOTFS)/usr/include

	@mkdir -p $(ROOTFS)/opt
	@mkdir -p $(ROOTFS)/var
	@mkdir -p $(ROOTFS)/var/log

lib:
	@cd lib && $(MAKE) all

all: # clean
	@$(MAKE) lib
	@$(MAKE) -C src all

clean:
	@echo -e '\e[33m[RM]\e[0m Cleaning build files ...'
	@cd src && $(MAKE) clean
	@cd lib && $(MAKE) clean

makeimg: all
	@echo -e '\e[34m[DD]\e[0m $(IMG)'
	@$(DD) if=/dev/zero of=$(IMG) bs=1M count=$(ZERO_FILL_SIZE)
	@echo -e '\e[34m[PARTED]\e[0m $(IMG)'
	@$(PARTED) -s $(IMG) mklabel msdos mkpart primary ext2 1MiB 100%

	@sudo $(LOSETUP) -P $(LOOP_DEV) $(IMG)
	@echo -e '\e[34m[MKFS]\e[0m $(MKFS) $(PARTED_DEV)'
	@sudo $(MKFS) $(PARTED_DEV)

	@echo -e '\e[34m[GRUB_INSTALL]\e[0m Installing grub ...'
	@sudo mkdir -p $(MOUNT_POINT)
	@sudo $(MOUNT) $(PARTED_DEV) $(MOUNT_POINT)
	@$(GRUB_INSTALL) --target=i386-pc --root-directory=$(MOUNT_POINT) $(LOOP_DEV)
	@sudo cp -r $(ROOTFS)/* $(MOUNT_POINT)
	@sudo $(UMOUNT) $(MOUNT_POINT)
	@sudo $(LOSETUP) -d $(LOOP_DEV)

cpfiles: mkdirs
	@echo -e '\e[34m[CP]\e[0m Copying files to rootfs ...'
	@cp $(KERNEL_ELF) $(ROOTFS)/boot/kernel.elf
	@cp -r scripts/etc/ $(ROOTFS)/
	@cp scripts/grub.cfg $(ROOTFS)/boot/grub/grub.cfg
	
	@sudo $(LOSETUP) -P $(LOOP_DEV) $(IMG)
	@sudo $(MOUNT) $(PARTED_DEV) $(MOUNT_POINT)
	@sudo cp -r $(ROOTFS)/* $(MOUNT_POINT)
	@sudo $(UMOUNT) $(MOUNT_POINT)
	@sudo $(LOSETUP) -d $(LOOP_DEV)

qemu: 
	@echo -e '\e[34m[QEMU]\e[0m Running QEMU ...'
	@$(QEMU) $(QEMU_OPTS)

vmdk:
	@echo -e '\e[34m[VMDK]\e[0m Creating vmdk ...'
	@qemu-img convert -f raw -O vmdk $(IMG) build/disk.vmdk

