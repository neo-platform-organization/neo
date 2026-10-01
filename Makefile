CC ?= cc
AR ?= ar
CPPFLAGS += -Ineo-vm/include
CFLAGS ?= -std=c17 -O0 -g
WARNINGS = -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow -Wstrict-prototypes
BUILD = build
SOURCES = neo-vm/source/object.c neo-vm/source/message.c neo-vm/source/image.c neo-vm/source/evaluator.c neo-vm/source/scheduler.c
HEADERS = neo-vm/include/neo.h neo-vm/include/neo_message.h neo-vm/include/neo_image.h neo-vm/include/neo_execution.h neo-vm/source/internal.h
OBJECTS = $(SOURCES:neo-vm/source/%.c=$(BUILD)/%.o)
TEST_NAMES = objects messages runtime
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

test: $(TESTS) $(BUILD)/neo
	@for test in $(TESTS); do ./$$test || exit 1; done
	sh neo-vm/tests/test_cli.sh ./$(BUILD)/neo

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
