# AArch64 Kernel Makefile

CPP=arm64-linux-gnu-g++
CPPFLAGS=-c -march=armv8-a -mtune=cortex-a53 -fno-builtin -fno-stack-protector -nostartfiles -nostdinc -nostdlib -std=c++11 \
            -I ./include \
			-I ../../lib/libc/include/ \
            -I ../../lib/libc++/include/
LD=arm64-linux-gnu-ld
LDFLAGS= -nostdlib -z noexecstack --no-warn-rwx-segments -T kernel.lds -m aarch64linux

MANGLE_HDR = include/cpp_mangle.h

DEBUG?=false
ifeq ($(DEBUG), true)
CPPFLAGS+= -D DEBUG
endif

OBJS+= \
	kernel/entry.o \
    kernel/boot.o

.PHONY: all clean

all: clean kernel/entry.o kernel.elf

kernel.elf: $(OBJS)
	@echo -e '\e[32m[LD]\e[0m $@'
	@$(LD) $(LDFLAGS) -o $@ $(OBJS)

# Compile the C++ entry object first
kernel/entry.o: kernel/entry.cc
	@echo -e '\e[32m[CPP]\e[0m $<' 
	@$(CPP) $(CPPFLAGS) -c -o $@ $<
	@mangled=`aarch64-linux-gnu-nm -g --defined-only $@ | awk '/cppstart/ {print $$3; exit}'`; \
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
