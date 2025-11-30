CPP=g++
CPPFLAGS=-c -m64 -fno-builtin -mcmodel=large -fno-stack-protector -nostartfiles -nostdinc -nostdlib -std=c++11 -fno-asynchronous-unwind-tables -fno-exceptions -fno-rtti -s \
            -I ./include \
			-I ../../lib/libc/include/ \
            -I ../../lib/libc++/include/
LD=ld
LDFLAGS= -nostdlib -z noexecstack --no-warn-rwx-segments -no-relax -T kernel.lds -m elf_x86_64

MANGLE_HDR = include/cpp_mangle.h

DEBUG?=false
ifeq ($(DEBUG), true)
CPPFLAGS+= -D DEBUG
endif

OBJS+= \
	kernel/entry.o \
    kernel/boot.o \
    kernel/boot_video.o \
    kernel/mm.o \
	kernel/init.o

.PHONY: all clean

all: clean kernel/entry.o kernel.elf
	@cp kernel.elf ../../build/rootfs/boot/kernel.elf

kernel.elf: $(OBJS)
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