
CPP=g++
CPPFLAGS=-c -m64 -nostdlib -nostdinc -fno-stack-protector \
			-ffreestanding -fno-exceptions \
			-fno-rtti \
			-std=c++11 -fno-pie -O0 \
            -I ../../include/
LD=ld
LDFLAGS= -nostdlib -m elf_x86_64

USER_LIBC = ../../build/libc.a

SH = ../../build/rootfs/bin/sh

all : $(SH)

$(SH): head.o sh.o
	@echo -e '\e[32m[LD]\e[0m $@'
	@$(LD) $(LDFLAGS) -o $@ $^ $(USER_LIBC)

%.o: %.S
	@echo -e '\e[32m[CPP]\e[0m $<'
	@$(CPP) $(CPPFLAGS) -x assembler-with-cpp -o $@ $<

# Generic rule for compiling .cc -> .o (others)
%.o: %.cc
	@echo -e '\e[32m[CPP]\e[0m $<'
	@$(CPP) $(CPPFLAGS) -o $@ $<
