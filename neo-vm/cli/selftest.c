#include "cli/cli.h"
#include "objects/objects.h"
#include "execution/execution.h"
#include "image/image.h"
#include "platform/platform.h"
#include "io/io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct neo_test_allocator {
    size_t calls;
    size_t fail_at;
    size_t live;
} neo_test_allocator;

static void *neo_test_allocate(void *context, size_t size) {
    neo_test_allocator *state = context;
    ++state->calls;
    if (state->calls == state->fail_at) { return NULL; }
    void *memory = malloc(size);
    if (memory != NULL) { ++state->live; }
    return memory;
}
static void neo_test_release(void *context, void *memory) {
    neo_test_allocator *state = context;
    if (memory != NULL) { --state->live; free(memory); }
}

typedef struct neo_test_case {
    const char *name;
    const char *expression;
    neo_status status;
    neo_value result;
} neo_test_case;

static const neo_test_case neo_cases[] = {
    {"less equal", "(<= (left 4) (right 4))", NEO_OK, {.kind=NEO_BOOLEAN,.boolean=true}},
    {"greater equal", "(>= (left 4) (right 5))", NEO_OK, {.kind=NEO_BOOLEAN,.boolean=false}},
    {"inequality", "(!= (left 4) (right 5))", NEO_OK, {.kind=NEO_BOOLEAN,.boolean=true}},
    {"quoted comparison", "(to-text (value \"<=\"))", NEO_OK, {.kind=NEO_TEXT,.text="<="}},
    {"self is reference", "(self)", NEO_OK, {.kind=NEO_REFERENCE}},
    {"same target", "(same (left (self)) (right (restrict (target (self)) (rights 1))))", NEO_OK, {.kind=NEO_BOOLEAN,.boolean=true}},
    {"reference equality requires same", "(== (left (self)) (right (self)))", NEO_WRONG_KIND, {0}},
    {"child name", "(object-name (target (child (target (self)) (name \"value\"))))", NEO_OK, {.kind=NEO_TEXT,.text="value"}},
    {"child order", "(object-name (target (child-at (target (self)) (index 1))))", NEO_OK, {.kind=NEO_TEXT,.text="array"}},
    {"child count", "(child-count (target (self)))", NEO_OK, {.kind=NEO_INTEGER,.integer=5}},
    {"inert inspection", "(object-kind (target (child (target (self)) (name \"operation\"))))", NEO_OK, {.kind=NEO_TEXT,.text="primitive"}},
    {"primitive payload stays inert", "(inspect (target (child (target (self)) (name \"operation\"))))", NEO_OK, {.kind=NEO_PRIMITIVE,.text="fail"}},
    {"array inspection", "(inspect (target (child (target (self)) (name \"array\"))))", NEO_OK, {.kind=NEO_INTEGERS,.integers=(const int64_t[]){1,2},.count=2}},
    {"connection read", "(inspect (target (child (target (connection (target (self)) (name \"read-only\"))) (name \"number\"))))", NEO_OK, {.kind=NEO_INTEGER,.integer=7}},
    {"missing child", "(has-child (target (self)) (name \"absent\"))", NEO_OK, {.kind=NEO_BOOLEAN,.boolean=false}},
    {"missing connection", "(connection (target (self)) (name \"absent\"))", NEO_UNAVAILABLE, {0}},
    {"negative index", "(child-at (target (self)) (index -1))", NEO_INVALID, {0}},
    {"index outside listing", "(child-at (target (self)) (index 900))", NEO_UNAVAILABLE, {0}},
    {"integer cannot forge reference", "(inspect (target 1))", NEO_WRONG_KIND, {0}},
    {"text cannot forge reference", "(inspect (target \"vault\"))", NEO_WRONG_KIND, {0}},
    {"denied inspection", "(inspect (target (restrict (target (self)) (rights 0))))", NEO_DENIED, {0}},
    {"denied presence query", "(has-child (target (restrict (target (self)) (rights 0))) (name \"value\"))", NEO_DENIED, {0}},
    {"no authority escalation", "(restrict (target (connection (target (self)) (name \"read-only\"))) (rights 255))", NEO_DENIED, {0}},
    {"invalid rights", "(restrict (target (self)) (rights 256))", NEO_INVALID, {0}},
    {"reference cannot be stored", "(write (slot \"value\") (value (self)))", NEO_WRONG_KIND, {0}},
    {"reference cannot be created as payload", "(object-create (target (self)) (name \"ref\") (value (self)))", NEO_WRONG_KIND, {0}},
    {"denied external write", "(object-write (target (child (target (connection (target (self)) (name \"read-only\"))) (name \"number\"))) (value 8))", NEO_DENIED, {0}},
    {"create then inspect", "(inspect (target (object-create (target (self)) (name \"new\") (value 42))))", NEO_OK, {.kind=NEO_INTEGER,.integer=42}},
    {"create conflict", "(object-create (target (self)) (name \"value\") (value 42))", NEO_CONFLICT, {0}},
    {"write owned value", "(object-write (target (child (target (self)) (name \"value\"))) (value 9))", NEO_OK, {.kind=NEO_INTEGER,.integer=9}},
    {"copy via kernel", "(inspect (target (object-copy (target (child (target (self)) (name \"array\"))) (destination (child (target (self)) (name \"scratch\"))) (name \"copy\"))))", NEO_OK, {.kind=NEO_INTEGERS,.integers=(const int64_t[]){1,2},.count=2}},
    {"move deletes original", "(do (first (object-move (target (child (target (self)) (name \"array\"))) (destination (child (target (self)) (name \"scratch\"))) (name \"moved\"))) (last (has-child (target (self)) (name \"array\"))))", NEO_OK, {.kind=NEO_BOOLEAN,.boolean=false}},
    {"delete then lookup", "(do (first (object-delete (target (child (target (self)) (name \"value\"))))) (last (has-child (target (self)) (name \"value\"))))", NEO_OK, {.kind=NEO_BOOLEAN,.boolean=false}},
    {"protect receiver", "(object-delete (target (self)))", NEO_DENIED, {0}},
    {"protect handlers", "(object-create (target (child (target (self)) (name \"handlers\"))) (name \"new\") (value 0))", NEO_DENIED, {0}},
    {"protect active code", "(object-write (target (child (target (child (target (self)) (name \"handlers\"))) (name \"main\"))) (value 0))", NEO_DENIED, {0}},
    {"create attenuated connection", "(do (first (connect (target (self)) (name \"number\") (value (child (target (self)) (name \"value\"))) (rights 1))) (last (inspect (target (connection (target (self)) (name \"number\"))))))", NEO_OK, {.kind=NEO_INTEGER,.integer=7}},
    {"no delegation through read-only", "(connect (target (self)) (name \"stolen\") (value (connection (target (self)) (name \"read-only\"))) (rights 1))", NEO_DENIED, {0}},
    {"nested operand deletion", "(object-write (target (child (target (connection (target (self)) (name \"vault\"))) (name \"number\"))) (value (do (first (object-delete (target (connection (target (self)) (name \"vault\"))))) (last 9))))", NEO_UNAVAILABLE, {0}},
    {"legacy write revalidates identity", "(write (slot \"value\") (value (do (first (object-delete (target (child (target (self)) (name \"value\"))))) (last 9))))", NEO_UNAVAILABLE, {0}},
    {"return propagates", "(child (target (self)) (name (return (value \"early\"))))", NEO_OK, {.kind=NEO_TEXT,.text="early"}},
    {"local call returns reference", "(same (left (self)) (right (call (selector \"helper\"))))", NEO_OK, {.kind=NEO_BOOLEAN,.boolean=true}},
    {"invoke target behavior", "(invoke (target (connection (target (self)) (name \"vault\"))) (selector \"echo\"))", NEO_OK, {.kind=NEO_INTEGER,.integer=42}},
    {"invoke requires ACT", "(invoke (target (connection (target (self)) (name \"read-only\"))) (selector \"echo\"))", NEO_DENIED, {0}},
    {"callee cannot delete suspended caller", "(invoke (target (connection (target (self)) (name \"vault\"))) (selector \"attack\"))", NEO_DENIED, {0}},
    {"callee shares budget", "(invoke (target (connection (target (self)) (name \"vault\"))) (selector \"spin\"))", NEO_LIMIT, {0}},
    {"scalar to text", "(to-text (value -9223372036854775808))", NEO_OK, {.kind=NEO_TEXT,.text="-9223372036854775808"}},
    {"references have no printable pointer", "(to-text (value (self)))", NEO_WRONG_KIND, {0}},
    {"bounded loop", "(while (condition true) (body (self)))", NEO_LIMIT, {0}}
};

static bool neo_test_equal(neo_value a, neo_value b) {
    if (a.kind != b.kind) { return false; }
    switch (a.kind) {
        case NEO_OBJECT: return true;
        case NEO_REFERENCE: return a.reference != NULL;
        case NEO_INTEGER: return a.integer == b.integer;
        case NEO_BOOLEAN: return a.boolean == b.boolean;
        case NEO_TEXT: case NEO_PRIMITIVE: return a.text != NULL && strcmp(a.text, b.text) == 0;
        case NEO_INTEGERS: return a.count == b.count && (a.count == 0 || memcmp(a.integers, b.integers, a.count * sizeof(int64_t)) == 0);
    }
    return false;
}

static bool neo_test_expression(neo_vm *vm, const neo_test_case *item) {
    char source[8192];
    int length = snprintf(source, sizeof(source),
        "(image (vault #vault @caller #actor 255 (number 7) (handlers "
        "(echo (body (return (value 42)))) "
        "(attack (body (object-delete (target (connection (target (self)) (name \"caller\")))))) "
        "(spin (body (while (condition true) (body (self))))))) "
        "(actor #actor @read-only #vault 1 @vault #vault 255 "
        "(value 7) (array [1 2]) (scratch) (operation :primitive fail) "
        "(handlers (main (body %s)) (helper (body (return (value (self))))))))", item->expression);
    if (length < 0 || (size_t)length >= sizeof(source)) { return false; }
    const neo_capability *root = NULL, *actor = NULL;
    neo_diagnostic error = {0};
    neo_status status = neo_image_parse(vm, source, &root, &error);
    if (status == NEO_OK) { status = neo_object_child(vm, root, "actor", &actor); }
    neo_execution report = {0};
    if (status == NEO_OK) { status = neo_behavior_run(vm, actor, "main", NULL, 10000, &report); }
    bool ok = status == item->status && (status != NEO_OK || neo_test_equal(report.result, item->result));
    if (!ok) { fprintf(stderr, "FAIL %s: got %s, expected %s (%s)\n", item->name, neo_status_name(status), neo_status_name(item->status), error.message); }
    neo_execution_release(vm, &report);
    if (root != NULL && neo_image_unload(vm, root) != NEO_OK) { ok = false; }
    return ok;
}

/* Native lifetime checks complement image expressions without adding a second VM. */
static bool neo_test_lifetime(neo_vm *vm, neo_test_allocator *allocator) {
    const neo_capability *root = NULL, *source = NULL, *child = NULL, *copy = NULL, *copied_child = NULL, *edge = NULL;
    neo_diagnostic error = {0};
    neo_status status = neo_image_parse(vm, "(image (prototype #p (value #v 7) @inside #v 1))", &root, &error);
    if (status == NEO_OK) { status = neo_object_child(vm, root, "prototype", &source); }
    if (status == NEO_OK) { status = neo_object_child(vm, source, "value", &child); }
    if (status == NEO_OK) {
        allocator->fail_at = allocator->calls + 1;
        neo_status failed = neo_object_copy(vm, source, root, "failed", &copy);
        allocator->fail_at = 0;
        if (failed != NEO_OUT_OF_MEMORY || copy != NULL) { status = NEO_BAD_STATE; }
    }
    if (status == NEO_OK) { status = neo_object_copy(vm, source, root, "copy", &copy); }
    if (status == NEO_OK) { status = neo_object_child(vm, copy, "value", &copied_child); }
    if (status == NEO_OK) { status = neo_object_connection(vm, copy, "inside", &edge); }
    neo_object_id a = 0, b = 0;
    if (status == NEO_OK) { status = neo_object_identity(vm, copied_child, &a); }
    if (status == NEO_OK) { status = neo_object_identity(vm, edge, &b); }
    if (status == NEO_OK && a != b) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_object_delete(vm, source); }
    neo_value value = {0};
    if (status == NEO_OK && neo_object_read(vm, child, &value) != NEO_UNAVAILABLE) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_object_read(vm, copied_child, &value); }
    if (status == NEO_OK && (value.kind != NEO_INTEGER || value.integer != 7)) { status = NEO_BAD_STATE; }
    const neo_capability *invalid = NULL;
    if (status == NEO_OK && neo_object_create(vm, root, "ref", (neo_value){.kind=NEO_REFERENCE,.reference=copy}, &invalid) != NEO_INVALID) { status = NEO_BAD_STATE; }
    char *text = NULL;
    const neo_capability *restored = NULL;
    if (status == NEO_OK) { status = neo_image_format(vm, root, &text); }
    if (status == NEO_OK) { status = neo_image_parse(vm, text, &restored, &error); }
    neo_image_text_free(vm, text);
    if (restored != NULL) { (void)neo_image_unload(vm, restored); }
    if (root != NULL) { (void)neo_image_unload(vm, root); }
    if (status != NEO_OK) { fprintf(stderr, "FAIL lifetime/copy/persistence: %s\n", neo_status_name(status)); }
    return status == NEO_OK;
}

typedef struct neo_test_output {
    char bytes[4096];
    size_t count;
    size_t destroyed;
} neo_test_output;

static neo_status neo_test_output_write(void *context, const uint8_t *bytes, size_t count, neo_io_result *out) {
    neo_test_output *output = context;
    size_t partial = count > 7 ? 7 : count;
    if (partial >= sizeof(output->bytes) - output->count) { return NEO_LIMIT; }
    memcpy(output->bytes + output->count, bytes, partial);
    output->count += partial;
    output->bytes[output->count] = '\0';
    *out = (neo_io_result){.state=NEO_IO_TRANSFERRED,.count=partial};
    return NEO_OK;
}
static void neo_test_output_destroy(void *context) { ++((neo_test_output *)context)->destroyed; }

static bool neo_test_os_image(neo_vm *vm) {
    const neo_capability *root = NULL, *browser = NULL, *stream = NULL, *workspace = NULL;
    const neo_capability *vector = NULL, *left = NULL, *right = NULL;
    neo_diagnostic error = {0};
    neo_status status = neo_platform_load_image(vm, "neo/neo-os/image.neo", &root, &error);
    neo_test_output output = {0};
    neo_stream_backend backend = {.write=neo_test_output_write,.destroy=neo_test_output_destroy};
    char *serialized = NULL;
    const neo_capability *restored = NULL;
    if (status == NEO_OK) { status = neo_image_format(vm, root, &serialized); }
    if (status == NEO_OK) { status = neo_image_parse(vm, serialized, &restored, &error); }
    neo_image_text_free(vm, serialized);
    if (restored != NULL) { (void)neo_image_unload(vm, restored); }
    if (status == NEO_OK) { status = neo_object_child(vm, root, "browser", &browser); }
    if (status == NEO_OK) { status = neo_stream_create(vm, root, "output", &backend, &output, &stream); }
    if (status == NEO_OK) { status = neo_object_connect(vm, browser, "stdout", stream, NEO_WRITE); }
    neo_execution report = {0};
    if (status == NEO_OK) { status = neo_behavior_run(vm, browser, "demo", NULL, 1000000, &report); }
    neo_execution_release(vm, &report);
    const char *expected =
        "Project Substrate - live prototype browser\n"
        "package maths\n  prototype vector | children: 5\n"
        "package spatial\n  prototype point | children: 3\n"
        "Workspace objects: 0\n4D dot product: 70\n4D point shape valid: true\n"
        "Original point dimension: 3\nIndependent workspace copies: 2\n";
    if (status == NEO_OK && strcmp(output.bytes, expected) != 0) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_object_child(vm, root, "workspace", &workspace); }
    if (status == NEO_OK) { status = neo_object_child(vm, workspace, "vector-copy", &vector); }
    if (status == NEO_OK) { status = neo_object_child(vm, vector, "left-values", &left); }
    if (status == NEO_OK) { status = neo_object_child(vm, vector, "right-values", &right); }
    const int64_t entries[] = {1,2,3,4,5,6,7};
    const size_t dimensions[] = {0,2,3,7};
    const int64_t sums[] = {0,5,14,140};
    for (size_t i = 0; status == NEO_OK && i < 4; ++i) {
        neo_value values = {.kind=NEO_INTEGERS,.integers=entries,.count=dimensions[i]};
        status = neo_object_write(vm, left, values);
        if (status == NEO_OK) { status = neo_object_write(vm, right, values); }
        if (status == NEO_OK) { status = neo_behavior_run(vm, vector, "dot", NULL, 10000, &report); }
        if (status == NEO_OK && (report.result.kind != NEO_INTEGER || report.result.integer != sums[i])) { status = NEO_BAD_STATE; }
        neo_execution_release(vm, &report);
    }
    if (status == NEO_OK) { status = neo_object_write(vm, right, (neo_value){.kind=NEO_INTEGERS,.integers=entries,.count=1}); }
    if (status == NEO_OK && neo_behavior_run(vm, vector, "dot", NULL, 10000, &report) != NEO_RAISED) { status = NEO_BAD_STATE; }
    neo_execution_release(vm, &report);
    const int64_t large[] = {INT64_MAX}, two[] = {2};
    if (status == NEO_OK) { status = neo_object_write(vm, left, (neo_value){.kind=NEO_INTEGERS,.integers=large,.count=1}); }
    if (status == NEO_OK) { status = neo_object_write(vm, right, (neo_value){.kind=NEO_INTEGERS,.integers=two,.count=1}); }
    if (status == NEO_OK && neo_behavior_run(vm, vector, "dot", NULL, 10000, &report) != NEO_OVERFLOW) { status = NEO_BAD_STATE; }
    neo_execution_release(vm, &report);
    if (root != NULL) { (void)neo_image_unload(vm, root); }
    if (status == NEO_OK && output.destroyed != 1) { status = NEO_BAD_STATE; }
    if (status != NEO_OK) { fprintf(stderr, "FAIL OS image: %s (%s)\n", neo_status_name(status), error.message); }
    return status == NEO_OK;
}

int neo_cli_selftest(void) {
    neo_test_allocator memory = {0};
    neo_allocator allocator = {&memory, neo_test_allocate, neo_test_release};
    neo_vm *vm = NULL;
    if (neo_vm_create(&allocator, &vm) != NEO_OK) { return EXIT_FAILURE; }
    neo_platform_services platform;
    neo_status setup = neo_platform_native_services(NEO_PLATFORM_HOST_FILES, &platform);
    if (setup == NEO_OK) { setup = neo_vm_install_platform(vm, &platform); }
    if (setup != NEO_OK) { neo_vm_destroy(vm); return EXIT_FAILURE; }
    size_t failed = 0;
    for (size_t i = 0; i < sizeof(neo_cases) / sizeof(neo_cases[0]); ++i) {
        if (!neo_test_expression(vm, &neo_cases[i])) { ++failed; }
    }
    if (!neo_test_lifetime(vm, &memory)) { ++failed; }
    if (!neo_test_os_image(vm)) { ++failed; }
    neo_vm_destroy(vm);
    if (memory.live != 0) { fprintf(stderr, "FAIL: %zu unreleased allocations\n", memory.live); ++failed; }
    if (failed != 0) { fprintf(stderr, "%zu selftest groups failed\n", failed); return EXIT_FAILURE; }
    printf("PASS: %zu expression cases; authority, lifetime, copy remapping, allocation failure, persistence, balanced ownership; OS browser, partial writes, dimensional maths\n", sizeof(neo_cases) / sizeof(neo_cases[0]));
    return EXIT_SUCCESS;
}
