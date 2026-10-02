CC ?= cc
AR ?= ar
CPPFLAGS += -Ineo-vm/include
CFLAGS ?= -std=c17 -O0 -g
WARNINGS = -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow -Wstrict-prototypes
BUILD = build
SOURCES = neo-vm/source/object.c neo-vm/source/message.c neo-vm/source/image.c neo-vm/source/evaluator.c neo-vm/source/scheduler.c neo-vm/source/display.c neo-vm/source/io.c neo-vm/source/bootstrap.c
HEADERS = neo-vm/include/neo_bootstrap.h neo-vm/include/neo_io.h neo-vm/include/neo.h neo-vm/include/neo_display.h neo-vm/include/neo_message.h neo-vm/include/neo_image.h neo-vm/include/neo_execution.h neo-vm/source/internal.h
OBJECTS = $(SOURCES:neo-vm/source/%.c=$(BUILD)/%.o)
TEST_NAMES = objects messages runtime display triangle io renderer
TESTS = $(addprefix $(BUILD)/test_,$(TEST_NAMES))
SANITIZERS = $(addprefix $(BUILD)/sanitize_,$(TEST_NAMES))

.PHONY: all test sanitize demo clean
all: $(BUILD)/neo $(BUILD)/demo $(TESTS)

$(BUILD):
	mkdir -p $(BUILD)

# Compile the runtime once. Generated dependencies track included headers.
$(BUILD)/%.o: neo-vm/source/%.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) -MMD -MP -c $< -o $@

$(BUILD)/libneo.a: $(OBJECTS)
	$(AR) rcs $@ $(OBJECTS)

$(BUILD)/neo: neo-vm/source/main.c $(BUILD)/libneo.a $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $< $(BUILD)/libneo.a $(LDFLAGS) -o $@

$(BUILD)/demo: neo-vm/examples/demo.c $(BUILD)/libneo.a $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $< $(BUILD)/libneo.a $(LDFLAGS) -o $@

$(BUILD)/test_%: neo-vm/tests/test_%.c $(BUILD)/libneo.a $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $< $(BUILD)/libneo.a $(LDFLAGS) -o $@

test: $(TESTS) $(BUILD)/neo $(BUILD)/neo-io
	@for test in $(TESTS); do ./$$test || exit 1; done
	sh neo-vm/tests/test_cli.sh ./$(BUILD)/neo ./$(BUILD)/neo-io

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
.PHONY: window
window: $(BUILD)/neo-window

$(BUILD)/neo-window: neo-vm/examples/window.c neo-vm/source/window_x11.c neo-vm/include/neo_window_x11.h $(BUILD)/libneo.a $(HEADERS)
	$(CC) $(CPPFLAGS) $(X11_CFLAGS) $(CFLAGS) $(WARNINGS) neo-vm/examples/window.c neo-vm/source/window_x11.c $(BUILD)/libneo.a $(LDFLAGS) $(X11_LIBS) -o $@

# Explicit opt-in: creates a short-lived test window on DISPLAY.
.PHONY: test-window
test-window: $(BUILD)/test_window_x11
	./$(BUILD)/test_window_x11

$(BUILD)/test_window_x11: neo-vm/tests/test_window_x11.c neo-vm/source/window_x11.c neo-vm/include/neo_window_x11.h $(BUILD)/libneo.a $(HEADERS)
	$(CC) $(CPPFLAGS) $(X11_CFLAGS) $(CFLAGS) $(WARNINGS) neo-vm/tests/test_window_x11.c neo-vm/source/window_x11.c $(BUILD)/libneo.a $(LDFLAGS) $(X11_LIBS) -o $@

.PHONY: io
io: $(BUILD)/neo-io

$(BUILD)/neo-io: neo-vm/examples/terminal.c neo-vm/source/io_posix.c neo-vm/include/neo_io_posix.h $(BUILD)/libneo.a $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) neo-vm/examples/terminal.c neo-vm/source/io_posix.c $(BUILD)/libneo.a $(LDFLAGS) -o $@

$(BUILD)/test_io: neo-vm/tests/test_io.c neo-vm/source/io_posix.c neo-vm/include/neo_io_posix.h $(BUILD)/libneo.a $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) neo-vm/tests/test_io.c neo-vm/source/io_posix.c $(BUILD)/libneo.a $(LDFLAGS) -o $@

$(BUILD)/sanitize_io: neo-vm/tests/test_io.c neo-vm/source/io_posix.c neo-vm/include/neo_io_posix.h $(SOURCES) $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) -std=c17 -O1 -g $(WARNINGS) -fsanitize=address,undefined -fno-omit-frame-pointer $(SOURCES) neo-vm/source/io_posix.c neo-vm/tests/test_io.c -o $@
