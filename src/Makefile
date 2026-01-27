#  /* clang-format off */

CPP=g++
CPPFLAGS=-c -m64 -nostdlib -nostdinc -fno-stack-protector -mcmodel=large \
			-ffreestanding -fno-exceptions \
			-fno-rtti \
			-std=c++11 -fno-pie \
            -I ../include/
LD=ld
LDFLAGS= -nostdlib -z noexecstack --no-warn-rwx-segments -T ../scripts/kernel.lds -m elf_x86_64 

KERNEL_ELF = ../build/kernel.elf

SRCS:=${wildcard kernel/*.cc kernel/*/*.cc \
				mm/*.cc \
				task/*.cc \
				drivers/*.cc \
				drivers/*/*.cc \
				fs/*.cc \
				fs/*/*.cc \
		}
OBJS:=$(patsubst %.cc, %.o, $(SRCS))
AS_OBJS:=kernel/boot/head.o kernel/interrupt.o task/proc.o

KLIBC = ../build/klibc.a

ifeq ($(ENABLE_TEXT_OUTPUT), true)
	CPPFLAGS += -D ENABLE_TEXT_OUTPUT=true
endif
ifeq ($(OUTPUT_TO_SERIAL), true)
	CPPFLAGS += -D OUTPUT_TO_SERIAL=true
else ifeq ($(OUTPUT_TO_SERIAL), false)
	CPPFLAGS += -D OUTPUT_TO_SERIAL=false
endif

all: $(KERNEL_ELF)
	cd user && $(MAKE)

$(KERNEL_ELF): $(OBJS) $(AS_OBJS)
	@echo -e '\e[32m[LD]\e[0m $@'
	@$(LD) $(LDFLAGS) -o $@ $(OBJS) $(AS_OBJS) $(KLIBC)

%.o: %.S
	@echo -e '\e[32m[CPP]\e[0m $<'
	@$(CPP) $(CPPFLAGS) -x assembler-with-cpp -o $@ $<

# Generic rule for compiling .cc -> .o (others)
%.o: %.cc
	@echo -e '\e[32m[CPP]\e[0m $<'
	@$(CPP) $(CPPFLAGS) -o $@ $<

clean: 
	@rm -f $(OBJS) $(AS_OBJS) $(KERNEL_ELF)
