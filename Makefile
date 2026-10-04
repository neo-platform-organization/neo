CC ?= cc
CFLAGS ?= -std=c17 -O0 -g
WARNINGS = -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow -Wstrict-prototypes
CPPFLAGS += -Ineo-vm
# Normalize uname spellings to the target directory names. TARGET remains an
# explicit override for cross compilation; unsupported targets fail as stubs.
HOST_ARCH := $(shell uname -m | sed -e 's/^amd64$$/x86_64/' -e 's/^aarch64$$/arm64/' -e 's/^armv7l$$/armv7/' -e 's/^i[3-6]86$$/i686/')
HOST_OS := $(shell uname -s | sed -e 's/^Darwin$$/macOS/' -e 's/^MINGW.*/Windows/' -e 's/^MSYS.*/Windows/' -e 's/^CYGWIN.*/Windows/')
TARGET ?= $(HOST_ARCH)/$(HOST_OS)
WITH_X11 ?= 1
BUILD_ROOT = build
BUILD = $(BUILD_ROOT)/$(TARGET)
.DEFAULT_GOAL := all
TARGETS = x86_64/Linux x86_64/Windows x86_64/macOS x86_64/FreeBSD x86_64/neo i686/Linux i686/Windows i686/FreeBSD i686/neo arm64/Linux arm64/Windows arm64/macOS arm64/FreeBSD arm64/neo armv7/Linux armv7/FreeBSD armv7/neo riscv64/Linux riscv64/FreeBSD riscv64/neo ppc64le/Linux ppc64le/FreeBSD ppc64le/neo s390x/Linux s390x/neo wasm32 wasm64

ifeq ($(filter $(TARGET),$(TARGETS)),)
$(error Unknown TARGET '$(TARGET)'; use make targets)
endif
# Each architecture/OS directory selects an implementation or fails as a stub.
ifneq ($(filter-out clean targets path,$(MAKECMDGOALS)),)
include neo-vm/platform/$(TARGET)/platform.mk
else ifeq ($(strip $(MAKECMDGOALS)),)
include neo-vm/platform/$(TARGET)/platform.mk
endif

SOURCES = neo-vm/main.c neo-vm/cli/cli.c neo-vm/cli/launch.c neo-vm/cli/selftest.c \
          neo-vm/objects/objects.c neo-vm/messages/messages.c neo-vm/image/image.c \
          neo-vm/execution/evaluator.c neo-vm/execution/graph.c neo-vm/execution/scheduler.c \
          neo-vm/display/display.c neo-vm/io/io.c neo-vm/platform/platform.c \
          $(PLATFORM_SOURCES)
HEADERS = $(shell find neo-vm -name '*.h')
ifeq ($(WITH_X11),1)
CPPFLAGS += -DNEO_PLATFORM_WITH_X11 $(shell pkg-config --cflags x11)
SOURCES += neo-vm/platform/x11/x11.c
LDLIBS += $(shell pkg-config --libs x11)
else ifneq ($(WITH_X11),0)
$(error WITH_X11 must be 0 or 1)
endif

.PHONY: all test clean targets path FORCE $(TARGETS)
all: $(BUILD)/neo

test: $(BUILD)/neo
	./$(BUILD)/neo selftest

path:
	@printf '%s\n' '$(BUILD)/neo'
# A complete build produces only the universal executable. Rebuilding also applies
# mode/compiler option changes without leaving object files or configuration stamps.
$(BUILD)/neo: $(SOURCES) $(HEADERS) Makefile FORCE
	mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $(SOURCES) $(LDFLAGS) $(LDLIBS) -o $@.tmp
	mv $@.tmp $@

FORCE:

$(TARGETS):
	$(MAKE) TARGET=$@ all

targets:
	@printf '%s\n' $(TARGETS)

# Preserve neo image files if a user has placed any beneath build/.
clean:
	@if test -d $(BUILD_ROOT); then find $(BUILD_ROOT) -type f ! -name '*.neo' -delete; find $(BUILD_ROOT) -depth -type d -empty -delete; fi
