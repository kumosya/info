PROJECT := osinfo
SMP ?= 4
BUILD_DIR := build
OBJECT_DIR := obj/linux
INCLUDE_DIR := include
SOURCE_DIR := src
KERNEL_LINKER_SCRIPT := script/kernel.lds

CROSS ?= x86_64-elf-
ifeq ($(shell command -v $(CROSS)gcc 2>/dev/null),)
CC := gcc
CXX := g++
else
CC := $(CROSS)gcc
CXX := $(CROSS)g++
endif

CPPFLAGS := -I$(INCLUDE_DIR)
DEPENDENCY_FLAGS := -MMD -MP
CFLAGS := -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -nostdinc -m32 -O2 -Wall -Wextra
CXXFLAGS := -std=c++20 -ffreestanding -fno-builtin -fno-stack-protector -fno-exceptions -fno-rtti \
	-fno-use-cxa-atexit -fno-threadsafe-statics -fno-pic -m64 -mcmodel=large \
	-mno-red-zone -nostdinc -nostdinc++ -O2 -Wall -Wextra
ASFLAGS := -m64 -ffreestanding -fno-pic
KERNEL_LDFLAGS := -no-pie -nostdlib -nostartfiles -nodefaultlibs \
	-Wl,--build-id=none -Wl,-T,$(KERNEL_LINKER_SCRIPT)

CPP_SOURCES := $(wildcard $(SOURCE_DIR)/kernel/*.cc $(SOURCE_DIR)/kernel/*/*.cc $(SOURCE_DIR)/kernel/*/*/*.cc)
ASM_SOURCES := $(wildcard $(SOURCE_DIR)/kernel/*.S $(SOURCE_DIR)/kernel/*/*.S $(SOURCE_DIR)/kernel/*/*/*.S)
KERNEL_CPP_OBJECTS := $(patsubst $(SOURCE_DIR)/kernel/%.cc,$(OBJECT_DIR)/kernel/%.o,$(CPP_SOURCES))
KERNEL_ASM_OBJECTS := $(patsubst $(SOURCE_DIR)/kernel/%.S,$(OBJECT_DIR)/kernel/%.o,$(ASM_SOURCES))
KERNEL_OBJECTS := $(KERNEL_CPP_OBJECTS) $(KERNEL_ASM_OBJECTS)
KERNEL := $(BUILD_DIR)/kernel.elf

USER_CXXFLAGS := -std=c++20 -ffreestanding -fno-builtin -fno-stack-protector \
	-fno-exceptions -fno-rtti -fno-use-cxa-atexit -fno-threadsafe-statics \
	-fPIC -m64 -mcmodel=small -mno-red-zone -nostdinc -nostdinc++ \
	-Os -Wall -Wextra
USER_LDFLAGS := -no-pie -nostdlib -nostartfiles -nodefaultlibs \
	-Wl,--build-id=none -Wl,--hash-style=sysv -Wl,-T,script/user.lds
USER_RUNTIME_OBJECTS := $(OBJECT_DIR)/user/runtime/runtime.o \
	$(OBJECT_DIR)/user/runtime/service.o \
	$(OBJECT_DIR)/user/runtime/gui.o $(OBJECT_DIR)/user/runtime/process.o \
	$(OBJECT_DIR)/user/runtime/file.o $(OBJECT_DIR)/user/runtime/input.o \
	$(OBJECT_DIR)/user/runtime/pty.o $(OBJECT_DIR)/user/runtime/c_api.o
CORE_SERVICE_NAMES := init task fs device storage
DISK_SERVICE_NAMES := console window
SERVICE_NAMES := $(CORE_SERVICE_NAMES) $(DISK_SERVICE_NAMES)
APPLICATION_NAMES := shell hello echo cat ls ps sleep touch mkdir rmdir rm cp mv \
	find grep edit kill mount umount mkfs terminal
SERVICE_PROGRAMS := $(addprefix $(BUILD_DIR)/services/,$(addsuffix .elf,$(SERVICE_NAMES)))
APPLICATION_PROGRAMS := $(addprefix $(BUILD_DIR)/bin/,$(APPLICATION_NAMES))
USER_PROGRAMS := $(SERVICE_PROGRAMS) $(APPLICATION_PROGRAMS)
SERVICE_SOURCES := $(foreach service,$(SERVICE_NAMES),$(wildcard \
	$(SOURCE_DIR)/$(service)/*.cc $(SOURCE_DIR)/$(service)/*/*.cc))
SERVICE_ASM_SOURCES := $(foreach service,$(SERVICE_NAMES),$(wildcard $(SOURCE_DIR)/$(service)/*.S))
SERVICE_CXX_OBJECTS := $(patsubst $(SOURCE_DIR)/%.cc,$(OBJECT_DIR)/%.o,$(SERVICE_SOURCES))
SERVICE_ASM_OBJECTS := $(patsubst $(SOURCE_DIR)/%.S,$(OBJECT_DIR)/%.o,$(SERVICE_ASM_SOURCES))
SERVICE_OBJECTS := $(SERVICE_CXX_OBJECTS) $(SERVICE_ASM_OBJECTS)
APPLICATION_MAIN_OBJECTS := $(addprefix $(OBJECT_DIR)/user/,$(addsuffix /main.o,$(APPLICATION_NAMES)))
AR ?= ar
DISK_IMAGE := $(BUILD_DIR)/$(PROJECT).img
TEST_ISO := $(BUILD_DIR)/$(PROJECT)-test.iso
LIBC := $(BUILD_DIR)/lib/libc.a
LIBCXX := $(BUILD_DIR)/lib/libc++.a
LIBCXXABI := $(BUILD_DIR)/lib/libc++abi.a
LIBOS_CXX_SO := $(BUILD_DIR)/lib/libos-c++.so
LIBC_SO := $(BUILD_DIR)/lib/libc.so
LIBCXX_SO := $(BUILD_DIR)/lib/libc++.so
USER_SHARED_LIBRARIES := $(LIBOS_CXX_SO) $(LIBC_SO) $(LIBCXX_SO)
IMAGE_INPUTS := $(KERNEL) $(APPLICATION_PROGRAMS) $(SERVICE_PROGRAMS) \
	$(USER_SHARED_LIBRARIES)
USER_SHARED_LDFLAGS := -shared -nostdlib -nostartfiles -nodefaultlibs \
	-Wl,--build-id=none -Wl,--hash-style=sysv
LIBC_SOURCES := internal.c fd.c filesystem.c directory.c memory_runtime.c \
	string_runtime.c ctype_runtime.c stdlib_runtime.c stdio_runtime.c \
	time_runtime.c process_runtime.c signal_runtime.c spawn.c mman.c aio.c mqueue.c environment.c errors.c pthread.c threads.c \
	semaphore.c posix_stubs.c errno.c
LIBC_OBJECTS := $(addprefix $(OBJECT_DIR)/lib/libc/,$(LIBC_SOURCES:.c=.o))
LIBCXX_OBJECTS := $(OBJECT_DIR)/lib/libc++/new.o \
	$(OBJECT_DIR)/lib/libc++/iostream_globals.o \
	$(OBJECT_DIR)/lib/libc++/ostream.o $(OBJECT_DIR)/lib/libc++/istream.o \
	$(OBJECT_DIR)/lib/libc++/thread.o
LIBCXXABI_OBJECTS := $(OBJECT_DIR)/lib/libc++/abi.o
GENERATED_OBJECTS := $(KERNEL_OBJECTS) $(SERVICE_OBJECTS) $(APPLICATION_MAIN_OBJECTS) \
	$(USER_RUNTIME_OBJECTS) $(OBJECT_DIR)/user/runtime/start.o \
	$(OBJECT_DIR)/user/runtime/service_start.o $(LIBC_OBJECTS) \
	$(LIBCXX_OBJECTS) $(LIBCXXABI_OBJECTS)
DEPENDENCY_FILES := $(sort $(GENERATED_OBJECTS:.o=.d))

.SECONDARY: $(SERVICE_OBJECTS) $(APPLICATION_MAIN_OBJECTS)

.PHONY: all build kernel services applications libraries disk mkimg iso check \
	run debug clean posix-test

all: build

build: kernel services applications libraries

kernel: $(KERNEL)

services: $(SERVICE_PROGRAMS)

applications: $(APPLICATION_PROGRAMS)

libraries: $(USER_SHARED_LIBRARIES)

$(KERNEL): $(KERNEL_OBJECTS) $(KERNEL_LINKER_SCRIPT) Makefile
	@mkdir -p $(@D)
	@echo "[KERNEL LD] $@"
	@$(CXX) $(CXXFLAGS) $(KERNEL_LDFLAGS) -o $@ $(KERNEL_OBJECTS) -lgcc

$(OBJECT_DIR)/kernel/%.o: $(SOURCE_DIR)/kernel/%.cc
	@mkdir -p $(@D)
	@echo "[KERNEL CXX] $<"
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(DEPENDENCY_FLAGS) -c $< -o $@

$(OBJECT_DIR)/kernel/%.o: $(SOURCE_DIR)/kernel/%.S
	@mkdir -p $(@D)
	@echo "[KERNEL AS] $<"
	@$(CC) $(CPPFLAGS) $(ASFLAGS) $(DEPENDENCY_FLAGS) -c $< -o $@

$(OBJECT_DIR)/user/%.o: $(SOURCE_DIR)/user/%.cc
	@mkdir -p $(@D)
	@echo "[USER CXX] $<"
	@$(CXX) $(CPPFLAGS) $(USER_CXXFLAGS) $(DEPENDENCY_FLAGS) -c $< -o $@

$(OBJECT_DIR)/user/%.o: $(SOURCE_DIR)/user/%.S
	@mkdir -p $(@D)
	@echo "[USER AS] $<"
	@$(CC) $(CPPFLAGS) -m64 -ffreestanding -fno-pic $(DEPENDENCY_FLAGS) -c $< -o $@

$(SERVICE_CXX_OBJECTS): $(OBJECT_DIR)/%.o: $(SOURCE_DIR)/%.cc
	@mkdir -p $(@D)
	@echo "[SERVICE CXX] $<"
	@$(CXX) $(CPPFLAGS) $(USER_CXXFLAGS) $(DEPENDENCY_FLAGS) -c $< -o $@

$(SERVICE_ASM_OBJECTS): $(OBJECT_DIR)/%.o: $(SOURCE_DIR)/%.S
	@mkdir -p $(@D)
	@echo "[SERVICE AS] $<"
	@$(CC) $(CPPFLAGS) -m64 -ffreestanding -fno-pic $(DEPENDENCY_FLAGS) -c $< -o $@

$(OBJECT_DIR)/lib/%.o: lib/%.cc
	@mkdir -p $(@D)
	@echo "[LIB CXX] $<"
	@$(CXX) $(CPPFLAGS) $(USER_CXXFLAGS) $(DEPENDENCY_FLAGS) -c $< -o $@

$(OBJECT_DIR)/lib/%.o: lib/%.c
	@mkdir -p $(@D)
	@echo "[LIB C] $<"
	@$(CC) $(CPPFLAGS) -std=c17 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIC -m64 -mcmodel=small -mno-red-zone \
		-nostdinc -Os -Wall -Wextra -Werror $(DEPENDENCY_FLAGS) -c $< -o $@

$(LIBC): $(LIBC_OBJECTS)
	@mkdir -p $(@D)
	@echo "[AR] $@"
	@$(RM) $@
	@$(AR) rcs $@ $^

$(LIBCXX): $(LIBCXX_OBJECTS)
	@mkdir -p $(@D)
	@echo "[AR] $@"
	@$(RM) $@
	@$(AR) rcs $@ $^

$(LIBCXXABI): $(LIBCXXABI_OBJECTS)
	@mkdir -p $(@D)
	@echo "[AR] $@"
	@$(RM) $@
	@$(AR) rcs $@ $^

$(LIBOS_CXX_SO): $(USER_RUNTIME_OBJECTS) Makefile
	@mkdir -p $(@D)
	@echo "[SO] $@"
	@$(CXX) $(USER_CXXFLAGS) $(USER_SHARED_LDFLAGS) \
		-Wl,-soname,libos-c++.so -o $@ $(USER_RUNTIME_OBJECTS) -lgcc

$(LIBC_SO): $(LIBC_OBJECTS) $(LIBOS_CXX_SO) Makefile
	@mkdir -p $(@D)
	@echo "[SO] $@"
	@$(CXX) $(USER_CXXFLAGS) $(USER_SHARED_LDFLAGS) \
		-Wl,-soname,libc.so -o $@ $(LIBC_OBJECTS) \
		-Wl,--no-as-needed $(LIBOS_CXX_SO) -lgcc

$(LIBCXX_SO): $(LIBCXX_OBJECTS) $(LIBCXXABI_OBJECTS) $(LIBC_SO) $(LIBOS_CXX_SO) Makefile
	@mkdir -p $(@D)
	@echo "[SO] $@"
	@$(CXX) $(USER_CXXFLAGS) $(USER_SHARED_LDFLAGS) \
		-Wl,-soname,libc++.so -o $@ $(LIBCXX_OBJECTS) \
		$(LIBCXXABI_OBJECTS) -Wl,--no-as-needed $(LIBC_SO) $(LIBOS_CXX_SO) -lgcc

define SERVICE_template
$(BUILD_DIR)/services/$(1).elf: $$(filter $(OBJECT_DIR)/$(1)/%,$$(SERVICE_OBJECTS)) \
		$(OBJECT_DIR)/user/runtime/service_start.o $$(LIBOS_CXX_SO) \
		script/user.lds Makefile
	@mkdir -p $$(@D)
	@echo "[SERVICE LD] $$@"
	@$$(CXX) $$(USER_CXXFLAGS) $$(USER_LDFLAGS) -o $$@ \
		$$(filter $(OBJECT_DIR)/$(1)/%,$$(SERVICE_OBJECTS)) \
		$(OBJECT_DIR)/user/runtime/service_start.o \
		-Wl,-E,--no-dynamic-linker -Wl,--no-as-needed $$(LIBOS_CXX_SO) -lgcc
endef

$(foreach service,$(SERVICE_NAMES),$(eval $(call SERVICE_template,$(service))))

$(BUILD_DIR)/bin/%: $(OBJECT_DIR)/user/%/main.o \
		$(OBJECT_DIR)/user/runtime/start.o $(USER_SHARED_LIBRARIES) script/user.lds \
		Makefile
	@mkdir -p $(@D)
	@echo "[APP LD] $@"
	@$(CXX) $(USER_CXXFLAGS) $(USER_LDFLAGS) -o $@ $< \
		$(OBJECT_DIR)/user/runtime/start.o -Wl,-E,--no-dynamic-linker \
		-Wl,--no-as-needed $(LIBCXX_SO) $(LIBC_SO) $(LIBOS_CXX_SO) -lgcc

disk: mkimg

mkimg:
	@missing=0; \
	for input in $(IMAGE_INPUTS); do \
		if [ ! -f "$$input" ]; then \
			echo "missing build input: $$input" >&2; \
			missing=1; \
		fi; \
	done; \
	if [ "$$missing" -ne 0 ]; then \
		echo "run 'make all' before 'make mkimg'" >&2; \
		exit 1; \
	fi
	@echo "[MKIMG] $(DISK_IMAGE)"
	@sudo sh script/mkimg.sh "$(DISK_IMAGE)" "$(BUILD_DIR)"

$(TEST_ISO): $(wildcard script/cdrom/*)
	@mkdir -p $(@D)
	@command -v xorriso >/dev/null || { echo "xorriso is required"; exit 1; }
	@xorriso -as mkisofs -quiet -V OSINFO_TEST -o $@ script/cdrom

iso: $(TEST_ISO)

check: $(KERNEL) $(USER_PROGRAMS) $(USER_SHARED_LIBRARIES)
	@grub-file --is-x86-multiboot2 $(KERNEL)
	@readelf -h $(KERNEL) | grep -q 'EXEC (Executable file)'
	@test -z "$$(readelf -d $(KERNEL) 2>/dev/null | grep NEEDED)"
	@readelf -d $(LIBOS_CXX_SO) | grep -q 'Library soname: \[libos-c++.so\]'
	@readelf -d $(LIBC_SO) | grep -q 'Library soname: \[libc.so\]'
	@readelf -d $(LIBCXX_SO) | grep -q 'Library soname: \[libc++.so\]'
	@for service in $(SERVICE_PROGRAMS); do \
		readelf -d "$$service" | grep -q '(HASH)' && \
		readelf -d "$$service" | grep -q 'Shared library: \[libos-c++.so\]' || exit 1; \
	done
	@for application in $(APPLICATION_PROGRAMS); do \
		readelf -d "$$application" | grep -q '(HASH)' && \
		readelf -d "$$application" | grep -q 'Shared library: \[libc++.so\]' && \
		readelf -d "$$application" | grep -q 'Shared library: \[libc.so\]' && \
		readelf -d "$$application" | grep -q 'Shared library: \[libos-c++.so\]' || exit 1; \
	done

posix-test: $(OBJECT_DIR)/tests/posix/headers.o

$(OBJECT_DIR)/tests/posix/%.o: tests/posix/%.c
	@mkdir -p $(@D)
	@echo "[POSIX TEST] $<"
	@$(CC) $(CPPFLAGS) -std=c17 -ffreestanding -fno-builtin -nostdinc \
		-Wall -Wextra -Werror -c $< -o $@

run: iso
	@test -f "$(DISK_IMAGE)" || { echo "$(DISK_IMAGE) is missing; run 'make mkimg' first"; exit 1; }
	qemu-system-x86_64 -smp $(SMP) -boot c \
		-drive file=$(DISK_IMAGE),if=none,format=raw,id=osdisk \
		-device virtio-blk-pci,drive=osdisk,disable-legacy=on,bootindex=0 \
		-drive file=$(TEST_ISO),if=none,format=raw,readonly=on,id=testcd \
		-device virtio-blk-pci,drive=testcd,disable-legacy=on \
		-display gtk -serial stdio

debug: iso
	@test -f "$(DISK_IMAGE)" || { echo "$(DISK_IMAGE) is missing; run 'make mkimg' first"; exit 1; }
	qemu-system-x86_64 -smp $(SMP) -boot c \
		-drive file=$(DISK_IMAGE),if=none,format=raw,id=osdisk \
		-device virtio-blk-pci,drive=osdisk,disable-legacy=on,bootindex=0 \
		-drive file=$(TEST_ISO),if=none,format=raw,readonly=on,id=testcd \
		-device virtio-blk-pci,drive=testcd,disable-legacy=on \
		-display gtk -serial stdio -S -s

-include $(DEPENDENCY_FILES)

clean:
	rm -rf $(OBJECT_DIR) $(KERNEL) $(BUILD_DIR)/bin $(BUILD_DIR)/services \
		$(BUILD_DIR)/lib $(BUILD_DIR)/isodir $(TEST_ISO) \
		$(DISK_IMAGE)
