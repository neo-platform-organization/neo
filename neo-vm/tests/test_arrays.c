#include "neo_image.h"
#include "neo_execution.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(EXIT_FAILURE); } } while (0)
#define OK(x) CHECK((x) == NEO_OK)

static const char *neo_source =
    "(image (actor (data [-9223372036854775808 7 9223372036854775807]) (empty [])"
    " (handlers"
    " (get (body (array-get (slot \"data\") (index 1))))"
    " (set (body (array-set (slot \"data\") (index 1) (value 42))))"
    " (size (body (array-size (slot \"data\"))))"
    " (bad (body (array-get (slot \"data\") (index -1))))"
    " (end (body (array-set (slot \"data\") (index 3) (value 0))))"
    " (wrong (body (array-set (slot \"data\") (index 1) (value false))))"
    " (early (body (array-set (slot \"data\") (index (return (value 9))) (value 0))))"
    " (equal (body (eq (left [1 2]) (right [1 2]))))"
    " (copy (body (write (slot \"empty\") (value (read (slot \"data\")))))))))";

static const neo_capability *neo_child(neo_vm *vm, const neo_capability *parent, const char *name) {
    const neo_capability *out;
    OK(neo_object_child(vm, parent, name, &out));
    return out;
}
static int64_t neo_at(neo_vm *vm, const neo_capability *array, size_t index) {
    int64_t value;
    OK(neo_array_get(vm, array, index, &value));
    return value;
}
static void neo_run(neo_vm *vm, const neo_capability *actor, const char *name, neo_status expected) {
    neo_execution report;
    CHECK(neo_behavior_run(vm, actor, name, NULL, 10000, &report) == expected);
    if (expected == NEO_OK) {
        if (strcmp(name, "equal") == 0) { CHECK(report.result.kind == NEO_BOOLEAN && report.result.boolean); }
        if (strcmp(name, "size") == 0) { CHECK(report.result.kind == NEO_INTEGER && report.result.integer == 3); }
        if (strcmp(name, "get") == 0) { CHECK(report.result.kind == NEO_INTEGER && report.result.integer == 7); }
        if (strcmp(name, "early") == 0) { CHECK(report.result.kind == NEO_INTEGER && report.result.integer == 9); }
    }
    neo_execution_release(vm, &report);
}

typedef struct neo_heap { size_t calls, fail, live; } neo_heap;
static void *neo_allocate(void *context, size_t size) {
    neo_heap *heap = context;
    if (heap->calls++ == heap->fail) { return NULL; }
    void *p = malloc(size);
    if (p != NULL) { ++heap->live; }
    return p;
}
static void neo_release(void *context, void *p) {
    neo_heap *heap = context;
    --heap->live;
    free(p);
}

int main(void) {
    neo_vm *vm;
    const neo_capability *root, *copy, *restricted;
    neo_diagnostic diagnostic;
    OK(neo_vm_create(NULL, &vm));
    OK(neo_image_parse(vm, neo_source, &root, &diagnostic));
    const neo_capability *actor = neo_child(vm, root, "actor");
    const neo_capability *data = neo_child(vm, actor, "data");
    size_t count;
    OK(neo_array_size(vm, data, &count)); CHECK(count == 3);
    OK(neo_object_child_count(vm, data, &count)); CHECK(count == 0);
    CHECK(neo_at(vm, data, 0) == INT64_MIN && neo_at(vm, data, 2) == INT64_MAX);
    neo_run(vm, actor, "get", NEO_OK);
    neo_run(vm, actor, "size", NEO_OK);
    neo_run(vm, actor, "equal", NEO_OK);
    neo_run(vm, actor, "bad", NEO_INVALID);
    neo_run(vm, actor, "end", NEO_INVALID);
    neo_run(vm, actor, "wrong", NEO_WRONG_KIND);
    neo_run(vm, actor, "early", NEO_OK);
    CHECK(neo_at(vm, data, 1) == 7);
    neo_run(vm, actor, "copy", NEO_OK);
    neo_run(vm, actor, "set", NEO_OK);
    CHECK(neo_at(vm, data, 1) == 42);
    CHECK(neo_at(vm, neo_child(vm, actor, "empty"), 1) == 7);
    OK(neo_capability_restrict(vm, actor, NEO_READ | NEO_ACT, &restricted));
    neo_run(vm, restricted, "set", NEO_DENIED);
    OK(neo_capability_restrict(vm, data, NEO_READ, &restricted));
    CHECK(neo_array_set(vm, restricted, 0, 0) == NEO_DENIED);
    int64_t value;
    CHECK(neo_array_get(vm, data, SIZE_MAX, &value) == NEO_INVALID);
    CHECK(neo_array_get(vm, root, 0, &value) == NEO_WRONG_KIND);
    OK(neo_object_copy(vm, data, actor, "duplicate", &copy));
    OK(neo_array_set(vm, copy, 1, 100)); CHECK(neo_at(vm, data, 1) == 42);
    neo_value borrowed;
    OK(neo_object_read(vm, copy, &borrowed));
    OK(neo_object_write(vm, copy, borrowed)); CHECK(neo_at(vm, copy, 1) == 100);
    OK(neo_object_write(vm, copy, (neo_value){.kind = NEO_INTEGER, .integer = 5}));
    CHECK(neo_array_get(vm, copy, 0, &value) == NEO_WRONG_KIND);
    char *text;
    OK(neo_image_format(vm, root, &text));
    OK(neo_image_parse(vm, text, &copy, &diagnostic));
    neo_image_text_free(vm, text);
    CHECK(neo_at(vm, neo_child(vm, neo_child(vm, copy, "actor"), "data"), 1) == 42);
    OK(neo_image_unload(vm, root));
    CHECK(neo_array_get(vm, data, 0, &value) == NEO_UNAVAILABLE);
    const char *bad[] = {"(x [1", "(x [1 true])", "(x [\"1\"])", "(x [[1]])", "(x [9223372036854775808])", "(x [] [])", "(x :integer 1 [])"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        CHECK(neo_image_parse(vm, bad[i], &root, &diagnostic) == NEO_PARSE_ERROR);
    }
    neo_vm_destroy(vm);
    /* Fail every allocation in parsing, including array growth and rollback. */
    bool completed = false;
    for (size_t fail = 0; fail < 512; ++fail) {
        neo_heap heap = {.fail = SIZE_MAX};
        neo_allocator allocator = {&heap, neo_allocate, neo_release};
        OK(neo_vm_create(&allocator, &vm));
        heap.calls = 0; heap.fail = fail;
        neo_status status = neo_image_parse(vm, "(image (a [0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16]))", &root, &diagnostic);
        CHECK(status == NEO_OK || status == NEO_OUT_OF_MEMORY);
        neo_vm_destroy(vm); CHECK(heap.live == 0);
        if (status == NEO_OK) { completed = true; break; }
    }
    CHECK(completed);
    puts("PASS: packed arrays, bounds, authority, independent copies, roundtrip, parse rollback");
    return EXIT_SUCCESS;
}
