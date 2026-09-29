#include "neo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)
#define OK(expression) CHECK((expression) == NEO_OK)

static neo_value neo_integer(int64_t value) {
    return (neo_value){.kind = NEO_INTEGER, .integer = value};
}

static neo_object_id neo_id(neo_vm *vm, const neo_capability *object) {
    neo_object_id id = 0;
    OK(neo_object_identity(vm, object, &id));
    return id;
}

static int64_t neo_number(neo_vm *vm, const neo_capability *object) {
    neo_value value;
    OK(neo_object_read(vm, object, &value));
    CHECK(value.kind == NEO_INTEGER);
    return value.integer;
}

static size_t neo_count(neo_vm *vm, const neo_capability *object) {
    size_t count = 0;
    OK(neo_object_child_count(vm, object, &count));
    return count;
}

static void neo_test_copy_move(void) {
    neo_vm *vm = NULL;
    const neo_capability *plate, *cell, *dna, *behavior, *food, *copy;
    const neo_capability *copied_dna, *copied_behavior, *target, *moved, *temporary;
    OK(neo_vm_create(NULL, &vm));
    OK(neo_image_create(vm, "plate", &plate));
    OK(neo_object_create(vm, plate, "cell", (neo_value){.kind = NEO_OBJECT}, &cell));
    OK(neo_object_create(vm, cell, "dna", neo_integer(42), &dna));
    OK(neo_object_create(vm, cell, "behavior",
        (neo_value){.kind = NEO_PRIMITIVE, .text = "neo.test/1"}, &behavior));
    OK(neo_object_create(vm, plate, "food", neo_integer(9), &food));
    OK(neo_object_connect(vm, behavior, "dna", dna, NEO_READ | NEO_WRITE));
    OK(neo_object_connect(vm, dna, "owner", cell, NEO_READ));
    OK(neo_object_connect(vm, cell, "food", food, NEO_READ));
    OK(neo_object_connect(vm, cell, "self", cell, NEO_READ));
    OK(neo_object_copy(vm, cell, plate, "cell-copy", &copy));
    CHECK(neo_id(vm, copy) != neo_id(vm, cell));
    OK(neo_object_child(vm, copy, "dna", &copied_dna));
    OK(neo_object_child(vm, copy, "behavior", &copied_behavior));
    OK(neo_object_connection(vm, copied_behavior, "dna", &target));
    CHECK(neo_id(vm, target) == neo_id(vm, copied_dna));
    OK(neo_object_write(vm, target, neo_integer(7)));
    CHECK(neo_number(vm, dna) == 42);
    CHECK(neo_number(vm, copied_dna) == 7);
    OK(neo_object_connection(vm, copied_dna, "owner", &target));
    CHECK(neo_id(vm, target) == neo_id(vm, copy));
    OK(neo_object_connection(vm, copy, "self", &target));
    CHECK(neo_id(vm, target) == neo_id(vm, copy));
    OK(neo_object_connection(vm, copy, "food", &target));
    CHECK(neo_id(vm, target) == neo_id(vm, food));
    CHECK(neo_object_write(vm, target, neo_integer(0)) == NEO_DENIED);
    OK(neo_object_write(vm, copied_behavior,
        (neo_value){.kind = NEO_PRIMITIVE, .text = "neo.changed/1"}));
    neo_value value;
    OK(neo_object_read(vm, behavior, &value));
    CHECK(strcmp(value.text, "neo.test/1") == 0);
    CHECK(neo_object_copy(vm, cell, dna, "bad", &temporary) == NEO_CYCLE);
    CHECK(temporary == NULL);
    CHECK(neo_object_move(vm, cell, dna, "bad", &temporary) == NEO_CYCLE);
    CHECK(neo_object_copy(vm, cell, plate, "food", &temporary) == NEO_CONFLICT);
    CHECK(neo_object_move(vm, cell, plate, "food", &temporary) == NEO_CONFLICT);
    CHECK(neo_count(vm, plate) == 3);
    neo_object_id original_id = neo_id(vm, food);
    OK(neo_object_move(vm, food, cell, "meal", &moved));
    CHECK(neo_id(vm, moved) != original_id);
    CHECK(neo_count(vm, plate) == 2);
    CHECK(neo_count(vm, cell) == 3);
    CHECK(neo_object_read(vm, food, &value) == NEO_UNAVAILABLE);
    CHECK(neo_object_read(vm, target, &value) == NEO_UNAVAILABLE);
    CHECK(neo_object_connection(vm, copy, "food", &target) == NEO_UNAVAILABLE);
    CHECK(neo_object_child(vm, plate, "food", &target) == NEO_UNAVAILABLE);
    OK(neo_object_delete(vm, cell));
    CHECK(neo_object_read(vm, dna, &value) == NEO_UNAVAILABLE);
    CHECK(neo_object_read(vm, moved, &value) == NEO_UNAVAILABLE);
    CHECK(neo_number(vm, copied_dna) == 7);
    CHECK(neo_object_delete(vm, plate) == NEO_DENIED);
    OK(neo_image_unload(vm, plate));
    CHECK(neo_object_read(vm, copy, &value) == NEO_UNAVAILABLE);
    neo_vm_destroy(vm);
}

static void neo_test_authority_and_images(void) {
    neo_vm *vm = NULL, *other_vm = NULL;
    const neo_capability *root, *second, *object, *limited, *target, *clone, *child;
    const neo_capability *foreign, *read_copy;
    OK(neo_vm_create(NULL, &vm));
    OK(neo_vm_create(NULL, &other_vm));
    OK(neo_image_create(vm, "image", &root));
    OK(neo_image_create(vm, "image", &second));
    OK(neo_image_create(other_vm, "foreign", &foreign));
    OK(neo_object_create(vm, root, "value", neo_integer(5), &object));
    OK(neo_capability_restrict(vm, object, NEO_READ, &limited));
    CHECK(neo_object_write(vm, limited, neo_integer(1)) == NEO_DENIED);
    CHECK(neo_object_delete(vm, limited) == NEO_DENIED);
    CHECK(neo_capability_restrict(vm, limited, NEO_ALL, &target) == NEO_DENIED);
    CHECK(target == NULL);
    CHECK(neo_object_connect(vm, root, "bad", limited, NEO_READ) == NEO_DENIED);
    CHECK(neo_object_connect(vm, root, "cross", second, NEO_READ) == NEO_WRONG_IMAGE);
    CHECK(neo_object_copy(vm, object, second, "cross", &target) == NEO_WRONG_IMAGE);
    CHECK(neo_object_move(vm, object, second, "cross", &target) == NEO_WRONG_IMAGE);
    neo_value value;
    CHECK(neo_object_read(vm, foreign, &value) == NEO_DENIED);
    CHECK(neo_object_copy(vm, limited, root, "denied", &target) == NEO_DENIED);
    OK(neo_capability_restrict(vm, object, NEO_READ | NEO_COPY, &read_copy));
    OK(neo_object_copy(vm, read_copy, root, "limited-copy", &target));
    CHECK(neo_object_write(vm, target, neo_integer(8)) == NEO_DENIED);
    CHECK(neo_object_move(vm, read_copy, root, "no-delete", &target) == NEO_DENIED);
    OK(neo_object_connect(vm, object, "root", root, NEO_READ));
    OK(neo_image_duplicate(vm, root, "clone", &clone));
    OK(neo_object_child(vm, clone, "value", &child));
    CHECK(neo_id(vm, child) != neo_id(vm, object));
    OK(neo_object_connection(vm, child, "root", &target));
    CHECK(neo_id(vm, target) == neo_id(vm, clone));
    OK(neo_object_write(vm, child, neo_integer(99)));
    CHECK(neo_number(vm, object) == 5);
    OK(neo_image_unload(vm, root));
    CHECK(neo_number(vm, child) == 99);
    CHECK(neo_count(vm, second) == 0);
    CHECK(neo_image_unload(vm, child) == NEO_WRONG_KIND);
    neo_vm_destroy(other_vm);
    neo_vm_destroy(vm);
}

typedef struct neo_fault_allocator {
    size_t live;
    size_t calls;
    size_t fail_at;
} neo_fault_allocator;

static void *neo_fault_allocate(void *context, size_t size) {
    neo_fault_allocator *allocator = context;
    if (allocator->calls++ == allocator->fail_at) {
        return NULL;
    }
    void *memory = malloc(size);
    if (memory != NULL) {
        ++allocator->live;
    }
    return memory;
}

static void neo_fault_release(void *context, void *memory) {
    neo_fault_allocator *allocator = context;
    CHECK(memory != NULL && allocator->live > 0);
    --allocator->live;
    free(memory);
}

/* Fail each allocation in turn, including after part of the duplicate exists.
 * Check no destination becomes visible and no original state is destroyed. */
static void neo_test_allocation_rollback(void) {
    for (unsigned operation = 0; operation < 5; ++operation) {
        bool reached_success = false;
        for (size_t fail_at = 0; fail_at < 128; ++fail_at) {
            neo_fault_allocator state = {.fail_at = SIZE_MAX};
            neo_allocator allocator = {&state, neo_fault_allocate, neo_fault_release};
            neo_vm *vm = NULL;
            const neo_capability *root, *source, *child, *out;
            OK(neo_vm_create(&allocator, &vm));
            OK(neo_image_create(vm, "image", &root));
            OK(neo_object_create(vm, root, "source",
                (neo_value){.kind = NEO_TEXT, .text = "owned text"}, &source));
            OK(neo_object_create(vm, source, "child", neo_integer(12), &child));
            OK(neo_object_connect(vm, source, "internal", child, NEO_READ));
            OK(neo_object_connect(vm, child, "external", root, NEO_READ));
            size_t live_before = state.live;
            state.calls = 0;
            state.fail_at = fail_at;
            neo_status status;
            out = NULL;
            if (operation == 0) {
                status = neo_object_copy(vm, source, root, "copy", &out);
            } else if (operation == 1) {
                status = neo_object_move(vm, source, root, "copy", &out);
            } else if (operation == 2) {
                status = neo_image_duplicate(vm, root, "copy", &out);
            } else if (operation == 3) {
                status = neo_object_create(vm, root, "copy",
                    (neo_value){.kind = NEO_TEXT, .text = "copy"}, &out);
            } else {
                status = neo_object_write(vm, source,
                    (neo_value){.kind = NEO_TEXT, .text = "replacement"});
            }
            state.fail_at = SIZE_MAX;
            if (status == NEO_OUT_OF_MEMORY) {
                CHECK(out == NULL);
                CHECK(state.live == live_before);
                CHECK(neo_count(vm, root) == 1);
                CHECK(neo_number(vm, child) == 12);
                neo_value value;
                OK(neo_object_read(vm, source, &value));
                CHECK(strcmp(value.text, "owned text") == 0);
                CHECK(neo_object_child(vm, root, "copy", &out) == NEO_UNAVAILABLE);
            } else {
                CHECK(status == NEO_OK);
                reached_success = true;
            }
            neo_vm_destroy(vm);
            CHECK(state.live == 0);
            if (reached_success) {
                break;
            }
        }
        CHECK(reached_success);
    }
}

static void neo_test_invalid_and_payloads(void) {
    neo_vm *vm = NULL;
    const neo_capability *root, *text, *boolean, *out;
    neo_value value;
    CHECK(neo_vm_create(NULL, NULL) == NEO_INVALID);
    neo_allocator invalid = {0};
    CHECK(neo_vm_create(&invalid, &vm) == NEO_INVALID && vm == NULL);
    OK(neo_vm_create(NULL, &vm));
    OK(neo_image_create(vm, "root", &root));
    CHECK(neo_object_create(vm, root, "bad", (neo_value){.kind = NEO_TEXT}, &out)
          == NEO_INVALID);
    CHECK(out == NULL);
    CHECK(neo_object_read(NULL, root, &value) == NEO_INVALID);
    CHECK(neo_object_read(vm, NULL, &value) == NEO_INVALID);
    CHECK(neo_object_read(vm, root, NULL) == NEO_INVALID);
    CHECK(neo_object_child(vm, root, NULL, &out) == NEO_INVALID);
    CHECK(neo_object_copy(vm, root, root, NULL, &out) == NEO_INVALID);
    char input[] = "original";
    OK(neo_object_create(vm, root, "text", (neo_value){.kind = NEO_TEXT, .text = input}, &text));
    input[0] = 'X';
    OK(neo_object_read(vm, text, &value));
    CHECK(strcmp(value.text, "original") == 0);
    /* Writing a borrowed payload back must copy before releasing the original. */
    OK(neo_object_write(vm, text, value));
    OK(neo_object_read(vm, text, &value));
    CHECK(strcmp(value.text, "original") == 0);
    OK(neo_object_create(vm, root, "boolean",
        (neo_value){.kind = NEO_BOOLEAN, .boolean = true}, &boolean));
    OK(neo_object_read(vm, boolean, &value));
    CHECK(value.kind == NEO_BOOLEAN && value.boolean);
    neo_object_id old_id = neo_id(vm, text);
    OK(neo_object_delete(vm, text));
    OK(neo_object_create(vm, root, "text", neo_integer(4), &out));
    CHECK(neo_id(vm, out) != old_id);
    CHECK(neo_object_read(vm, text, &value) == NEO_UNAVAILABLE);
    neo_vm_destroy(vm);
    neo_vm_destroy(NULL);
}

int main(void) {
    neo_test_copy_move();
    neo_test_authority_and_images();
    neo_test_allocation_rollback();
    neo_test_invalid_and_payloads();
    puts("PASS: copy/move, graph remapping, authority, images, allocation rollback, payloads");
    return EXIT_SUCCESS;
}
