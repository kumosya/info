CC=gcc
CPP=g++
CFLAGS=-c -m64 -fno-builtin -fno-stack-protector -nostartfiles \
	-nostdinc -nostdlib  -Wall -Wextra \
	 -I ../include -O2 -std=c11
CPPFLAGS=-c -m64 -fno-builtin -fno-stack-protector -nostartfiles -nostdinc -nostdlib \
	 -Wall -Wextra \
	-I ../include -O2 -std=c++11

AR=ar
ARFLAGS=rcs

# 编译模式
ifdef DEBUG
CFLAGS += -g -O0
else
CFLAGS += -O2
endif

KLIBC_SRCS=${wildcard klibc/*.c klibc/*.cc}
KLIBC_OBJS=${patsubst %.c,%.o,${patsubst %.cc,%.o,${KLIBC_SRCS}}}

LIBC_SRCS=${wildcard libc/*.c}
LIBC_OBJS=${patsubst %.c,%.o,${LIBC_SRCS}}

LIBCPP_SRCS=${wildcard libc++/*.cc}
LIBCPP_OBJS=${patsubst %.cc,%.o,${LIBCPP_SRCS}}

# The name of the static library
KLIBC=../build/klibc.a
LIBC=../build/libc.a
LIBCPP=../build/libc++.a

# Default target
all: $(KLIBC) $(LIBC) $(LIBCPP)

# Build the static library
$(KLIBC): $(KLIBC_OBJS)
	@echo -e '\e[32m[AR]\e[0m $@'
	@$(AR) $(ARFLAGS) $(KLIBC) $(KLIBC_OBJS)

$(LIBC): $(LIBC_OBJS)
	@echo -e '\e[32m[AR]\e[0m $@'
	@$(AR) $(ARFLAGS) $(LIBC) $(LIBC_OBJS)
	
$(LIBCPP): $(LIBCPP_OBJS)
	@echo -e '\e[32m[AR]\e[0m $@'
	@$(AR) $(ARFLAGS) $(LIBCPP) $(LIBCPP_OBJS)

# Compilation rules
%.o: %.c
	@$(CC) $(CFLAGS) -o $@ $<

%.o: %.cc
	@$(CPP) $(CPPFLAGS) -o $@ $<

# Clean rules
clean:
	@rm -f $(KLIBC_OBJS) $(KLIBC) $(LIBC) $(LIBCPP) libc/*.o klibc/*.o libc++/*.o

.PHONY: all clean

	
