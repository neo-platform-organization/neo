CC ?= cc
CPPFLAGS += -Iinclude
CFLAGS ?= -std=c17 -O0 -g
WARNINGS = -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow -Wstrict-prototypes
BUILD = build

.PHONY: all test sanitize demo clean
all: $(BUILD)/test_objects $(BUILD)/demo

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/test_objects: src/object.c tests/test_objects.c include/neo.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) src/object.c tests/test_objects.c $(LDFLAGS) -o $@

$(BUILD)/demo: src/object.c examples/demo.c include/neo.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) src/object.c examples/demo.c $(LDFLAGS) -o $@

test: $(BUILD)/test_objects
	./$(BUILD)/test_objects

demo: $(BUILD)/demo
	./$(BUILD)/demo

$(BUILD)/test_sanitize: src/object.c tests/test_objects.c include/neo.h | $(BUILD)
	$(CC) $(CPPFLAGS) -std=c17 -O1 -g $(WARNINGS) -fsanitize=address,undefined -fno-omit-frame-pointer src/object.c tests/test_objects.c -o $@

sanitize: $(BUILD)/test_sanitize
	./$(BUILD)/test_sanitize

clean:
	rm -rf $(BUILD)
