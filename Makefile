ARCH = x86-64
#ARCH = aarch64

DD = dd
PARTED = parted
LOSETUP = losetup
MKFS = mkfs.ext2
GRUB_INSTALL = grub-install
MOUNT = mount
UMOUNT = umount

ROOTFS = build/rootfs
LOOP_DEV = /dev/loop7
PARTED_DEV = $(LOOP_DEV)p1
MOUNT_POINT = /mnt/disk
ZERO_FILL_SIZE = 32M

ifeq ($(ARCH), x86-64)
	QEMU := qemu-system-x86_64
else ifeq ($(ARCH), aarch64)
	QEMU := qemu-system-aarch64
endif
IMG = build/disk.img
ifeq ($(ARCH), x86-64)
	QEMU_OPTS = -m 512M -hda $(IMG)
else ifeq ($(ARCH), aarch64)
	QEMU_OPTS = -M virt -cpu cortex-a53 -kernel arch/aarch64/kernel.elf -nographic -serial stdio -monitor null -s -S
endif

ifeq ($(DEBUG), true)
	QEMU_OPTS += -s -S
endif

# set architecture aarch64:isa64r2
# target remote :1234

.PHONY: all clean makeimg cpfiles run runall

all:
	@cd lib/libc && $(MAKE) all ARCH=$(ARCH)
	@cd arch/$(ARCH) && $(MAKE) all

clean:
	@cd lib/libc && $(MAKE) clean ARCH=$(ARCH)
	@cd arch/$(ARCH) && $(MAKE) clean

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

cpfiles:
ifeq ($(ARCH), x86-64)
	@echo -e '\e[34m[CP]\e[0m Copying files to rootfs ...'
	@sudo $(LOSETUP) -P $(LOOP_DEV) $(IMG)
	@sudo $(MOUNT) $(PARTED_DEV) $(MOUNT_POINT)
	@sudo cp -r $(ROOTFS)/* $(MOUNT_POINT)
	@sudo $(UMOUNT) $(MOUNT_POINT)
	@sudo $(LOSETUP) -d $(LOOP_DEV)
else ifeq ($(ARCH), aarch64)
	@echo -e 'There is no need to copy files to rootfs for aarch64.'
endif

qemu: 
	@echo -e '\e[34m[QEMU]\e[0m Running QEMU ...'
	@$(QEMU) $(QEMU_OPTS)
