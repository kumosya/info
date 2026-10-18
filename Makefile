PROJECT := osinfo
SMP ?= 4
BUILD_DIR := build
PACKAGE_DIR := packages
OBJECT_DIR := obj
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

CPPFLAGS := -I$(INCLUDE_DIR) -I$(INCLUDE_DIR)/stdc -I$(INCLUDE_DIR)/stdc++ -I$(INCLUDE_DIR)/posix -I$(INCLUDE_DIR)/os-c++ -I$(INCLUDE_DIR)/osgui-c++
DEPENDENCY_FLAGS := -MMD -MP
CFLAGS := -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -nostdinc -m32 -O2 -Wall -Wextra
CXXFLAGS := -std=c++20 -ffreestanding -fno-builtin -fno-stack-protector -fno-exceptions -fno-rtti \
	-fno-use-cxa-atexit -fno-threadsafe-statics -fno-pic -m64 -mcmodel=large \
	-mno-red-zone -mgeneral-regs-only -nostdinc -nostdinc++ -O2 -Wall -Wextra
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
	$(OBJECT_DIR)/user/runtime/task.o \
	$(OBJECT_DIR)/user/runtime/file.o $(OBJECT_DIR)/user/runtime/input.o \
	$(OBJECT_DIR)/user/runtime/pty.o
CORE_SERVICE_NAMES := init task fs pci device storage
DISK_SERVICE_NAMES := tty console guiw users
SERVICE_NAMES := $(CORE_SERVICE_NAMES) $(DISK_SERVICE_NAMES)
PACKAGE_CONFIGS := $(wildcard user/*/package.conf lib/*.conf)
include $(BUILD_DIR)/packages.mk
SERVICE_PROGRAMS := $(addprefix $(BUILD_DIR)/services/,$(addsuffix .elf,$(SERVICE_NAMES)))
USER_PROGRAMS := $(SERVICE_PROGRAMS) $(APPLICATION_PROGRAMS)
SERVICE_SOURCES := $(foreach service,$(filter-out guiw,$(SERVICE_NAMES)),$(wildcard \
	$(SOURCE_DIR)/$(service)/*.cc $(SOURCE_DIR)/$(service)/*/*.cc))
SERVICE_ASM_SOURCES := $(foreach service,$(SERVICE_NAMES),$(wildcard $(SOURCE_DIR)/$(service)/*.S))
SERVICE_CXX_OBJECTS := $(patsubst $(SOURCE_DIR)/%.cc,$(OBJECT_DIR)/%.o,$(SERVICE_SOURCES))
GUIW_SOURCES := $(wildcard user/guiw/server/*.cc)
GUIW_OBJECTS := $(patsubst user/guiw/server/%.cc,$(OBJECT_DIR)/user/guiw/server/%.o,$(GUIW_SOURCES))
SERVICE_CXX_OBJECTS += $(GUIW_OBJECTS)
SERVICE_ASM_OBJECTS := $(patsubst $(SOURCE_DIR)/%.S,$(OBJECT_DIR)/%.o,$(SERVICE_ASM_SOURCES))
SERVICE_OBJECTS := $(SERVICE_CXX_OBJECTS) $(SERVICE_ASM_OBJECTS)
application_object = $(OBJECT_DIR)/user/$(if $(filter terminal,$(1)),guiw,$(1))/main.o
APPLICATION_MAIN_OBJECTS := $(foreach app,$(APPLICATION_NAMES),$(call application_object,$(app)))
AR ?= ar
DISK_IMAGE := $(BUILD_DIR)/$(PROJECT).img
LIBC := $(BUILD_DIR)/lib/libc.a
LIBCXX := $(BUILD_DIR)/lib/libc++.a
LIBCXXABI := $(BUILD_DIR)/lib/libc++abi.a
LIBOS_CXX_SO := $(PACKAGE_DIR)/libos-c++/libos-c++.so
LIBOSGUI_CXX_SO := $(PACKAGE_DIR)/libosgui-c++/libosgui-c++.so
LIBC_SO := $(PACKAGE_DIR)/libc++/libc.so
LIBCXX_SO := $(PACKAGE_DIR)/libc++/libc++.so
USER_SHARED_LIBRARIES := $(LIBOS_CXX_SO) $(LIBC_SO) $(LIBCXX_SO) $(LIBOSGUI_CXX_SO)
IMAGE_INPUTS := $(KERNEL) $(APPLICATION_PROGRAMS) $(SERVICE_PROGRAMS) \
	$(USER_SHARED_LIBRARIES)
USER_SHARED_LDFLAGS := -shared -nostdlib -nostartfiles -nodefaultlibs \
	-Wl,--build-id=none -Wl,--hash-style=sysv
LIBC_SOURCES := internal.c fd.c filesystem.c directory.c glob.c credentials.c memory_runtime.c \
	string_runtime.c ctype_runtime.c stdlib_runtime.c stdio_runtime.c \
	time_runtime.c task_runtime.c signal_runtime.c spawn.c mman.c aio.c mqueue.c environment.c errors.c pthread.c threads.c \
	semaphore.c errno.c locale_runtime.c unicode_runtime.c stdio_extra.c posix_runtime.c accounts.c extensions.c
LIBC_OBJECTS := $(addprefix $(OBJECT_DIR)/lib/libc/,$(LIBC_SOURCES:.c=.o)) $(OBJECT_DIR)/lib/libc/os_adapter.o
PORTABLE_LIBC_SOURCES := $(wildcard lib/libc/portable/*/*.c)
PORTABLE_LIBC_ASM := $(wildcard lib/libc/portable/*/*.S)
PORTABLE_LIBC_OBJECTS := $(patsubst lib/%.c,$(OBJECT_DIR)/lib/%.o,$(PORTABLE_LIBC_SOURCES)) $(patsubst lib/%.S,$(OBJECT_DIR)/lib/%.o,$(PORTABLE_LIBC_ASM))
LIBC_OBJECTS += $(PORTABLE_LIBC_OBJECTS)
LIBC_OBJECTS += $(patsubst lib/%.c,$(OBJECT_DIR)/lib/%.o,$(wildcard lib/libc/fallback/*.c))
LIBCXX_OBJECTS := $(OBJECT_DIR)/lib/libc++/new.o \
	$(OBJECT_DIR)/lib/libc++/iostream_globals.o \
	$(OBJECT_DIR)/lib/libc++/ostream.o $(OBJECT_DIR)/lib/libc++/istream.o \
	$(OBJECT_DIR)/lib/libc++/thread.o
LIBCXXABI_OBJECTS := $(OBJECT_DIR)/lib/libc++/abi.o
GENERATED_OBJECTS := $(KERNEL_OBJECTS) $(SERVICE_OBJECTS) $(APPLICATION_MAIN_OBJECTS) \
	$(USER_RUNTIME_OBJECTS) $(OBJECT_DIR)/lib/osgui/gui.o $(OBJECT_DIR)/user/runtime/start.o \
	$(OBJECT_DIR)/user/runtime/service_start.o $(LIBC_OBJECTS) \
	$(LIBCXX_OBJECTS) $(LIBCXXABI_OBJECTS)
DEPENDENCY_FILES := $(sort $(GENERATED_OBJECTS:.o=.d))

.SECONDARY: $(SERVICE_OBJECTS) $(APPLICATION_MAIN_OBJECTS)

.PHONY: all build kernel services applications libraries package-archives disk mkimg cpfiles check \
	run debug clean

.DEFAULT_GOAL := all
all: build package-archives

$(BUILD_DIR)/packages.mk: script/packages.py $(PACKAGE_CONFIGS) Makefile user lib $(wildcard user/*/)
	@python3 script/packages.py --package-dir "$(PACKAGE_DIR)" --makefile "$@"

package-archives: applications libraries $(PACKAGE_DIR)/guiw/guiw.elf
	@python3 script/packages.py --package-dir "$(PACKAGE_DIR)"

$(PACKAGE_DIR)/guiw/guiw.elf: $(BUILD_DIR)/services/guiw.elf
	@mkdir -p $(@D)
	@cp $< $@

build: kernel services applications libraries

kernel: $(KERNEL)

ifeq ($(USER_SUBBUILD),1)
services: $(SERVICE_PROGRAMS)
applications: $(APPLICATION_PROGRAMS)
else
services applications: libraries
	+$(MAKE) -C user $@
endif

libraries: $(USER_SHARED_LIBRARIES) $(PACKAGE_DIR)/libc++/COPYRIGHT

$(PACKAGE_DIR)/libc++/COPYRIGHT: lib/libc/portable/COPYRIGHT
	@mkdir -p $(@D)
	@cp $< $@

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

$(OBJECT_DIR)/user/runtime/%.o: lib/os/%.cc
	@mkdir -p $(@D)
	@echo "[USER CXX] $<"
	@$(CXX) $(CPPFLAGS) $(USER_CXXFLAGS) $(DEPENDENCY_FLAGS) -c $< -o $@

$(OBJECT_DIR)/user/runtime/%.o: lib/os/%.S
	@mkdir -p $(@D)
	@echo "[USER AS] $<"
	@$(CC) $(CPPFLAGS) -m64 -ffreestanding -fno-pic $(DEPENDENCY_FLAGS) -c $< -o $@

$(filter-out $(GUIW_OBJECTS),$(SERVICE_CXX_OBJECTS)): $(OBJECT_DIR)/%.o: $(SOURCE_DIR)/%.cc
	@mkdir -p $(@D)
	@echo "[SERVICE CXX] $<"
	@$(CXX) $(CPPFLAGS) $(USER_CXXFLAGS) $(DEPENDENCY_FLAGS) -c $< -o $@

$(GUIW_OBJECTS): $(OBJECT_DIR)/user/guiw/server/%.o: user/guiw/server/%.cc
	@mkdir -p $(@D)
	@echo "[GUIW CXX] $<"
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

$(OBJECT_DIR)/lib/libc/fallback/%.o: lib/libc/fallback/%.c
	@mkdir -p $(@D)
	@$(CC) $(CPPFLAGS) -std=c17 -ffreestanding -fno-builtin -fno-stack-protector \
		-fPIC -m64 -mno-red-zone -nostdinc -Os -Wall -Wextra -Werror \
		-Wno-array-parameter $(DEPENDENCY_FLAGS) -c $< -o $@

$(OBJECT_DIR)/lib/libc/portable/%.o: lib/libc/portable/%.c
	@mkdir -p $(@D)
	@echo "[PORTABLE C] $<"
	@$(CC) $(CPPFLAGS) -Ilib/libc/portable/internal -include lib/libc/portable/internal/compat.h \
		-std=c17 -D_GNU_SOURCE -ffreestanding -fno-builtin -fno-stack-protector -fPIC -m64 \
		-mno-red-zone -nostdinc -O2 $(DEPENDENCY_FLAGS) -c $< -o $@

$(OBJECT_DIR)/lib/libc/portable/%.o: lib/libc/portable/%.S
	@mkdir -p $(@D)
	@$(CC) -m64 -fPIC -c $< -o $@

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

$(LIBOSGUI_CXX_SO): $(OBJECT_DIR)/lib/osgui/gui.o $(LIBOS_CXX_SO) Makefile
	@mkdir -p $(@D)
	@echo "[SO] $@"
	@$(CXX) $(USER_CXXFLAGS) $(USER_SHARED_LDFLAGS) \
		-Wl,-soname,libosgui-c++.so -o $@ $< -Wl,--no-as-needed $(LIBOS_CXX_SO) -lgcc

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
$(BUILD_DIR)/services/$(1).elf: $$(if $$(filter guiw,$(1)),$$(GUIW_OBJECTS),$$(filter $(OBJECT_DIR)/$(1)/%,$$(SERVICE_OBJECTS))) \
		$(OBJECT_DIR)/user/runtime/service_start.o $$(LIBOS_CXX_SO) \
		$$(if $$(filter $(1),$$(DISK_SERVICE_NAMES)),$$(LIBOSGUI_CXX_SO)) \
		$$(if $$(filter $(1),users),$$(LIBC_SO)) \
		script/user.lds Makefile
	@mkdir -p $$(@D)
	@echo "[SERVICE LD] $$@"
	@$$(CXX) $$(USER_CXXFLAGS) $$(USER_LDFLAGS) -o $$@ \
		$$(if $$(filter guiw,$(1)),$$(GUIW_OBJECTS),$$(filter $(OBJECT_DIR)/$(1)/%,$$(SERVICE_OBJECTS))) \
		$(OBJECT_DIR)/user/runtime/service_start.o \
		-Wl,-E,--no-dynamic-linker -Wl,--no-as-needed $$(LIBOS_CXX_SO) \
		$$(if $$(filter $(1),$$(DISK_SERVICE_NAMES)),$$(LIBOSGUI_CXX_SO)) \
		$$(if $$(filter $(1),users),$$(LIBC_SO)) -lgcc
endef

$(foreach service,$(SERVICE_NAMES),$(eval $(call SERVICE_template,$(service))))

define APPLICATION_template
$(call application_object,$(1)): $$(firstword $$(wildcard user/coreutils/$(1)/main.cc user/$(1)/main.cc user/mount/$(1)/main.cc $$(if $$(filter terminal,$(1)),user/guiw/main.cc)))
	@mkdir -p $$(@D)
	@echo "[USER CXX] $$<"
	@$$(CXX) $$(CPPFLAGS) $$(USER_CXXFLAGS) $$(DEPENDENCY_FLAGS) -c $$< -o $$@

$$(APP_OUTPUT_$(1)): $(call application_object,$(1)) \
		$(OBJECT_DIR)/user/runtime/start.o $(USER_SHARED_LIBRARIES) script/user.lds \
		Makefile
	@mkdir -p $$(@D)
	@echo "[APP LD] $$@"
	@$$(CXX) $$(USER_CXXFLAGS) $$(USER_LDFLAGS) -o $$@ $$< \
		$(OBJECT_DIR)/user/runtime/start.o -Wl,-E,--no-dynamic-linker \
		-Wl,--no-as-needed $$(LIBCXX_SO) $$(LIBC_SO) $$(LIBOS_CXX_SO) $$(LIBOSGUI_CXX_SO) -lgcc
endef
$(foreach application,$(APPLICATION_NAMES),$(eval $(call APPLICATION_template,$(application))))

disk: mkimg cpfiles

mkimg:
	@echo "[MKIMG] $(DISK_IMAGE)"
	@sudo sh script/mkimg.sh "$(DISK_IMAGE)" "$(BUILD_DIR)"

cpfiles: all
	@missing=0; \
	for input in $(IMAGE_INPUTS); do \
		if [ ! -f "$$input" ]; then \
			echo "missing build input: $$input" >&2; \
			missing=1; \
		fi; \
	done; \
	if [ "$$missing" -ne 0 ]; then \
		exit 1; \
	fi
	@echo "[CPFILES] $(DISK_IMAGE)"
	@sudo sh script/cpfiles.sh "$(DISK_IMAGE)" "$(BUILD_DIR)" "$(PACKAGE_DIR)"

check: $(KERNEL) $(USER_PROGRAMS) $(USER_SHARED_LIBRARIES)
	@python3 script/check-service-imports.py
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

run: cpfiles
	qemu-system-x86_64 -smp $(SMP) -boot c \
		-drive file=$(DISK_IMAGE),if=none,format=raw,id=osdisk \
		-device virtio-blk-pci,drive=osdisk,disable-legacy=on,bootindex=0 \
		-display gtk -serial stdio

debug: cpfiles
	qemu-system-x86_64 -smp $(SMP) -boot c \
		-drive file=$(DISK_IMAGE),if=none,format=raw,id=osdisk \
		-device virtio-blk-pci,drive=osdisk,disable-legacy=on,bootindex=0 \
		-display gtk -serial stdio -S -s

ifeq ($(filter clean,$(MAKECMDGOALS)),)
-include $(DEPENDENCY_FILES)
endif

clean:
	rm -rf $(OBJECT_DIR) $(KERNEL) $(BUILD_DIR)/bin $(BUILD_DIR)/services \
		$(BUILD_DIR)/lib $(BUILD_DIR)/isodir \
		$(BUILD_DIR)/packages $(PACKAGE_DIR) $(DISK_IMAGE)
