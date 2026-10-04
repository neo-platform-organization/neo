CC ?= cc
AR ?= ar
CPPFLAGS += -Ineo-vm/include
CFLAGS ?= -std=c17 -O0 -g
WARNINGS = -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow -Wstrict-prototypes
BUILD = build
WITH_X11 ?= 1
SOURCES = neo-vm/source/object.c neo-vm/source/message.c neo-vm/source/image.c neo-vm/source/evaluator.c neo-vm/source/scheduler.c neo-vm/source/display.c neo-vm/source/io.c neo-vm/source/bootstrap.c neo-vm/source/platform.c
HEADERS = neo-vm/include/neo_platform.h neo-vm/include/neo_bootstrap.h neo-vm/include/neo_io.h neo-vm/include/neo.h neo-vm/include/neo_display.h neo-vm/include/neo_message.h neo-vm/include/neo_image.h neo-vm/include/neo_execution.h neo-vm/source/internal.h
# Host adapter stays outside the portable VM archive.
PLATFORM ?= linux-x86_64
ifneq ($(PLATFORM),linux-x86_64)
$(error Unsupported PLATFORM '$(PLATFORM)'; implemented: linux-x86_64)
endif
HOST_SOURCE = neo-vm/source/platform_linux.c neo-vm/source/io_posix.c
HOST_HEADERS = neo-vm/include/neo_io_posix.h
OBJECTS = $(SOURCES:neo-vm/source/%.c=$(BUILD)/%.o)
TEST_NAMES = platform platform_services objects messages runtime display triangle io renderer arrays
TESTS = $(addprefix $(BUILD)/test_,$(TEST_NAMES))
SANITIZERS = $(addprefix $(BUILD)/sanitize_,$(TEST_NAMES))

.PHONY: all test sanitize demo clean
all: $(BUILD)/neo

$(BUILD):
	mkdir -p $(BUILD)

# Compile the runtime once. Generated dependencies track included headers.
$(BUILD)/%.o: neo-vm/source/%.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) -MMD -MP -c $< -o $@

$(BUILD)/libneo.a: $(OBJECTS)
	$(AR) rcs $@ $(OBJECTS)


CLI_SOURCES = neo-vm/source/cli.c neo-vm/source/launch.c
CLI_HEADERS = neo-vm/include/neo_cli.h
ifeq ($(WITH_X11),1)
CLI_FLAGS = -DNEO_PLATFORM_WITH_X11 $(X11_CFLAGS)
CLI_BACKEND = neo-vm/source/window_x11.c
CLI_LIBS = $(X11_LIBS)
else ifneq ($(WITH_X11),0)
$(error WITH_X11 must be 0 or 1)
endif

# Switching optional backends in the same build directory must relink the CLI.
.PHONY: FORCE
FORCE:
$(BUILD)/cli-config: FORCE | $(BUILD)
	@printf '%s\n' '$(WITH_X11)' > $@.tmp
	@cmp -s $@.tmp $@ || cp $@.tmp $@
	@rm -f $@.tmp

$(BUILD)/neo: neo-vm/source/main.c $(CLI_SOURCES) $(CLI_HEADERS) $(CLI_BACKEND) neo-vm/include/neo_window_x11.h $(BUILD)/libneo.a $(HEADERS) $(HOST_SOURCE) $(HOST_HEADERS) $(BUILD)/cli-config
	$(CC) $(CPPFLAGS) $(CLI_FLAGS) $(CFLAGS) $(WARNINGS) neo-vm/source/main.c $(CLI_SOURCES) $(HOST_SOURCE) $(CLI_BACKEND) $(BUILD)/libneo.a $(LDFLAGS) $(CLI_LIBS) -o $@

$(BUILD)/demo: neo-vm/examples/demo.c $(BUILD)/libneo.a $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $< $(BUILD)/libneo.a $(LDFLAGS) -o $@

$(BUILD)/test_%: neo-vm/tests/test_%.c $(BUILD)/libneo.a $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $< $(BUILD)/libneo.a $(LDFLAGS) -o $@

test: $(TESTS) $(BUILD)/neo
	@for test in $(TESTS); do ./$$test || exit 1; done
	sh neo-vm/tests/test_cli.sh ./$(BUILD)/neo $(WITH_X11)

demo: $(BUILD)/demo
	./$(BUILD)/demo

# Separate binaries avoid mixing instrumented and uninstrumented objects.
$(BUILD)/sanitize_%: neo-vm/tests/test_%.c $(SOURCES) $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) -std=c17 -O1 -g $(WARNINGS) -fsanitize=address,undefined -fno-omit-frame-pointer $(SOURCES) $< -o $@

sanitize: $(SANITIZERS)
	@for test in $(SANITIZERS); do ./$$test || exit 1; done

clean:
	rm -rf $(BUILD)

-include $(OBJECTS:.o=.d)

# Optional X11 host adapter; the core runtime remains independent of Xlib.
X11_CFLAGS ?= $(shell pkg-config --cflags x11)
X11_LIBS ?= $(shell pkg-config --libs x11)
# Explicit opt-in: creates a short-lived test window on DISPLAY.
.PHONY: test-window
test-window: $(BUILD)/test_window_x11
	./$(BUILD)/test_window_x11

$(BUILD)/test_window_x11: neo-vm/tests/test_window_x11.c neo-vm/source/window_x11.c neo-vm/include/neo_window_x11.h $(BUILD)/libneo.a $(HEADERS) $(HOST_SOURCE) $(HOST_HEADERS)
	$(CC) $(CPPFLAGS) -DNEO_PLATFORM_WITH_X11 $(X11_CFLAGS) $(CFLAGS) $(WARNINGS) neo-vm/tests/test_window_x11.c neo-vm/source/window_x11.c $(BUILD)/libneo.a $(HOST_SOURCE) $(LDFLAGS) $(X11_LIBS) -o $@

$(BUILD)/test_io: neo-vm/tests/test_io.c neo-vm/source/io_posix.c neo-vm/include/neo_io_posix.h $(BUILD)/libneo.a $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) neo-vm/tests/test_io.c neo-vm/source/io_posix.c $(BUILD)/libneo.a $(LDFLAGS) -o $@

$(BUILD)/sanitize_io: neo-vm/tests/test_io.c neo-vm/source/io_posix.c neo-vm/include/neo_io_posix.h $(SOURCES) $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) -std=c17 -O1 -g $(WARNINGS) -fsanitize=address,undefined -fno-omit-frame-pointer $(SOURCES) neo-vm/source/io_posix.c neo-vm/tests/test_io.c -o $@

$(BUILD)/test_platform: neo-vm/tests/test_platform.c $(HOST_SOURCE) $(BUILD)/libneo.a $(HEADERS) $(HOST_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $< $(HOST_SOURCE) $(BUILD)/libneo.a $(LDFLAGS) -o $@

$(BUILD)/sanitize_platform: neo-vm/tests/test_platform.c $(HOST_SOURCE) $(SOURCES) $(HEADERS) $(HOST_HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) -std=c17 -O1 -g $(WARNINGS) -fsanitize=address,undefined -fno-omit-frame-pointer $(SOURCES) $(HOST_SOURCE) $< -o $@
