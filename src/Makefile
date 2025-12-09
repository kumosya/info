CPP=g++
CPPFLAGS=-c -m64 -nostdlib -nostdinc -fno-stack-protector -mcmodel=large \
			-fno-asynchronous-unwind-tables -ffreestanding -fno-exceptions \
			-std=c++11 -fno-pie \
            -I ../include/kernel/ \
			-I ../include/libc/ \
            -I ../include/libc++/
LD=ld
LDFLAGS= -nostdlib -static -z noexecstack --no-warn-rwx-segments -no-relax -T kernel/kernel.lds -m elf_x86_64 \
			
MANGLE_HDR = ../include/kernel/cpp_mangle.h

KERNEL_ELF = ../build/kernel.elf
OBJS= \
	kernel/start.o \
	kernel/boot.o \
	kernel/boot_video.o \
	kernel/mm.o \
	kernel/pic.o \
	kernel/assert.o \
	kernel/serial.o \
	kernel/timer.o \
	kernel/keyboard.o \
	kernel/main.o \
	kernel/idt.o \
	kernel/gdt.o \
	kernel/tty.o \
	kernel/interrupt.o \
	mm/page.o \
	mm/new.o \
	mm/pool.o \
	task/proc.o \
	../lib/libc.a

all: kernel/start.o $(KERNEL_ELF)

$(KERNEL_ELF): $(OBJS)
	@echo -e '\e[32m[LD]\e[0m $@'
	@$(LD) $(LDFLAGS) -o $@ $(OBJS)

# Compile the C++ start object first
kernel/start.o: kernel/start.cc
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

# Assemble interrupt stubs
kernel/interrupt.o: kernel/interrupt.S
	@echo -e '\e[32m[ASM]\e[0m $<'
	@$(CPP) $(CPPFLAGS) -x assembler-with-cpp -o $@ $<

# Generic rule for compiling .cc -> .o (others)
%.o: %.cc
	@echo -e '\e[32m[CPP]\e[0m $<'
	@$(CPP) $(CPPFLAGS) -o $@ $<

clean: 
	@rm -f kernel.elf $(OBJS) $(MANGLE_HDR)
