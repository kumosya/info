PROJECT := osinfo
SMP ?= 4
CONFIGURE_BUILD ?= build
CONFIGURATION := $(CONFIGURE_BUILD)/config.mk
ifeq ($(filter clean,$(MAKECMDGOALS)),)
ifeq ($(wildcard $(CONFIGURATION)),)
$(error Run ./configure before make)
endif
include $(CONFIGURATION)
else
-include $(CONFIGURATION)
endif
BUILD_DIR ?= build
PACKAGE_DIR ?= packages
OBJECT_DIR ?= obj
INCLUDE_DIR := include
SOURCE_DIR := src
KERNEL_LINKER_SCRIPT := script/kernel.lds
HOST_CPP ?= g++
HOST_TOOL := tools/osinfo-host
INPKG_HOST := tools/inpkg
CONFIG_FILES := $(wildcard script/config/*.json) $(wildcard $(BUILD_DIR)/config/*.conf)

CPPFLAGS := -I$(INCLUDE_DIR) -I$(INCLUDE_DIR)/stdc -I$(INCLUDE_DIR)/stdc++ -I$(INCLUDE_DIR)/posix -I$(INCLUDE_DIR)/os-c++ -I$(BUILD_DIR)/generated
DEPENDENCY_FLAGS := -MMD -MP
CFLAGS := -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -nostdinc -m32 -O2 -Wall -Wextra
CPPFLAGS := -std=c++20 -ffreestanding -fno-builtin -fno-stack-protector -fno-exceptions -fno-rtti \
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

USER_CPPFLAGS := -std=c++20 -ffreestanding -fno-builtin -fno-stack-protector \
	-fno-exceptions -fno-rtti -fno-use-cxa-atexit -fno-threadsafe-statics \
	-fPIC -m64 -mcmodel=small -mno-red-zone -nostdinc -nostdinc++ \
	-Os -Wall -Wextra
BASE_USER_LDFLAGS := -no-pie -nostdlib -nostartfiles -nodefaultlibs \
	-Wl,--build-id=none -Wl,--hash-style=sysv
USER_LDFLAGS := $(BASE_USER_LDFLAGS) -Wl,-T,script/user.lds
BOOT_SERVICE_LDFLAGS := $(BASE_USER_LDFLAGS) -Wl,-T,script/boot-service.lds
USER_RUNTIME_SOURCES := $(wildcard lib/os/*.cc lib/os/*/*.cc)
USER_RUNTIME_OBJECTS := $(patsubst lib/%.cc,$(OBJECT_DIR)/lib/%.o,$(USER_RUNTIME_SOURCES))
-include $(BUILD_DIR)/package-inputs.mk
ifeq ($(filter clean,$(MAKECMDGOALS)),)
include $(BUILD_DIR)/generated/services.mk
include $(BUILD_DIR)/packages.mk
else
-include $(BUILD_DIR)/generated/services.mk $(BUILD_DIR)/packages.mk
endif
SERVICE_PROGRAMS := $(addprefix $(BUILD_DIR)/services/,$(addsuffix .elf,$(SERVICE_NAMES)))
USER_PROGRAMS := $(SERVICE_PROGRAMS) $(APPLICATION_PROGRAMS)
service_directory = $(if $(SERVICE_DIR_$(1)),$(SERVICE_DIR_$(1)),$(1))
service_sources = $(wildcard $(SOURCE_DIR)/$(call service_directory,$(1))/*.cc) $(SERVICE_EXTRA_$(1))
SERVICE_SOURCES := $(foreach service,$(SERVICE_NAMES),$(call service_sources,$(service)))
SERVICE_ASM_SOURCES := $(foreach service,$(SERVICE_NAMES),$(wildcard $(SOURCE_DIR)/$(call service_directory,$(service))/*.S))
SERVICE_CPP_OBJECTS := $(patsubst $(SOURCE_DIR)/%.cc,$(OBJECT_DIR)/%.o,$(SERVICE_SOURCES))
SERVICE_ASM_OBJECTS := $(patsubst $(SOURCE_DIR)/%.S,$(OBJECT_DIR)/%.o,$(SERVICE_ASM_SOURCES))
SERVICE_OBJECTS := $(SERVICE_CPP_OBJECTS) $(SERVICE_ASM_OBJECTS)
AR ?= ar
DISK_IMAGE := $(BUILD_DIR)/$(PROJECT).img
LIBC := $(BUILD_DIR)/lib/libc.a
LIBCPP := $(BUILD_DIR)/lib/libc++.a
LIBCPPABI := $(BUILD_DIR)/lib/libc++abi.a
LIBOS_CPP_SO := $(PACKAGE_DIR)/libos-c++/libos-c++.so
GRAPHIC_A := $(BUILD_DIR)/lib/graphic.a
GRAPHIC_OBJECTS := $(OBJECT_DIR)/lib/graphic/font.o $(OBJECT_DIR)/lib/graphic/font/console8x16.o $(OBJECT_DIR)/lib/graphic/surface.o
LIBC_SO := $(PACKAGE_DIR)/libc++/libc.so
LIBCPP_SO := $(PACKAGE_DIR)/libc++/libc++.so
USER_SHARED_LIBRARIES := $(LIBOS_CPP_SO) $(LIBC_SO) $(LIBCPP_SO)
INTERPRETER := $(BUILD_DIR)/lib/ld-osinfo-x86-64.so
IMAGE_INPUTS := $(KERNEL) $(APPLICATION_PROGRAMS) $(SERVICE_PROGRAMS) \
	$(USER_SHARED_LIBRARIES) $(INTERPRETER) script/lib/version.1 script/etc/hostname
USER_SHARED_LDFLAGS := -shared -nostdlib -nostartfiles -nodefaultlibs \
	-Wl,--build-id=none -Wl,--hash-style=sysv
LIBC_SOURCES := internal.c fd.c filesystem.c directory.c glob.c credentials.c memory_runtime.c fenv_runtime.c resource_runtime.c basic_posix.c random_runtime.c signal_names.c message_runtime.c \
	string_runtime.c ctype_runtime.c stdlib_runtime.c stdio_runtime.c \
	time_runtime.c task_runtime.c signal_runtime.c atfork.c spawn.c mman.c aio.c mqueue.c environment.c errors.c pthread.c threads.c \
	semaphore.c errno.c locale_runtime.c unicode_runtime.c stdio_extra.c posix_runtime.c accounts.c extensions.c
LIBC_OBJECTS := $(addprefix $(OBJECT_DIR)/lib/libc/,$(LIBC_SOURCES:.c=.o)) $(OBJECT_DIR)/lib/libc/os_adapter.o
LIBC_OBJECTS += $(OBJECT_DIR)/lib/libc/dlfcn_runtime.o
PORTABLE_LIBC_SOURCES := $(wildcard lib/libc/portable/*/*.c)
PORTABLE_LIBC_ASM := $(wildcard lib/libc/portable/*/*.S)
PORTABLE_LIBC_OBJECTS := $(patsubst lib/%.c,$(OBJECT_DIR)/lib/%.o,$(PORTABLE_LIBC_SOURCES)) $(patsubst lib/%.S,$(OBJECT_DIR)/lib/%.o,$(PORTABLE_LIBC_ASM))
LIBC_OBJECTS += $(PORTABLE_LIBC_OBJECTS)
LIBC_OBJECTS += $(patsubst lib/%.c,$(OBJECT_DIR)/lib/%.o,$(wildcard lib/libc/fallback/*.c))
LIBCPP_OBJECTS := $(OBJECT_DIR)/lib/libc++/new.o \
	$(OBJECT_DIR)/lib/libc++/iostream_globals.o \
	$(OBJECT_DIR)/lib/libc++/ostream.o $(OBJECT_DIR)/lib/libc++/istream.o \
	$(OBJECT_DIR)/lib/libc++/thread.o
LIBCPPABI_OBJECTS := $(OBJECT_DIR)/lib/libc++/abi.o
GENERATED_OBJECTS := $(KERNEL_OBJECTS) $(SERVICE_OBJECTS) $(APPLICATION_MAIN_OBJECTS) \
	$(USER_RUNTIME_OBJECTS) $(GRAPHIC_OBJECTS) $(OBJECT_DIR)/lib/os/start.o \
	$(OBJECT_DIR)/lib/os/service_start.o $(LIBC_OBJECTS) \
	$(LIBCPP_OBJECTS) $(LIBCPPABI_OBJECTS)
DEPENDENCY_FILES := $(sort $(GENERATED_OBJECTS:.o=.d))

.SECONDARY: $(SERVICE_OBJECTS) $(APPLICATION_MAIN_OBJECTS)

.PHONY: all build kernel services applications libraries package-archives disk mkimg check \
	run debug clean

.DEFAULT_GOAL := all
all: build package-archives

.PHONY: sdk sdk-base
SDK_OUTPUT ?= $(BUILD_DIR)/$(if $(filter sdk-base,$(MAKECMDGOALS)),sdk-base,sdk)
sdk sdk-base: libraries $(OBJECT_DIR)/lib/os/start.o $(OBJECT_DIR)/lib/os/service_start.o
	@$(HOST_TOOL) sdk $(if $(filter sdk-base,$@),--argument base) --build "$(BUILD_DIR)" --packages "$(PACKAGE_DIR)" --objects "$(OBJECT_DIR)" --output "$(SDK_OUTPUT)"

.PHONY: host-tools
host-tools: $(HOST_TOOL) $(INPKG_HOST) tools/infoide
$(CONFIGURATION): configure Makefile $(CONFIG_FILES) $(wildcard lib/*.conf tools/host/*.cc tools/host/*.h) tools/host/Makefile $(PACKAGE_INPUTS) $(HOST_TOOL) $(INPKG_HOST) tools/infoide
	@echo "Configuration changed; run ./configure again" >&2
	@false
$(BUILD_DIR)/generated/services.mk $(BUILD_DIR)/generated/stamp $(BUILD_DIR)/packages.mk $(BUILD_DIR)/package-inputs.mk $(HOST_TOOL) $(INPKG_HOST) tools/infoide:
	@echo "Missing configured input: $@; run ./configure" >&2
	@false
$(KERNEL_CPP_OBJECTS) $(SERVICE_CPP_OBJECTS) $(OBJECT_DIR)/tests/lifecycle-main.o: $(BUILD_DIR)/generated/stamp

package-archives: applications libraries
	@$(INPKG_HOST) catalog --config "$(BUILD_DIR)/packages.conf" --package-dir "$(PACKAGE_DIR)"

build: kernel services applications libraries

kernel: $(KERNEL)

services: $(SERVICE_PROGRAMS)
applications: $(APPLICATION_PROGRAMS)

libraries: $(USER_SHARED_LIBRARIES) $(PACKAGE_DIR)/libc++/COPYRIGHT $(INTERPRETER)

$(OBJECT_DIR)/lib/ld-x86-64/main.o: lib/ld-x86-64/main.cc
	@mkdir -p $(@D)
	@$(CPP) $(CPPFLAGS) $(USER_CPPFLAGS) -mcmodel=large $(DEPENDENCY_FLAGS) -c $< -o $@

$(OBJECT_DIR)/lib/ld-x86-64/start.o: lib/ld-x86-64/start.S
	@mkdir -p $(@D)
	@$(CC) -m64 -c $< -o $@

$(INTERPRETER): $(OBJECT_DIR)/lib/ld-x86-64/main.o $(OBJECT_DIR)/lib/ld-x86-64/start.o $(USER_RUNTIME_OBJECTS) script/interpreter.lds
	@mkdir -p $(@D)
	@$(CPP) -no-pie -nostdlib -nostartfiles -nodefaultlibs -Wl,--build-id=none,--no-relax -Wl,-T,script/interpreter.lds \
		-o $@ $(OBJECT_DIR)/lib/ld-x86-64/main.o $(OBJECT_DIR)/lib/ld-x86-64/start.o $(USER_RUNTIME_OBJECTS) -lgcc

$(PACKAGE_DIR)/libc++/COPYRIGHT: lib/libc/portable/COPYRIGHT
	@mkdir -p $(@D)
	@cp $< $@

$(KERNEL): $(KERNEL_OBJECTS) $(KERNEL_LINKER_SCRIPT) Makefile
	@mkdir -p $(@D)
	@echo "[KERNEL LD] $@"
	@$(CPP) $(CPPFLAGS) $(KERNEL_LDFLAGS) -o $@ $(KERNEL_OBJECTS) -lgcc

$(OBJECT_DIR)/kernel/%.o: $(SOURCE_DIR)/kernel/%.cc
	@mkdir -p $(@D)
	@echo "[KERNEL CPP] $<"
	@$(CPP) $(CPPFLAGS) $(CPPFLAGS) $(DEPENDENCY_FLAGS) -c $< -o $@

$(OBJECT_DIR)/kernel/%.o: $(SOURCE_DIR)/kernel/%.S
	@mkdir -p $(@D)
	@echo "[KERNEL AS] $<"
	@$(CC) $(CPPFLAGS) $(ASFLAGS) $(DEPENDENCY_FLAGS) -c $< -o $@

$(OBJECT_DIR)/lib/os/%.o: lib/os/%.cc
	@mkdir -p $(@D)
	@echo "[USER CPP] $<"
	@$(CPP) $(CPPFLAGS) $(USER_CPPFLAGS) $(DEPENDENCY_FLAGS) -c $< -o $@

$(OBJECT_DIR)/lib/os/start.o: lib/os/syscall.S
	@mkdir -p $(@D)
	@$(CC) $(CPPFLAGS) -m64 -ffreestanding -fno-pic $(DEPENDENCY_FLAGS) -c $< -o $@

$(OBJECT_DIR)/lib/os/%.o: lib/os/%.S
	@mkdir -p $(@D)
	@echo "[USER AS] $<"
	@$(CC) $(CPPFLAGS) -m64 -ffreestanding -fno-pic $(DEPENDENCY_FLAGS) -c $< -o $@

$(OBJECT_DIR)/lib/os/service_start.o: src/service/start.S
	@mkdir -p $(@D)
	@$(CC) $(CPPFLAGS) -m64 -ffreestanding -fno-pic $(DEPENDENCY_FLAGS) -c $< -o $@

$(SERVICE_CPP_OBJECTS): $(OBJECT_DIR)/%.o: $(SOURCE_DIR)/%.cc
	@mkdir -p $(@D)
	@echo "[SERVICE CPP] $<"
	@$(CPP) $(CPPFLAGS) $(USER_CPPFLAGS) $(DEPENDENCY_FLAGS) -c $< -o $@

$(SERVICE_ASM_OBJECTS): $(OBJECT_DIR)/%.o: $(SOURCE_DIR)/%.S
	@mkdir -p $(@D)
	@echo "[SERVICE AS] $<"
	@$(CC) $(CPPFLAGS) -m64 -ffreestanding -fno-pic $(DEPENDENCY_FLAGS) -c $< -o $@

$(OBJECT_DIR)/lib/%.o: lib/%.cc
	@mkdir -p $(@D)
	@echo "[LIB CPP] $<"
	@$(CPP) $(CPPFLAGS) $(USER_CPPFLAGS) $(DEPENDENCY_FLAGS) -c $< -o $@

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

$(LIBCPP): $(LIBCPP_OBJECTS)
	@mkdir -p $(@D)
	@echo "[AR] $@"
	@$(RM) $@
	@$(AR) rcs $@ $^

$(LIBCPPABI): $(LIBCPPABI_OBJECTS)
	@mkdir -p $(@D)
	@echo "[AR] $@"
	@$(RM) $@
	@$(AR) rcs $@ $^

$(LIBOS_CPP_SO): $(USER_RUNTIME_OBJECTS) Makefile
	@mkdir -p $(@D)
	@echo "[SO] $@"
	@$(CPP) $(USER_CPPFLAGS) $(USER_SHARED_LDFLAGS) \
		-Wl,-soname,libos-c++.so -o $@ $(USER_RUNTIME_OBJECTS) -lgcc

$(GRAPHIC_A): $(GRAPHIC_OBJECTS)
	@mkdir -p "$(@D)"
	@$(AR) rcs "$@" $^

$(LIBC_SO): $(LIBC_OBJECTS) $(LIBOS_CPP_SO) Makefile
	@mkdir -p $(@D)
	@echo "[SO] $@"
	@$(CPP) $(USER_CPPFLAGS) $(USER_SHARED_LDFLAGS) \
		-Wl,-soname,libc.so -o $@ $(LIBC_OBJECTS) \
		-Wl,--no-as-needed $(LIBOS_CPP_SO) -lgcc

$(LIBCPP_SO): $(LIBCPP_OBJECTS) $(LIBCPPABI_OBJECTS) $(LIBC_SO) $(LIBOS_CPP_SO) Makefile
	@mkdir -p $(@D)
	@echo "[SO] $@"
	@$(CPP) $(USER_CPPFLAGS) $(USER_SHARED_LDFLAGS) \
		-Wl,-soname,libc++.so -o $@ $(LIBCPP_OBJECTS) \
		$(LIBCPPABI_OBJECTS) -Wl,--no-as-needed $(LIBC_SO) $(LIBOS_CPP_SO) -lgcc

define SERVICE_template
$(BUILD_DIR)/services/$(1).elf: $$(call service_objects,$(1)) \
		$(OBJECT_DIR)/lib/os/service_start.o $$(LIBOS_CPP_SO) \
		$$(SERVICE_LIBS_$(1)) \
		script/boot-service.lds Makefile
	@mkdir -p $$(@D)
	@echo "[SERVICE LD] $$@"
	@$$(CPP) $$(USER_CPPFLAGS) $$(BOOT_SERVICE_LDFLAGS) -o $$@ \
		$$(call service_objects,$(1)) \
		$(OBJECT_DIR)/lib/os/service_start.o \
		-Wl,-E,--no-dynamic-linker -Wl,--no-as-needed $$(LIBOS_CPP_SO) \
		$$(SERVICE_LIBS_$(1)) -lgcc
endef

service_objects = $(patsubst $(SOURCE_DIR)/%.cc,$(OBJECT_DIR)/%.o,$(call service_sources,$(1))) $(patsubst $(SOURCE_DIR)/%.S,$(OBJECT_DIR)/%.o,$(wildcard $(SOURCE_DIR)/$(call service_directory,$(1))/*.S))
$(foreach service,$(SERVICE_NAMES),$(eval $(call SERVICE_template,$(service))))

disk: mkimg
	+$(MAKE) install DESTDIR= INSTALL_GRUB=0

mkimg: host-tools $(BUILD_DIR)/generated/services.mk
	@test "$(IMAGE_TOOLS)" = 1 || { echo "Run ./configure with image tools enabled" >&2; exit 1; }
	@echo "[MKIMG] $(DISK_IMAGE)"
	@sudo sh script/mkimg.sh "$(DISK_IMAGE)" "$(BUILD_DIR)"

check: $(KERNEL) $(USER_PROGRAMS) $(USER_SHARED_LIBRARIES) $(INTERPRETER)
	@$(HOST_TOOL) audit --build "$(BUILD_DIR)" --objects "$(OBJECT_DIR)"
	@grub-file --is-x86-multiboot2 $(KERNEL)
	@readelf -h $(KERNEL) | grep -q 'EXEC (Executable file)'
	@test -z "$$(readelf -d $(KERNEL) 2>/dev/null | grep NEEDED)"
	@readelf -d $(LIBOS_CPP_SO) | grep -q 'Library soname: \[libos-c++.so\]'
	@readelf -d $(LIBC_SO) | grep -q 'Library soname: \[libc.so\]'
	@readelf -d $(LIBCPP_SO) | grep -q 'Library soname: \[libc++.so\]'
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

DESTDIR ?=
INSTALL_GRUB ?= 0
GRUB_DISK ?=
EXPORT_ACCOUNT_PROFILE ?= development
.PHONY: install image iso export-images
install: all
	@missing=0; for input in $(IMAGE_INPUTS); do \
		if [ ! -f "$$input" ]; then echo "missing build input: $$input" >&2; missing=1; fi; \
		done; test "$$missing" = 0
	@set -eu; if [ -n "$(DESTDIR)" ]; then \
		set -- sh script/install.sh "$(DESTDIR)" "$(BUILD_DIR)" "$(PACKAGE_DIR)" "$(INSTALL_GRUB)" "$(GRUB_DISK)"; \
	else \
		test "$(IMAGE_TOOLS)" = 1 || { echo 'Run ./configure with image tools enabled' >&2; exit 1; }; \
		set -- sh script/install.sh --image "$(DISK_IMAGE)" "$(BUILD_DIR)" "$(PACKAGE_DIR)" "$(INSTALL_GRUB)" "$(GRUB_DISK)"; \
		if [ "$$(id -u)" != 0 ]; then set -- sudo "$$@"; fi; \
	fi; "$$@"

image: all
	@test "$(IMAGE_TOOLS)" = 1 || { echo 'Run ./configure with image tools enabled' >&2; exit 1; }
	@sh script/mkbootimg.sh "$(BUILD_DIR)" "$(PACKAGE_DIR)" "$(BUILD_DIR)/export/osinfo.img" "$(EXPORT_ACCOUNT_PROFILE)"

iso: all
	@test "$(IMAGE_TOOLS)" = 1 || { echo 'Run ./configure with image tools enabled' >&2; exit 1; }
	@sh script/mkiso.sh "$(BUILD_DIR)" "$(PACKAGE_DIR)" "$(BUILD_DIR)/export/osinfo.iso" "$(EXPORT_ACCOUNT_PROFILE)"

export-images: image iso

run: install
	qemu-system-x86_64 -m 512 -smp $(SMP) -boot c \
		-drive file=$(DISK_IMAGE),if=none,format=raw,id=osdisk \
		-device virtio-blk-pci,drive=osdisk,disable-legacy=on,bootindex=0 \
		-display gtk -serial stdio

debug: install
	qemu-system-x86_64 -m 512 -smp $(SMP) -boot c \
		-drive file=$(DISK_IMAGE),if=none,format=raw,id=osdisk \
		-device virtio-blk-pci,drive=osdisk,disable-legacy=on,bootindex=0 \
		-display gtk -serial stdio -S -s

ifeq ($(filter clean,$(MAKECMDGOALS)),)
-include $(sort $(DEPENDENCY_FILES) $(wildcard $(OBJECT_DIR)/tests/*.d $(OBJECT_DIR)/tests/linker/*.d))
endif

clean:
	rm -rf $(OBJECT_DIR) $(KERNEL) $(BUILD_DIR)/bin $(BUILD_DIR)/services \
		$(BUILD_DIR)/lib $(BUILD_DIR)/isodir \
		$(BUILD_DIR)/packages $(DISK_IMAGE)
