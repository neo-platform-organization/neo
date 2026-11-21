# One build file for all targets. Component repositories are independent siblings.
DEPTH_DIR ?= ../depth
IMAGE_DIR ?= ../neo-image
ifeq ($(wildcard $(DEPTH_DIR)/provider.c),)
$(error Depth plugins missing at $(DEPTH_DIR). See README.md for the sibling checkout)
endif

.DEFAULT_GOAL := all
.DELETE_ON_ERROR:

ifeq ($(OS),Windows_NT)
SHELL := cmd.exe
.SHELLFLAGS := /d /c
HOST_TARGET := x86_64/Windows
else
HOST_TARGET := x86_64/Linux
endif

TARGET ?= $(HOST_TARGET)
ifneq ($(words $(TARGET)),1)
$(error Select exactly one TARGET)
endif
ifeq ($(TARGET),x86_64)
override TARGET := x86_64/x86_64
endif
TARGETS := x86_64/Windows x86_64/Linux x86_64/x86_64 arm/Windows arm/Linux arm/arm arm64/Windows arm64/Linux arm64/macOS arm64/arm64
ifneq ($(filter $(TARGET),$(TARGETS)),$(TARGET))
$(error Unknown TARGET '$(TARGET)'. Use make targets)
endif
ifeq ($(origin CC),default)
CC := gcc
endif
NODE ?= node
WSL_DISTRO ?= Debian
OPT ?= -O3
WARNINGS := -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow -Wstrict-prototypes
CFLAGS ?= -std=c17 $(OPT) $(WARNINGS)
CPPFLAGS += -Ineo-vm -I$(DEPTH_DIR) -I$(DEPTH_DIR)/common -I$(DEPTH_DIR)/language
OUT := build/$(TARGET)
EXE := $(OUT)/neo$(if $(filter %/Windows,$(TARGET)),.exe,$(if $(filter x86_64/x86_64 arm/arm arm64/arm64,$(TARGET)),.bin,))
IMAGE ?= $(IMAGE_DIR)/neo-os/image.neo
COMMON := neo-vm/main.c neo-vm/platform.c $(DEPTH_DIR)/provider.c neo-vm/language.c $(DEPTH_DIR)/language/jit.c $(DEPTH_DIR)/language/provider.c $(DEPTH_DIR)/common/hosted.c $(DEPTH_DIR)/language/weft.c $(DEPTH_DIR)/common/lattice.c $(DEPTH_DIR)/common/events.c $(DEPTH_DIR)/common/services.c $(DEPTH_DIR)/common/services_test.c $(DEPTH_DIR)/common/presentation.c $(DEPTH_DIR)/common/console.c $(DEPTH_DIR)/common/exercise.c $(DEPTH_DIR)/common/launch.c $(DEPTH_DIR)/common/ports.c $(DEPTH_DIR)/common/ports_test.c
HEADERS := $(wildcard neo-vm/*.h $(DEPTH_DIR)/common/*.h $(DEPTH_DIR)/*.h $(DEPTH_DIR)/language/*.h)

.PHONY: all test run path targets clean x86_64 $(TARGETS)
all: $(EXE)
path:
	@echo $(EXE)
targets:
	@echo $(TARGETS)
x86_64:
	$(MAKE) TARGET=x86_64/x86_64 all
$(TARGETS):
	$(MAKE) TARGET=$@ all

ifeq ($(TARGET),x86_64/Windows)
$(EXE): $(COMMON) $(DEPTH_DIR)/plugins/windows.c $(DEPTH_DIR)/plugins/services_windows.c $(HEADERS) Makefile
ifeq ($(OS),Windows_NT)
	@if not exist "$(OUT)" mkdir "$(OUT)"
else
	mkdir -p "$(OUT)"
endif
	$(CC) $(CPPFLAGS) $(CFLAGS) $(COMMON) $(DEPTH_DIR)/plugins/windows.c $(DEPTH_DIR)/plugins/services_windows.c $(LDFLAGS) -luserenv -lws2_32 -lbcrypt -lgdi32 -lopengl32 -lwinmm $(LDLIBS) -o "$@"
ifeq ($(OS),Windows_NT)
	icacls "$@" /grant "*S-1-15-2-1:(RX)"
endif
test: all
	$(NODE) $(DEPTH_DIR)/tools/regression.mjs "$(EXE)" "$(IMAGE)"
run: all
	"$(EXE)" run "$(IMAGE)"
else ifeq ($(TARGET),x86_64/Linux)
ifeq ($(OS),Windows_NT)
.PHONY: $(EXE)
$(EXE):
	wsl.exe -d $(WSL_DISTRO) --cd "$(subst /,\,$(CURDIR))" --exec make TARGET=$(TARGET) all
test run:
	wsl.exe -d $(WSL_DISTRO) --cd "$(subst /,\,$(CURDIR))" --exec make TARGET=$(TARGET) $@
else
$(EXE): $(COMMON) $(DEPTH_DIR)/plugins/linux.c $(DEPTH_DIR)/plugins/services_linux.c $(HEADERS) Makefile
	mkdir -p "$(OUT)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(COMMON) $(DEPTH_DIR)/plugins/linux.c $(DEPTH_DIR)/plugins/services_linux.c $(LDFLAGS) -lseccomp -lX11 -lGL -lasound -pthread -lm $(LDLIBS) -o "$@"
test: all
	$(NODE) $(DEPTH_DIR)/tools/regression.mjs "$(EXE)" "$(IMAGE)"
run: all
	"$(EXE)" run "$(IMAGE)"
endif
else ifeq ($(TARGET),x86_64/x86_64)
BOOT_MODE ?= surface
SURFACE_OBJECT ?= surface-exercise
ifeq ($(OS),Windows_NT)
.PHONY: $(EXE)
$(EXE):
	wsl.exe -d $(WSL_DISTRO) --cd "$(subst /,\,$(CURDIR))" --exec make TARGET=$(TARGET) BOOT_MODE=$(BOOT_MODE) SURFACE_OBJECT=$(SURFACE_OBJECT) all
test run:
	wsl.exe -d $(WSL_DISTRO) --cd "$(subst /,\,$(CURDIR))" --exec make TARGET=$(TARGET) BOOT_MODE=$(BOOT_MODE) SURFACE_OBJECT=$(SURFACE_OBJECT) $@
else
EFI_FLAGS := -std=c17 -O2 -Wall -Wextra -Werror -Wno-unused-function -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns -fno-stack-protector -fpic -fshort-wchar -mno-red-zone -ffunction-sections -fdata-sections -DEFI_FUNCTION_WRAPPER -DNEO_FREESTANDING -DNEO_INTEGER_PROFILE -I/usr/include/efi -I/usr/include/efi/x86_64 -I$(OUT) -Ineo-vm -I$(DEPTH_DIR) -I$(DEPTH_DIR)/common -I$(DEPTH_DIR)/language
EFI_SOURCES := neo-vm/main.c neo-vm/platform.c $(DEPTH_DIR)/provider.c neo-vm/language.c $(DEPTH_DIR)/language/jit.c $(DEPTH_DIR)/language/provider.c $(DEPTH_DIR)/common/hosted.c $(DEPTH_DIR)/language/weft.c $(DEPTH_DIR)/common/lattice.c $(DEPTH_DIR)/common/events.c $(DEPTH_DIR)/common/services.c $(DEPTH_DIR)/common/presentation.c $(DEPTH_DIR)/common/console.c $(DEPTH_DIR)/common/ports.c $(DEPTH_DIR)/plugins/x86_64/libc.c $(DEPTH_DIR)/plugins/x86_64/boot.c $(DEPTH_DIR)/plugins/x86_64/interrupts.c
EFI_CORE := $(filter neo-vm/%,$(EFI_SOURCES))
EFI_DEPTH := $(filter $(DEPTH_DIR)/%,$(EFI_SOURCES))
EFI_OBJECTS := $(patsubst %.c,$(OUT)/obj/%.o,$(EFI_CORE)) $(patsubst $(DEPTH_DIR)/%.c,$(OUT)/obj/depth/%.o,$(EFI_DEPTH)) $(OUT)/entry.o
.PHONY: bundle
all: $(OUT)/neo.iso
bundle:
	mkdir -p "$(OUT)"
	$(NODE) $(DEPTH_DIR)/tools/embed-image.mjs "$(IMAGE)" "$(OUT)/image.inc" "$(SURFACE_OBJECT)" "$(BOOT_MODE)"
$(OUT)/obj/depth/plugins/x86_64/boot.o: bundle
$(OUT)/obj/depth/%.o: $(DEPTH_DIR)/%.c $(HEADERS) $(DEPTH_DIR)/plugins/x86_64/x86_64.h Makefile
	mkdir -p "$(@D)"
	$(CC) $(EFI_FLAGS) $(if $(findstring interrupts.c,$<),-mgeneral-regs-only,) -c "$<" -o "$@"
$(OUT)/obj/%.o: %.c $(HEADERS) $(DEPTH_DIR)/plugins/x86_64/x86_64.h Makefile
	mkdir -p "$(@D)"
	$(CC) $(EFI_FLAGS) $(if $(findstring interrupts.c,$<),-mgeneral-regs-only,) -c "$<" -o "$@"
$(OUT)/entry.o: $(DEPTH_DIR)/plugins/x86_64/entry.S
	mkdir -p "$(@D)"
	$(CC) -fpic -mno-red-zone -c "$<" -o "$@"
$(OUT)/neo.elf: $(EFI_OBJECTS) Makefile
	ld -nostdlib --gc-sections -z defs -znocombreloc -T /usr/lib/elf_x86_64_efi.lds -shared -Bsymbolic /usr/lib/crt0-efi-x86_64.o $(EFI_OBJECTS) -L/usr/lib -lefi -lgnuefi -o "$@"
$(EXE): $(OUT)/neo.elf
	objcopy -j .text -j .sdata -j .data -j .dynamic -j .dynsym -j .rel -j .rela -j .reloc -j .rodata --target=efi-app-x86_64 "$<" "$@"
$(OUT)/neo.iso: $(EXE)
	mkdir -p "$(OUT)/iso-root"
	cp /usr/share/doc/gnu-efi/copyright "$(OUT)/GNU-EFI-LICENSE.txt"
	mformat -i "$(OUT)/iso-root/boot.img" -C -f 2880 ::
	mmd -i "$(OUT)/iso-root/boot.img" ::/EFI ::/EFI/BOOT
	mcopy -i "$(OUT)/iso-root/boot.img" "$(EXE)" ::/EFI/BOOT/BOOTX64.EFI
	xorriso -as mkisofs -quiet -R -J -V NEO -e boot.img -no-emul-boot -o "$@" "$(OUT)/iso-root"
test: all
	$(NODE) $(DEPTH_DIR)/tools/test-boot.mjs "$(OUT)" "$(BOOT_MODE)"
run: all
	qemu-system-x86_64 -machine q35 -m 512 -bios /usr/share/OVMF/OVMF_CODE_4M.fd -cdrom "$(OUT)/neo.iso" -serial stdio
endif
else
# A registered target is not a working adapter. Never emit a dummy VM.
.PHONY: $(EXE)
$(EXE):
	@echo Target $(TARGET): platform adapter is not implemented. No binary produced.
	@exit 1
test run: all
endif

# Remove only this target's executable. Keep images and unrelated artifacts.
clean:
ifeq ($(OS),Windows_NT)
	@if exist "$(subst /,\,$(EXE))" del /q "$(subst /,\,$(EXE))"
else
	rm -f "$(EXE)"
endif
