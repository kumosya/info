# 定义默认架构
ARCH ?= x86-64

# 根据架构选择编译器和编译选项
ifeq ($(ARCH), x86-64)
CC=gcc
CFLAGS=-c -m64 -fno-builtin -fno-stack-protector -nostartfiles -nostdinc -nostdlib  -Wall -Wextra -Werror=implicit-function-declaration \
	-I ./include/
AR=ar
ARFLAGS=rcs
else ifeq ($(ARCH), aarch64)
CC=aarch64-linux-gcc
CFLAGS=-c -aarch64 -fno-builtin -fno-stack-protector -nostartfiles -nostdinc -nostdlib -Wall -Wextra -Werror=implicit-function-declaration \
	-I ./include/
AR=aarch64-linux-gnu-ar
ARFLAGS=rcs
else
$(error 不支持的架构: $(ARCH))
endif

# 编译模式
ifdef DEBUG
CFLAGS += -g -O0
else
CFLAGS += -O2
endif

SRCS=${wildcard *.c}
OBJS=${SRCS:.c=.o}

# The name of the static library
LIBC=../libc.a

# Default target
all: $(LIBC)

# Build the static library
$(LIBC): $(OBJS)
	@echo -e '\e[32m[AR]\e[0m $@'
	@$(AR) $(ARFLAGS) $(LIBC) $(OBJS)

# Compilation rules
%.o: %.c
	@$(CC) $(CFLAGS) -o $@ $<

# Clean rules
clean:
	@rm -f $(OBJS) $(LIBC)

# 依赖声明
.PHONY: all clean

# 显示编译进度
.NOTPARALLEL:
	
