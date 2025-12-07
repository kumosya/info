CPP=g++
CPPFLAGS=-c -m64 -nostdlib -nostdinc -fno-stack-protector -mcmodel=large \
			-fno-asynchronous-unwind-tables -ffreestanding -fno-exceptions \
			-std=c++11 -fno-pie \
            -I ./include \
			-I ./lib/libc/include/ \
            -I ./lib/libc++/include/
LD=ld
LDFLAGS= -nostdlib -static -z noexecstack --no-warn-rwx-segments -no-relax -T kernel/kernel.lds -m elf_x86_64 \
			
MANGLE_HDR = include/cpp_mangle.h

KERNEL_ELF = build/kernel.elf
OBJS+= \
	kernel/entry.o \
	kernel/boot.o \
	kernel/boot_video.o \
	kernel/mm.o \
	kernel/pic.o \
	kernel/assert.o \
	kernel/serial.o \
	kernel/timer.o \
	kernel/init.o \
	kernel/idt.o \
	kernel/tty.o \
	mm/page.o \
	mm/new.o \
	mm/pool.o \
	lib/libc.a

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

QEMU = qemu-system-x86_64
IMG = build/disk.img
QEMU_OPTS = -m 512M -hda $(IMG) -nographic -serial mon:stdio -boot c

ifeq ($(DEBUG), true)
	CPPFLAGS+=  -O0 -g -D DEBUG
	QEMU_OPTS += -s -S 
endif

.PHONY: all lib clean makeimg cpfiles run runall

lib:
	@cd lib/libc && $(MAKE) all

all: clean lib kernel/entry.o $(KERNEL_ELF)

$(KERNEL_ELF): $(OBJS)
	@echo -e '\e[32m[LD]\e[0m $@'
	@$(LD) $(LDFLAGS) -o $@ $(OBJS)

# Compile the C++ entry object first
kernel/entry.o: kernel/entry.cc
	@echo -e '\e[32m[CPP]\e[0m $<'
	@$(CPP) $(CPPFLAGS) -c -o $@ $<
	@mangled=`nm -g --defined-only $@ | awk '/cppstart/ {print $$3; exit}'`; \
	if [ -z "$$mangled" ]; then \
		echo "ERROR: can't find cppstart symbol in $@"; exit 1; \
	fi; \
	printf '%s\n' "#define CPPSTART $$mangled" > $(MANGLE_HDR)

# Assemble/Preprocess boot.S after the mangled-header is available.
# Use assembler-with-cpp so #include "include/cpp_mangle.h" in boot.S works.
kernel/boot.o: kernel/boot.S
	@echo -e '\e[32m[CPP]\e[0m $<'
	@$(CPP) $(CPPFLAGS) -x assembler-with-cpp -o $@ $<

# Generic rule for compiling .cc -> .o (others)
%.o: %.cc
	@echo -e '\e[32m[CPP]\e[0m $<'
	@$(CPP) $(CPPFLAGS) -o $@ $<


clean:
	@echo -e '\e[33m[RM]\e[0m Cleaning build files ...'
	@rm -f kernel.elf $(OBJS) $(MANGLE_HDR)
	# @cd lib/libc && $(MAKE) clean ARCH=$(ARCH)

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
	@echo -e '\e[34m[CP]\e[0m Copying files to rootfs ...'
	@cp $(KERNEL_ELF) $(ROOTFS)/boot/kernel.elf
	@sudo $(LOSETUP) -P $(LOOP_DEV) $(IMG)
	@sudo $(MOUNT) $(PARTED_DEV) $(MOUNT_POINT)
	@sudo cp -r $(ROOTFS)/* $(MOUNT_POINT)
	@sudo $(UMOUNT) $(MOUNT_POINT)
	@sudo $(LOSETUP) -d $(LOOP_DEV)

qemu: 
	@echo -e '\e[34m[QEMU]\e[0m Running QEMU ...'
	@$(QEMU) $(QEMU_OPTS)
