CPP=g++
CPPFLAGS=-c -m64 -nostdlib -nostdinc -fno-stack-protector -mcmodel=large \
			-fno-asynchronous-unwind-tables -ffreestanding -fno-exceptions \
			-std=c++11 -fno-pie \
            -I ../include/kernel/ \
			-I ../include/libc/ \
            -I ../include/libc++/
LD=ld
LDFLAGS= -nostdlib -static -z noexecstack --no-warn-rwx-segments -no-relax -T ../scripts/kernel.lds -m elf_x86_64 \
			
MANGLE_HDR = ../include/kernel/cpp_mangle.h

KERNEL_ELF = ../build/kernel.elf

SRCS:=${wildcard kernel/*/*.cc kernel/*.cc \
				mm/*.cc \
				task/*.cc \
				drivers/*.cc \
				drivers/*/*.cc \
				fs/*.cc \
				fs/*/*.cc \
		}
OBJS:=$(patsubst %.cc, %.o, $(SRCS))
AS_OBJS:=kernel/boot/head.o kernel/interrupt.o

LIBC = ../lib/libc.a

all: $(KERNEL_ELF)

$(KERNEL_ELF): $(OBJS) $(AS_OBJS)
	@echo -e '\e[32m[LD]\e[0m $@'
	@$(LD) $(LDFLAGS) -o $@ $(OBJS) $(AS_OBJS) $(LIBC)

%.o: %.S
	@echo -e '\e[32m[CPP]\e[0m $<'
	@$(CPP) $(CPPFLAGS) -x assembler-with-cpp -o $@ $<

# Generic rule for compiling .cc -> .o (others)
%.o: %.cc
	@echo -e '\e[32m[CPP]\e[0m $<'
	@$(CPP) $(CPPFLAGS) -o $@ $<

clean: 
	@rm -f $(OBJS) $(AS_OBJS) $(KERNEL_ELF)
