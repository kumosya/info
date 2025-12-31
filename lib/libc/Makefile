CC=gcc
CFLAGS=-c -m64 -fno-builtin -fno-stack-protector -nostartfiles -nostdinc -nostdlib  -Wall -Wextra -Werror=implicit-function-declaration \
	-I ../../include/libc -I ../../include
AR=ar
ARFLAGS=rcs

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

.PHONY: all clean

	
