# One build file for all targets. Component repositories are independent siblings.
DEPTH_DIR ?= ../depth
IMAGE_DIR ?= ../neo-image

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
CPPFLAGS += -Ineo-vm
OUT := build/$(TARGET)
EXE := $(OUT)/neo$(if $(filter %/Windows,$(TARGET)),.exe,$(if $(filter x86_64/x86_64 arm/arm arm64/arm64,$(TARGET)),.bin,))
IMAGE ?= $(IMAGE_DIR)/neo-os/image.neo
CONFIG ?= .neo-config.json
CATALOG ?= input/plugins/catalog.json
NEEDS_CONFIG := $(if $(wildcard $(CONFIG)),,$(if $(filter configure plugins targets path clean,$(MAKECMDGOALS)),,yes))
ifneq ($(filter configure plugins targets path clean,$(MAKECMDGOALS))$(NEEDS_CONFIG),)
PLUGIN_SOURCES :=
PLUGIN_LIBS :=
PLUGIN_ROLES :=
else
PLUGIN_PLAN := $(shell $(NODE) tools/plugins.mjs emit "$(TARGET)" "$(CONFIG)" "$(CATALOG)" "$(OUT)")
ifneq ($(PLUGIN_PLAN),ok)
$(error VM plugin selection failed; run make configure or inspect CONFIG and CATALOG)
endif
include $(OUT)/plugins.mk
CPPFLAGS += $(addprefix -I,$(PLUGIN_INCLUDE_DIRS))
BUILT_PROFILE := $(shell $(NODE) -e "try{process.stdout.write(require('fs').readFileSync(process.argv[1],'utf8'))}catch{}" "$(OUT)/built-profile")
ifneq ($(BUILT_PROFILE),$(PLUGIN_KEY))
.PHONY: force-profile
$(EXE) $(OUT)/neo.elf $(OUT)/obj/selection.o: force-profile
endif
endif
COMMON := neo-vm/main.c neo-vm/platform.c neo-vm/language.c $(OUT)/selection.c $(PLUGIN_SOURCES)
HEADERS := $(wildcard neo-vm/*.h) $(foreach dir,$(PLUGIN_INCLUDE_DIRS),$(wildcard $(dir)/*.h))

.PHONY: all test run path targets clean x86_64 $(TARGETS)
ifeq ($(NEEDS_CONFIG),yes)
.PHONY: all test run
all test run: configure
ifeq ($(if $(filter --%,$(firstword $(MAKEFLAGS))),,$(findstring n,$(firstword $(MAKEFLAGS)))),)
	$(MAKE) $@ TARGET=$(TARGET) CONFIG=$(CONFIG) CATALOG=$(CATALOG)
endif
else
all: $(EXE)
endif
path:
	@echo $(EXE)
targets:
	@echo $(TARGETS)
x86_64:
	$(MAKE) TARGET=x86_64/x86_64 all
$(TARGETS):
	$(MAKE) TARGET=$@ all

ifneq ($(NEEDS_CONFIG),yes)
ifeq ($(TARGET),x86_64/Windows)
$(EXE): $(COMMON) $(HEADERS) Makefile
ifeq ($(OS),Windows_NT)
	@if not exist "$(OUT)" mkdir "$(OUT)"
else
	mkdir -p "$(OUT)"
endif
	$(CC) $(CPPFLAGS) $(CFLAGS) $(COMMON) $(LDFLAGS) $(PLUGIN_LIBS) $(LDLIBS) -o "$@"
	$(NODE) -e "require('fs').writeFileSync(process.argv[1],process.argv[2])" "$(OUT)/built-profile" "$(PLUGIN_KEY)"
ifeq ($(OS),Windows_NT)
	icacls "$@" /grant "*S-1-15-2-1:(RX)"
endif
test: all
	$(NODE) tools/test-plugins.mjs "$(MAKE)"
	$(NODE) tools/test-build.mjs "$(EXE)" "$(IMAGE)" "$(DEPTH_DIR)" $(PLUGIN_ROLES)
run: all
	"$(EXE)" run "$(IMAGE)"
else ifeq ($(TARGET),x86_64/Linux)
ifeq ($(OS),Windows_NT)
.PHONY: $(EXE)
$(EXE):
	wsl.exe -d $(WSL_DISTRO) --cd "$(subst /,\,$(CURDIR))" --exec make TARGET=$(TARGET) CONFIG=$(CONFIG) CATALOG=$(CATALOG) all
test run:
	wsl.exe -d $(WSL_DISTRO) --cd "$(subst /,\,$(CURDIR))" --exec make TARGET=$(TARGET) CONFIG=$(CONFIG) CATALOG=$(CATALOG) $@
else
$(EXE): $(COMMON) $(HEADERS) Makefile
	mkdir -p "$(OUT)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(COMMON) $(LDFLAGS) $(PLUGIN_LIBS) $(LDLIBS) -o "$@"
	$(NODE) -e "require('fs').writeFileSync(process.argv[1],process.argv[2])" "$(OUT)/built-profile" "$(PLUGIN_KEY)"
test: all
	$(NODE) tools/test-plugins.mjs "$(MAKE)"
	$(NODE) tools/test-build.mjs "$(EXE)" "$(IMAGE)" "$(DEPTH_DIR)" $(PLUGIN_ROLES)
run: all
	"$(EXE)" run "$(IMAGE)"
endif
else ifeq ($(TARGET),x86_64/x86_64)
BOOT_MODE ?= surface
SURFACE_OBJECT ?= surface-exercise
ifeq ($(OS),Windows_NT)
.PHONY: $(EXE)
$(EXE):
	wsl.exe -d $(WSL_DISTRO) --cd "$(subst /,\,$(CURDIR))" --exec make TARGET=$(TARGET) BOOT_MODE=$(BOOT_MODE) SURFACE_OBJECT=$(SURFACE_OBJECT) CONFIG=$(CONFIG) CATALOG=$(CATALOG) all
test run:
	wsl.exe -d $(WSL_DISTRO) --cd "$(subst /,\,$(CURDIR))" --exec make TARGET=$(TARGET) BOOT_MODE=$(BOOT_MODE) SURFACE_OBJECT=$(SURFACE_OBJECT) CONFIG=$(CONFIG) CATALOG=$(CATALOG) $@
else
EFI_FLAGS := -std=c17 -O2 -Wall -Wextra -Werror -Wno-unused-function -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns -fno-stack-protector -fpic -fshort-wchar -mno-red-zone -ffunction-sections -fdata-sections -DEFI_FUNCTION_WRAPPER -DNEO_FREESTANDING -DNEO_INTEGER_PROFILE -I/usr/include/efi -I/usr/include/efi/x86_64 -I$(OUT) -Ineo-vm $(addprefix -I,$(PLUGIN_INCLUDE_DIRS))
EFI_SOURCES := $(filter-out $(OUT)/selection.c,$(COMMON))
EFI_OBJECTS := $(foreach source,$(EFI_SOURCES),$(OUT)/obj/$(subst ../,__parent__/,$(source:.c=.o))) $(OUT)/obj/selection.o $(OUT)/entry.o
.PHONY: bundle
all: $(OUT)/neo.iso
bundle:
	mkdir -p "$(OUT)"
	$(NODE) $(DEPTH_DIR)/tools/embed-image.mjs "$(if $(BUNDLE_IMAGE),$(BUNDLE_IMAGE),-)" "$(OUT)/image.inc" "$(SURFACE_OBJECT)" "$(BOOT_MODE)"
$(foreach source,$(PLUGIN_EMBED_SOURCES),$(OUT)/obj/$(subst ../,__parent__/,$(source:.c=.o))): bundle
define NEO_EFI_COMPILE
$(OUT)/obj/$(subst ../,__parent__/,$(1:.c=.o)): $(1) $(HEADERS) Makefile
	mkdir -p "$$(@D)"
	$$(CC) $$(EFI_FLAGS) $(if $(findstring interrupts.c,$(1)),-mgeneral-regs-only,) -c "$$<" -o "$$@"
endef
$(foreach source,$(EFI_SOURCES),$(eval $(call NEO_EFI_COMPILE,$(source))))
$(OUT)/obj/selection.o: $(OUT)/selection.c $(HEADERS) Makefile
	mkdir -p "$(@D)"
	$(CC) $(EFI_FLAGS) -c "$<" -o "$@"
$(OUT)/entry.o: $(PLUGIN_ASSEMBLY)
	mkdir -p "$(@D)"
	$(CC) -fpic -mno-red-zone -c "$<" -o "$@"
$(OUT)/neo.elf: $(EFI_OBJECTS) Makefile
	ld -nostdlib --gc-sections -z defs -znocombreloc -T /usr/lib/elf_x86_64_efi.lds -shared -Bsymbolic /usr/lib/crt0-efi-x86_64.o $(EFI_OBJECTS) -L/usr/lib -lefi -lgnuefi -o "$@"
$(EXE): $(OUT)/neo.elf
	objcopy -j .text -j .sdata -j .data -j .dynamic -j .dynsym -j .rel -j .rela -j .reloc -j .rodata --target=efi-app-x86_64 "$<" "$@"
	$(NODE) -e "require('fs').writeFileSync(process.argv[1],process.argv[2])" "$(OUT)/built-profile" "$(PLUGIN_KEY)"
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

endif # configured build

# Remove only this target's executable. Keep images and unrelated artifacts.
clean:
ifeq ($(OS),Windows_NT)
	@if exist "$(subst /,\,$(EXE))" del /q "$(subst /,\,$(EXE))"
else
	rm -f "$(EXE)"
endif

.PHONY: configure plugins
configure:
	$(NODE) tools/plugins.mjs configure "$(TARGET)" "$(CONFIG)" "$(CATALOG)" "$(OUT)" $(CONFIGURE_ARGS)
plugins:
	$(NODE) tools/plugins.mjs list "$(TARGET)" "$(CONFIG)" "$(CATALOG)" "$(OUT)"
