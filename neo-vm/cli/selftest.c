#include "cli/cli.h"
#include "objects/objects.h"
#include "execution/execution.h"
#include "image/image.h"
#include "platform/platform.h"
#include "io/io.h"
#include "display/headless.h"
#include "display/text.h"
#include "packages/packages.h"
#include "replication/replication.h"
#ifdef NEO_PLATFORM_WITH_WIN32
#include "platform/win32/tls.h"
#include "platform/win32/win32.h"
#endif

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
    {"text raw UTF8 bytes", "(text-bytes (value \"A\xc4\xb1\"))", NEO_OK, {.kind=NEO_INTEGERS,.integers=(int64_t[]){65,196,177},.count=3}},
    {"empty text bytes", "(text-bytes (value \"\"))", NEO_OK, {.kind=NEO_INTEGERS}},
    {"text bytes wrong kind", "(text-bytes (value 1))", NEO_WRONG_KIND, {0}},
    {"concatenate text", "(text-concat (left \"instance-\") (right (to-text (value 7))))", NEO_OK, {.kind=NEO_TEXT,.text="instance-7"}},
    {"concatenate empty text", "(text-concat (left \"\") (right \"\"))", NEO_OK, {.kind=NEO_TEXT,.text=""}},
    {"concatenate wrong kind", "(text-concat (left \"a\") (right 7))", NEO_WRONG_KIND, {0}},
    {"allocate filled array", "(array-new (size 3) (value -7))", NEO_OK, {.kind=NEO_INTEGERS,.integers=(const int64_t[]){-7,-7,-7},.count=3}},
    {"allocate empty array", "(array-new (size 0) (value 4))", NEO_OK, {.kind=NEO_INTEGERS,.count=0}},
    {"negative array size", "(array-new (size -1) (value 0))", NEO_LIMIT, {0}},
    {"array allocation budget", "(array-new (size 1000000) (value 0))", NEO_LIMIT, {0}},
    {"array allocation kind", "(array-new (size true) (value 0))", NEO_WRONG_KIND, {0}},

    {"direct if predicate", "(if (== (left 1) (right 1)) (then 42) (else (fail)))", NEO_OK, {.kind=NEO_INTEGER,.integer=42}},
    {"direct predicate type", "(if (probe 7) (then 1) (else 2))", NEO_WRONG_KIND, {0}},
    {"missing direct predicate", "(if (then 1) (else 2))", NEO_INVALID, {0}},
    {"direct while predicate", "(do (while (< (left (read (slot \"value\"))) (right 9)) (body (write (slot \"value\") (value (add (left (read (slot \"value\"))) (right 1)))))) (read (slot \"value\")))", NEO_OK, {.kind=NEO_INTEGER,.integer=9}},
    {"unwrapped sequence order", "(do (write (slot \"value\") (value 10)) (write (slot \"value\") (value 11)) (read (slot \"value\")))", NEO_OK, {.kind=NEO_INTEGER,.integer=11}},
    {"ordinary duplicate names rejected", "(data (n 1) (n 2))", NEO_CONFLICT, {0}},
    {"data named do stays ordinary", "(do :object (n 1) (n 2))", NEO_CONFLICT, {0}},
    {"recovery result", "(attempt (body (div (left 1) (right 0))) (recover 42))", NEO_OK, {.kind=NEO_INTEGER,.integer=42}},
    {"successful body skips recovery", "(attempt (body 9) (recover (fail)))", NEO_OK, {.kind=NEO_INTEGER,.integer=9}},
    {"failure status", "(attempt (body (fail)) (recover (failure (field \"status\"))))", NEO_OK, {.kind=NEO_TEXT,.text="explicit failure"}},
    {"failure receiver reference", "(attempt (body (fail)) (recover (same (left (self)) (right (failure (field \"receiver-reference\") (target (self)))))))", NEO_OK, {.kind=NEO_BOOLEAN,.boolean=true}},
    {"failure operation inspection", "(attempt (body (fail)) (recover (object-kind (target (failure (field \"operation-reference\") (target (self)))))))", NEO_OK, {.kind=NEO_TEXT,.text="primitive"}},
    {"failure reference requires READ", "(attempt (body (fail)) (recover (failure (field \"operation-reference\") (target (restrict (target (self)) (rights 0))))))", NEO_DENIED, {0}},
    {"failure scope cannot be an ID", "(attempt (body (fail)) (recover (failure (field \"receiver-reference\") (target (failure (field \"receiver\"))))))", NEO_WRONG_KIND, {0}},
    {"failure scope must contain location", "(attempt (body (fail)) (recover (failure (field \"receiver-reference\") (target (connection (target (self)) (name \"vault\"))))))", NEO_DENIED, {0}},
    {"failure reference cannot write", "(attempt (body (fail)) (recover (object-write (target (failure (field \"receiver-reference\") (target (self)))) (value 7))))", NEO_DENIED, {0}},
    {"failure reference cannot act", "(attempt (body (fail)) (recover (invoke (target (failure (field \"receiver-reference\") (target (self)))) (selector \"helper\"))))", NEO_DENIED, {0}},
    {"failure reference cannot escalate", "(attempt (body (fail)) (recover (restrict (target (failure (field \"receiver-reference\") (target (self)))) (rights 255))))", NEO_DENIED, {0}},
    {"release cannot use an ordinary reference", "(message-release (target (self)))", NEO_WRONG_KIND, {0}},
    {"nested missing-handler location", "(attempt (body (invoke (target (connection (target (self)) (name \"vault\"))) (selector \"absent\"))) (recover (object-name (target (failure (field \"receiver-reference\") (target (connection (target (self)) (name \"vault\"))))))))", NEO_OK, {.kind=NEO_TEXT,.text="vault"}},
    {"failure operation reference outside recovery", "(failure (field \"operation-reference\") (target (self)))", NEO_BAD_STATE, {0}},
    {"rethrow", "(attempt (body (div (left 1) (right 0))) (recover (rethrow)))", NEO_DIVIDE_BY_ZERO, {0}},
    {"failure outside recovery", "(failure (field \"status\"))", NEO_BAD_STATE, {0}},
    {"rethrow outside recovery", "(rethrow)", NEO_BAD_STATE, {0}},
    {"recovery keeps mutations", "(attempt (body (do (first (write (slot \"value\") (value 31))) (last (fail)))) (recover (read (slot \"value\"))))", NEO_OK, {.kind=NEO_INTEGER,.integer=31}},
    {"recovery new failure", "(attempt (body (fail)) (recover (div (left 1) (right 0))))", NEO_DIVIDE_BY_ZERO, {0}},
    {"recovery return", "(attempt (body (fail)) (recover (return (value 19))))", NEO_OK, {.kind=NEO_INTEGER,.integer=19}},
    {"nested recovery restores diagnostic", "(attempt (body (fail)) (recover (do (first (attempt (body (div (left 1) (right 0))) (recover 0))) (last (failure (field \"status\"))))))", NEO_OK, {.kind=NEO_TEXT,.text="explicit failure"}},
    {"budget cannot be caught", "(attempt (body (while (condition true) (body 0))) (recover 42))", NEO_LIMIT, {0}},
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
    /* Live rename must preserve identity/links and publish its namespace atomically. */
    const neo_capability *renamed=NULL,*read_only=NULL;
    if (status == NEO_OK) {
        allocator->fail_at=allocator->calls+1;
        neo_status failed=neo_object_rename(vm,copied_child,"renamed");
        allocator->fail_at=0;
        if (failed != NEO_OUT_OF_MEMORY || neo_object_child(vm,copy,"value",&renamed) != NEO_OK) { status=NEO_BAD_STATE; }
    }
    if (status == NEO_OK) { status=neo_capability_restrict(vm,copied_child,NEO_READ,&read_only); }
    if (status == NEO_OK && neo_object_rename(vm,read_only,"renamed") != NEO_DENIED) { status=NEO_BAD_STATE; }
    if (status == NEO_OK) { status=neo_object_rename(vm,copied_child,"renamed"); }
    if (status == NEO_OK && (neo_object_child(vm,copy,"value",&renamed) != NEO_UNAVAILABLE ||
        neo_object_child(vm,copy,"renamed",&renamed) != NEO_OK || neo_object_identity(vm,renamed,&b) != NEO_OK || a != b)) { status=NEO_BAD_STATE; }
    uintptr_t forged=0;
    if (status == NEO_OK && neo_object_read(vm,(const neo_capability *)&forged,&value) != NEO_DENIED) { status=NEO_BAD_STATE; }
    neo_vm *foreign=NULL;
    if (status == NEO_OK) { status=neo_vm_create(NULL,&foreign); }
    if (status == NEO_OK && neo_object_read(foreign,copied_child,&value) != NEO_DENIED) { status=NEO_BAD_STATE; }
    neo_vm_destroy(foreign);
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

static bool neo_test_boot_image(neo_vm *vm) {
    const neo_capability *root = NULL, *browser = NULL, *stream = NULL, *packages = NULL, *system = NULL, *workspace = NULL;
    neo_diagnostic error = {0};
    neo_status status = neo_platform_load_image(vm, "neo/neo-os/image.neo", &root, &error);
    neo_test_output output = {0};
    neo_stream_backend backend = {.write=neo_test_output_write,.destroy=neo_test_output_destroy};
    size_t count = 0;
    if (status == NEO_OK) { status = neo_object_child(vm, root, "workspace", &workspace); }
    if (status == NEO_OK) { status = neo_object_child_count(vm, workspace, &count); }
    if (status == NEO_OK && count != 0) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_object_child(vm, root, "packages", &packages); }
    if (status == NEO_OK) { status = neo_object_child_count(vm, packages, &count); }
    if (status == NEO_OK && count != 1) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_object_child(vm, packages, "system", &system); }
    if (status == NEO_OK) { status = neo_object_child_count(vm, system, &count); }
    if (status == NEO_OK && count != 2) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_object_child(vm, root, "browser", &browser); }
    if (status == NEO_OK) { status = neo_stream_create(vm, root, "output", &backend, &output, &stream); }
    if (status == NEO_OK) { status = neo_object_connect(vm, browser, "stdout", stream, NEO_WRITE); }
    neo_execution report = {0};
    if (status == NEO_OK) { status = neo_behavior_run(vm, browser, "main", NULL, 100000, &report); }
    neo_execution_release(vm, &report);
    if (status == NEO_OK && strcmp(output.bytes,
        "neo system catalogue\npackage system\n  prototype environment | children: 12\n"
        "  prototype scheduler | children: 17\nWorkspace objects: 0\n") != 0) { status = NEO_BAD_STATE; }
    if (root != NULL) { (void)neo_image_unload(vm, root); }
    if (status != NEO_OK || output.destroyed != 1) { fputs("FAIL clean OS boot image\n", stderr); return false; }
    return true;
}

static bool neo_test_os_image(neo_vm *vm) {
    const neo_capability *root = NULL, *browser = NULL, *stream = NULL, *workspace = NULL;
    const neo_capability *vector = NULL, *left = NULL, *right = NULL;
    neo_diagnostic error = {0};
    neo_status status = neo_platform_load_image(vm, "neo/fixtures/substrate-tests.neo", &root, &error);
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
        "package system\n  prototype counter | children: 4\n"
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
    if (status != NEO_OK) { fprintf(stderr, "FAIL substrate fixture: %s (%s)\n", neo_status_name(status), error.message); }
    return status == NEO_OK;
}

static bool neo_test_message_language(neo_vm *vm) {
    const char *source = "(test (maker @receiver #r 255 @watcher #w 255 (handlers (create (body (message-link (target (message-create (ether (connection (target (self)) (name \"ether\"))) (receiver (connection (target (self)) (name \"receiver\"))) (value \"open\"))) (name \"request\"))))(grant (body (message-grant (target (connection (target (self)) (name \"request\"))) (actor (connection (target (self)) (name \"watcher\"))) (rights 1))))(submit (body (message-submit (target (connection (target (self)) (name \"request\"))))))(edit (body (message-write (target (connection (target (self)) (name \"request\"))) (path \"\") (value \"close\"))))(field (body (message-field (target (connection (target (self)) (name \"request\"))) (path \"\") (name \"amount\") (value 7))))(state (body (message-info (target (connection (target (self)) (name \"request\"))) (field \"state\"))))(accept (body (message-accept (target (connection (target (self)) (name \"request\"))))))(bypass (body (inspect (target (connection (target (self)) (name \"request\"))))))(read (body (message-read (target (connection (target (self)) (name \"request\"))) (path \"amount\"))))(unlink (body (disconnect (target (self)) (name \"request\"))))))(receiver #r (handlers (accept (body (message-accept (target (message-pending (ether (connection (target (self)) (name \"ether\"))))))))(begin (body (message-link (target (message-begin)) (name \"current\"))))(read (body (message-read (target (connection (target (self)) (name \"current\"))) (path \"\"))))(edit (body (message-write (target (connection (target (self)) (name \"current\"))) (path \"\") (value \"bad\"))))(finish (body (message-finish (target (connection (target (self)) (name \"current\"))))))(retry (body (message-retry)))(unlink (body (disconnect (target (self)) (name \"current\"))))(early-finish (body (message-finish (target (message-active)))))))(watcher #w (handlers (read (body (message-read (target (child-at (target (connection (target (self)) (name \"ether\"))) (index 0))) (path \"\"))))(inspect (body (message-info (target (child-at (target (connection (target (self)) (name \"ether\"))) (index 0))) (field \"state\"))))(edit (body (message-write (target (child-at (target (connection (target (self)) (name \"ether\"))) (index 0))) (path \"\") (value \"bad\"))))))(intruder  (handlers (read (body (message-read (target (child-at (target (connection (target (self)) (name \"ether\"))) (index 0))) (path \"\"))))(inspect (body (message-info (target (child-at (target (connection (target (self)) (name \"ether\"))) (index 0))) (field \"state\"))))(edit (body (message-write (target (child-at (target (connection (target (self)) (name \"ether\"))) (index 0))) (path \"\") (value \"bad\")))))))";
    const neo_capability *root = NULL, *ether = NULL;
    neo_diagnostic diagnostic = {0};
    neo_status status = neo_image_parse(vm, source, &root, &diagnostic);
    if (status == NEO_OK) { status = neo_ether_create(vm, root, &ether); }
    const char *names[] = {"maker", "receiver", "watcher", "intruder"};
    for (size_t i = 0; status == NEO_OK && i < 4; ++i) {
        const neo_capability *actor = NULL;
        status = neo_object_child(vm, root, names[i], &actor);
        if (status == NEO_OK) { status = neo_object_connect(vm, actor, "ether", ether, NEO_READ); }
    }
    struct { const char *actor; const char *selector; neo_status expected; const char *text; } cases[] = {
        {"maker","create",NEO_OK,NULL},
        {"maker","field",NEO_OK,NULL},
        {"maker","grant",NEO_OK,NULL},
        {"maker","state",NEO_OK,"created"},
        {"intruder","read",NEO_DENIED,NULL},
        {"watcher","read",NEO_OK,"open"},
        {"watcher","inspect",NEO_DENIED,NULL},
        {"watcher","edit",NEO_DENIED,NULL},
        {"maker","bypass",NEO_DENIED,NULL},
        {"maker","submit",NEO_OK,NULL},
        {"maker","submit",NEO_BAD_STATE,NULL},
        {"maker","grant",NEO_BAD_STATE,NULL},
        {"maker","edit",NEO_OK,NULL},
        {"maker","state",NEO_OK,"pending"},
        {"maker","accept",NEO_DENIED,NULL},
        {"receiver","accept",NEO_OK,NULL},
        {"maker","state",NEO_OK,"accepted"},
        {"maker","edit",NEO_DENIED,NULL},
        {"maker","field",NEO_DENIED,NULL},
        {"receiver","begin",NEO_OK,NULL},
        {"receiver","read",NEO_OK,"close"},
        {"receiver","edit",NEO_DENIED,NULL},
        {"receiver","retry",NEO_OK,NULL},
        {"receiver","unlink",NEO_OK,NULL},
        {"receiver","begin",NEO_OK,NULL},
        {"receiver","finish",NEO_OK,NULL},
        {"maker","state",NEO_OK,"processed"},
        {"receiver","finish",NEO_BAD_STATE,NULL},
        {"receiver","begin",NEO_UNAVAILABLE,NULL},
        {"maker","unlink",NEO_OK,NULL},
    };
    for (size_t i = 0; status == NEO_OK && i < sizeof(cases)/sizeof(cases[0]); ++i) {
        const neo_capability *actor = NULL;
        status = neo_object_child(vm, root, cases[i].actor, &actor);
        neo_execution report = {0};
        if (status == NEO_OK) {
            neo_status actual = neo_behavior_run(vm, actor, cases[i].selector, NULL, 10000, &report);
            if (actual != cases[i].expected || (cases[i].text != NULL &&
                (report.result.kind != NEO_TEXT || strcmp(report.result.text, cases[i].text) != 0))) {
                fprintf(stderr, "FAIL message primitive %zu %s/%s: %s\n", i, cases[i].actor, cases[i].selector, neo_status_name(actual));
                status = NEO_BAD_STATE;
            }
        }
        neo_execution_release(vm, &report);
    }
    /* A handler may not finish the message owned by its scheduler frame. */
    const neo_capability *maker = NULL, *receiver = NULL;
    const neo_context *context = NULL;
    const neo_message *managed = NULL;
    if (status == NEO_OK) { status = neo_object_child(vm, root, "maker", &maker); }
    if (status == NEO_OK) { status = neo_object_child(vm, root, "receiver", &receiver); }
    if (status == NEO_OK) { status = neo_context_create(vm, maker, &context); }
    if (status == NEO_OK) { status = neo_message_create(vm, context, ether, receiver, (neo_value){.kind=NEO_TEXT,.text="early-finish"}, &managed); }
    if (status == NEO_OK) { status = neo_message_submit(vm, context, managed); }
    if (status == NEO_OK) { status = neo_scheduler_register(vm, receiver); }
    neo_execution failed = {0};
    size_t turns = 0;
    if (status == NEO_OK && neo_scheduler_tick(vm, 10000, &turns, &failed) != NEO_BUSY) { status = NEO_BAD_STATE; }
    neo_execution_release(vm, &failed);
    if (status == NEO_OK) { status = neo_scheduler_recover(vm, receiver, false); }
    if (status == NEO_OK) { status = neo_scheduler_unregister(vm, receiver); }
    if (root != NULL) { (void)neo_image_unload(vm, root); }
    if (status != NEO_OK) { fprintf(stderr, "FAIL message language: %s %s\n", neo_status_name(status), diagnostic.message); }
    return status == NEO_OK;
}

static bool neo_test_graph_scheduler(neo_vm *vm) {
    const neo_capability *root = NULL, *packages = NULL, *system = NULL, *workspace = NULL;
    const neo_capability *prototype = NULL, *scheduler = NULL, *bad = NULL, *good = NULL, *entries = NULL;
    neo_diagnostic error = {0};
    neo_status status = neo_platform_load_image(vm, "neo/neo-os/image.neo", &root, &error);
    if (status == NEO_OK) { status = neo_object_child(vm, root, "packages", &packages); }
    if (status == NEO_OK) { status = neo_object_child(vm, packages, "system", &system); }
    if (status == NEO_OK) { status = neo_object_child(vm, root, "workspace", &workspace); }
    if (status == NEO_OK) { status = neo_object_child(vm, system, "scheduler", &prototype); }
    if (status == NEO_OK) { status = neo_object_copy(vm, prototype, workspace, "scheduler", &scheduler); }
    const neo_capability *worker_fixture = NULL;
    if (status == NEO_OK) { status = neo_platform_load_image(vm, "neo/fixtures/counter.neo", &worker_fixture, &error); }
    if (status == NEO_OK) { status = neo_object_import(vm, worker_fixture, workspace, "bad", &bad); }
    if (status == NEO_OK) { status = neo_object_import(vm, worker_fixture, workspace, "good", &good); }
    if (worker_fixture != NULL) { (void)neo_image_unload(vm, worker_fixture); }
    const neo_capability *increment = NULL;
    if (status == NEO_OK) { status = neo_object_child(vm, bad, "increment", &increment); }
    if (status == NEO_OK) { status = neo_object_write(vm, increment, (neo_value){.kind=NEO_TEXT,.text="broken"}); }
    if (status == NEO_OK) { status = neo_object_child(vm, scheduler, "entries", &entries); }
    const neo_capability *bad_entry = NULL;
    for (size_t i = 0; status == NEO_OK && i < 2; ++i) {
        const neo_capability *entry = NULL, *field = NULL;
        status = neo_object_create(vm, entries, i == 0 ? "bad" : "good", (neo_value){0}, &entry);
        if (i == 0) { bad_entry = entry; }
        if (status == NEO_OK) { status = neo_object_create(vm, entry, "enabled", (neo_value){.kind=NEO_BOOLEAN,.boolean=true}, &field); }
        if (status == NEO_OK) { status = neo_object_create(vm, entry, "periodic", (neo_value){.kind=NEO_BOOLEAN,.boolean=true}, &field); }
        if (status == NEO_OK) { status = neo_object_create(vm, entry, "error", (neo_value){.kind=NEO_TEXT,.text=""}, &field); }
        if (status == NEO_OK) { status = neo_object_connect(vm, entry, "target", i == 0 ? bad : good, NEO_READ | NEO_WRITE | NEO_ACT); }
    }
    neo_execution report = {0};
    if (status == NEO_OK) { status = neo_behavior_run(vm, scheduler, "tick", NULL, 10000, &report); }
    neo_execution_release(vm, &report);
    const neo_capability *field = NULL;
    neo_value value = {0};
    if (status == NEO_OK) { status = neo_object_child(vm, good, "value", &field); }
    if (status == NEO_OK) { status = neo_object_read(vm, field, &value); }
    if (status == NEO_OK && (value.kind != NEO_INTEGER || value.integer != 1)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_object_child(vm, bad_entry, "enabled", &field); }
    if (status == NEO_OK) { status = neo_object_read(vm, field, &value); }
    if (status == NEO_OK && (value.kind != NEO_BOOLEAN || value.boolean)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_object_child(vm, bad_entry, "error", &field); }
    if (status == NEO_OK) { status = neo_object_read(vm, field, &value); }
    if (status == NEO_OK && (value.kind != NEO_TEXT || strcmp(value.text, neo_status_name(NEO_WRONG_KIND)) != 0)) { status = NEO_BAD_STATE; }
    char *saved = NULL;
    const neo_capability *copy = NULL;
    if (status == NEO_OK) { status = neo_image_format(vm, root, &saved); }
    if (status == NEO_OK) { status = neo_image_parse(vm, saved, &copy, &error); }
    neo_image_text_free(vm, saved);
    if (copy != NULL) { (void)neo_image_unload(vm, copy); }
    const neo_capability *ether = NULL;
    const neo_context *context = NULL;
    const neo_message *message = NULL;
    if (status == NEO_OK) { status = neo_ether_create(vm, root, &ether); }
    if (status == NEO_OK) { status = neo_context_create(vm, bad, &context); }
    if (status == NEO_OK) { status = neo_message_create(vm, context, ether, good, (neo_value){.kind=NEO_TEXT,.text="reset"}, &message); }
    if (status == NEO_OK) { status = neo_message_submit(vm, context, message); }
    if (status == NEO_OK) { status = neo_behavior_run(vm, scheduler, "tick", NULL, 10000, &report); }
    neo_execution_release(vm, &report);
    neo_message_info info = {0};
    if (status == NEO_OK) { status = neo_message_inspect(vm, context, message, &info); }
    if (status == NEO_OK && info.state != NEO_MESSAGE_PROCESSED) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_object_child(vm, good, "value", &field); }
    if (status == NEO_OK) { status = neo_object_read(vm, field, &value); }
    if (status == NEO_OK && value.integer != 1) { status = NEO_BAD_STATE; }
    if (root != NULL) { (void)neo_image_unload(vm, root); }
    if (status != NEO_OK) { fprintf(stderr, "FAIL graph scheduler: %s %s\n", neo_status_name(status), error.message); }
    return status == NEO_OK;
}

/* Audit the existing checkpoint path separately from ordinary graph formatting.
 * Round-trip an active request, a two-item inbox, pending work, and policy. */
static bool neo_test_message_checkpoint(neo_vm *vm, neo_test_allocator *allocator) {
    const neo_capability *root = NULL, *restored = NULL, *maker = NULL;
    const neo_capability *receiver = NULL, *observer = NULL, *ether = NULL;
    const neo_context *sender_context = NULL, *receiver_context = NULL, *observer_context = NULL;
    const neo_message *messages[6] = {NULL}, *message = NULL;
    neo_diagnostic error = {0};
    neo_status status = neo_image_parse(vm, "(image (maker) (receiver) (observer))", &root, &error);
    const char *invalid[] = {
        "!neo-checkpoint 2\n(image)",
        "!neo-checkpoint\n(image)",
        "!neo-checkpoint 1\n(image (a #same) (b #same))",
        "!neo-checkpoint 1\n(image @bad #missing 255)",
        "!neo-checkpoint 1\n(image (maker #s) (receiver #r) (ETHER !ether (m !message #s #r accepted 1 2)))",
        "!neo-checkpoint 1\n(image (maker #s) (receiver #r) (ETHER !ether (a !message #s #r processing 1 0) (b !message #s #r processing 2 0)))",
        "!neo-checkpoint 1\n(image (maker #s) (receiver #r) (ETHER !ether (a !message #s #r pending 1 0) (b !message #s #r pending 1 0)))"
    };
    for (size_t i = 0; status == NEO_OK && i < sizeof(invalid)/sizeof(invalid[0]); ++i) {
        const neo_capability *rejected = NULL;
        neo_status attempted = neo_image_parse(vm, invalid[i], &rejected, &error);
        if (attempted == NEO_OK || rejected != NULL || (i == 0 && attempted != NEO_UNSUPPORTED)) { status = NEO_BAD_STATE; }
    }
    if (status == NEO_OK) { status = neo_object_child(vm, root, "maker", &maker); }
    if (status == NEO_OK) { status = neo_object_child(vm, root, "receiver", &receiver); }
    if (status == NEO_OK) { status = neo_object_child(vm, root, "observer", &observer); }
    if (status == NEO_OK) { status = neo_context_create(vm, maker, &sender_context); }
    if (status == NEO_OK) { status = neo_context_create(vm, receiver, &receiver_context); }
    if (status == NEO_OK) { status = neo_ether_create(vm, root, &ether); }
    for (size_t i = 0; status == NEO_OK && i < 6; ++i) {
        status = neo_message_create(vm, sender_context, ether, receiver,
            (neo_value){.kind=NEO_INTEGER,.integer=(int64_t)i}, &messages[i]);
        if (status == NEO_OK) { status = neo_message_grant(vm, sender_context, messages[i], observer, NEO_MESSAGE_READ); }
        if (status == NEO_OK && i < 5) { status = neo_message_submit(vm, sender_context, messages[i]); }
        if (status == NEO_OK && i < 4) { status = neo_message_accept(vm, receiver_context, messages[i]); }
        if (status == NEO_OK && i < 2) { status = neo_message_begin(vm, receiver_context, &message); }
        if (status == NEO_OK && i == 0) { status = neo_message_finish(vm, receiver_context, message); }
    }
    char *text = NULL;
    if (status == NEO_OK && neo_image_format(vm, root, &text) != NEO_UNSUPPORTED) { status = NEO_BAD_STATE; }
    neo_image_text_free(vm, text); text = NULL;
    if (status == NEO_OK) { status = neo_image_checkpoint(vm, root, &text); }
    if (status == NEO_OK && strncmp(text, "!neo-checkpoint 1\n", 18) != 0) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && (strstr(text, " processed ") == NULL || strstr(text, " created ") == NULL)) { status = NEO_BAD_STATE; }
    /* Every decoder allocation may fail before publication; the source remains usable. */
    for (size_t offset = 1; status == NEO_OK; ++offset) {
        const neo_capability *trial = NULL;
        allocator->fail_at = allocator->calls + offset;
        neo_status attempted = neo_image_parse(vm, text, &trial, &error);
        allocator->fail_at = 0;
        if (attempted == NEO_OK) { (void)neo_image_unload(vm, trial); break; }
        if (attempted != NEO_OUT_OF_MEMORY || trial != NULL || offset > 4096) { status = NEO_BAD_STATE; }
    }
    if (status == NEO_OK) { status = neo_image_parse(vm, text, &restored, &error); }
    neo_image_text_free(vm, text); text = NULL;
    if (status == NEO_OK) { status = neo_object_child(vm, restored, "receiver", &receiver); }
    if (status == NEO_OK) { status = neo_object_child(vm, restored, "observer", &observer); }
    if (status == NEO_OK) { status = neo_object_child(vm, restored, "ETHER", &ether); }
    if (status == NEO_OK) { status = neo_context_create(vm, receiver, &receiver_context); }
    if (status == NEO_OK) { status = neo_context_create(vm, observer, &observer_context); }
    if (status == NEO_OK) { status = neo_message_active(vm, receiver_context, &message); }
    const neo_message *blocked = NULL;
    if (status == NEO_OK && neo_message_begin(vm, receiver_context, &blocked) != NEO_BUSY) { status = NEO_BAD_STATE; }
    neo_value value = {0};
    if (status == NEO_OK) { status = neo_message_read(vm, observer_context, message, "", &value); }
    if (status == NEO_OK && (value.kind != NEO_INTEGER || value.integer != 1)) { status = NEO_BAD_STATE; }
    neo_message_info info = {0};
    if (status == NEO_OK && neo_message_inspect(vm, observer_context, message, &info) != NEO_DENIED) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && neo_message_write(vm, receiver_context, message, "", (neo_value){0}) != NEO_DENIED) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_message_finish(vm, receiver_context, message); }
    for (int64_t expected = 2; status == NEO_OK && expected <= 4; ++expected) {
        if (expected == 4) {
            status = neo_message_pending(vm, receiver_context, ether, &message);
            if (status == NEO_OK) { status = neo_message_accept(vm, receiver_context, message); }
        }
        if (status == NEO_OK) { status = neo_message_begin(vm, receiver_context, &message); }
        if (status == NEO_OK) { status = neo_message_read(vm, receiver_context, message, "", &value); }
        if (status == NEO_OK && (value.kind != NEO_INTEGER || value.integer != expected)) { status = NEO_BAD_STATE; }
        if (status == NEO_OK) { status = neo_message_finish(vm, receiver_context, message); }
    }
    if (status == NEO_OK && neo_message_begin(vm, receiver_context, &message) != NEO_UNAVAILABLE) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_image_checkpoint(vm, restored, &text); }
    if (status == NEO_OK && strstr(text, " created ") == NULL) { status = NEO_BAD_STATE; }
    neo_image_text_free(vm, text);
    if (restored != NULL) { (void)neo_image_unload(vm, restored); }
    if (root != NULL) { (void)neo_image_unload(vm, root); }
    if (status != NEO_OK) { fprintf(stderr, "FAIL message checkpoint: %s %s\n", neo_status_name(status), error.message); }
    return status == NEO_OK;
}

static bool neo_test_message_call(neo_vm *vm, const neo_capability *actor,
                                   const char *selector, neo_status expected) {
    neo_execution report = {0};
    neo_status status = neo_behavior_run(vm, actor, selector, NULL, 10000, &report);
    neo_execution_release(vm, &report);
    if (status != expected) {
        fprintf(stderr, "FAIL lifecycle handler %s: %s, expected %s\n", selector,
                neo_status_name(status), neo_status_name(expected));
        return false;
    }
    return true;
}

/* Participant loss is exercised in every lifecycle state, including when an
 * explicitly specialized third party is the only surviving cleanup actor. */
static bool neo_test_message_loss(neo_vm *vm, neo_message_state state, unsigned loss) {
    const char *source =
        "(image (maker (handlers (stale (body (message-info (target (connection (target (self)) (name \"request\"))) (field \"state\")))))) "
        "(receiver) (janitor (handlers "
        "(release (body (message-release (target (child-at (target (connection (target (self)) (name \"ether\"))) (index 0)))))))) "
        "(intruder (handlers "
        "(release (body (message-release (target (child-at (target (connection (target (self)) (name \"ether\"))) (index 0)))))))))";
    const neo_capability *root = NULL, *actors[4] = {NULL}, *ether = NULL;
    const neo_context *contexts[4] = {NULL};
    const char *names[] = {"maker", "receiver", "janitor", "intruder"};
    neo_diagnostic error = {0};
    neo_status status = neo_image_parse(vm, source, &root, &error);
    bool okay = status == NEO_OK;
    for (size_t i = 0; okay && i < 4; ++i) {
        okay = neo_object_child(vm, root, names[i], &actors[i]) == NEO_OK &&
            neo_context_create(vm, actors[i], &contexts[i]) == NEO_OK;
    }
    if (okay) { okay = neo_ether_create(vm, root, &ether) == NEO_OK; }
    for (size_t i = 2; okay && i < 4; ++i) {
        okay = neo_object_connect(vm, actors[i], "ether", ether, NEO_READ) == NEO_OK;
    }
    const neo_message *message = NULL, *active = NULL;
    if (okay) { okay = neo_message_create(vm, contexts[0], ether, actors[1], (neo_value){.kind=NEO_TEXT,.text="run"}, &message) == NEO_OK; }
    if (okay) { okay = neo_message_grant(vm, contexts[0], message, actors[2], NEO_MESSAGE_RELEASE | NEO_MESSAGE_INSPECT) == NEO_OK; }
    if (okay) { okay = neo_message_grant(vm, contexts[0], message, actors[3], NEO_MESSAGE_READ | NEO_MESSAGE_INSPECT) == NEO_OK; }
    if (okay) { okay = neo_message_link(vm, contexts[0], actors[0], "request", message) == NEO_OK; }
    if (okay) { okay = neo_message_field_create(vm, contexts[0], message, "", "nested", (neo_value){0}) == NEO_OK; }
    if (okay) { okay = neo_message_field_create(vm, contexts[0], message, "nested", "value", (neo_value){.kind=NEO_INTEGER,.integer=7}) == NEO_OK; }
    if (okay && state >= NEO_MESSAGE_PENDING) { okay = neo_message_submit(vm, contexts[0], message) == NEO_OK; }
    if (okay && state >= NEO_MESSAGE_ACCEPTED) { okay = neo_message_accept(vm, contexts[1], message) == NEO_OK; }
    if (okay && state >= NEO_MESSAGE_PROCESSING) { okay = neo_message_begin(vm, contexts[1], &active) == NEO_OK; }
    if (okay && state == NEO_MESSAGE_PROCESSED) { okay = neo_message_finish(vm, contexts[1], active) == NEO_OK; }
    if (okay && state >= NEO_MESSAGE_ACCEPTED) {
        okay = neo_message_write(vm, contexts[0], message, "nested/value", (neo_value){.kind=NEO_INTEGER,.integer=8}) == NEO_DENIED &&
            neo_message_field_create(vm, contexts[0], message, "nested", "extra", (neo_value){0}) == NEO_DENIED;
    }
    if (okay) { okay = neo_test_message_call(vm, actors[3], "release", NEO_DENIED); }
    if (okay && state == NEO_MESSAGE_CREATED) { okay = neo_message_release(vm, contexts[1], message) == NEO_DENIED; }
    if (okay && state > NEO_MESSAGE_CREATED && state < NEO_MESSAGE_PROCESSED) {
        okay = neo_test_message_call(vm, actors[2], "release", NEO_BUSY);
    }
    /* loss bits: 1 maker, 2 receiver. No message state is silently changed. */
    if (okay && (loss & 1u) != 0) { okay = neo_object_delete(vm, actors[0]) == NEO_OK; }
    if (okay && (loss & 2u) != 0) { okay = neo_object_delete(vm, actors[1]) == NEO_OK; }
    neo_message_info info = {0};
    if (okay) {
        okay = neo_message_inspect(vm, contexts[2], message, &info) == NEO_OK &&
            info.state == state && info.sender_available == ((loss & 1u) == 0) &&
            info.receiver_available == ((loss & 2u) == 0);
    }
    if (okay && (loss & 2u) == 0 && state != NEO_MESSAGE_CREATED && state != NEO_MESSAGE_PROCESSED) {
        /* Sender loss does not cancel delivery. The recipient can complete it. */
        if (state == NEO_MESSAGE_PENDING) { okay = neo_message_accept(vm, contexts[1], message) == NEO_OK; }
        if (okay && state < NEO_MESSAGE_PROCESSING) { okay = neo_message_begin(vm, contexts[1], &active) == NEO_OK; }
        if (okay) { okay = neo_message_finish(vm, contexts[1], message) == NEO_OK; }
    }
    neo_value value = {0};
    if (okay) { okay = neo_message_read(vm, contexts[2], message, "", &value) == NEO_DENIED; }
    if (okay) { okay = neo_test_message_call(vm, actors[2], "release", NEO_OK); }
    if (okay) { okay = neo_message_inspect(vm, contexts[2], message, &info) == NEO_UNAVAILABLE && neo_message_release(vm, contexts[2], message) == NEO_UNAVAILABLE; }
    if (okay && (loss & 1u) == 0) {
        okay = neo_test_message_call(vm, actors[0], "stale", NEO_UNAVAILABLE) &&
            neo_object_disconnect(vm, actors[0], "request") == NEO_OK;
    }
    size_t count = 1;
    if (okay) { okay = neo_object_child_count(vm, ether, &count) == NEO_OK && count == 0; }
    char *text = NULL;
    if (okay) { okay = neo_image_checkpoint(vm, root, &text) == NEO_OK; }
    neo_image_text_free(vm, text);
    if (root != NULL) { (void)neo_image_unload(vm, root); }
    if (!okay) { fprintf(stderr, "FAIL message loss state %d participants %u (%s)\n", (int)state, loss, error.message); }
    return okay;
}

static bool neo_test_dispatch_recovery(neo_vm *vm) {
    const char *source =
        "(image (sender) (receiver #r (n 0) (handlers (broken (body (do (first (write (slot \"n\") (value 9))) "
        "(last (fail))))) (early-release (body (message-release (target (message-active))))) (early-finish (b"
        "ody (message-finish (target (message-active))))) (early-retry (body (message-retry))) (limit (body ("
        "while (condition true) (body 0)))))) (driver @target #r 67 (handlers (run (body (attempt (body (disp"
        "atch (target (connection (target (self)) (name \"target\"))))) (recover (object-name (target (failure "
        "(field \"receiver-reference\") (target (connection (target (self)) (name \"target\")))))))))) (retry (bo"
        "dy (receiver-retry (target (connection (target (self)) (name \"target\")))))) (discard (body (receiver"
        "-discard (target (connection (target (self)) (name \"target\")))))))))";
    const neo_capability *root = NULL, *sender = NULL, *receiver = NULL, *driver = NULL, *ether = NULL;
    const neo_context *context = NULL;
    neo_diagnostic error = {0};
    bool okay = neo_image_parse(vm, source, &root, &error) == NEO_OK;
    if (okay) {
        okay = neo_object_child(vm, root, "sender", &sender) == NEO_OK &&
            neo_object_child(vm, root, "receiver", &receiver) == NEO_OK &&
            neo_object_child(vm, root, "driver", &driver) == NEO_OK &&
            neo_context_create(vm, sender, &context) == NEO_OK && neo_ether_create(vm, root, &ether) == NEO_OK;
    }
    const char *selectors[] = {"missing", "broken", "early-release", "early-finish", "early-retry", "limit", NULL};
    for (size_t i = 0; okay && i < sizeof(selectors)/sizeof(selectors[0]); ++i) {
        const neo_message *message = NULL;
        neo_value selector = selectors[i] == NULL ? (neo_value){.kind=NEO_INTEGER,.integer=3} :
            (neo_value){.kind=NEO_TEXT,.text=selectors[i]};
        okay = neo_message_create(vm, context, ether, receiver, selector, &message) == NEO_OK &&
            neo_message_submit(vm, context, message) == NEO_OK;
        neo_execution report = {0};
        if (okay) {
            neo_status status = neo_behavior_run(vm, driver, "run", NULL, 10000, &report);
            okay = i == 5 ? status == NEO_LIMIT : status == NEO_OK &&
                report.result.kind == NEO_TEXT && strcmp(report.result.text, "receiver") == 0;
        }
        neo_execution_release(vm, &report);
        neo_message_info info = {0};
        if (okay) { okay = neo_message_inspect(vm, context, message, &info) == NEO_OK && info.state == NEO_MESSAGE_PROCESSING; }
        if (okay && i == 0) {
            okay = neo_test_message_call(vm, driver, "retry", NEO_OK) &&
                neo_message_inspect(vm, context, message, &info) == NEO_OK && info.state == NEO_MESSAGE_ACCEPTED &&
                neo_test_message_call(vm, driver, "run", NEO_OK);
        }
        if (okay && i == 1) {
            const neo_capability *field = NULL;
            neo_value value = {0};
            okay = neo_object_child(vm, receiver, "n", &field) == NEO_OK &&
                neo_object_read(vm, field, &value) == NEO_OK && value.kind == NEO_INTEGER && value.integer == 9;
        }
        if (okay) {
            okay = neo_test_message_call(vm, driver, "discard", NEO_OK) &&
                neo_message_inspect(vm, context, message, &info) == NEO_OK && info.state == NEO_MESSAGE_PROCESSED &&
                neo_message_release(vm, context, message) == NEO_OK;
        }
    }
    if (root != NULL) { (void)neo_image_unload(vm, root); }
    if (!okay) { fprintf(stderr, "FAIL dispatch recovery/completion: %s\n", error.message); }
    return okay;
}

static bool neo_test_message_order(neo_vm *vm) {
    const neo_capability *root = NULL, *ether = NULL, *actors[3] = {NULL};
    const neo_context *contexts[3] = {NULL};
    const char *names[] = {"first", "second", "receiver"};
    neo_diagnostic error = {0};
    bool okay = neo_image_parse(vm, "(image (first) (second) (receiver))", &root, &error) == NEO_OK;
    for (size_t i = 0; okay && i < 3; ++i) {
        okay = neo_object_child(vm, root, names[i], &actors[i]) == NEO_OK && neo_context_create(vm, actors[i], &contexts[i]) == NEO_OK;
    }
    if (okay) { okay = neo_ether_create(vm, root, &ether) == NEO_OK; }
    const neo_message *messages[3] = {NULL}, *active = NULL;
    for (size_t i = 0; okay && i < 3; ++i) {
        okay = neo_message_create(vm, contexts[i == 2 ? 1 : 0], ether, actors[2], (neo_value){0}, &messages[i]) == NEO_OK;
        if (okay) { okay = neo_message_submit(vm, contexts[i == 2 ? 1 : 0], messages[i]) == NEO_OK; }
    }
    if (okay) {
        okay = neo_message_accept(vm, contexts[2], messages[1]) == NEO_BUSY &&
            neo_message_accept(vm, contexts[2], messages[2]) == NEO_OK &&
            neo_message_accept(vm, contexts[2], messages[0]) == NEO_OK &&
            neo_message_accept(vm, contexts[2], messages[1]) == NEO_OK;
    }
    const size_t order[] = {2, 0, 1};
    for (size_t i = 0; okay && i < 3; ++i) {
        okay = neo_message_begin(vm, contexts[2], &active) == NEO_OK && active == messages[order[i]];
        const neo_message *blocked = NULL;
        if (okay) { okay = neo_message_begin(vm, contexts[2], &blocked) == NEO_BUSY; }
        if (okay) { okay = neo_message_finish(vm, contexts[2], active) == NEO_OK && neo_message_finish(vm, contexts[2], active) == NEO_BAD_STATE; }
        if (okay) { okay = neo_message_release(vm, contexts[2], active) == NEO_OK; }
    }
    /* RELEASE alone is sufficient for cleanup but grants no observation. */
    const neo_message *unsubmitted = NULL;
    neo_message_info info = {0};
    neo_value value = {0};
    if (okay) {
        okay = neo_message_create(vm, contexts[0], ether, actors[2], (neo_value){0}, &unsubmitted) == NEO_OK &&
            neo_message_grant(vm, contexts[0], unsubmitted, actors[1], NEO_MESSAGE_RELEASE) == NEO_OK &&
            neo_message_inspect(vm, contexts[1], unsubmitted, &info) == NEO_DENIED &&
            neo_message_read(vm, contexts[1], unsubmitted, "", &value) == NEO_DENIED &&
            neo_message_release(vm, contexts[1], unsubmitted) == NEO_OK;
    }
    if (root != NULL) { (void)neo_image_unload(vm, root); }
    if (!okay) { fputs("FAIL multi-sender acceptance/queue order\n", stderr); }
    return okay;
}

static bool neo_test_sequence_namespace(neo_vm *vm) {
    const neo_capability *root = NULL, *steps = NULL, *ambiguous = NULL, *restored = NULL;
    neo_diagnostic error = {0};
    bool okay = neo_image_parse(vm, "(image (steps :primitive do (n 1) (n 2)))", &root, &error) == NEO_OK &&
        neo_object_child(vm, root, "steps", &steps) == NEO_OK &&
        neo_object_child(vm, steps, "n", &ambiguous) == NEO_CONFLICT;
    char *text = NULL;
    if (okay) { okay = neo_image_format(vm, root, &text) == NEO_OK && neo_image_parse(vm, text, &restored, &error) == NEO_OK; }
    neo_image_text_free(vm, text); text = NULL;
    if (okay) { okay = neo_object_write(vm, steps, (neo_value){0}) == NEO_OK && neo_image_format(vm, root, &text) == NEO_CONFLICT; }
    neo_image_text_free(vm, text);
    if (restored != NULL) { (void)neo_image_unload(vm, restored); }
    if (root != NULL) { (void)neo_image_unload(vm, root); }
    if (!okay) { fputs("FAIL ordered-sequence namespace/persistence\n", stderr); }
    return okay;
}

static bool neo_test_closed_import(neo_vm *vm, neo_test_allocator *allocator) {
    const neo_capability *source = NULL, *destination = NULL, *copy = NULL, *child = NULL, *back = NULL, *limited = NULL;
    neo_diagnostic error = {0};
    bool okay = neo_image_parse(vm, "(fixture #root (child @back #root 1))", &source, &error) == NEO_OK &&
        neo_image_create(vm, "destination", &destination) == NEO_OK;
    if (okay) { okay = neo_capability_restrict(vm, source, NEO_READ, &limited) == NEO_OK &&
        neo_object_import(vm, limited, destination, "denied", &copy) == NEO_DENIED; }
    for (size_t offset = 1; okay; ++offset) {
        allocator->fail_at = allocator->calls + offset;
        neo_status status = neo_object_import(vm, source, destination, "copy", &copy);
        allocator->fail_at = 0;
        if (status == NEO_OK) { break; }
        size_t count = 0;
        okay = status == NEO_OUT_OF_MEMORY && copy == NULL && offset < 4096 &&
            neo_object_child_count(vm, destination, &count) == NEO_OK && count == 0;
    }
    neo_object_id expected = 0, actual = 0;
    if (okay) { okay = neo_object_child(vm, copy, "child", &child) == NEO_OK &&
        neo_object_connection(vm, child, "back", &back) == NEO_OK &&
        neo_object_identity(vm, copy, &expected) == NEO_OK && neo_object_identity(vm, back, &actual) == NEO_OK && expected == actual; }
    if (okay) { okay = neo_object_child(vm, source, "child", &child) == NEO_OK &&
        neo_object_import(vm, child, destination, "external", &copy) == NEO_WRONG_IMAGE; }
    if (destination != NULL) { (void)neo_image_unload(vm, destination); }
    if (source != NULL) { (void)neo_image_unload(vm, source); }
    if (!okay) { fputs("FAIL closed graph import/authority/failure/remapping\n", stderr); }
    return okay;
}

static neo_status neo_test_idle_wait(void *context, uint32_t milliseconds) {
    if (milliseconds != 1) { return NEO_INVALID; }
    ++*(size_t *)context;
    return NEO_OK;
}

static bool neo_test_packages(neo_vm *vm, neo_test_allocator *allocator) {
    const neo_capability *root = NULL, *catalogue = NULL, *workspace = NULL, *clock = NULL, *counter = NULL, *record = NULL, *resolved = NULL;
    neo_diagnostic error = {0};
    bool okay = neo_image_parse(vm, "(world (catalogue) (workspace))", &root, &error) == NEO_OK &&
        neo_object_child(vm, root, "catalogue", &catalogue) == NEO_OK && neo_object_child(vm, root, "workspace", &workspace) == NEO_OK &&
        neo_platform_load_image(vm, "neo/fixtures/packages/clock.neo", &clock, &error) == NEO_OK &&
        neo_platform_load_image(vm, "neo/fixtures/packages/counter.neo", &counter, &error) == NEO_OK;
    if (okay) { okay = neo_package_validate(vm, clock) == NEO_OK && neo_package_validate(vm, counter) == NEO_OK &&
        neo_package_install(vm, catalogue, counter, false, &record) == NEO_UNAVAILABLE &&
        neo_package_install(vm, catalogue, clock, false, &record) == NEO_OK; }
    const neo_capability *test_manifest = NULL, *test_field = NULL, *test_imports = NULL, *test_import = NULL;
    if (okay) { okay = neo_object_child(vm, counter, "manifest", &test_manifest) == NEO_OK &&
        neo_object_child(vm, test_manifest, "schema", &test_field) == NEO_OK &&
        neo_object_write(vm, test_field, (neo_value){.kind=NEO_INTEGER,.integer=2}) == NEO_OK &&
        neo_package_validate(vm, counter) == NEO_UNSUPPORTED &&
        neo_object_write(vm, test_field, (neo_value){.kind=NEO_INTEGER,.integer=1}) == NEO_OK &&
        neo_object_create(vm, test_manifest, "unknown", (neo_value){0}, &test_field) == NEO_OK &&
        neo_package_validate(vm, counter) == NEO_INVALID && neo_object_delete(vm, test_field) == NEO_OK &&
        neo_object_child(vm, test_manifest, "imports", &test_imports) == NEO_OK &&
        neo_object_child(vm, test_imports, "clock", &test_import) == NEO_OK && neo_object_child(vm, test_import, "rights", &test_field) == NEO_OK &&
        neo_object_write(vm, test_field, (neo_value){.kind=NEO_INTEGER,.integer=3}) == NEO_OK &&
        neo_package_install(vm, catalogue, counter, false, &record) == NEO_DENIED &&
        neo_object_write(vm, test_field, (neo_value){.kind=NEO_INTEGER,.integer=65}) == NEO_OK; }
    if (okay) { okay = neo_object_child(vm, test_import, "export", &test_field) == NEO_OK &&
        neo_object_write(vm, test_field, (neo_value){.kind=NEO_TEXT,.text="private"}) == NEO_OK &&
        neo_package_install(vm, catalogue, counter, false, &record) == NEO_UNAVAILABLE &&
        neo_object_write(vm, test_field, (neo_value){.kind=NEO_TEXT,.text="clock"}) == NEO_OK; }
    const neo_capability *dependencies = NULL, *dependency = NULL, *pin = NULL;
    if (okay) { okay = neo_object_child(vm, test_manifest, "dependencies", &dependencies) == NEO_OK &&
        neo_object_child(vm, dependencies, "clock-package", &dependency) == NEO_OK && neo_object_child(vm, dependency, "id", &test_field) == NEO_OK &&
        neo_object_write(vm, test_field, (neo_value){.kind=NEO_TEXT,.text="local.counter"}) == NEO_OK && neo_package_validate(vm, counter) == NEO_OK &&
        neo_object_write(vm, test_field, (neo_value){.kind=NEO_TEXT,.text="local.clock"}) == NEO_OK &&
        neo_object_child(vm, dependency, "version", &pin) == NEO_OK && neo_object_child(vm, pin, "patch", &test_field) == NEO_OK &&
        neo_object_write(vm, test_field, (neo_value){.kind=NEO_INTEGER,.integer=1}) == NEO_OK &&
        neo_package_validate(vm, counter) == NEO_OK &&
        neo_object_write(vm, test_field, (neo_value){.kind=NEO_INTEGER,.integer=0}) == NEO_OK; }
    const neo_capability *bindings = NULL, *binding = NULL;
    if (okay) { okay = neo_object_child(vm, test_manifest, "bindings", &bindings) == NEO_OK &&
        neo_object_child(vm, bindings, "counter-clock", &binding) == NEO_OK && neo_object_child(vm, binding, "name", &test_field) == NEO_OK &&
        neo_object_write(vm, test_field, (neo_value){.kind=NEO_TEXT,.text="identity"}) == NEO_OK && neo_package_validate(vm, counter) == NEO_CONFLICT &&
        neo_object_write(vm, test_field, (neo_value){.kind=NEO_TEXT,.text="clock"}) == NEO_OK; }
    const neo_capability *limited = NULL;
    if (okay) { okay = neo_capability_restrict(vm, catalogue, NEO_ALL & ~(unsigned)NEO_DELEGATE, &limited) == NEO_OK &&
        neo_package_install(vm, limited, counter, false, &record) == NEO_DENIED; }
    for (size_t offset = 1; okay; ++offset) {
        allocator->fail_at = allocator->calls + offset;
        neo_status status = neo_package_install(vm, catalogue, counter, false, &record);
        allocator->fail_at = 0;
        if (status == NEO_OK) { break; }
        size_t count = 0;
        okay = status == NEO_OUT_OF_MEMORY && record == NULL && offset < 4096 &&
            neo_object_child_count(vm, catalogue, &count) == NEO_OK && count == 1;
    }
    if (okay) { okay = neo_package_install(vm, catalogue, counter, false, &record) == NEO_CONFLICT &&
        neo_package_resolve(vm, catalogue, "local.counter", "counter", NEO_WRITE, &resolved) == NEO_DENIED &&
        neo_package_resolve(vm, catalogue, "local.counter", "counter", NEO_READ | NEO_COPY, &resolved) == NEO_OK &&
        neo_object_copy(vm, resolved, workspace, "copy", &record) == NEO_OK && neo_object_child(vm, workspace, "copy", &record) == NEO_OK; }
    neo_execution report = {0};
    if (okay) { okay = neo_behavior_run(vm, record, "tick", NULL, 1000, &report) == NEO_OK && report.result.kind == NEO_INTEGER && report.result.integer == 1; }
    neo_execution_release(vm, &report);
    const neo_capability *artifact = NULL;
    if (okay) { okay = neo_package_export(vm, catalogue, "local.counter", &artifact) == NEO_OK && neo_package_validate(vm, artifact) == NEO_OK; }
    if (artifact != NULL) { (void)neo_image_unload(vm, artifact); artifact = NULL; }
    const neo_capability *field = NULL;
    if (okay) { okay = neo_object_child(vm, resolved, "value", &field) == NEO_OK; }
    /* Resolve a manager-owned path for a deliberate installed edit. */
    const neo_capability *installed = NULL, *bundle = NULL, *contents = NULL, *prototype = NULL;
    if (okay) { okay = neo_object_child(vm, catalogue, "local.counter", &installed) == NEO_OK && neo_object_child(vm, installed, "bundle", &bundle) == NEO_OK &&
        neo_object_child(vm, bundle, "contents", &contents) == NEO_OK && neo_object_child(vm, contents, "counter", &prototype) == NEO_OK &&
        neo_object_child(vm, prototype, "value", &field) == NEO_OK && neo_object_write(vm, field, (neo_value){.kind=NEO_INTEGER,.integer=99}) == NEO_OK &&
        neo_package_export(vm, catalogue, "local.counter", &artifact) == NEO_CONFLICT && artifact == NULL &&
        neo_object_write(vm, field, (neo_value){.kind=NEO_INTEGER,.integer=0}) == NEO_OK; }
    const neo_capability *manifest = NULL, *version = NULL;
    if (okay) { okay = neo_object_child(vm, counter, "manifest", &manifest) == NEO_OK && neo_object_child(vm, manifest, "version", &version) == NEO_OK &&
        neo_object_child(vm, version, "patch", &field) == NEO_OK && neo_object_write(vm, field, (neo_value){.kind=NEO_INTEGER,.integer=1}) == NEO_OK &&
        neo_object_connect(vm, workspace, "watch", prototype, NEO_READ) == NEO_OK &&
        neo_package_install(vm, catalogue, counter, true, &installed) == NEO_BUSY && neo_object_disconnect(vm, workspace, "watch") == NEO_OK; }
    for (size_t offset = 1; okay; ++offset) {
        allocator->fail_at = allocator->calls + offset;
        neo_status status = neo_package_install(vm, catalogue, counter, true, &installed);
        allocator->fail_at = 0;
        if (status == NEO_OK) { break; }
        okay = status == NEO_OUT_OF_MEMORY && installed == NULL && offset < 4096;
        const neo_capability *still_old = NULL;
        neo_object_id id = 0, previous = 0;
        if (okay) { okay = neo_package_resolve(vm, catalogue, "local.counter", "counter", NEO_READ, &still_old) == NEO_OK &&
            neo_object_identity(vm, still_old, &id) == NEO_OK && neo_object_identity(vm, prototype, &previous) == NEO_OK && id == previous; }
    }
    if (okay) { okay = neo_package_remove(vm, catalogue, "local.clock") == NEO_BUSY && neo_package_remove(vm, catalogue, "local.counter") == NEO_OK &&
        neo_package_remove(vm, catalogue, "local.clock") == NEO_BUSY && neo_object_delete(vm, record) == NEO_OK &&
        neo_package_remove(vm, catalogue, "local.clock") == NEO_OK; }
    if (counter != NULL) { (void)neo_image_unload(vm, counter); }
    if (clock != NULL) { (void)neo_image_unload(vm, clock); }
    if (root != NULL) { (void)neo_image_unload(vm, root); }
    if (!okay) { fputs("FAIL local packages/schema/staging/bindings/copy/update/removal\n", stderr); }
    return okay;
}

static bool neo_test_vector_package(neo_vm *vm) {
    const neo_capability *root = NULL, *contents = NULL, *vector = NULL;
    const neo_capability *dimension = NULL, *left = NULL, *right = NULL;
    neo_diagnostic error = {0};
    neo_status status = neo_platform_load_image(vm, "neo/packages/vector.neo", &root, &error);
    if (status == NEO_OK) { status = neo_object_child(vm, root, "contents", &contents); }
    if (status == NEO_OK) { status = neo_object_child(vm, contents, "vector", &vector); }
    if (status == NEO_OK) { status = neo_object_child(vm, vector, "dimension", &dimension); }
    if (status == NEO_OK) { status = neo_object_child(vm, vector, "left", &left); }
    if (status == NEO_OK) { status = neo_object_child(vm, vector, "right", &right); }
    const size_t dimensions[] = {0, 1, 2, 3, 4, 7, 33, 100, 257};
    const int64_t magnitudes[] = {0,1000000,1414214,1732051,2000000,2645751,5744563,10000000,16031220};
    const int64_t normalized[] = {0,1000000,707107,577350,500000,377964,174078,100000,62378};
    int64_t values[257];
    for (size_t i = 0; i < 257; ++i) { values[i] = 1000000; }
    for (size_t i = 0; status == NEO_OK && i < sizeof(dimensions) / sizeof(dimensions[0]); ++i) {
        size_t n = dimensions[i];
        status = neo_object_write(vm, dimension, (neo_value){.kind=NEO_INTEGER,.integer=(int64_t)n});
        neo_execution report = {0};
        if (status == NEO_OK) { status = neo_behavior_run(vm, vector, "initialize", NULL, 100000, &report); }
        neo_execution_release(vm, &report);
        if (status == NEO_OK) { status = neo_object_write(vm, left, (neo_value){.kind=NEO_INTEGERS,.integers=values,.count=n}); }
        if (status == NEO_OK) { status = neo_object_write(vm, right, (neo_value){.kind=NEO_INTEGERS,.integers=values,.count=n}); }
        if (status == NEO_OK) { status = neo_behavior_run(vm, vector, "add", NULL, 100000, &report); }
        if (status == NEO_OK && (report.result.kind != NEO_INTEGERS || report.result.count != n)) { status = NEO_BAD_STATE; }
        for (size_t j = 0; status == NEO_OK && j < n; ++j) {
            if (report.result.integers[j] != 2000000) { status = NEO_BAD_STATE; }
        }
        neo_execution_release(vm, &report);
        if (status == NEO_OK) { status = neo_behavior_run(vm, vector, "dot", NULL, 100000, &report); }
        if (status == NEO_OK && (report.result.kind != NEO_INTEGER || report.result.integer != (int64_t)n * 1000000)) { status = NEO_BAD_STATE; }
        neo_execution_release(vm, &report);
        if (status == NEO_OK) { status = neo_behavior_run(vm, vector, "subtract", NULL, 100000, &report); }
        if (status == NEO_OK && (report.result.kind != NEO_INTEGERS || report.result.count != n)) { status = NEO_BAD_STATE; }
        for (size_t j = 0; status == NEO_OK && j < n; ++j) {
            if (report.result.integers[j] != 0) { status = NEO_BAD_STATE; }
        }
        neo_execution_release(vm, &report);
        const neo_capability *factor = NULL;
        if (status == NEO_OK) { status = neo_object_child(vm, vector, "factor", &factor); }
        if (status == NEO_OK) { status = neo_object_write(vm, factor, (neo_value){.kind=NEO_INTEGER,.integer=-500000}); }
        if (status == NEO_OK) { status = neo_behavior_run(vm, vector, "scale", NULL, 100000, &report); }
        if (status == NEO_OK && (report.result.kind != NEO_INTEGERS || report.result.count != n)) { status = NEO_BAD_STATE; }
        for (size_t j = 0; status == NEO_OK && j < n; ++j) {
            if (report.result.integers[j] != -500000) { status = NEO_BAD_STATE; }
        }
        neo_execution_release(vm, &report);
        if (status == NEO_OK) { status = neo_behavior_run(vm, vector, "magnitude", NULL, 100000, &report); }
        if (status == NEO_OK && (report.result.kind != NEO_INTEGER || report.result.integer != magnitudes[i])) { status = NEO_BAD_STATE; }
        neo_execution_release(vm, &report);
        if (status == NEO_OK) {
            neo_status attempted = neo_behavior_run(vm, vector, "normalize", NULL, 100000, &report);
            if (attempted != (n == 0 ? NEO_RAISED : NEO_OK)) { status = NEO_BAD_STATE; }
        }
        if (status == NEO_OK && n != 0 && (report.result.kind != NEO_INTEGERS || report.result.count != n)) { status = NEO_BAD_STATE; }
        for (size_t j = 0; status == NEO_OK && j < n; ++j) {
            if (report.result.integers[j] != normalized[i]) { status = NEO_BAD_STATE; }
        }
        neo_execution_release(vm, &report);
    }
    if (root != NULL) { (void)neo_image_unload(vm, root); }
    if (status != NEO_OK) { fprintf(stderr, "FAIL vector package: %s (%s)\n", neo_status_name(status), error.message); }
    return status == NEO_OK;
}

static bool neo_test_vector_edges(neo_vm *vm) {
    typedef struct neo_vector_case {
        const char *selector;
        int64_t dimension;
        neo_value left, right, factor;
        neo_status status;
        neo_value result;
    } neo_vector_case;
    const neo_vector_case cases[] = {
        {"subtract",2,{.kind=NEO_INTEGERS,.integers=(const int64_t[]){3,-4},.count=2},{.kind=NEO_INTEGERS,.integers=(const int64_t[]){-2,8},.count=2},{.kind=NEO_INTEGER,.integer=1000000},NEO_OK,{.kind=NEO_INTEGERS,.integers=(const int64_t[]){5,-12},.count=2}},
        {"scale",2,{.kind=NEO_INTEGERS,.integers=(const int64_t[]){1,-1},.count=2},{.kind=NEO_INTEGERS},{.kind=NEO_INTEGER,.integer=500000},NEO_OK,{.kind=NEO_INTEGERS,.integers=(const int64_t[]){1,-1},.count=2}},
        {"scale",2,{.kind=NEO_INTEGERS,.integers=(const int64_t[]){1,-1},.count=2},{.kind=NEO_INTEGERS},{.kind=NEO_INTEGER,.integer=-500000},NEO_OK,{.kind=NEO_INTEGERS,.integers=(const int64_t[]){-1,1},.count=2}},
        {"scale",2,{.kind=NEO_INTEGERS,.integers=(const int64_t[]){7,-9},.count=2},{.kind=NEO_INTEGERS},{.kind=NEO_INTEGER},NEO_OK,{.kind=NEO_INTEGERS,.integers=(const int64_t[]){0,0},.count=2}},
        {"magnitude",2,{.kind=NEO_INTEGERS,.integers=(const int64_t[]){3000000,4000000},.count=2},{.kind=NEO_INTEGERS},{.kind=NEO_INTEGER},NEO_OK,{.kind=NEO_INTEGER,.integer=5000000}},
        {"normalize",2,{.kind=NEO_INTEGERS,.integers=(const int64_t[]){3000000,-4000000},.count=2},{.kind=NEO_INTEGERS},{.kind=NEO_INTEGER},NEO_OK,{.kind=NEO_INTEGERS,.integers=(const int64_t[]){600000,-800000},.count=2}},
        {"magnitude",2,{.kind=NEO_INTEGERS,.integers=(const int64_t[]){1,1},.count=2},{.kind=NEO_INTEGERS},{.kind=NEO_INTEGER},NEO_OK,{.kind=NEO_INTEGER,.integer=1}},
        {"normalize",2,{.kind=NEO_INTEGERS,.integers=(const int64_t[]){1,-1},.count=2},{.kind=NEO_INTEGERS},{.kind=NEO_INTEGER},NEO_OK,{.kind=NEO_INTEGERS,.integers=(const int64_t[]){707107,-707107},.count=2}},
        {"normalize",3,{.kind=NEO_INTEGERS,.integers=(const int64_t[]){1,2,2},.count=3},{.kind=NEO_INTEGERS},{.kind=NEO_INTEGER},NEO_OK,{.kind=NEO_INTEGERS,.integers=(const int64_t[]){333333,666667,666667},.count=3}},
        {"magnitude",1,{.kind=NEO_INTEGERS,.integers=(const int64_t[]){3037000499LL},.count=1},{.kind=NEO_INTEGERS},{.kind=NEO_INTEGER},NEO_OK,{.kind=NEO_INTEGER,.integer=3037000499LL}},
        {"normalize",1,{.kind=NEO_INTEGERS,.integers=(const int64_t[]){-3037000499LL},.count=1},{.kind=NEO_INTEGERS},{.kind=NEO_INTEGER},NEO_OK,{.kind=NEO_INTEGERS,.integers=(const int64_t[]){-1000000},.count=1}},
        {"normalize",0,{.kind=NEO_INTEGERS},{.kind=NEO_INTEGERS},{.kind=NEO_INTEGER},NEO_RAISED,{0}},
        {"normalize",2,{.kind=NEO_INTEGERS,.integers=(const int64_t[]){0,0},.count=2},{.kind=NEO_INTEGERS},{.kind=NEO_INTEGER},NEO_RAISED,{0}},
        {"magnitude",2,{.kind=NEO_INTEGERS,.integers=(const int64_t[]){2147483648LL,2147483648LL},.count=2},{.kind=NEO_INTEGERS},{.kind=NEO_INTEGER},NEO_OVERFLOW,{0}},
        {"normalize",1,{.kind=NEO_INTEGERS,.integers=(const int64_t[]){3037000500LL},.count=1},{.kind=NEO_INTEGERS},{.kind=NEO_INTEGER},NEO_OVERFLOW,{0}},
        {"magnitude",1,{.kind=NEO_INTEGERS,.integers=(const int64_t[]){INT64_MIN},.count=1},{.kind=NEO_INTEGERS},{.kind=NEO_INTEGER},NEO_OVERFLOW,{0}},
        {"scale",2,{.kind=NEO_INTEGERS,.integers=(const int64_t[]){1,INT64_MAX},.count=2},{.kind=NEO_INTEGERS},{.kind=NEO_INTEGER,.integer=2},NEO_OVERFLOW,{0}},
        {"subtract",2,{.kind=NEO_INTEGERS,.integers=(const int64_t[]){0,INT64_MIN},.count=2},{.kind=NEO_INTEGERS,.integers=(const int64_t[]){0,1},.count=2},{.kind=NEO_INTEGER},NEO_OVERFLOW,{0}},
        {"scale",0,{.kind=NEO_INTEGERS},{.kind=NEO_INTEGERS},{.kind=NEO_BOOLEAN,.boolean=true},NEO_WRONG_KIND,{0}},
        {"magnitude",2,{.kind=NEO_INTEGERS,.integers=(const int64_t[]){1},.count=1},{.kind=NEO_INTEGERS},{.kind=NEO_INTEGER},NEO_RAISED,{0}},
        {"subtract",1,{.kind=NEO_INTEGERS,.integers=(const int64_t[]){1},.count=1},{.kind=NEO_INTEGERS},{.kind=NEO_INTEGER},NEO_RAISED,{0}},
        {"normalize",-1,{.kind=NEO_INTEGERS},{.kind=NEO_INTEGERS},{.kind=NEO_INTEGER},NEO_RAISED,{0}},
        {"magnitude",0,{.kind=NEO_BOOLEAN},{.kind=NEO_INTEGERS},{.kind=NEO_INTEGER},NEO_WRONG_KIND,{0}}
    };
    const neo_capability *root = NULL, *contents = NULL, *vector = NULL;
    neo_diagnostic diagnostic = {0};
    neo_status status = neo_platform_load_image(vm, "neo/packages/vector.neo", &root, &diagnostic);
    if (status == NEO_OK) { status = neo_package_validate(vm, root); }
    if (status == NEO_OK) { status = neo_object_child(vm, root, "contents", &contents); }
    if (status == NEO_OK) { status = neo_object_child(vm, contents, "vector", &vector); }
    const char *names[] = {"dimension","left","right","factor","result","length"};
    const neo_capability *fields[6] = {0};
    for (size_t j = 0; status == NEO_OK && j < 6; ++j) { status = neo_object_child(vm, vector, names[j], &fields[j]); }
    const neo_value marker = {.kind=NEO_INTEGERS,.integers=(const int64_t[]){-91,-92},.count=2};
    for (size_t i = 0; status == NEO_OK && i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const neo_vector_case *item = &cases[i];
        const neo_value inputs[] = {{.kind=NEO_INTEGER,.integer=item->dimension},item->left,item->right,item->factor,marker,{.kind=NEO_INTEGER,.integer=99}};
        for (size_t j = 0; status == NEO_OK && j < 6; ++j) { status = neo_object_write(vm, fields[j], inputs[j]); }
        neo_execution report = {0};
        if (status == NEO_OK) {
            neo_status attempted = neo_behavior_run(vm, vector, item->selector, NULL, 100000, &report);
            if (attempted != item->status || (attempted == NEO_OK && !neo_test_equal(report.result, item->result))) {
                fprintf(stderr, "vector edge %zu %s: %s expected %s\n", i, item->selector, neo_status_name(attempted), neo_status_name(item->status));
                status = NEO_BAD_STATE;
            }
        }
        neo_execution_release(vm, &report);
        if (status == NEO_OK && item->status != NEO_OK) {
            neo_value retained = {0};
            status = neo_object_read(vm, fields[4], &retained);
            if (status == NEO_OK && !neo_test_equal(retained, marker)) { status = NEO_BAD_STATE; }
            if (status == NEO_OK) { status = neo_object_read(vm, fields[5], &retained); }
            if (status == NEO_OK && (retained.kind != NEO_INTEGER || retained.integer != 99)) { status = NEO_BAD_STATE; }
        }
    }
    if (root != NULL) { (void)neo_image_unload(vm, root); }
    if (status != NEO_OK) { fprintf(stderr, "FAIL vector edge/precision/result-retention checks: %s\n", neo_status_name(status)); }
    return status == NEO_OK;
}

static neo_status neo_test_matrix_write(neo_vm *vm, const neo_capability *matrix, const char *name, neo_value value) {
    const neo_capability *field = NULL;
    neo_status status = neo_object_child(vm, matrix, name, &field);
    return status == NEO_OK ? neo_object_write(vm, field, value) : status;
}
static bool neo_test_matrix_value(neo_vm *vm, const neo_capability *matrix, const char *name, neo_value expected) {
    const neo_capability *field = NULL;
    neo_value value = {0};
    return neo_object_child(vm, matrix, name, &field) == NEO_OK &&
        neo_object_read(vm, field, &value) == NEO_OK && neo_test_equal(value, expected);
}
static bool neo_test_matrix_call(neo_vm *vm, const neo_capability *matrix, const char *selector, neo_status expected) {
    neo_execution report = {0};
    neo_status status = neo_behavior_run(vm, matrix, selector, NULL, 100000, &report);
    neo_execution_release(vm, &report);
    if (status != expected) { fprintf(stderr, "FAIL matrix %s: %s expected %s\n", selector, neo_status_name(status), neo_status_name(expected)); }
    return status == expected;
}
static bool neo_test_matrix_package(neo_vm *vm) {
    const neo_capability *root = NULL, *contents = NULL, *matrix = NULL;
    neo_diagnostic diagnostic = {0};
    neo_status status = neo_platform_load_image(vm, "neo/packages/matrix.neo", &root, &diagnostic);
    if (status == NEO_OK) { status = neo_package_validate(vm, root); }
    if (status == NEO_OK) { status = neo_object_child(vm, root, "contents", &contents); }
    if (status == NEO_OK) { status = neo_object_child(vm, contents, "matrix", &matrix); }
    const int64_t dimensions[] = {0,1,2,3,4,7,33,100,257,INT64_MAX};
    int64_t input[257];
    for (size_t i = 0; i < 257; ++i) { input[i] = (int64_t)i * 1000000; }
    for (size_t d = 0; status == NEO_OK && d < sizeof(dimensions) / sizeof(dimensions[0]); ++d) {
        int64_t dimension = dimensions[d];
        status = neo_test_matrix_write(vm, matrix, "dimension", (neo_value){.kind=NEO_INTEGER,.integer=dimension});
        if (status == NEO_OK && !neo_test_matrix_call(vm, matrix, "identity", NEO_OK)) { status = NEO_BAD_STATE; }
        if (status == NEO_OK && !neo_test_matrix_call(vm, matrix, "compose", NEO_OK)) { status = NEO_BAD_STATE; }
        if (status == NEO_OK && !neo_test_matrix_call(vm, matrix, "invert", NEO_OK)) { status = NEO_BAD_STATE; }
        if (status == NEO_OK && (!neo_test_matrix_value(vm, matrix, "left", (neo_value){.kind=NEO_INTEGERS}) ||
            !neo_test_matrix_value(vm, matrix, "right", (neo_value){.kind=NEO_INTEGERS}) ||
            !neo_test_matrix_value(vm, matrix, "result-kind", (neo_value){.kind=NEO_TEXT,.text="identity"}) ||
            !neo_test_matrix_value(vm, matrix, "result-rows", (neo_value){.kind=NEO_INTEGER,.integer=dimension}) ||
            !neo_test_matrix_value(vm, matrix, "result-columns", (neo_value){.kind=NEO_INTEGER,.integer=dimension}))) { status = NEO_BAD_STATE; }
        if (dimension == INT64_MAX) {
            if (status == NEO_OK && !neo_test_matrix_call(vm, matrix, "materialize", NEO_OVERFLOW)) { status = NEO_BAD_STATE; }
            continue;
        }
        size_t count = (size_t)dimension;
        status = status == NEO_OK ? neo_test_matrix_write(vm, matrix, "input", (neo_value){.kind=NEO_INTEGERS,.integers=input,.count=count}) : status;
        if (status == NEO_OK && !neo_test_matrix_call(vm, matrix, "apply", NEO_OK)) { status = NEO_BAD_STATE; }
        if (status == NEO_OK && !neo_test_matrix_value(vm, matrix, "result", (neo_value){.kind=NEO_INTEGERS,.integers=input,.count=count})) { status = NEO_BAD_STATE; }
        if (dimension > 7) { continue; }
        int64_t dense[49] = {0};
        for (size_t i = 0; i < count; ++i) { dense[i * count + i] = 1000000; }
        if (status == NEO_OK && !neo_test_matrix_call(vm, matrix, "materialize", NEO_OK)) { status = NEO_BAD_STATE; }
        if (status == NEO_OK && !neo_test_matrix_value(vm, matrix, "result", (neo_value){.kind=NEO_INTEGERS,.integers=dense,.count=count*count})) { status = NEO_BAD_STATE; }
        if (status == NEO_OK && !neo_test_matrix_call(vm, matrix, "initialize", NEO_OK)) { status = NEO_BAD_STATE; }
        if (status == NEO_OK) { status = neo_test_matrix_write(vm, matrix, "left", (neo_value){.kind=NEO_INTEGERS,.integers=dense,.count=count*count}); }
        if (status == NEO_OK) { status = neo_test_matrix_write(vm, matrix, "input", (neo_value){.kind=NEO_INTEGERS,.integers=input,.count=count}); }
        if (status == NEO_OK && !neo_test_matrix_call(vm, matrix, "apply", NEO_OK)) { status = NEO_BAD_STATE; }
        if (status == NEO_OK && !neo_test_matrix_value(vm, matrix, "result", (neo_value){.kind=NEO_INTEGERS,.integers=input,.count=count})) { status = NEO_BAD_STATE; }
        for (size_t i = 0; i < count; ++i) { dense[i * count + i] = ((int64_t)i + 2) * 1000000; }
        if (status == NEO_OK) { status = neo_test_matrix_write(vm, matrix, "right", (neo_value){.kind=NEO_INTEGERS,.integers=dense,.count=count*count}); }
        if (status == NEO_OK && !neo_test_matrix_call(vm, matrix, "compose", NEO_OK)) { status = NEO_BAD_STATE; }
        if (status == NEO_OK && (!neo_test_matrix_value(vm, matrix, "result", (neo_value){.kind=NEO_INTEGERS,.integers=dense,.count=count*count}) ||
            !neo_test_matrix_value(vm, matrix, "result-kind", (neo_value){.kind=NEO_TEXT,.text="dense"}))) { status = NEO_BAD_STATE; }
    }
    if (root != NULL) { (void)neo_image_unload(vm, root); }
    if (status != NEO_OK) { fprintf(stderr, "FAIL dimension-driven matrix package: %s\n", neo_status_name(status)); }
    return status == NEO_OK;
}

static bool neo_test_matrix_edges(neo_vm *vm) {
    typedef struct neo_matrix_case {
        const char *selector;
        int64_t rows, inner, columns;
        const char *left_kind, *right_kind;
        neo_value left, right, input;
        neo_status status;
        neo_value result;
        const char *result_kind;
        int64_t result_columns;
    } neo_matrix_case;
    const int64_t zeros400[400] = {0};
    const neo_matrix_case cases[] = {
        {"invert",2,2,0,"dense","dense",{.kind=NEO_INTEGERS, .integers=(const int64_t[]){0,2000000,1000000,3000000},.count=4},{.kind=NEO_INTEGERS},{.kind=NEO_INTEGERS},NEO_OK,{.kind=NEO_INTEGERS, .integers=(const int64_t[]){-1500000,1000000,500000,0},.count=4},"dense",2},
        {"invert",1,1,0,"dense","dense",{.kind=NEO_INTEGERS, .integers=(const int64_t[]){-2000000},.count=1},{.kind=NEO_INTEGERS},{.kind=NEO_INTEGERS},NEO_OK,{.kind=NEO_INTEGERS, .integers=(const int64_t[]){-500000},.count=1},"dense",1},
        {"invert",0,0,0,"dense","dense",{.kind=NEO_INTEGERS},{.kind=NEO_INTEGERS},{.kind=NEO_INTEGERS},NEO_OK,{.kind=NEO_INTEGERS},"dense",0},
        {"invert",2,2,0,"dense","dense",{.kind=NEO_INTEGERS, .integers=(const int64_t[]){1000000,2000000,2000000,4000000},.count=4},{.kind=NEO_INTEGERS},{.kind=NEO_INTEGERS},NEO_RAISED,{.kind=NEO_INTEGERS},"dense",2},
        {"invert",1,2,0,"dense","dense",{.kind=NEO_INTEGERS, .integers=(const int64_t[]){1,2},.count=2},{.kind=NEO_INTEGERS},{.kind=NEO_INTEGERS},NEO_RAISED,{.kind=NEO_INTEGERS},"dense",2},
        {"invert",1,1,0,"dense","dense",{.kind=NEO_INTEGERS, .integers=(const int64_t[]){1},.count=1},{.kind=NEO_INTEGERS},{.kind=NEO_INTEGERS},NEO_RAISED,{.kind=NEO_INTEGERS},"dense",1},
        {"invert",1,1,0,"dense","dense",{.kind=NEO_INTEGERS, .integers=(const int64_t[]){INT64_MIN},.count=1},{.kind=NEO_INTEGERS},{.kind=NEO_INTEGERS},NEO_OVERFLOW,{.kind=NEO_INTEGERS},"dense",1},
        {"invert",1,1,0,"dense","dense",{.kind=NEO_INTEGERS, .integers=(const int64_t[]){INT64_MAX},.count=1},{.kind=NEO_INTEGERS},{.kind=NEO_INTEGERS},NEO_OVERFLOW,{.kind=NEO_INTEGERS},"dense",1},
        {"apply",2,3,0,"dense","dense",{.kind=NEO_INTEGERS,.integers=(const int64_t[]){1000000,2000000,3000000,4000000,5000000,6000000},.count=6},{.kind=NEO_INTEGERS},{.kind=NEO_INTEGERS,.integers=(const int64_t[]){1000000,2000000,3000000},.count=3},NEO_OK,{.kind=NEO_INTEGERS,.integers=(const int64_t[]){14000000,32000000},.count=2},"vector",1},
        {"compose",2,3,2,"dense","dense",{.kind=NEO_INTEGERS,.integers=(const int64_t[]){1000000,2000000,3000000,4000000,5000000,6000000},.count=6},{.kind=NEO_INTEGERS,.integers=(const int64_t[]){7000000,8000000,9000000,10000000,11000000,12000000},.count=6},{.kind=NEO_INTEGERS},NEO_OK,{.kind=NEO_INTEGERS,.integers=(const int64_t[]){58000000,64000000,139000000,154000000},.count=4},"dense",2},
        {"compose",2,2,2,"dense","dense",{.kind=NEO_INTEGERS,.integers=(const int64_t[]){1000000,1000000,0,1000000},.count=4},{.kind=NEO_INTEGERS,.integers=(const int64_t[]){2000000,0,0,3000000},.count=4},{.kind=NEO_INTEGERS},NEO_OK,{.kind=NEO_INTEGERS,.integers=(const int64_t[]){2000000,3000000,0,3000000},.count=4},"dense",2},
        {"compose",2,2,3,"identity","dense",{.kind=NEO_INTEGERS},{.kind=NEO_INTEGERS,.integers=(const int64_t[]){1,2,3,4,5,6},.count=6},{.kind=NEO_INTEGERS},NEO_OK,{.kind=NEO_INTEGERS,.integers=(const int64_t[]){1,2,3,4,5,6},.count=6},"dense",3},
        {"compose",3,2,2,"dense","identity",{.kind=NEO_INTEGERS,.integers=(const int64_t[]){1,2,3,4,5,6},.count=6},{.kind=NEO_INTEGERS},{.kind=NEO_INTEGERS},NEO_OK,{.kind=NEO_INTEGERS,.integers=(const int64_t[]){1,2,3,4,5,6},.count=6},"dense",2},
        {"apply",1,1,0,"identity","dense",{.kind=NEO_INTEGERS},{.kind=NEO_INTEGERS},{.kind=NEO_INTEGERS,.integers=(const int64_t[]){INT64_MAX},.count=1},NEO_OK,{.kind=NEO_INTEGERS,.integers=(const int64_t[]){INT64_MAX},.count=1},"vector",1},
        {"apply",2,2,0,"dense","dense",{.kind=NEO_INTEGERS,.integers=(const int64_t[]){1000000,0,0,0},.count=4},{.kind=NEO_INTEGERS},{.kind=NEO_INTEGERS,.integers=(const int64_t[]){3000000,4000000},.count=2},NEO_OK,{.kind=NEO_INTEGERS,.integers=(const int64_t[]){3000000,0},.count=2},"vector",1},
        {"apply",2,0,3,"dense","invalid",{.kind=NEO_INTEGERS},{.kind=NEO_BOOLEAN,.boolean=true},{.kind=NEO_INTEGERS},NEO_OK,{.kind=NEO_INTEGERS,.integers=(const int64_t[]){0,0},.count=2},"vector",1},
        {"compose",2,0,3,"dense","dense",{.kind=NEO_INTEGERS},{.kind=NEO_INTEGERS},{.kind=NEO_INTEGERS},NEO_OK,{.kind=NEO_INTEGERS,.integers=(const int64_t[]){0,0,0,0,0,0},.count=6},"dense",3},
        {"apply",1,2,0,"dense","dense",{.kind=NEO_INTEGERS,.integers=(const int64_t[]){1,1},.count=2},{.kind=NEO_INTEGERS},{.kind=NEO_INTEGERS,.integers=(const int64_t[]){500000,500000},.count=2},NEO_OK,{.kind=NEO_INTEGERS,.integers=(const int64_t[]){1},.count=1},"vector",1},
        {"compose",1,2,1,"dense","dense",{.kind=NEO_INTEGERS,.integers=(const int64_t[]){1,1},.count=2},{.kind=NEO_INTEGERS,.integers=(const int64_t[]){500000,500000},.count=2},{.kind=NEO_INTEGERS},NEO_OK,{.kind=NEO_INTEGERS,.integers=(const int64_t[]){1},.count=1},"dense",1},
        {"apply",1,2,0,"dense","dense",{.kind=NEO_INTEGERS,.integers=(const int64_t[]){-1,-1},.count=2},{.kind=NEO_INTEGERS},{.kind=NEO_INTEGERS,.integers=(const int64_t[]){500000,500000},.count=2},NEO_OK,{.kind=NEO_INTEGERS,.integers=(const int64_t[]){-1},.count=1},"vector",1},
        {"compose",1,2,1,"dense","dense",{.kind=NEO_INTEGERS,.integers=(const int64_t[]){-1,-1},.count=2},{.kind=NEO_INTEGERS,.integers=(const int64_t[]){500000,500000},.count=2},{.kind=NEO_INTEGERS},NEO_OK,{.kind=NEO_INTEGERS,.integers=(const int64_t[]){-1},.count=1},"dense",1},
        {"apply",1,2,0,"dense","dense",{.kind=NEO_INTEGERS,.integers=(const int64_t[]){INT64_MAX,1},.count=2},{.kind=NEO_INTEGERS},{.kind=NEO_INTEGERS,.integers=(const int64_t[]){1,1},.count=2},NEO_OVERFLOW,{.kind=NEO_INTEGERS},NULL,0},
        {"compose",1,2,1,"dense","dense",{.kind=NEO_INTEGERS,.integers=(const int64_t[]){INT64_MAX,1},.count=2},{.kind=NEO_INTEGERS,.integers=(const int64_t[]){1,1},.count=2},{.kind=NEO_INTEGERS},NEO_OVERFLOW,{.kind=NEO_INTEGERS},NULL,1},
        {"apply",1,1,0,"dense","dense",{.kind=NEO_INTEGERS,.integers=(const int64_t[]){INT64_MIN},.count=1},{.kind=NEO_INTEGERS},{.kind=NEO_INTEGERS,.integers=(const int64_t[]){-1},.count=1},NEO_OVERFLOW,{.kind=NEO_INTEGERS},NULL,0},
        {"compose",1,1,1,"dense","dense",{.kind=NEO_INTEGERS,.integers=(const int64_t[]){INT64_MAX},.count=1},{.kind=NEO_INTEGERS,.integers=(const int64_t[]){2},.count=1},{.kind=NEO_INTEGERS},NEO_OVERFLOW,{.kind=NEO_INTEGERS},NULL,1},
        {"apply",2,2,0,"dense","dense",{.kind=NEO_INTEGERS,.integers=(const int64_t[]){1000000},.count=1},{.kind=NEO_INTEGERS},{.kind=NEO_INTEGERS,.integers=(const int64_t[]){1000000,1000000},.count=2},NEO_RAISED,{.kind=NEO_INTEGERS},NULL,0},
        {"apply",1,2,0,"dense","dense",{.kind=NEO_INTEGERS,.integers=(const int64_t[]){1000000,1000000},.count=2},{.kind=NEO_INTEGERS},{.kind=NEO_INTEGERS,.integers=(const int64_t[]){1000000},.count=1},NEO_RAISED,{.kind=NEO_INTEGERS},NULL,0},
        {"compose",1,2,3,"dense","dense",{.kind=NEO_INTEGERS,.integers=(const int64_t[]){1000000,1000000},.count=2},{.kind=NEO_INTEGERS,.integers=(const int64_t[]){1000000,1000000},.count=2},{.kind=NEO_INTEGERS},NEO_RAISED,{.kind=NEO_INTEGERS},NULL,3},
        {"compose",1,2,2,"identity","identity",{.kind=NEO_INTEGERS},{.kind=NEO_INTEGERS},{.kind=NEO_INTEGERS},NEO_RAISED,{.kind=NEO_INTEGERS},NULL,2},
        {"compose",2,2,3,"identity","identity",{.kind=NEO_INTEGERS},{.kind=NEO_INTEGERS},{.kind=NEO_INTEGERS},NEO_RAISED,{.kind=NEO_INTEGERS},NULL,3},
        {"apply",1,1,0,"invalid","dense",{.kind=NEO_INTEGERS},{.kind=NEO_INTEGERS},{.kind=NEO_INTEGERS,.integers=(const int64_t[]){1},.count=1},NEO_RAISED,{.kind=NEO_INTEGERS},NULL,0},
        {"apply",1,1,0,"identity","dense",{.kind=NEO_INTEGERS,.integers=(const int64_t[]){0},.count=1},{.kind=NEO_INTEGERS},{.kind=NEO_INTEGERS,.integers=(const int64_t[]){1},.count=1},NEO_RAISED,{.kind=NEO_INTEGERS},NULL,0},
        {"compose",1,1,1,"dense","invalid",{.kind=NEO_INTEGERS,.integers=(const int64_t[]){0},.count=1},{.kind=NEO_INTEGERS},{.kind=NEO_INTEGERS},NEO_RAISED,{.kind=NEO_INTEGERS},NULL,1},
        {"apply",1,1,0,"dense","dense",{.kind=NEO_BOOLEAN,.boolean=true},{.kind=NEO_INTEGERS},{.kind=NEO_INTEGERS,.integers=(const int64_t[]){1},.count=1},NEO_WRONG_KIND,{.kind=NEO_INTEGERS},NULL,0},
        {"apply",-1,0,0,"dense","dense",{.kind=NEO_INTEGERS},{.kind=NEO_INTEGERS},{.kind=NEO_INTEGERS},NEO_RAISED,{.kind=NEO_INTEGERS},NULL,0},
        {"compose",0,0,-1,"dense","dense",{.kind=NEO_INTEGERS},{.kind=NEO_INTEGERS},{.kind=NEO_INTEGERS},NEO_RAISED,{.kind=NEO_INTEGERS},NULL,-1},
        {"identity",-1,0,0,"dense","dense",{.kind=NEO_INTEGERS},{.kind=NEO_INTEGERS},{.kind=NEO_INTEGERS},NEO_RAISED,{.kind=NEO_INTEGERS},NULL,0},
        {"materialize",500,500,500,"identity","identity",{.kind=NEO_INTEGERS},{.kind=NEO_INTEGERS},{.kind=NEO_INTEGERS},NEO_LIMIT,{.kind=NEO_INTEGERS},NULL,500},
        {"initialize",1000000,1000000,0,"dense","dense",{.kind=NEO_INTEGERS},{.kind=NEO_INTEGERS},{.kind=NEO_INTEGERS},NEO_LIMIT,{.kind=NEO_INTEGERS},NULL,0},
        {"compose",20,20,20,"dense","dense",{.kind=NEO_INTEGERS,.integers=zeros400,.count=400},{.kind=NEO_INTEGERS,.integers=zeros400,.count=400},{.kind=NEO_INTEGERS},NEO_LIMIT,{.kind=NEO_INTEGERS},NULL,20}
    };
    const neo_capability *root = NULL, *contents = NULL, *matrix = NULL;
    neo_diagnostic diagnostic = {0};
    neo_status status = neo_platform_load_image(vm, "neo/packages/matrix.neo", &root, &diagnostic);
    if (status == NEO_OK) { status = neo_object_child(vm, root, "contents", &contents); }
    if (status == NEO_OK) { status = neo_object_child(vm, contents, "matrix", &matrix); }
    const neo_value marker = {.kind=NEO_INTEGERS,.integers=(const int64_t[]){-91,-92},.count=2};
    for (size_t i = 0; status == NEO_OK && i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const neo_matrix_case *item = &cases[i];
        const char *names[] = {"rows","inner","columns","dimension","left-kind","right-kind","left","right","input","result","result-kind","result-rows","result-columns"};
        const neo_value inputs[] = {{.kind=NEO_INTEGER,.integer=item->rows},{.kind=NEO_INTEGER,.integer=item->inner},{.kind=NEO_INTEGER,.integer=item->columns},
            {.kind=NEO_INTEGER,.integer=item->rows},{.kind=NEO_TEXT,.text=item->left_kind},{.kind=NEO_TEXT,.text=item->right_kind},
            item->left,item->right,item->input,marker,{.kind=NEO_TEXT,.text="sentinel"},{.kind=NEO_INTEGER,.integer=17},{.kind=NEO_INTEGER,.integer=19}};
        for (size_t j = 0; status == NEO_OK && j < sizeof(inputs) / sizeof(inputs[0]); ++j) { status = neo_test_matrix_write(vm, matrix, names[j], inputs[j]); }
        neo_execution report = {0};
        if (status == NEO_OK) {
            neo_status attempted = neo_behavior_run(vm, matrix, item->selector, NULL, 100000, &report);
            if (attempted != item->status || (attempted == NEO_OK && !neo_test_equal(report.result, item->result))) {
                fprintf(stderr, "matrix edge %zu %s: %s expected %s\n", i, item->selector, neo_status_name(attempted), neo_status_name(item->status));
                status = NEO_BAD_STATE;
            }
        }
        neo_execution_release(vm, &report);
        if (status == NEO_OK && (!neo_test_matrix_value(vm, matrix, "result", item->status == NEO_OK ? item->result : marker) ||
            !neo_test_matrix_value(vm, matrix, "result-kind", (neo_value){.kind=NEO_TEXT,.text=item->status == NEO_OK ? item->result_kind : "sentinel"}) ||
            !neo_test_matrix_value(vm, matrix, "result-rows", (neo_value){.kind=NEO_INTEGER,.integer=item->status == NEO_OK ? item->rows : 17}) ||
            !neo_test_matrix_value(vm, matrix, "result-columns", (neo_value){.kind=NEO_INTEGER,.integer=item->status == NEO_OK ? item->result_columns : 19}))) { status = NEO_BAD_STATE; }
    }
    if (root != NULL) { (void)neo_image_unload(vm, root); }
    if (status != NEO_OK) { fprintf(stderr, "FAIL matrix shape/order/rounding/overflow/budget retention: %s\n", neo_status_name(status)); }
    return status == NEO_OK;
}

static bool neo_test_affine_package(neo_vm *vm) {
    const neo_capability *root = NULL, *contents = NULL, *matrix = NULL;
    neo_diagnostic diagnostic = {0};
    neo_status status = neo_platform_load_image(vm, "neo/packages/matrix.neo", &root, &diagnostic);
    if (status == NEO_OK) { status = neo_object_child(vm, root, "contents", &contents); }
    if (status == NEO_OK) { status = neo_object_child(vm, contents, "matrix", &matrix); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, matrix, "dimension", (neo_value){.kind=NEO_INTEGER,.integer=0}); }
    if (status == NEO_OK && !neo_test_matrix_call(vm, matrix, "affine-identity", NEO_OK)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, matrix, "result-translation", (neo_value){.kind=NEO_INTEGERS})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, matrix, "input", (neo_value){.kind=NEO_INTEGERS}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, matrix, "left-translation", (neo_value){.kind=NEO_INTEGERS}); }
    if (status == NEO_OK && !neo_test_matrix_call(vm, matrix, "point", NEO_OK)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, matrix, "result", (neo_value){.kind=NEO_INTEGERS})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_call(vm, matrix, "apply", NEO_OK)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, matrix, "result", (neo_value){.kind=NEO_INTEGERS})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, matrix, "dimension", (neo_value){.kind=NEO_INTEGER,.integer=1}); }
    if (status == NEO_OK && !neo_test_matrix_call(vm, matrix, "affine-identity", NEO_OK)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, matrix, "result-translation", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){0},.count=1})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, matrix, "input", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){0},.count=1}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, matrix, "left-translation", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){1000000},.count=1}); }
    if (status == NEO_OK && !neo_test_matrix_call(vm, matrix, "point", NEO_OK)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, matrix, "result", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){1000000},.count=1})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_call(vm, matrix, "apply", NEO_OK)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, matrix, "result", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){0},.count=1})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, matrix, "dimension", (neo_value){.kind=NEO_INTEGER,.integer=2}); }
    if (status == NEO_OK && !neo_test_matrix_call(vm, matrix, "affine-identity", NEO_OK)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, matrix, "result-translation", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){0,0},.count=2})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, matrix, "input", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){0,1000000},.count=2}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, matrix, "left-translation", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){1000000,1000000},.count=2}); }
    if (status == NEO_OK && !neo_test_matrix_call(vm, matrix, "point", NEO_OK)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, matrix, "result", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){1000000,2000000},.count=2})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_call(vm, matrix, "apply", NEO_OK)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, matrix, "result", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){0,1000000},.count=2})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, matrix, "dimension", (neo_value){.kind=NEO_INTEGER,.integer=3}); }
    if (status == NEO_OK && !neo_test_matrix_call(vm, matrix, "affine-identity", NEO_OK)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, matrix, "result-translation", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){0,0,0},.count=3})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, matrix, "input", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){0,1000000,2000000},.count=3}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, matrix, "left-translation", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){1000000,1000000,1000000},.count=3}); }
    if (status == NEO_OK && !neo_test_matrix_call(vm, matrix, "point", NEO_OK)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, matrix, "result", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){1000000,2000000,3000000},.count=3})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_call(vm, matrix, "apply", NEO_OK)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, matrix, "result", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){0,1000000,2000000},.count=3})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, matrix, "dimension", (neo_value){.kind=NEO_INTEGER,.integer=7}); }
    if (status == NEO_OK && !neo_test_matrix_call(vm, matrix, "affine-identity", NEO_OK)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, matrix, "result-translation", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){0,0,0,0,0,0,0},.count=7})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, matrix, "input", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){0,1000000,2000000,3000000,4000000,5000000,6000000},.count=7}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, matrix, "left-translation", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){1000000,1000000,1000000,1000000,1000000,1000000,1000000},.count=7}); }
    if (status == NEO_OK && !neo_test_matrix_call(vm, matrix, "point", NEO_OK)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, matrix, "result", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){1000000,2000000,3000000,4000000,5000000,6000000,7000000},.count=7})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_call(vm, matrix, "apply", NEO_OK)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, matrix, "result", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){0,1000000,2000000,3000000,4000000,5000000,6000000},.count=7})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, matrix, "dimension", (neo_value){.kind=NEO_INTEGER,.integer=33}); }
    if (status == NEO_OK && !neo_test_matrix_call(vm, matrix, "affine-identity", NEO_OK)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, matrix, "result-translation", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},.count=33})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, matrix, "input", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){0,1000000,2000000,3000000,4000000,5000000,6000000,7000000,8000000,9000000,10000000,11000000,12000000,13000000,14000000,15000000,16000000,17000000,18000000,19000000,20000000,21000000,22000000,23000000,24000000,25000000,26000000,27000000,28000000,29000000,30000000,31000000,32000000},.count=33}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, matrix, "left-translation", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000},.count=33}); }
    if (status == NEO_OK && !neo_test_matrix_call(vm, matrix, "point", NEO_OK)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, matrix, "result", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){1000000,2000000,3000000,4000000,5000000,6000000,7000000,8000000,9000000,10000000,11000000,12000000,13000000,14000000,15000000,16000000,17000000,18000000,19000000,20000000,21000000,22000000,23000000,24000000,25000000,26000000,27000000,28000000,29000000,30000000,31000000,32000000,33000000},.count=33})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_call(vm, matrix, "apply", NEO_OK)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, matrix, "result", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){0,1000000,2000000,3000000,4000000,5000000,6000000,7000000,8000000,9000000,10000000,11000000,12000000,13000000,14000000,15000000,16000000,17000000,18000000,19000000,20000000,21000000,22000000,23000000,24000000,25000000,26000000,27000000,28000000,29000000,30000000,31000000,32000000},.count=33})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, matrix, "dimension", (neo_value){.kind=NEO_INTEGER,.integer=257}); }
    if (status == NEO_OK && !neo_test_matrix_call(vm, matrix, "affine-identity", NEO_OK)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, matrix, "result-translation", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},.count=257})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, matrix, "input", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){0,1000000,2000000,3000000,4000000,5000000,6000000,7000000,8000000,9000000,10000000,11000000,12000000,13000000,14000000,15000000,16000000,17000000,18000000,19000000,20000000,21000000,22000000,23000000,24000000,25000000,26000000,27000000,28000000,29000000,30000000,31000000,32000000,33000000,34000000,35000000,36000000,37000000,38000000,39000000,40000000,41000000,42000000,43000000,44000000,45000000,46000000,47000000,48000000,49000000,50000000,51000000,52000000,53000000,54000000,55000000,56000000,57000000,58000000,59000000,60000000,61000000,62000000,63000000,64000000,65000000,66000000,67000000,68000000,69000000,70000000,71000000,72000000,73000000,74000000,75000000,76000000,77000000,78000000,79000000,80000000,81000000,82000000,83000000,84000000,85000000,86000000,87000000,88000000,89000000,90000000,91000000,92000000,93000000,94000000,95000000,96000000,97000000,98000000,99000000,100000000,101000000,102000000,103000000,104000000,105000000,106000000,107000000,108000000,109000000,110000000,111000000,112000000,113000000,114000000,115000000,116000000,117000000,118000000,119000000,120000000,121000000,122000000,123000000,124000000,125000000,126000000,127000000,128000000,129000000,130000000,131000000,132000000,133000000,134000000,135000000,136000000,137000000,138000000,139000000,140000000,141000000,142000000,143000000,144000000,145000000,146000000,147000000,148000000,149000000,150000000,151000000,152000000,153000000,154000000,155000000,156000000,157000000,158000000,159000000,160000000,161000000,162000000,163000000,164000000,165000000,166000000,167000000,168000000,169000000,170000000,171000000,172000000,173000000,174000000,175000000,176000000,177000000,178000000,179000000,180000000,181000000,182000000,183000000,184000000,185000000,186000000,187000000,188000000,189000000,190000000,191000000,192000000,193000000,194000000,195000000,196000000,197000000,198000000,199000000,200000000,201000000,202000000,203000000,204000000,205000000,206000000,207000000,208000000,209000000,210000000,211000000,212000000,213000000,214000000,215000000,216000000,217000000,218000000,219000000,220000000,221000000,222000000,223000000,224000000,225000000,226000000,227000000,228000000,229000000,230000000,231000000,232000000,233000000,234000000,235000000,236000000,237000000,238000000,239000000,240000000,241000000,242000000,243000000,244000000,245000000,246000000,247000000,248000000,249000000,250000000,251000000,252000000,253000000,254000000,255000000,256000000},.count=257}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, matrix, "left-translation", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000},.count=257}); }
    if (status == NEO_OK && !neo_test_matrix_call(vm, matrix, "point", NEO_OK)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, matrix, "result", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){1000000,2000000,3000000,4000000,5000000,6000000,7000000,8000000,9000000,10000000,11000000,12000000,13000000,14000000,15000000,16000000,17000000,18000000,19000000,20000000,21000000,22000000,23000000,24000000,25000000,26000000,27000000,28000000,29000000,30000000,31000000,32000000,33000000,34000000,35000000,36000000,37000000,38000000,39000000,40000000,41000000,42000000,43000000,44000000,45000000,46000000,47000000,48000000,49000000,50000000,51000000,52000000,53000000,54000000,55000000,56000000,57000000,58000000,59000000,60000000,61000000,62000000,63000000,64000000,65000000,66000000,67000000,68000000,69000000,70000000,71000000,72000000,73000000,74000000,75000000,76000000,77000000,78000000,79000000,80000000,81000000,82000000,83000000,84000000,85000000,86000000,87000000,88000000,89000000,90000000,91000000,92000000,93000000,94000000,95000000,96000000,97000000,98000000,99000000,100000000,101000000,102000000,103000000,104000000,105000000,106000000,107000000,108000000,109000000,110000000,111000000,112000000,113000000,114000000,115000000,116000000,117000000,118000000,119000000,120000000,121000000,122000000,123000000,124000000,125000000,126000000,127000000,128000000,129000000,130000000,131000000,132000000,133000000,134000000,135000000,136000000,137000000,138000000,139000000,140000000,141000000,142000000,143000000,144000000,145000000,146000000,147000000,148000000,149000000,150000000,151000000,152000000,153000000,154000000,155000000,156000000,157000000,158000000,159000000,160000000,161000000,162000000,163000000,164000000,165000000,166000000,167000000,168000000,169000000,170000000,171000000,172000000,173000000,174000000,175000000,176000000,177000000,178000000,179000000,180000000,181000000,182000000,183000000,184000000,185000000,186000000,187000000,188000000,189000000,190000000,191000000,192000000,193000000,194000000,195000000,196000000,197000000,198000000,199000000,200000000,201000000,202000000,203000000,204000000,205000000,206000000,207000000,208000000,209000000,210000000,211000000,212000000,213000000,214000000,215000000,216000000,217000000,218000000,219000000,220000000,221000000,222000000,223000000,224000000,225000000,226000000,227000000,228000000,229000000,230000000,231000000,232000000,233000000,234000000,235000000,236000000,237000000,238000000,239000000,240000000,241000000,242000000,243000000,244000000,245000000,246000000,247000000,248000000,249000000,250000000,251000000,252000000,253000000,254000000,255000000,256000000,257000000},.count=257})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_call(vm, matrix, "apply", NEO_OK)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, matrix, "result", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){0,1000000,2000000,3000000,4000000,5000000,6000000,7000000,8000000,9000000,10000000,11000000,12000000,13000000,14000000,15000000,16000000,17000000,18000000,19000000,20000000,21000000,22000000,23000000,24000000,25000000,26000000,27000000,28000000,29000000,30000000,31000000,32000000,33000000,34000000,35000000,36000000,37000000,38000000,39000000,40000000,41000000,42000000,43000000,44000000,45000000,46000000,47000000,48000000,49000000,50000000,51000000,52000000,53000000,54000000,55000000,56000000,57000000,58000000,59000000,60000000,61000000,62000000,63000000,64000000,65000000,66000000,67000000,68000000,69000000,70000000,71000000,72000000,73000000,74000000,75000000,76000000,77000000,78000000,79000000,80000000,81000000,82000000,83000000,84000000,85000000,86000000,87000000,88000000,89000000,90000000,91000000,92000000,93000000,94000000,95000000,96000000,97000000,98000000,99000000,100000000,101000000,102000000,103000000,104000000,105000000,106000000,107000000,108000000,109000000,110000000,111000000,112000000,113000000,114000000,115000000,116000000,117000000,118000000,119000000,120000000,121000000,122000000,123000000,124000000,125000000,126000000,127000000,128000000,129000000,130000000,131000000,132000000,133000000,134000000,135000000,136000000,137000000,138000000,139000000,140000000,141000000,142000000,143000000,144000000,145000000,146000000,147000000,148000000,149000000,150000000,151000000,152000000,153000000,154000000,155000000,156000000,157000000,158000000,159000000,160000000,161000000,162000000,163000000,164000000,165000000,166000000,167000000,168000000,169000000,170000000,171000000,172000000,173000000,174000000,175000000,176000000,177000000,178000000,179000000,180000000,181000000,182000000,183000000,184000000,185000000,186000000,187000000,188000000,189000000,190000000,191000000,192000000,193000000,194000000,195000000,196000000,197000000,198000000,199000000,200000000,201000000,202000000,203000000,204000000,205000000,206000000,207000000,208000000,209000000,210000000,211000000,212000000,213000000,214000000,215000000,216000000,217000000,218000000,219000000,220000000,221000000,222000000,223000000,224000000,225000000,226000000,227000000,228000000,229000000,230000000,231000000,232000000,233000000,234000000,235000000,236000000,237000000,238000000,239000000,240000000,241000000,242000000,243000000,244000000,245000000,246000000,247000000,248000000,249000000,250000000,251000000,252000000,253000000,254000000,255000000,256000000},.count=257})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, matrix, "rows", (neo_value){.kind=NEO_INTEGER,.integer=2}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, matrix, "inner", (neo_value){.kind=NEO_INTEGER,.integer=2}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, matrix, "columns", (neo_value){.kind=NEO_INTEGER,.integer=2}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, matrix, "left-kind", (neo_value){.kind=NEO_TEXT,.text="dense"}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, matrix, "right-kind", (neo_value){.kind=NEO_TEXT,.text="dense"}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, matrix, "left", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){1000000,1000000,0,1000000},.count=4}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, matrix, "right", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){2000000,0,0,4000000},.count=4}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, matrix, "left-translation", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){3000000,5000000},.count=2}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, matrix, "right-translation", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){7000000,11000000},.count=2}); }
    if (status == NEO_OK && !neo_test_matrix_call(vm, matrix, "affine-compose", NEO_OK)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, matrix, "result", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){2000000,4000000,0,4000000},.count=4})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, matrix, "result-translation", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){21000000,16000000},.count=2})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, matrix, "result-kind", (neo_value){.kind=NEO_TEXT,.text="dense"})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, matrix, "result-rows", (neo_value){.kind=NEO_INTEGER,.integer=2})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, matrix, "result-columns", (neo_value){.kind=NEO_INTEGER,.integer=2})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, matrix, "left", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){0,2000000,1000000,3000000},.count=4}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, matrix, "left-translation", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){2000000,4000000},.count=2}); }
    if (status == NEO_OK && !neo_test_matrix_call(vm, matrix, "affine-invert", NEO_OK)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, matrix, "result", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){-1500000,1000000,500000,0},.count=4})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, matrix, "result-translation", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){-1000000,-1000000},.count=2})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, matrix, "result", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){-91},.count=1}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, matrix, "result-translation", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){-92},.count=1}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, matrix, "result-kind", (neo_value){.kind=NEO_TEXT,.text="sentinel"}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, matrix, "result-rows", (neo_value){.kind=NEO_INTEGER,.integer=17}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, matrix, "result-columns", (neo_value){.kind=NEO_INTEGER,.integer=19}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, matrix, "right-translation", (neo_value){.kind=NEO_INTEGERS}); }
    if (status == NEO_OK && !neo_test_matrix_call(vm, matrix, "affine-compose", NEO_RAISED)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, matrix, "left", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){1000000,2000000,2000000,4000000},.count=4}); }
    if (status == NEO_OK && !neo_test_matrix_call(vm, matrix, "affine-invert", NEO_RAISED)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, matrix, "result", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){-91},.count=1})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, matrix, "result-translation", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){-92},.count=1})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, matrix, "result-kind", (neo_value){.kind=NEO_TEXT,.text="sentinel"})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, matrix, "result-rows", (neo_value){.kind=NEO_INTEGER,.integer=17})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, matrix, "result-columns", (neo_value){.kind=NEO_INTEGER,.integer=19})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, matrix, "rows", (neo_value){.kind=NEO_INTEGER,.integer=1}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, matrix, "inner", (neo_value){.kind=NEO_INTEGER,.integer=1}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, matrix, "left", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){3000000},.count=1}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, matrix, "left-translation", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){100000000},.count=1}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, matrix, "inverse-tolerance", (neo_value){.kind=NEO_INTEGER,.integer=10}); }
    if (status == NEO_OK && !neo_test_matrix_call(vm, matrix, "affine-invert", NEO_RAISED)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, matrix, "result", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){-91},.count=1})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, matrix, "result-translation", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){-92},.count=1})) { status = NEO_BAD_STATE; }
    if (root != NULL) { (void)neo_image_unload(vm, root); }
    if (status != NEO_OK) { fprintf(stderr, "FAIL affine order/translation/inverse/retention: %s\n", neo_status_name(status)); }
    return status == NEO_OK;
}


static bool neo_test_frames_package(neo_vm *vm) {
    const neo_capability *root = NULL, *math_root = NULL, *contents = NULL, *math_contents = NULL;
    const neo_capability *space = NULL, *frame = NULL, *converter = NULL, *calculator = NULL;
    const neo_capability *source = NULL, *target = NULL, *middle = NULL, *other = NULL, *imported_math = NULL;
    neo_diagnostic diagnostic = {0};
    neo_status status = neo_platform_load_image(vm, "neo/packages/frames.neo", &root, &diagnostic);
    if (status == NEO_OK) { status = neo_platform_load_image(vm, "neo/packages/matrix.neo", &math_root, &diagnostic); }
    if (status == NEO_OK) { status = neo_object_child(vm, root, "contents", &contents); }
    if (status == NEO_OK) { status = neo_object_import(vm, math_root, contents, "math-package", &imported_math); }
    if (status == NEO_OK) { status = neo_object_child(vm, imported_math, "contents", &math_contents); }
    if (status == NEO_OK) { status = neo_object_child(vm, math_contents, "matrix", &calculator); }
    if (status == NEO_OK) { status = neo_object_child(vm, contents, "space", &space); }
    if (status == NEO_OK) { status = neo_object_child(vm, contents, "frame", &frame); }
    if (status == NEO_OK) { status = neo_object_child(vm, contents, "converter", &converter); }
    if (status == NEO_OK) { status = neo_object_connect(vm, frame, "space", space, NEO_READ); }
    if (status == NEO_OK) { status = neo_object_copy(vm, frame, contents, "source", &source); }
    if (status == NEO_OK) { status = neo_object_copy(vm, frame, contents, "target", &target); }
    if (status == NEO_OK) { status = neo_object_copy(vm, frame, contents, "middle", &middle); }
    if (status == NEO_OK) { status = neo_object_copy(vm, space, contents, "other", &other); }
    if (status == NEO_OK) { status = neo_object_connect(vm, source, "parent", middle, NEO_READ | NEO_DELEGATE); }
    if (status == NEO_OK) { status = neo_object_connect(vm, middle, "parent", frame, NEO_READ | NEO_DELEGATE); }
    if (status == NEO_OK) { status = neo_object_connect(vm, target, "parent", frame, NEO_READ | NEO_DELEGATE); }
    if (status == NEO_OK) { status = neo_object_connect(vm, converter, "source", source, NEO_READ | NEO_DELEGATE); }
    if (status == NEO_OK) { status = neo_object_connect(vm, converter, "target", target, NEO_READ | NEO_DELEGATE); }
    if (status == NEO_OK) { status = neo_object_connect(vm, converter, "calculator", calculator, NEO_READ | NEO_WRITE | NEO_ACT); }
    const int64_t dimensions[] = {0,1,2,3,7,33,100,257};
    int64_t input[257], translation[257], expected[257];
    for (size_t d = 0; status == NEO_OK && d < sizeof(dimensions)/sizeof(dimensions[0]); ++d) {
        int64_t dimension = dimensions[d];
        size_t count = (size_t)dimension;
        status = neo_test_matrix_write(vm, space, "dimension", (neo_value){.kind=NEO_INTEGER,.integer=dimension});
        if (status == NEO_OK && !neo_test_matrix_call(vm, space, "initialize", NEO_OK)) { status = NEO_BAD_STATE; }
        const neo_capability *frames[] = {frame, source, middle, target};
        for (size_t j = 0; status == NEO_OK && j < 4; ++j) {
            if (!neo_test_matrix_call(vm, frames[j], "initialize", NEO_OK)) { status = NEO_BAD_STATE; break; }
            status = neo_test_matrix_write(vm, frames[j], "root", (neo_value){.kind=NEO_BOOLEAN,.boolean=j == 0});
            for (size_t i = 0; i < count; ++i) { translation[i] = j == 0 ? 0 : j == 1 ? 1000000 : j == 2 ? 3000000 : 2000000; }
            if (status == NEO_OK) { status = neo_test_matrix_write(vm, frames[j], "translation", (neo_value){.kind=NEO_INTEGERS,.integers=translation,.count=count}); }
        }
        for (size_t i = 0; i < count; ++i) { input[i] = (int64_t)i*1000000; expected[i] = input[i]+2000000; }
        if (status == NEO_OK) { status = neo_test_matrix_write(vm, converter, "input", (neo_value){.kind=NEO_INTEGERS,.integers=input,.count=count}); }
        if (status == NEO_OK) { status = neo_test_matrix_write(vm, converter, "kind", (neo_value){.kind=NEO_TEXT,.text="point"}); }
        if (status == NEO_OK && !neo_test_matrix_call(vm, converter, "convert", NEO_OK)) { status = NEO_BAD_STATE; }
        if (status == NEO_OK && !neo_test_matrix_value(vm, converter, "result", (neo_value){.kind=NEO_INTEGERS,.integers=expected,.count=count})) { status = NEO_BAD_STATE; }
        if (status == NEO_OK) { status = neo_test_matrix_write(vm, converter, "kind", (neo_value){.kind=NEO_TEXT,.text="displacement"}); }
        if (status == NEO_OK && !neo_test_matrix_call(vm, converter, "convert", NEO_OK)) { status = NEO_BAD_STATE; }
        if (status == NEO_OK && !neo_test_matrix_value(vm, converter, "result", (neo_value){.kind=NEO_INTEGERS,.integers=input,.count=count})) { status = NEO_BAD_STATE; }
    }
    /* A cycle must fail before transforming or publishing. */
    if (status == NEO_OK) { status = neo_object_disconnect(vm, middle, "parent"); }
    if (status == NEO_OK) { status = neo_object_connect(vm, middle, "parent", source, NEO_READ | NEO_DELEGATE); }
    if (status == NEO_OK && !neo_test_matrix_call(vm, converter, "convert", NEO_RAISED)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, converter, "result", (neo_value){.kind=NEO_INTEGERS,.integers=input,.count=257})) { status = NEO_BAD_STATE; }
    /* Matching metadata does not establish space identity. */
    if (status == NEO_OK) { status = neo_object_disconnect(vm, middle, "parent"); }
    if (status == NEO_OK) { status = neo_object_connect(vm, middle, "parent", frame, NEO_READ | NEO_DELEGATE); }
    if (status == NEO_OK) { status = neo_object_disconnect(vm, target, "space"); }
    if (status == NEO_OK) { status = neo_object_connect(vm, target, "space", other, NEO_READ); }
    if (status == NEO_OK && !neo_test_matrix_call(vm, converter, "convert", NEO_RAISED)) { status = NEO_BAD_STATE; }
    if (root != NULL) { (void)neo_image_unload(vm, root); }
    if (math_root != NULL) { (void)neo_image_unload(vm, math_root); }
    if (status != NEO_OK) { fprintf(stderr, "FAIL frames identity/hierarchy/dimensions/cycle: %s\n", neo_status_name(status)); }
    return status == NEO_OK;
}


static bool neo_test_geometry_package(neo_vm *vm) {
    const neo_capability *root = NULL, *contents = NULL, *geometry = NULL;
    neo_diagnostic diagnostic = {0};
    neo_status status = neo_platform_load_image(vm, "neo/packages/geometry.neo", &root, &diagnostic);
    if (status == NEO_OK) { status = neo_object_child(vm, root, "contents", &contents); }
    if (status == NEO_OK) { status = neo_object_child(vm, contents, "geometry", &geometry); }
    const int64_t dimensions[] = {0,1,2,3,7,33,100,257};
    int64_t vertices[771] = {0};
    for (size_t d = 0; status == NEO_OK && d < sizeof(dimensions)/sizeof(dimensions[0]); ++d) {
        int64_t dimension = dimensions[d];
        size_t count = (size_t)dimension;
        for (size_t i = 0; i < 771; ++i) { vertices[i] = 0; }
        if (count > 0) { vertices[count] = 1000000; vertices[3*count-1] = 1000000; }
        status = neo_test_matrix_write(vm, geometry, "dimension", (neo_value){.kind=NEO_INTEGER,.integer=dimension});
        if (status == NEO_OK) { status = neo_test_matrix_write(vm, geometry, "vertex-count", (neo_value){.kind=NEO_INTEGER,.integer=3}); }
        if (status == NEO_OK && !neo_test_matrix_call(vm, geometry, "initialize", NEO_OK)) { status = NEO_BAD_STATE; }
        const char *names[] = {"vertices","points","segments","triangles"};
        const neo_value values[] = {
            {.kind=NEO_INTEGERS,.integers=vertices,.count=3*count},
            {.kind=NEO_INTEGERS,.integers=(const int64_t[]){0,2},.count=2},
            {.kind=NEO_INTEGERS,.integers=(const int64_t[]){0,1,1,1},.count=4},
            {.kind=NEO_INTEGERS,.integers=(const int64_t[]){0,1,2},.count=3}};
        for (size_t i = 0; status == NEO_OK && i < 4; ++i) { status = neo_test_matrix_write(vm, geometry, names[i], values[i]); }
        if (status == NEO_OK && !neo_test_matrix_call(vm, geometry, "validate", NEO_OK)) { status = NEO_BAD_STATE; }
        if (status == NEO_OK && (!neo_test_matrix_value(vm, geometry, "valid", (neo_value){.kind=NEO_BOOLEAN,.boolean=true}) ||
            !neo_test_matrix_value(vm, geometry, "point-count", (neo_value){.kind=NEO_INTEGER,.integer=2}) ||
            !neo_test_matrix_value(vm, geometry, "segment-count", (neo_value){.kind=NEO_INTEGER,.integer=2}) ||
            !neo_test_matrix_value(vm, geometry, "triangle-count", (neo_value){.kind=NEO_INTEGER,.integer=1}) ||
            !neo_test_matrix_value(vm, geometry, "segment-degenerate", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){dimension == 0 ? 1 : 0,1},.count=2}) ||
            !neo_test_matrix_value(vm, geometry, "triangle-degenerate", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){dimension < 2 ? 1 : 0},.count=1}))) { status = NEO_BAD_STATE; }
        if (status == NEO_OK) { status = neo_test_matrix_write(vm, geometry, "vertex-index", (neo_value){.kind=NEO_INTEGER,.integer=2}); }
        if (status == NEO_OK && !neo_test_matrix_call(vm, geometry, "vertex", NEO_OK)) { status = NEO_BAD_STATE; }
        if (status == NEO_OK && !neo_test_matrix_value(vm, geometry, "result", (neo_value){.kind=NEO_INTEGERS,.integers=vertices+2*count,.count=count})) { status = NEO_BAD_STATE; }
    }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, geometry, "points", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){3},.count=1}); }
    if (status == NEO_OK && !neo_test_matrix_call(vm, geometry, "validate", NEO_RAISED)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && (!neo_test_matrix_value(vm, geometry, "valid", (neo_value){.kind=NEO_BOOLEAN,.boolean=false}) ||
        !neo_test_matrix_value(vm, geometry, "triangle-degenerate", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){0},.count=1}))) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, geometry, "dimension", (neo_value){.kind=NEO_INTEGER,.integer=INT64_MAX}); }
    if (status == NEO_OK && !neo_test_matrix_call(vm, geometry, "validate", NEO_OVERFLOW)) { status = NEO_BAD_STATE; }
    if (root != NULL) { (void)neo_image_unload(vm, root); }
    if (status != NEO_OK) { fprintf(stderr, "FAIL geometry dimensions/topology/degeneracy/retention: %s\n", neo_status_name(status)); }
    return status == NEO_OK;
}


static bool neo_test_projection_package(neo_vm *vm) {
    const neo_capability *root = NULL, *geometry_root = NULL, *contents = NULL, *imported = NULL;
    const neo_capability *projection = NULL, *geometry_contents = NULL, *geometry = NULL;
    neo_diagnostic diagnostic = {0};
    neo_status status = neo_platform_load_image(vm, "neo/packages/projection.neo", &root, &diagnostic);
    if (status == NEO_OK) { status = neo_platform_load_image(vm, "neo/packages/geometry.neo", &geometry_root, &diagnostic); }
    if (status == NEO_OK) { status = neo_object_child(vm, root, "contents", &contents); }
    if (status == NEO_OK) { status = neo_object_child(vm, contents, "projection", &projection); }
    if (status == NEO_OK) { status = neo_object_import(vm, geometry_root, contents, "geometry-package", &imported); }
    if (status == NEO_OK) { status = neo_object_child(vm, imported, "contents", &geometry_contents); }
    if (status == NEO_OK) { status = neo_object_child(vm, geometry_contents, "geometry", &geometry); }
    if (status == NEO_OK) { status = neo_object_connect(vm, projection, "source", geometry, NEO_READ | NEO_WRITE); }
    const int64_t dimensions[] = {0,1,2,4,7,33,100,257};
    int64_t vertices[514], expected[514];
    for (size_t d = 0; status == NEO_OK && d < sizeof(dimensions)/sizeof(dimensions[0]); ++d) {
        int64_t dimension = dimensions[d];
        size_t count = (size_t)dimension, axes_count = count < 2 ? count : 2;
        int64_t axes[2] = {0,dimension-1}, projected[4] = {0};
        for (size_t i = 0; i < 2*count; ++i) { vertices[i] = (int64_t)i; expected[i] = vertices[i]; }
        status = neo_test_matrix_write(vm, geometry, "dimension", (neo_value){.kind=NEO_INTEGER,.integer=dimension});
        if (status == NEO_OK) { status = neo_test_matrix_write(vm, geometry, "vertex-count", (neo_value){.kind=NEO_INTEGER,.integer=2}); }
        if (status == NEO_OK) { status = neo_test_matrix_write(vm, geometry, "vertices", (neo_value){.kind=NEO_INTEGERS,.integers=vertices,.count=2*count}); }
        if (status == NEO_OK) { status = neo_test_matrix_write(vm, projection, "view-dimension", (neo_value){.kind=NEO_INTEGER,.integer=(int64_t)axes_count}); }
        if (status == NEO_OK && !neo_test_matrix_call(vm, projection, "initialize", NEO_OK)) { status = NEO_BAD_STATE; }
        if (status == NEO_OK) { status = neo_test_matrix_write(vm, projection, "axes", (neo_value){.kind=NEO_INTEGERS,.integers=axes,.count=axes_count}); }
        for (size_t i = 0; i < 2; ++i) {
            for (size_t j = 0; j < axes_count; ++j) { projected[i*axes_count+j] = vertices[i*count+(size_t)axes[j]]; }
        }
        if (status == NEO_OK && !neo_test_matrix_call(vm, projection, "project", NEO_OK)) { status = NEO_BAD_STATE; }
        if (status == NEO_OK && (!neo_test_matrix_value(vm, projection, "result", (neo_value){.kind=NEO_INTEGERS,.integers=projected,.count=2*axes_count}) ||
            !neo_test_matrix_value(vm, projection, "result-dimension", (neo_value){.kind=NEO_INTEGER,.integer=(int64_t)axes_count}) ||
            !neo_test_matrix_value(vm, projection, "result-vertex-count", (neo_value){.kind=NEO_INTEGER,.integer=2}))) { status = NEO_BAD_STATE; }
        if (status == NEO_OK) { status = neo_test_matrix_write(vm, geometry, "valid", (neo_value){.kind=NEO_BOOLEAN,.boolean=true}); }
        if (status == NEO_OK) { status = neo_test_matrix_write(vm, projection, "vertex-index", (neo_value){.kind=NEO_INTEGER,.integer=1}); }
        if (status == NEO_OK) { status = neo_test_matrix_write(vm, projection, "edit", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){9000000,8000000},.count=axes_count}); }
        if (axes_count > 0) { expected[count] = 9000000; }
        if (axes_count > 1) { expected[2*count-1] = 8000000; }
        if (status == NEO_OK && !neo_test_matrix_call(vm, projection, "edit-vertex", NEO_OK)) { status = NEO_BAD_STATE; }
        if (status == NEO_OK && (!neo_test_matrix_value(vm, geometry, "vertices", (neo_value){.kind=NEO_INTEGERS,.integers=expected,.count=2*count}) ||
            !neo_test_matrix_value(vm, geometry, "valid", (neo_value){.kind=NEO_BOOLEAN,.boolean=false}))) { status = NEO_BAD_STATE; }
    }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, projection, "axes", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){0,0},.count=2}); }
    if (status == NEO_OK && !neo_test_matrix_call(vm, projection, "edit-vertex", NEO_RAISED)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, geometry, "vertices", (neo_value){.kind=NEO_INTEGERS,.integers=expected,.count=514})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, geometry, "dimension", (neo_value){.kind=NEO_INTEGER,.integer=4}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, geometry, "vertex-count", (neo_value){.kind=NEO_INTEGER,.integer=3}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, geometry, "vertices", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){1,2,3,4,5,2,6,4,7,9,8,4},.count=12}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, projection, "axes", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){0,2},.count=2}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, projection, "slice-axes", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){1},.count=1}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, projection, "slice-values", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){2},.count=1}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, projection, "slice-tolerances", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){0},.count=1}); }
    if (status == NEO_OK && !neo_test_matrix_call(vm, projection, "slice", NEO_OK)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, projection, "result", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){1,3,5,6},.count=4})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, projection, "source-indices", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){0,1},.count=2})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, projection, "visibility", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){1,1,0},.count=3})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, projection, "result-vertex-count", (neo_value){.kind=NEO_INTEGER,.integer=2})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, projection, "result-source-count", (neo_value){.kind=NEO_INTEGER,.integer=3})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, projection, "vertex-index", (neo_value){.kind=NEO_INTEGER,.integer=1}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, projection, "edit", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){9,8},.count=2}); }
    if (status == NEO_OK && !neo_test_matrix_call(vm, projection, "edit-slice-vertex", NEO_OK)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, geometry, "vertices", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){1,2,3,4,9,2,8,4,7,9,8,4},.count=12})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, projection, "vertex-index", (neo_value){.kind=NEO_INTEGER,.integer=2}); }
    if (status == NEO_OK && !neo_test_matrix_call(vm, projection, "edit-slice-vertex", NEO_RAISED)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, geometry, "vertices", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){1,2,3,4,9,2,8,4,7,9,8,4},.count=12})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, projection, "slice-tolerances", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){-1},.count=1}); }
    if (status == NEO_OK && !neo_test_matrix_call(vm, projection, "slice", NEO_RAISED)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, projection, "result", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){1,3,5,6},.count=4})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, projection, "source-indices", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){0,1},.count=2})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, projection, "slice-tolerances", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){0},.count=1}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, projection, "slice-values", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){999},.count=1}); }
    if (status == NEO_OK && !neo_test_matrix_call(vm, projection, "slice", NEO_OK)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, projection, "result", (neo_value){.kind=NEO_INTEGERS})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, projection, "source-indices", (neo_value){.kind=NEO_INTEGERS})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, projection, "visibility", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){0,0,0},.count=3})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_call(vm, projection, "project", NEO_OK)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, projection, "source-indices", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){0,1,2},.count=3})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, projection, "visibility", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){1,1,1},.count=3})) { status = NEO_BAD_STATE; }
    if (root != NULL) { (void)neo_image_unload(vm, root); }
    if (geometry_root != NULL) { (void)neo_image_unload(vm, geometry_root); }
    if (status != NEO_OK) { fprintf(stderr, "FAIL projection dimensions/hidden coordinates/source identity: %s\n", neo_status_name(status)); }
    return status == NEO_OK;
}


/* Worst-case high-dimensional construction needs a larger bounded invocation. */
static bool neo_test_spatial_call(neo_vm *vm, const neo_capability *faces, const char *selector, neo_status expected) {
    neo_execution report = {0};
    neo_status status = neo_behavior_run(vm, faces, selector, NULL, 250000, &report);
    neo_execution_release(vm, &report);
    if (status != expected) { fprintf(stderr, "FAIL spatial %s: %s expected %s\n", selector, neo_status_name(status), neo_status_name(expected)); }
    return status == expected;
}
static bool neo_test_section_package(neo_vm *vm) {
    const neo_capability *root = NULL, *geometry_root = NULL, *contents = NULL, *imported = NULL;
    const neo_capability *section = NULL, *geometry_contents = NULL, *geometry = NULL;
    neo_diagnostic diagnostic = {0};
    neo_status status = neo_platform_load_image(vm, "neo/packages/section.neo", &root, &diagnostic);
    if (status == NEO_OK) { status = neo_platform_load_image(vm, "neo/packages/geometry.neo", &geometry_root, &diagnostic); }
    if (status == NEO_OK) { status = neo_object_child(vm, root, "contents", &contents); }
    if (status == NEO_OK) { status = neo_object_child(vm, contents, "section", &section); }
    if (status == NEO_OK) { status = neo_object_import(vm, geometry_root, contents, "geometry-package", &imported); }
    if (status == NEO_OK) { status = neo_object_child(vm, imported, "contents", &geometry_contents); }
    if (status == NEO_OK) { status = neo_object_child(vm, geometry_contents, "geometry", &geometry); }
    if (status == NEO_OK) { status = neo_object_connect(vm, section, "source", geometry, NEO_READ); }
    const int64_t dimensions[] = {1,2,3,4,7,33,100,257};
    int64_t vertices[514], expected[257];
    for (size_t d = 0; status == NEO_OK && d < sizeof(dimensions)/sizeof(dimensions[0]); ++d) {
        size_t count = (size_t)dimensions[d];
        for (size_t i = 0; i < count; ++i) { vertices[i] = 0; vertices[count+i] = 4; expected[i] = 2; }
        vertices[0] = -2; vertices[count] = 2; expected[0] = 0;
        status = neo_test_matrix_write(vm, geometry, "dimension", (neo_value){.kind=NEO_INTEGER,.integer=dimensions[d]});
        if (status == NEO_OK) { status = neo_test_matrix_write(vm, geometry, "vertex-count", (neo_value){.kind=NEO_INTEGER,.integer=2}); }
        if (status == NEO_OK) { status = neo_test_matrix_write(vm, geometry, "vertices", (neo_value){.kind=NEO_INTEGERS,.integers=vertices,.count=2*count}); }
        if (status == NEO_OK) { status = neo_test_matrix_write(vm, geometry, "segments", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){0,1},.count=2}); }
        if (status == NEO_OK && !neo_test_matrix_call(vm, section, "intersect", NEO_OK)) { status = NEO_BAD_STATE; }
        if (status == NEO_OK && (!neo_test_matrix_value(vm, section, "result", (neo_value){.kind=NEO_INTEGERS,.integers=expected,.count=count}) ||
            !neo_test_matrix_value(vm, section, "result-points", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){0},.count=1}) ||
            !neo_test_matrix_value(vm, section, "source-vertices", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){-1},.count=1}))) { status = NEO_BAD_STATE; }
    }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, geometry, "dimension", (neo_value){.kind=NEO_INTEGER,.integer=2}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, geometry, "vertex-count", (neo_value){.kind=NEO_INTEGER,.integer=6}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, geometry, "vertices", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){-2,0,2,4,0,8,0,10,1,12,1,14},.count=12}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, geometry, "segments", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){0,1,0,2,2,3,4,5,2,2},.count=10}); }
    if (status == NEO_OK && !neo_test_matrix_call(vm, section, "intersect", NEO_OK)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, section, "result", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){0,2,0,8,0,8,0,10,0,8},.count=10})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, section, "result-points", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){0,1,4},.count=3})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, section, "result-segments", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){2,3},.count=2})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, section, "source-segments", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){0,1,2,2,4},.count=5})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, section, "source-vertices", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){-1,2,2,3,2},.count=5})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, section, "parameter-numerators", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){2,1,0,1,0},.count=5})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, section, "parameter-denominators", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){4,1,1,1,1},.count=5})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, section, "segment-kinds", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){1,1,2,0,1},.count=5})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, section, "segment-sources", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){2},.count=1})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, section, "plane-axis", (neo_value){.kind=NEO_INTEGER,.integer=2}); }
    if (status == NEO_OK && !neo_test_matrix_call(vm, section, "intersect", NEO_RAISED)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, section, "segment-kinds", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){1,1,2,0,1},.count=5})) { status = NEO_BAD_STATE; }

    const neo_capability *faces = NULL;
    if (status == NEO_OK) { status = neo_object_child(vm, contents, "triangle-section", &faces); }
    if (status == NEO_OK) { status = neo_object_connect(vm, faces, "source", geometry, NEO_READ); }
    int64_t triangle_vertices[771], triangle_result[514];
    for (size_t d = 0; status == NEO_OK && d < sizeof(dimensions)/sizeof(dimensions[0]); ++d) {
        size_t count = (size_t)dimensions[d];
        for (size_t j = 0; j < 3*count; ++j) { triangle_vertices[j] = 0; }
        for (size_t j = 0; j < 2*count; ++j) { triangle_result[j] = 0; }
        triangle_vertices[0] = -2; triangle_vertices[count] = 2;
        if (count > 1) {
            triangle_vertices[count+1] = 2;
            triangle_vertices[3*count-1] = 4;
            triangle_result[count-1] = 4;
            triangle_result[count+1] = 1;
        }
        status = neo_test_matrix_write(vm, geometry, "dimension", (neo_value){.kind=NEO_INTEGER,.integer=dimensions[d]});
        if (status == NEO_OK) { status = neo_test_matrix_write(vm, geometry, "vertex-count", (neo_value){.kind=NEO_INTEGER,.integer=3}); }
        if (status == NEO_OK) { status = neo_test_matrix_write(vm, geometry, "vertices", (neo_value){.kind=NEO_INTEGERS,.integers=triangle_vertices,.count=3*count}); }
        if (status == NEO_OK) { status = neo_test_matrix_write(vm, geometry, "triangles", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){0,1,2},.count=3}); }
        if (status == NEO_OK && !neo_test_spatial_call(vm, faces, "intersect", NEO_OK)) { status = NEO_BAD_STATE; }
        if (status == NEO_OK && (!neo_test_matrix_value(vm, faces, "result", (neo_value){.kind=NEO_INTEGERS,.integers=triangle_result,.count=count == 1 ? 1 : 2*count}) ||
            !neo_test_matrix_value(vm, faces, "triangle-ranks", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){count == 1 ? 1 : 2},.count=1}) ||
            !neo_test_matrix_value(vm, faces, "triangle-kinds", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){count == 1 ? 1 : 2},.count=1}))) { status = NEO_BAD_STATE; }
    }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, geometry, "dimension", (neo_value){.kind=NEO_INTEGER,.integer=3}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, geometry, "vertex-count", (neo_value){.kind=NEO_INTEGER,.integer=4}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, geometry, "vertices", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){0,0,0,0,4,0,0,0,4,0,2,0},.count=12}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, geometry, "triangles", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){0,1,2,0,3,1,0,0,0},.count=9}); }
    if (status == NEO_OK && !neo_test_spatial_call(vm, faces, "intersect", NEO_OK)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, faces, "result-triangles", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){0,1,2},.count=3})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, faces, "result-segments", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){3,4},.count=2})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, faces, "result-points", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){5},.count=1})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, faces, "triangle-kinds", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){3,2,1},.count=3})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, faces, "triangle-ranks", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){2,1,0},.count=3})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, faces, "segment-degenerate", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){0},.count=1})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, faces, "triangle-degenerate", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){0},.count=1})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, faces, "triangle-sources", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){0},.count=1})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, faces, "segment-sources", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){1},.count=1})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, geometry, "triangles", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){0,1,4},.count=3}); }
    if (status == NEO_OK && !neo_test_spatial_call(vm, faces, "intersect", NEO_RAISED)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, faces, "triangle-kinds", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){3,2,1},.count=3})) { status = NEO_BAD_STATE; }

    if (root != NULL) { (void)neo_image_unload(vm, root); }
    if (geometry_root != NULL) { (void)neo_image_unload(vm, geometry_root); }
    if (status != NEO_OK) { fprintf(stderr, "FAIL section dimensions/contact/coplanar/provenance: %s\n", neo_status_name(status)); }
    return status == NEO_OK;
}


static bool neo_test_linearview_package(neo_vm *vm) {
    const neo_capability *root = NULL, *geometry_root = NULL, *contents = NULL, *imported = NULL;
    const neo_capability *view = NULL, *geometry_contents = NULL, *geometry = NULL;
    neo_diagnostic diagnostic = {0};
    neo_status status = neo_platform_load_image(vm, "neo/packages/linearview.neo", &root, &diagnostic);
    if (status == NEO_OK) { status = neo_platform_load_image(vm, "neo/packages/geometry.neo", &geometry_root, &diagnostic); }
    if (status == NEO_OK) { status = neo_object_child(vm, root, "contents", &contents); }
    if (status == NEO_OK) { status = neo_object_child(vm, contents, "view", &view); }
    if (status == NEO_OK) { status = neo_object_import(vm, geometry_root, contents, "geometry-package", &imported); }
    if (status == NEO_OK) { status = neo_object_child(vm, imported, "contents", &geometry_contents); }
    if (status == NEO_OK) { status = neo_object_child(vm, geometry_contents, "geometry", &geometry); }
    if (status == NEO_OK) { status = neo_object_connect(vm, view, "source", geometry, NEO_READ | NEO_WRITE); }
    const int64_t dimensions[] = {0,1,2,4,7,33,100,257};
    int64_t vertices[514], map[514], lift[514], expected[514];
    for (size_t d = 0; status == NEO_OK && d < sizeof(dimensions)/sizeof(dimensions[0]); ++d) {
        size_t n = (size_t)dimensions[d], m = n < 2 ? n : 2;
        for (size_t i = 0; i < 514; ++i) { vertices[i] = (int64_t)i; expected[i] = vertices[i]; map[i] = 0; lift[i] = 0; }
        for (size_t j = 0; j < m; ++j) { map[j*n+j] = 1000000; lift[j*m+j] = 1000000; expected[n+j] = (int64_t)(9-j); }
        status = neo_test_matrix_write(vm, geometry, "dimension", (neo_value){.kind=NEO_INTEGER,.integer=dimensions[d]});
        if (status == NEO_OK) { status = neo_test_matrix_write(vm, geometry, "vertex-count", (neo_value){.kind=NEO_INTEGER,.integer=2}); }
        if (status == NEO_OK) { status = neo_test_matrix_write(vm, geometry, "vertices", (neo_value){.kind=NEO_INTEGERS,.integers=vertices,.count=2*n}); }
        if (status == NEO_OK) { status = neo_test_matrix_write(vm, view, "view-dimension", (neo_value){.kind=NEO_INTEGER,.integer=(int64_t)m}); }
        if (status == NEO_OK && !neo_test_spatial_call(vm, view, "initialize", NEO_OK)) { status = NEO_BAD_STATE; }
        if (status == NEO_OK) { status = neo_test_matrix_write(vm, view, "map", (neo_value){.kind=NEO_INTEGERS,.integers=map,.count=m*n}); }
        if (status == NEO_OK) { status = neo_test_matrix_write(vm, view, "lift", (neo_value){.kind=NEO_INTEGERS,.integers=lift,.count=n*m}); }
        if (status == NEO_OK && !neo_test_spatial_call(vm, view, "project", NEO_OK)) { status = NEO_BAD_STATE; }
        int64_t projected[4] = {0};
        for (size_t i = 0; i < 2; ++i) { for (size_t j = 0; j < m; ++j) { projected[i*m+j] = vertices[i*n+j]; } }
        if (status == NEO_OK && !neo_test_matrix_value(vm, view, "result", (neo_value){.kind=NEO_INTEGERS,.integers=projected,.count=2*m})) { status = NEO_BAD_STATE; }
        if (status == NEO_OK) { status = neo_test_matrix_write(vm, view, "vertex-index", (neo_value){.kind=NEO_INTEGER,.integer=1}); }
        if (status == NEO_OK) { status = neo_test_matrix_write(vm, view, "edit", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){9,8},.count=m}); }
        if (status == NEO_OK && !neo_test_spatial_call(vm, view, "edit-vertex", NEO_RAISED)) { status = NEO_BAD_STATE; }
        if (status == NEO_OK) { status = neo_test_matrix_write(vm, view, "edit-enabled", (neo_value){.kind=NEO_BOOLEAN,.boolean=true}); }
        if (status == NEO_OK && !neo_test_spatial_call(vm, view, "edit-vertex", NEO_OK)) { status = NEO_BAD_STATE; }
        if (status == NEO_OK && !neo_test_matrix_value(vm, geometry, "vertices", (neo_value){.kind=NEO_INTEGERS,.integers=expected,.count=2*n})) { status = NEO_BAD_STATE; }
    }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, geometry, "dimension", (neo_value){.kind=NEO_INTEGER,.integer=3}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, geometry, "vertex-count", (neo_value){.kind=NEO_INTEGER,.integer=1}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, geometry, "vertices", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){1000000,2000000,5000000},.count=3}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, view, "view-dimension", (neo_value){.kind=NEO_INTEGER,.integer=2}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, view, "map", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){1000000,1000000,0,0,1000000,1000000},.count=6}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, view, "translation", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){3000000,4000000},.count=2}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, view, "lift", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){1000000,-1000000,0,1000000,0,0},.count=6}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, view, "vertex-index", (neo_value){.kind=NEO_INTEGER,.integer=0}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, view, "edit", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){9000000,8000000},.count=2}); }
    if (status == NEO_OK && !neo_test_spatial_call(vm, view, "edit-vertex", NEO_OK)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, geometry, "vertices", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){7000000,-1000000,5000000},.count=3})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_spatial_call(vm, view, "project", NEO_OK)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, view, "result", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){9000000,8000000},.count=2})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, view, "lift", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){0,0,0,0,0,0},.count=6}); }
    if (status == NEO_OK && !neo_test_spatial_call(vm, view, "edit-vertex", NEO_RAISED)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, geometry, "vertices", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){7000000,-1000000,5000000},.count=3})) { status = NEO_BAD_STATE; }
    if (root != NULL) { (void)neo_image_unload(vm, root); }
    if (geometry_root != NULL) { (void)neo_image_unload(vm, geometry_root); }
    if (status != NEO_OK) { fprintf(stderr, "FAIL affine view projection/lift/edit/retention: %s\n", neo_status_name(status)); }
    return status == NEO_OK;
}


static bool neo_test_perspective_package(neo_vm *vm) {
    const neo_capability *root = NULL, *geometry_root = NULL, *contents = NULL, *imported = NULL;
    const neo_capability *view = NULL, *geometry_contents = NULL, *geometry = NULL;
    neo_diagnostic diagnostic = {0};
    neo_status status = neo_platform_load_image(vm, "neo/packages/perspective.neo", &root, &diagnostic);
    if (status == NEO_OK) { status = neo_platform_load_image(vm, "neo/packages/geometry.neo", &geometry_root, &diagnostic); }
    if (status == NEO_OK) { status = neo_object_child(vm, root, "contents", &contents); }
    if (status == NEO_OK) { status = neo_object_child(vm, contents, "view", &view); }
    if (status == NEO_OK) { status = neo_object_import(vm, geometry_root, contents, "geometry-package", &imported); }
    if (status == NEO_OK) { status = neo_object_child(vm, imported, "contents", &geometry_contents); }
    if (status == NEO_OK) { status = neo_object_child(vm, geometry_contents, "geometry", &geometry); }
    if (status == NEO_OK) { status = neo_object_connect(vm, view, "source", geometry, NEO_READ); }
    const int64_t dimensions[] = {0,1,2,4,7,33,100,257};
    int64_t vertices[514], map[771], projected[4];
    for (size_t d = 0; status == NEO_OK && d < sizeof(dimensions)/sizeof(dimensions[0]); ++d) {
        size_t n = (size_t)dimensions[d], m = n < 2 ? n : 2;
        for (size_t i = 0; i < 514; ++i) { vertices[i] = (int64_t)i; }
        for (size_t i = 0; i < 771; ++i) { map[i] = 0; }
        for (size_t j = 0; j < m; ++j) { map[j*n+j] = 1000000; }
        for (size_t i = 0; i < 2; ++i) { for (size_t j = 0; j < m; ++j) { projected[i*m+j] = (vertices[i*n+j]+1)/2; } }
        status = neo_test_matrix_write(vm, geometry, "dimension", (neo_value){.kind=NEO_INTEGER,.integer=dimensions[d]});
        if (status == NEO_OK) { status = neo_test_matrix_write(vm, geometry, "vertex-count", (neo_value){.kind=NEO_INTEGER,.integer=2}); }
        if (status == NEO_OK) { status = neo_test_matrix_write(vm, geometry, "vertices", (neo_value){.kind=NEO_INTEGERS,.integers=vertices,.count=2*n}); }
        if (status == NEO_OK) { status = neo_test_matrix_write(vm, view, "view-dimension", (neo_value){.kind=NEO_INTEGER,.integer=(int64_t)m}); }
        if (status == NEO_OK && !neo_test_spatial_call(vm, view, "initialize", NEO_OK)) { status = NEO_BAD_STATE; }
        if (status == NEO_OK) { status = neo_test_matrix_write(vm, view, "map", (neo_value){.kind=NEO_INTEGERS,.integers=map,.count=(m+1)*n}); }
        int64_t offset[3] = {0}; offset[m] = 2000000;
        if (status == NEO_OK) { status = neo_test_matrix_write(vm, view, "offset", (neo_value){.kind=NEO_INTEGERS,.integers=offset,.count=m+1}); }
        if (status == NEO_OK && !neo_test_spatial_call(vm, view, "project", NEO_OK)) { status = NEO_BAD_STATE; }
        if (status == NEO_OK && (!neo_test_matrix_value(vm, view, "result", (neo_value){.kind=NEO_INTEGERS,.integers=projected,.count=2*m}) ||
            !neo_test_matrix_value(vm, view, "source-indices", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){0,1},.count=2}) ||
            !neo_test_matrix_value(vm, view, "visibility", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){1,1},.count=2}))) { status = NEO_BAD_STATE; }
    }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, geometry, "dimension", (neo_value){.kind=NEO_INTEGER,.integer=3}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, geometry, "vertex-count", (neo_value){.kind=NEO_INTEGER,.integer=6}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, geometry, "vertices", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){1000000,0,2000000,0,0,0,0,0,-1000000,0,0,5000000,3000000,0,2000000,-2000000,2000000,2000000},.count=18}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, view, "view-dimension", (neo_value){.kind=NEO_INTEGER,.integer=2}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, view, "map", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){1000000,0,0,0,1000000,0,0,0,1000000},.count=9}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, view, "offset", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){0,0,0},.count=3}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, view, "near", (neo_value){.kind=NEO_INTEGER,.integer=1000000}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, view, "far", (neo_value){.kind=NEO_INTEGER,.integer=4000000}); }
    if (status == NEO_OK && !neo_test_spatial_call(vm, view, "project", NEO_OK)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, view, "result", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){500000,0,-1000000,1000000},.count=4})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, view, "source-indices", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){0,5},.count=2})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, view, "visibility", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){1,0,0,0,0,1},.count=6})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, view, "clip-codes", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){0,1,1,2,3,0},.count=6})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, view, "denominators", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){2000000,0,-1000000,5000000,2000000,2000000},.count=6})) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, view, "near", (neo_value){.kind=NEO_INTEGER,.integer=0}); }
    if (status == NEO_OK && !neo_test_spatial_call(vm, view, "project", NEO_RAISED)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, view, "source-indices", (neo_value){.kind=NEO_INTEGERS,.integers=(const int64_t[]){0,5},.count=2})) { status = NEO_BAD_STATE; }
    if (root != NULL) { (void)neo_image_unload(vm, root); }
    if (geometry_root != NULL) { (void)neo_image_unload(vm, geometry_root); }
    if (status != NEO_OK) { fprintf(stderr, "FAIL perspective dimensions/denominator/clipping/retention: %s\n", neo_status_name(status)); }
    return status == NEO_OK;
}

static bool neo_test_network(neo_vm *vm, neo_test_allocator *allocator) {
    const neo_network_backend *network = NULL;
    neo_status status = neo_platform_network(vm, &network);
#ifndef NEO_PLATFORM_WITH_WIN32
    (void)allocator;
    return status == NEO_UNSUPPORTED;
#else
    if (status != NEO_OK) { return false; }
    const neo_capability *root = NULL, *client_stream = NULL, *server_stream = NULL;
    const neo_capability *restricted = NULL;
    neo_network_socket *listener = NULL, *client = NULL, *server = NULL;
    neo_diagnostic diagnostic = {0};
    status = neo_image_parse(vm, "(image)", &root, &diagnostic);
    if (status == NEO_OK) { status = network->listen("127.0.0.1", 0, &listener); }
    uint16_t port = 0;
    if (status == NEO_OK) { status = network->local_port(listener, &port); }
    if (status == NEO_OK && port == 0) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) {
        status = network->accept(listener, &server);
        if (status == NEO_OK && server != NULL) { status = NEO_BAD_STATE; }
    }
    if (status == NEO_OK) {
        neo_network_socket *invalid = NULL;
        if (network->connect("not-an-address", port, &invalid) != NEO_INVALID || invalid != NULL) {
            status = NEO_BAD_STATE;
        }
    }
    if (status == NEO_OK) { status = network->connect("127.0.0.1", port, &client); }
    bool ready = false;
    for (size_t i = 0; status == NEO_OK && (!ready || server == NULL) && i < 2000; ++i) {
        status = network->ready(client, &ready);
        if (status == NEO_OK && server == NULL) { status = network->accept(listener, &server); }
        if (status == NEO_OK && (!ready || server == NULL)) { status = neo_platform_wait(vm, 1); }
    }
    if (status == NEO_OK && (!ready || server == NULL)) { status = NEO_BUSY; }
    if (status == NEO_OK) { status = neo_capability_restrict(vm, root, NEO_READ, &restricted); }
    if (status == NEO_OK && network->attach(client, vm, restricted, "denied", &client_stream) != NEO_DENIED) {
        status = NEO_BAD_STATE;
    }
    if (status == NEO_OK) {
        allocator->fail_at = allocator->calls + 1;
        neo_status failed = network->attach(client, vm, root, "failed", &client_stream);
        allocator->fail_at = 0;
        if (failed != NEO_OUT_OF_MEMORY || client_stream != NULL) { status = NEO_BAD_STATE; }
    }
    if (status == NEO_OK) {
        status = network->attach(client, vm, root, "client", &client_stream);
        if (status == NEO_OK) { client = NULL; }
    }
    if (status == NEO_OK) {
        status = network->attach(server, vm, root, "server", &server_stream);
        if (status == NEO_OK) { server = NULL; }
    }
    uint8_t bytes[4] = {0};
    const uint8_t payload[] = {0, 255, 42, 10};
    neo_io_result result = {0};
    if (status == NEO_OK) {
        status = neo_stream_read(vm, server_stream, bytes, sizeof(bytes), &result);
        if (status == NEO_OK && result.state != NEO_IO_WOULD_BLOCK) { status = NEO_BAD_STATE; }
    }
    if (status == NEO_OK) { status = neo_capability_restrict(vm, client_stream, NEO_READ, &restricted); }
    if (status == NEO_OK && neo_stream_write(vm, restricted, payload, sizeof(payload), &result) != NEO_DENIED) {
        status = NEO_BAD_STATE;
    }
    size_t sent = 0, received = 0;
    for (size_t i = 0; status == NEO_OK && received < sizeof(bytes) && i < 2000; ++i) {
        if (sent < sizeof(payload)) {
            status = neo_stream_write(vm, client_stream, payload + sent, sizeof(payload) - sent, &result);
            if (status == NEO_OK) { sent += result.count; }
        }
        if (status == NEO_OK) {
            status = neo_stream_read(vm, server_stream, bytes + received, sizeof(bytes) - received, &result);
            if (status == NEO_OK) { received += result.count; }
        }
        if (status == NEO_OK && received < sizeof(bytes)) { status = neo_platform_wait(vm, 1); }
    }
    if (status == NEO_OK && (received != sizeof(bytes) || memcmp(bytes, payload, sizeof(bytes)) != 0)) {
        status = NEO_BAD_STATE;
    }
    if (status == NEO_OK) { status = neo_object_delete(vm, client_stream); }
    bool eof = false;
    for (size_t i = 0; status == NEO_OK && !eof && i < 2000; ++i) {
        status = neo_stream_read(vm, server_stream, bytes, sizeof(bytes), &result);
        eof = status == NEO_OK && result.state == NEO_IO_EOF;
        if (status == NEO_OK && !eof) { status = neo_platform_wait(vm, 1); }
    }
    if (status == NEO_OK && !eof) { status = NEO_BAD_STATE; }
    network->close(client); network->close(server); network->close(listener);
    if (root != NULL) { (void)neo_image_unload(vm, root); }
    if (status != NEO_OK) { fprintf(stderr, "FAIL network loopback/authority/lifetime: %s\n", neo_status_name(status)); }
    return status == NEO_OK;
#endif
}

static bool neo_test_network_endpoints(neo_vm *vm) {
#ifndef NEO_PLATFORM_WITH_WIN32
    (void)vm;
    return true;
#else
    const neo_capability *root = NULL, *actor = NULL, *listener = NULL, *connector = NULL;
    const neo_capability *restricted = NULL, *client = NULL, *server = NULL, *copy = NULL;
    const char *source = "(image (actor (handlers "
        "(listen (body (network-open (target \"listener\")))) "
        "(dial (body (network-open (target \"connector\")))) "
        "(port (body (network-port (target \"listener\")))) "
        "(accept (body (network-stream (target \"listener\") (name \"server\")))) "
        "(publish (body (network-stream (target \"connector\") (name \"client\")))) "
        "(denied (body (network-open (target \"restricted\")))) "
        "(close (body (network-close (target \"listener\")))))))";
    neo_diagnostic error = {0};
    neo_status status = neo_image_parse(vm, source, &root, &error);
    if (status == NEO_OK) { status = neo_object_child(vm, root, "actor", &actor); }
    if (status == NEO_OK) { status = neo_network_endpoint_create(vm, root, "listener", "127.0.0.1", 0, true, &listener); }
    if (status == NEO_OK) { status = neo_object_connect(vm, actor, "listener", listener, NEO_READ | NEO_ACT); }
    if (status == NEO_OK) { status = neo_object_connect(vm, actor, "restricted", listener, NEO_READ); }
    neo_execution report = {0};
    if (status == NEO_OK) {
        if (neo_behavior_run(vm, actor, "denied", NULL, 10000, &report) != NEO_DENIED) { status = NEO_BAD_STATE; }
        neo_execution_release(vm, &report);
    }
    if (status == NEO_OK) { status = neo_behavior_run(vm, actor, "listen", NULL, 10000, &report); }
    neo_execution_release(vm, &report);
    uint16_t port = 0;
    if (status == NEO_OK) { status = neo_behavior_run(vm, actor, "port", NULL, 10000, &report); }
    if (status == NEO_OK && (report.result.kind != NEO_INTEGER || report.result.integer <= 0 || report.result.integer > UINT16_MAX)) {
        status = NEO_BAD_STATE;
    }
    if (status == NEO_OK) { port = (uint16_t)report.result.integer; }
    neo_execution_release(vm, &report);
    if (status == NEO_OK) { status = neo_network_endpoint_create(vm, root, "connector", "127.0.0.1", port, false, &connector); }
    if (status == NEO_OK) { status = neo_object_connect(vm, actor, "connector", connector, NEO_READ | NEO_ACT); }
    if (status == NEO_OK) { status = neo_behavior_run(vm, actor, "dial", NULL, 10000, &report); }
    neo_execution_release(vm, &report);
    bool accepted = false, published = false;
    for (size_t i = 0; status == NEO_OK && (!accepted || !published) && i < 2000; ++i) {
        if (!accepted) {
            status = neo_behavior_run(vm, actor, "accept", NULL, 10000, &report);
            accepted = status == NEO_OK && report.result.kind == NEO_BOOLEAN && report.result.boolean;
            neo_execution_release(vm, &report);
        }
        if (status == NEO_OK && !published) {
            status = neo_behavior_run(vm, actor, "publish", NULL, 10000, &report);
            published = status == NEO_OK && report.result.kind == NEO_BOOLEAN && report.result.boolean;
            neo_execution_release(vm, &report);
        }
        if (status == NEO_OK && (!accepted || !published)) { status = neo_platform_wait(vm, 1); }
    }
    if (status == NEO_OK && (!accepted || !published)) { status = NEO_BUSY; }
    if (status == NEO_OK) { status = neo_object_child(vm, actor, "client", &client); }
    if (status == NEO_OK) { status = neo_object_child(vm, actor, "server", &server); }
    if (status == NEO_OK) { status = neo_capability_restrict(vm, actor, NEO_READ, &restricted); }
    if (status == NEO_OK && neo_network_endpoint_stream(vm, listener, restricted, "denied", &copy) != NEO_DENIED) {
        status = NEO_BAD_STATE;
    }
    if (status == NEO_OK && neo_object_copy(vm, listener, root, "copy", &copy) != NEO_UNSUPPORTED) {
        status = NEO_BAD_STATE;
    }
    char *text = NULL;
    if (status == NEO_OK && neo_image_format(vm, root, &text) != NEO_UNSUPPORTED) { status = NEO_BAD_STATE; }
    neo_image_text_free(vm, text);
    if (status == NEO_OK) { status = neo_behavior_run(vm, actor, "close", NULL, 10000, &report); }
    neo_execution_release(vm, &report);
    /* Closing a listener must leave its already accepted stream usable. */
    uint8_t payload = 73, received = 0;
    neo_io_result result = {0};
    if (status == NEO_OK) { status = neo_stream_write(vm, client, &payload, 1, &result); }
    bool transferred = false;
    for (size_t i = 0; status == NEO_OK && !transferred && i < 2000; ++i) {
        status = neo_stream_read(vm, server, &received, 1, &result);
        transferred = status == NEO_OK && result.state == NEO_IO_TRANSFERRED;
        if (status == NEO_OK && !transferred) { status = neo_platform_wait(vm, 1); }
    }
    if (status == NEO_OK && (!transferred || received != payload)) { status = NEO_BAD_STATE; }
    if (root != NULL) { (void)neo_image_unload(vm, root); }
    if (status != NEO_OK) { fprintf(stderr, "FAIL image network grants/lifecycle: %s (%s)\n", neo_status_name(status), error.message); }
    return status == NEO_OK;
#endif
}

typedef struct neo_test_replica_pipe {
    uint8_t bytes[8192];
    size_t size, position, calls, destroyed;
    bool eof;
} neo_test_replica_pipe;
static neo_status neo_test_replica_write(void *context, const uint8_t *bytes, size_t count, neo_io_result *out) {
    neo_test_replica_pipe *pipe = context;
    if (++pipe->calls % 3 == 0) { *out = (neo_io_result){NEO_IO_WOULD_BLOCK,0}; return NEO_OK; }
    if (count > 3) { count = 3; }
    if (count > sizeof(pipe->bytes) - pipe->size) { return NEO_LIMIT; }
    memcpy(pipe->bytes + pipe->size, bytes, count); pipe->size += count;
    *out = (neo_io_result){NEO_IO_TRANSFERRED,count}; return NEO_OK;
}
static neo_status neo_test_replica_read(void *context, uint8_t *bytes, size_t count, neo_io_result *out) {
    neo_test_replica_pipe *pipe = context;
    if (++pipe->calls % 3 == 0) { *out = (neo_io_result){NEO_IO_WOULD_BLOCK,0}; return NEO_OK; }
    if (pipe->position == pipe->size) {
        *out = (neo_io_result){pipe->eof ? NEO_IO_EOF : NEO_IO_WOULD_BLOCK,0}; return NEO_OK;
    }
    if (count > 2) { count = 2; }
    if (count > pipe->size - pipe->position) { count = pipe->size - pipe->position; }
    memcpy(bytes, pipe->bytes + pipe->position, count); pipe->position += count;
    *out = (neo_io_result){NEO_IO_TRANSFERRED,count}; return NEO_OK;
}
static void neo_test_replica_destroy(void *context) { ++((neo_test_replica_pipe *)context)->destroyed; }
static bool neo_test_replicas(neo_vm *vm, neo_test_allocator *allocator) {
    const neo_capability *root = NULL, *source = NULL, *value = NULL, *private = NULL, *destination = NULL;
    const neo_capability *stream = NULL, *sender = NULL, *receiver = NULL, *copy = NULL, *restricted = NULL;
    neo_test_replica_pipe pipe = {0};
    const neo_stream_backend backend = {neo_test_replica_read,neo_test_replica_write,neo_test_replica_destroy};
    neo_diagnostic error = {0};
    neo_status status = neo_image_parse(vm,
        "(image (public #p @inside #p 1 (value 7) (handlers (main (body (write (slot \"value\") (value 999)))))) (private 99) (quarantine))",
        &root, &error);
    if (status == NEO_OK) { status = neo_object_child(vm, root, "public", &source); }
    if (status == NEO_OK) { status = neo_object_child(vm, source, "value", &value); }
    if (status == NEO_OK) { status = neo_object_child(vm, root, "private", &private); }
    if (status == NEO_OK) { status = neo_object_child(vm, root, "quarantine", &destination); }
    if (status == NEO_OK) { status = neo_stream_create(vm, root, "pipe", &backend, &pipe, &stream); }
    if (status == NEO_OK) { status = neo_replica_channel_create(vm, stream, root, "sender", &sender); }
    if (status == NEO_OK) { status = neo_replica_channel_create(vm, stream, root, "receiver", &receiver); }
    if (status == NEO_OK) { status = neo_capability_restrict(vm, sender, NEO_READ, &restricted); }
    if (status == NEO_OK && neo_replica_prepare(vm, restricted, source) != NEO_DENIED) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_capability_restrict(vm, source, NEO_READ, &restricted); }
    if (status == NEO_OK && neo_replica_prepare(vm, sender, restricted) != NEO_DENIED) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_object_connect(vm, source, "private", private, NEO_READ); }
    if (status == NEO_OK && neo_replica_prepare(vm, sender, source) != NEO_WRONG_IMAGE) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_object_disconnect(vm, source, "private"); }
    if (status == NEO_OK) {
        allocator->fail_at = allocator->calls + 1;
        neo_status failed = neo_replica_prepare(vm, sender, source);
        allocator->fail_at = 0;
        if (failed != NEO_OUT_OF_MEMORY) { status = NEO_BAD_STATE; }
    }
    if (status == NEO_OK) { status = neo_replica_prepare(vm, sender, source); }
    if (status == NEO_OK && neo_replica_prepare(vm, sender, source) != NEO_BUSY) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_object_write(vm, value, (neo_value){.kind=NEO_INTEGER,.integer=9}); }
    bool complete = false;
    for (size_t i = 0; status == NEO_OK && !complete && i < 8192; ++i) { status = neo_replica_send(vm, sender, &complete); }
    if (status == NEO_OK && !complete) { status = NEO_BUSY; }
    complete = false;
    for (size_t i = 0; status == NEO_OK && !complete && i < 8192; ++i) { status = neo_replica_receive(vm, receiver, &complete); }
    if (status == NEO_OK && !complete) { status = NEO_BUSY; }
    size_t count = 0;
    if (status == NEO_OK) { status = neo_object_child_count(vm, destination, &count); }
    if (status == NEO_OK && count != 0) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_capability_restrict(vm, destination, NEO_READ, &restricted); }
    if (status == NEO_OK && neo_replica_import(vm, receiver, restricted, "replica", &copy) != NEO_DENIED) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) {
        allocator->fail_at = allocator->calls + 1;
        neo_status failed = neo_replica_import(vm, receiver, destination, "replica", &copy);
        allocator->fail_at = 0;
        if (failed != NEO_OUT_OF_MEMORY || copy != NULL) { status = NEO_BAD_STATE; }
    }
    if (status == NEO_OK) { status = neo_replica_import(vm, receiver, destination, "replica", &copy); }
    const neo_capability *copied_value = NULL, *inside = NULL;
    if (status == NEO_OK) { status = neo_object_child(vm, copy, "value", &copied_value); }
    neo_value payload = {0};
    if (status == NEO_OK) { status = neo_object_read(vm, copied_value, &payload); }
    if (status == NEO_OK && (payload.kind != NEO_INTEGER || payload.integer != 7)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_object_connection(vm, copy, "inside", &inside); }
    neo_object_id a = 0, b = 0;
    if (status == NEO_OK) { status = neo_object_identity(vm, copy, &a); }
    if (status == NEO_OK) { status = neo_object_identity(vm, inside, &b); }
    if (status == NEO_OK && a != b) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && neo_object_child(vm, copy, "private", &restricted) != NEO_UNAVAILABLE) { status = NEO_BAD_STATE; }
    /* EOF between frames is a retained observation, never another snapshot. */
    bool ended = true;
    if (status == NEO_OK) { status = neo_replica_ended(vm, receiver, &ended); }
    if (status == NEO_OK && ended) { status = NEO_BAD_STATE; }
    pipe.eof = true;
    for (size_t i = 0; status == NEO_OK && !ended && i < 10; ++i) {
        status = neo_replica_receive(vm, receiver, &complete);
        if (status == NEO_OK && complete) { status = NEO_BAD_STATE; }
        if (status == NEO_OK) { status = neo_replica_ended(vm, receiver, &ended); }
    }
    if (status == NEO_OK && !ended) { status = NEO_BAD_STATE; }
    size_t calls_at_end = pipe.calls;
    if (status == NEO_OK) { status = neo_replica_receive(vm, receiver, &complete); }
    if (status == NEO_OK && (complete || pipe.calls != calls_at_end)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && neo_replica_import(vm, receiver, destination, "no-frame", &copy) != NEO_UNAVAILABLE) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_capability_restrict(vm, receiver, NEO_READ, &restricted); }
    if (status == NEO_OK && neo_replica_ended(vm, restricted, &ended) != NEO_DENIED) { status = NEO_BAD_STATE; }
    const neo_capability *write_channel = NULL;
    if (status == NEO_OK) { status = neo_capability_restrict(vm, stream, NEO_WRITE, &restricted); }
    if (status == NEO_OK) { status = neo_replica_channel_create(vm, restricted, root, "write-channel", &write_channel); }
    if (status == NEO_OK && neo_replica_ended(vm, write_channel, &ended) != NEO_DENIED) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_object_delete(vm, receiver); }
    if (status == NEO_OK) { status = neo_replica_channel_create(vm, stream, root, "receiver", &receiver); }
    /* Unknown wire version is rejected and remains failed. */
    pipe.position = 0; pipe.size = 12; pipe.calls = 0; pipe.bytes[7] = 2;
    complete = false;
    if (status == NEO_OK) {
        neo_status invalid = NEO_OK;
        for (size_t i = 0; invalid == NEO_OK && i < 20; ++i) { invalid = neo_replica_receive(vm, receiver, &complete); }
        if (invalid != NEO_INVALID || neo_replica_receive(vm, receiver, &complete) != NEO_IO_ERROR) { status = NEO_BAD_STATE; }
    }
    if (root != NULL) { (void)neo_image_unload(vm, root); }
    if (status == NEO_OK && pipe.destroyed != 1) { status = NEO_BAD_STATE; }
    if (status != NEO_OK) { fprintf(stderr, "FAIL snapshots/partial framing/inert receipt: %s (%s)\n", neo_status_name(status), error.message); }
    return status == NEO_OK;
}

static bool neo_test_replica_invalid(neo_vm *vm) {
    for (size_t scenario = 0; scenario < 7; ++scenario) {
        neo_test_replica_pipe pipe = {.size=12,.eof=true};
        memcpy(pipe.bytes, "NEOS\0\0\0\1", 8);
        const char *payload = "(object)";
        uint32_t declared = (uint32_t)strlen(payload);
        if (scenario == 0) { declared = 0; }
        if (scenario == 1) { declared = NEO_REPLICA_MAX_BYTES + 1; }
        if (scenario == 2) { pipe.size = 6; }
        if (scenario == 3) { declared = 8; payload = "(x"; }
        if (scenario == 4) { declared = 4; payload = "(x\0)"; }
        if (scenario == 5) { declared = 2; payload = "(x"; }
        if (scenario == 6) { payload = "(x @out #missing 1)"; declared = (uint32_t)strlen(payload); }
        for (size_t i = 0; i < 4; ++i) { pipe.bytes[8 + i] = (uint8_t)(declared >> (24u - (unsigned)i * 8u)); }
        if (scenario >= 3) {
            size_t actual = scenario == 4 ? 4 : strlen(payload);
            memcpy(pipe.bytes + 12, payload, actual); pipe.size += actual;
        }
        const neo_capability *root = NULL, *stream = NULL, *channel = NULL, *copy = NULL;
        const neo_stream_backend backend = {neo_test_replica_read,neo_test_replica_write,neo_test_replica_destroy};
        neo_diagnostic error = {0};
        neo_status status = neo_image_parse(vm, "(image)", &root, &error);
        if (status == NEO_OK) { status = neo_stream_create(vm, root, "pipe", &backend, &pipe, &stream); }
        if (status == NEO_OK) { status = neo_replica_channel_create(vm, stream, root, "channel", &channel); }
        bool complete = false;
        neo_status received = NEO_OK;
        for (size_t i = 0; status == NEO_OK && received == NEO_OK && !complete && i < 100; ++i) {
            received = neo_replica_receive(vm, channel, &complete);
        }
        if (status == NEO_OK && scenario < 4) {
            neo_status expected = scenario < 2 ? NEO_LIMIT : NEO_IO_ERROR;
            if (received != expected || complete) { status = NEO_BAD_STATE; }
        } else if (status == NEO_OK) {
            if (received != NEO_OK || !complete || neo_replica_import(vm, channel, root, "replica", &copy) == NEO_OK || copy != NULL) {
                status = NEO_BAD_STATE;
            }
            size_t count = 0;
            if (status == NEO_OK) { status = neo_object_child_count(vm, root, &count); }
            if (status == NEO_OK && count != 2) { status = NEO_BAD_STATE; }
        }
        if (root != NULL) { (void)neo_image_unload(vm, root); }
        if (status != NEO_OK || pipe.destroyed != 1) {
            fprintf(stderr, "FAIL invalid replica scenario %zu: %s\n", scenario, neo_status_name(status)); return false;
        }
    }
    return true;
}

static neo_status neo_test_exposure_write(neo_vm *vm, const neo_capability *object,
                                           const char *name, neo_value value) {
    const neo_capability *field = NULL;
    neo_status status = neo_object_child(vm, object, name, &field);
    return status == NEO_OK ? neo_object_write(vm, field, value) : status;
}
static neo_status neo_test_exposure_poll(neo_vm *vm, const neo_capability *object, const char *selector) {
    for (size_t i = 0; i < 8192; ++i) {
        neo_execution report = {0};
        neo_status status = neo_behavior_run(vm, object, selector, NULL, 100000, &report);
        bool complete = status == NEO_OK && report.result.kind == NEO_BOOLEAN && report.result.boolean;
        if (status == NEO_OK && report.result.kind != NEO_BOOLEAN) { status = NEO_BAD_STATE; }
        neo_execution_release(vm, &report);
        if (status != NEO_OK || complete) { return status; }
    }
    return NEO_LIMIT;
}
static bool neo_test_exposure(neo_vm *vm) {
    const neo_capability *root = NULL, *contents = NULL, *publisher = NULL, *subscriber = NULL;
    const neo_capability *scope = NULL, *field = NULL, *stream = NULL, *sender = NULL, *receiver = NULL;
    const neo_capability *accepted = NULL, *record = NULL, *state = NULL;
    neo_diagnostic error = {0};
    neo_test_replica_pipe pipe = {0};
    const neo_stream_backend backend = {neo_test_replica_read,neo_test_replica_write,neo_test_replica_destroy};
    neo_status status = neo_platform_load_image(vm, "neo/packages/exposure.neo", &root, &error);
    if (status == NEO_OK) { status = neo_package_validate(vm, root); }
    if (status == NEO_OK) { status = neo_object_child(vm, root, "contents", &contents); }
    if (status == NEO_OK) { status = neo_object_child(vm, contents, "publisher", &publisher); }
    if (status == NEO_OK) { status = neo_object_child(vm, contents, "subscriber", &subscriber); }
    if (status == NEO_OK) { status = neo_object_create(vm, root, "scope", (neo_value){0}, &scope); }
    if (status == NEO_OK) { status = neo_object_create(vm, scope, "value", (neo_value){.kind=NEO_INTEGER,.integer=42}, &field); }
    if (status == NEO_OK) { status = neo_stream_create(vm, root, "pipe", &backend, &pipe, &stream); }
    if (status == NEO_OK) { status = neo_replica_channel_create(vm, stream, root, "sender", &sender); }
    if (status == NEO_OK) { status = neo_replica_channel_create(vm, stream, root, "receiver", &receiver); }
    if (status == NEO_OK) { status = neo_object_connect(vm, publisher, "scope", scope, NEO_READ | NEO_COPY); }
    if (status == NEO_OK) { status = neo_object_connect(vm, publisher, "channel", sender, NEO_READ | NEO_ACT); }
    if (status == NEO_OK) { status = neo_object_connect(vm, subscriber, "channel", receiver, NEO_READ | NEO_ACT); }
    const neo_capability *services[] = {publisher,subscriber};
    for (size_t i = 0; status == NEO_OK && i < 2; ++i) {
        status = neo_test_exposure_write(vm, services[i], "origin", (neo_value){.kind=NEO_TEXT,.text="peer"});
        if (status == NEO_OK) { status = neo_test_exposure_write(vm, services[i], "publication", (neo_value){.kind=NEO_TEXT,.text="state"}); }
    }
    for (int64_t revision = 1; status == NEO_OK && revision <= 2; ++revision) {
        if (!neo_test_message_call(vm, publisher, "publish", NEO_OK)) { status = NEO_BAD_STATE; }
        if (status == NEO_OK) { status = neo_object_write(vm, field, (neo_value){.kind=NEO_INTEGER,.integer=42 + revision}); }
        if (status == NEO_OK) { status = neo_test_exposure_poll(vm, publisher, "send"); }
        if (status == NEO_OK) { status = neo_test_exposure_poll(vm, subscriber, "receive"); }
        const neo_capability *revision_field = NULL;
        neo_value value = {0};
        if (status == NEO_OK) { status = neo_object_child(vm, subscriber, "revision", &revision_field); }
        if (status == NEO_OK) { status = neo_object_read(vm, revision_field, &value); }
        if (status == NEO_OK && (value.kind != NEO_INTEGER || value.integer != revision - 1)) { status = NEO_BAD_STATE; }
        if (status == NEO_OK) { status = neo_test_exposure_write(vm, subscriber, "accept-name",
            (neo_value){.kind=NEO_TEXT,.text=revision == 1 ? "first" : "second"}); }
        if (status == NEO_OK && !neo_test_message_call(vm, subscriber, "accept", NEO_OK)) { status = NEO_BAD_STATE; }
        if (status == NEO_OK) { status = neo_object_child(vm, subscriber, "accepted", &accepted); }
        if (status == NEO_OK) { status = neo_object_child(vm, accepted, revision == 1 ? "first" : "second", &record); }
        if (status == NEO_OK) { status = neo_object_child(vm, record, "state", &state); }
        const neo_capability *copied = NULL;
        if (status == NEO_OK) { status = neo_object_child(vm, state, "value", &copied); }
        if (status == NEO_OK) { status = neo_object_read(vm, copied, &value); }
        if (status == NEO_OK && (value.kind != NEO_INTEGER || value.integer != 41 + revision)) { status = NEO_BAD_STATE; }
    }
    /* Replay revision 2: editable image policy refuses it; candidate stays inert. */
    if (status == NEO_OK) { status = neo_test_exposure_write(vm, publisher, "revision", (neo_value){.kind=NEO_INTEGER,.integer=1}); }
    if (status == NEO_OK && !neo_test_message_call(vm, publisher, "publish", NEO_OK)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_test_exposure_poll(vm, publisher, "send"); }
    if (status == NEO_OK && neo_test_exposure_poll(vm, subscriber, "receive") != NEO_RAISED) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_message_call(vm, subscriber, "accept", NEO_RAISED)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_message_call(vm, subscriber, "reject", NEO_OK)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_message_call(vm, publisher, "withdraw", NEO_OK)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_test_exposure_poll(vm, publisher, "send"); }
    if (status == NEO_OK) { status = neo_test_exposure_poll(vm, subscriber, "receive"); }
    if (status == NEO_OK) { status = neo_test_exposure_write(vm, subscriber, "accept-name", (neo_value){.kind=NEO_TEXT,.text="withdrawal"}); }
    if (status == NEO_OK && !neo_test_message_call(vm, subscriber, "accept", NEO_OK)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_message_call(vm, publisher, "publish", NEO_RAISED)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_object_child(vm, accepted, "withdrawal", &record); }
    if (status == NEO_OK && neo_object_child(vm, record, "state", &state) != NEO_UNAVAILABLE) { status = NEO_BAD_STATE; }
    /* Changing image policy can accept an older revision; the VM does not order it. */
    if (status == NEO_OK && !neo_test_message_call(vm, publisher, "resume", NEO_OK)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_test_exposure_write(vm, publisher, "revision", (neo_value){.kind=NEO_INTEGER,.integer=1}); }
    if (status == NEO_OK && !neo_test_message_call(vm, publisher, "publish", NEO_OK)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_test_exposure_poll(vm, publisher, "send"); }
    if (status == NEO_OK && neo_test_exposure_poll(vm, subscriber, "receive") != NEO_RAISED) { status = NEO_BAD_STATE; }
    const neo_capability *handlers = NULL, *policy = NULL, *body = NULL;
    if (status == NEO_OK) { status = neo_object_child(vm, subscriber, "handlers", &handlers); }
    if (status == NEO_OK) { status = neo_object_child(vm, handlers, "revision-policy", &policy); }
    if (status == NEO_OK) { status = neo_object_child(vm, policy, "body", &body); }
    if (status == NEO_OK) { status = neo_object_delete(vm, body); }
    if (status == NEO_OK) { status = neo_object_create(vm, policy, "body", (neo_value){.kind=NEO_BOOLEAN,.boolean=true}, &body); }
    if (status == NEO_OK) { status = neo_test_exposure_write(vm, subscriber, "accept-name", (neo_value){.kind=NEO_TEXT,.text="custom-policy"}); }
    if (status == NEO_OK && !neo_test_message_call(vm, subscriber, "accept", NEO_OK)) { status = NEO_BAD_STATE; }
    /* Image policy cannot manufacture COPY on its exposed source. */
    if (status == NEO_OK) { status = neo_object_disconnect(vm, publisher, "scope"); }
    if (status == NEO_OK) { status = neo_object_connect(vm, publisher, "scope", scope, NEO_READ); }
    if (status == NEO_OK && !neo_test_message_call(vm, publisher, "publish", NEO_DENIED)) { status = NEO_BAD_STATE; }
    if (root != NULL) { (void)neo_image_unload(vm, root); }
    if (status == NEO_OK && pipe.destroyed != 1) { status = NEO_BAD_STATE; }
    if (status != NEO_OK) { fprintf(stderr, "FAIL image exposure/revisions/withdrawal: %s (%s)\n", neo_status_name(status), error.message); }
    return status == NEO_OK;
}

static bool neo_test_reply_counter(neo_vm *vm, const neo_capability *publisher, const char *name, int64_t expected) {
    const neo_capability *field = NULL;
    neo_value value = {0};
    return neo_object_child(vm, publisher, name, &field) == NEO_OK &&
        neo_object_read(vm, field, &value) == NEO_OK && value.kind == NEO_INTEGER && value.integer == expected;
}
static neo_status neo_test_reply_poll(neo_vm *vm, const neo_capability *publisher,
                                      const neo_capability *subscriber, const char *name, int64_t expected) {
    for (size_t i = 0; i < 8192; ++i) {
        if (!neo_test_message_call(vm, subscriber, "send-reply", NEO_OK) ||
            !neo_test_message_call(vm, publisher, "receive-reply", NEO_OK)) { return NEO_BAD_STATE; }
        if (neo_test_reply_counter(vm, publisher, name, expected)) { return NEO_OK; }
    }
    return NEO_LIMIT;
}
static bool neo_test_network_package_capacity(neo_vm *vm, const char *path, const char *id,
                                               const char *first, const char *second) {
    const neo_capability *source = NULL, *root = NULL, *catalogue = NULL, *record = NULL, *prototype = NULL, *copy = NULL;
    neo_diagnostic diagnostic = {0};
    neo_status status = neo_platform_load_image(vm, path, &source, &diagnostic);
    if (status == NEO_OK) { status = neo_image_parse(vm, "(image (catalogue))", &root, &diagnostic); }
    if (status == NEO_OK) { status = neo_object_child(vm, root, "catalogue", &catalogue); }
    if (status == NEO_OK) { status = neo_package_install(vm, catalogue, source, false, &record); }
    const char *exports[] = {first,second};
    for (size_t i = 0; status == NEO_OK && i < 2 && exports[i] != NULL; ++i) {
        status = neo_package_resolve(vm, catalogue, id, exports[i], NEO_READ | NEO_COPY, &prototype);
        if (status == NEO_OK) { status = neo_object_extract(vm, prototype, &copy); }
        char *instance_text = NULL;
        if (status == NEO_OK) { status = neo_image_format(vm, copy, &instance_text); }
        neo_image_text_free(vm, instance_text);
        if (copy != NULL) { (void)neo_image_unload(vm, copy); copy = NULL; }
    }
    char *text = NULL;
    if (status == NEO_OK) { status = neo_image_format(vm, root, &text); }
    neo_image_text_free(vm, text);
    if (root != NULL) { (void)neo_image_unload(vm, root); }
    if (source != NULL) { (void)neo_image_unload(vm, source); }
    if (status != NEO_OK) { fprintf(stderr, "FAIL minimal networking package capacity %s: %s\n", id, neo_status_name(status)); }
    return status == NEO_OK;
}
static bool neo_test_reply_flag(neo_vm *vm, const neo_capability *service, const char *name) {
    const neo_capability *field = NULL;
    neo_value value = {0};
    return neo_object_child(vm, service, name, &field) == NEO_OK &&
        neo_object_read(vm, field, &value) == NEO_OK && value.kind == NEO_BOOLEAN && value.boolean;
}
static neo_status neo_test_probe(neo_vm *vm, const neo_capability *publisher,
                                 const neo_capability *subscriber, int64_t sequence) {
    if (!neo_test_message_call(vm, publisher, "probe", NEO_OK)) { return NEO_BAD_STATE; }
    for (size_t i = 0; neo_test_reply_flag(vm, publisher, "probe-pending") && i < 8192; ++i) {
        if (!neo_test_message_call(vm, publisher, "send-probe", NEO_OK)) { return NEO_BAD_STATE; }
    }
    if (neo_test_reply_flag(vm, publisher, "probe-pending")) { return NEO_LIMIT; }
    for (size_t i = 0; !neo_test_reply_counter(vm, subscriber, "last-probe", sequence) && i < 8192; ++i) {
        if (!neo_test_message_call(vm, subscriber, "receive-live", NEO_OK)) { return NEO_BAD_STATE; }
    }
    if (!neo_test_reply_counter(vm, subscriber, "last-probe", sequence)) { return NEO_LIMIT; }
    return neo_test_reply_poll(vm, publisher, subscriber, "awaiting-probe", 0);
}
static bool neo_test_acknowledgements(neo_vm *vm) {
    const neo_capability *root = NULL, *contents = NULL, *publisher = NULL, *subscriber = NULL;
    const neo_capability *scope = NULL, *stream = NULL, *outgoing = NULL, *incoming = NULL;
    neo_test_replica_pipe pipe = {0};
    const neo_stream_backend backend = {neo_test_replica_read,neo_test_replica_write,neo_test_replica_destroy};
    neo_diagnostic diagnostic = {0};
    neo_status status = neo_platform_load_image(vm, "neo/packages/exposure.neo", &root, &diagnostic);
    if (status == NEO_OK) { status = neo_object_child(vm, root, "contents", &contents); }
    if (status == NEO_OK) { status = neo_object_child(vm, contents, "publisher", &publisher); }
    if (status == NEO_OK) { status = neo_object_child(vm, contents, "subscriber", &subscriber); }
    if (status == NEO_OK) { status = neo_object_create(vm, root, "scope", (neo_value){.kind=NEO_INTEGER,.integer=42}, &scope); }
    if (status == NEO_OK) { status = neo_stream_create(vm, root, "pipe", &backend, &pipe, &stream); }
    if (status == NEO_OK) { status = neo_replica_channel_create(vm, stream, root, "outgoing", &outgoing); }
    if (status == NEO_OK) { status = neo_replica_channel_create(vm, stream, root, "incoming", &incoming); }
    if (status == NEO_OK) { status = neo_object_connect(vm, publisher, "scope", scope, NEO_READ | NEO_COPY); }
    if (status == NEO_OK) { status = neo_object_connect(vm, publisher, "channel", outgoing, NEO_READ | NEO_ACT); }
    if (status == NEO_OK) { status = neo_object_connect(vm, subscriber, "channel", incoming, NEO_READ | NEO_ACT); }
    const neo_capability *services[] = {publisher,subscriber};
    for (size_t i = 0; status == NEO_OK && i < 2; ++i) {
        status = neo_test_exposure_write(vm, services[i], "origin", (neo_value){.kind=NEO_TEXT,.text="peer"});
        if (status == NEO_OK) { status = neo_test_exposure_write(vm, services[i], "publication", (neo_value){.kind=NEO_TEXT,.text="scope"}); }
        if (status == NEO_OK) { status = neo_test_exposure_write(vm, services[i], "acknowledgements", (neo_value){.kind=NEO_BOOLEAN,.boolean=true}); }
    }
    for (int64_t revision = 1; status == NEO_OK && revision <= 2; ++revision) {
        if (!neo_test_message_call(vm, publisher, "publish", NEO_OK)) { status = NEO_BAD_STATE; }
        if (status == NEO_OK && !neo_test_message_call(vm, publisher, "publish", NEO_RAISED)) { status = NEO_BAD_STATE; }
        if (status == NEO_OK) { status = neo_test_exposure_poll(vm, publisher, "send"); }
        if (status == NEO_OK) { status = neo_test_exposure_poll(vm, subscriber, "receive"); }
        if (status == NEO_OK) { status = neo_test_reply_poll(vm, publisher, subscriber, "received-revision", revision); }
        if (status == NEO_OK && (!neo_test_reply_counter(vm, publisher, "accepted-revision", revision == 1 ? 0 : 1) ||
            !neo_test_reply_counter(vm, publisher, "awaiting-revision", revision))) { status = NEO_BAD_STATE; }
        if (status == NEO_OK && !neo_test_message_call(vm, subscriber, revision == 1 ? "accept" : "reject", NEO_OK)) { status = NEO_BAD_STATE; }
        if (status == NEO_OK) { status = neo_test_reply_poll(vm, publisher, subscriber, revision == 1 ? "accepted-revision" : "rejected-revision", revision); }
        if (status == NEO_OK && !neo_test_reply_counter(vm, publisher, "awaiting-revision", 0)) { status = NEO_BAD_STATE; }
    }
    if (status == NEO_OK && (!neo_test_reply_counter(vm, subscriber, "revision", 1) ||
        !neo_test_reply_counter(vm, publisher, "accepted-revision", 1))) { status = NEO_BAD_STATE; }
    for (size_t i = 0; status == NEO_OK && i < 2; ++i) {
        status = neo_test_exposure_write(vm, services[i], "heartbeats", (neo_value){.kind=NEO_BOOLEAN,.boolean=true});
    }
    if (status == NEO_OK) { status = neo_test_probe(vm, publisher, subscriber, 1); }
    if (status == NEO_OK && !neo_test_message_call(vm, publisher, "publish", NEO_OK)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_test_exposure_poll(vm, publisher, "send"); }
    if (status == NEO_OK) { status = neo_test_exposure_poll(vm, subscriber, "receive"); }
    if (status == NEO_OK) { status = neo_test_reply_poll(vm, publisher, subscriber, "received-revision", 3); }
    const neo_capability *inbox = NULL, *candidate = NULL;
    neo_object_id before = 0, after = 0;
    if (status == NEO_OK) { status = neo_object_child(vm, subscriber, "inbox", &inbox); }
    if (status == NEO_OK) { status = neo_object_child(vm, inbox, "candidate", &candidate); }
    if (status == NEO_OK) { status = neo_object_identity(vm, candidate, &before); }
    if (status == NEO_OK) { status = neo_test_probe(vm, publisher, subscriber, 2); }
    if (status == NEO_OK) { status = neo_object_child(vm, inbox, "candidate", &candidate); }
    if (status == NEO_OK) { status = neo_object_identity(vm, candidate, &after); }
    if (status == NEO_OK && (before != after || !neo_test_reply_counter(vm, subscriber, "revision", 1) ||
        !neo_test_reply_counter(vm, publisher, "accepted-revision", 1))) { status = NEO_BAD_STATE; }
    if (root != NULL) { (void)neo_image_unload(vm, root); }
    if (status == NEO_OK && pipe.destroyed != 1) { status = NEO_BAD_STATE; }
    if (status != NEO_OK) { fprintf(stderr, "FAIL image receipt/acceptance/rejection replies: %s\n", neo_status_name(status)); }
    return status == NEO_OK;
}

#ifdef NEO_PLATFORM_WITH_WIN32
static bool neo_test_session_phase(neo_vm *vm, const neo_capability *worker, const char *phase) {
    const neo_capability *field = NULL;
    neo_value value = {0};
    return neo_object_child(vm, worker, "phase", &field) == NEO_OK &&
        neo_object_read(vm, field, &value) == NEO_OK && value.kind == NEO_TEXT && strcmp(value.text, phase) == 0;
}
static neo_status neo_test_session_turn(neo_vm *vm, const neo_capability *scheduler) {
    neo_execution report = {0};
    neo_status status = neo_behavior_run(vm, scheduler, "tick", NULL, 100000, &report);
    if (status == NEO_OK && (report.result.kind != NEO_INTEGER || report.result.integer < 0)) { status = NEO_BAD_STATE; }
    neo_execution_release(vm, &report);
    return status;
}
static neo_status neo_test_session_pair(neo_vm *vm, const neo_capability *first, const neo_capability *second) {
    neo_status status = neo_test_session_turn(vm, first);
    return status == NEO_OK ? neo_test_session_turn(vm, second) : status;
}
#endif
static bool neo_test_network_sessions(neo_vm *vm) {
#ifndef NEO_PLATFORM_WITH_WIN32
    (void)vm;
    return true;
#else
    const neo_capability *root = NULL, *incoming_image = NULL;
    const neo_capability *publisher = NULL, *subscriber = NULL, *outgoing = NULL, *incoming = NULL;
    const neo_capability *scheduler = NULL, *incoming_scheduler = NULL, *scope = NULL, *field = NULL;
    const neo_capability *listen_grant = NULL, *connect_grant = NULL, *inbox = NULL, *candidate = NULL;
    neo_diagnostic error = {0};
    neo_status status = neo_platform_load_image(vm, "neo/fixtures/session-send.neo", &root, &error);
    if (status == NEO_OK) { status = neo_platform_load_image(vm, "neo/fixtures/session-receive.neo", &incoming_image, &error); }
    if (status == NEO_OK) { status = neo_object_child(vm, root, "service", &publisher); }
    if (status == NEO_OK) { status = neo_object_child(vm, root, "session", &outgoing); }
    if (status == NEO_OK) { status = neo_object_child(vm, incoming_image, "service", &subscriber); }
    if (status == NEO_OK) { status = neo_object_child(vm, incoming_image, "session", &incoming); }
    if (status == NEO_OK) { status = neo_object_child(vm, root, "scheduler", &scheduler); }
    if (status == NEO_OK) { status = neo_object_child(vm, incoming_image, "scheduler", &incoming_scheduler); }
    if (status == NEO_OK) { status = neo_object_child(vm, root, "scope", &scope); }
    if (status == NEO_OK) { status = neo_object_child(vm, scope, "value", &field); }
    const neo_capability *services[] = {publisher,subscriber};
    for (size_t i = 0; status == NEO_OK && i < 2; ++i) {
        status = neo_test_exposure_write(vm, services[i], "origin", (neo_value){.kind=NEO_TEXT,.text="peer"});
        if (status == NEO_OK) { status = neo_test_exposure_write(vm, services[i], "publication", (neo_value){.kind=NEO_TEXT,.text="scope"}); }
    }
    /* Liveness is opt-in and cannot silently use a one-way service. */
    if (status == NEO_OK) { status = neo_test_exposure_write(vm, subscriber, "heartbeats", (neo_value){.kind=NEO_BOOLEAN,.boolean=true}); }
    if (status == NEO_OK && !neo_test_message_call(vm, incoming, "configuration", NEO_RAISED)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_test_exposure_write(vm, subscriber, "acknowledgements", (neo_value){.kind=NEO_BOOLEAN,.boolean=true}); }
    if (status == NEO_OK) { status = neo_test_exposure_write(vm, incoming, "heartbeat-interval", (neo_value){.kind=NEO_INTEGER,.integer=0}); }
    if (status == NEO_OK && !neo_test_message_call(vm, incoming, "configuration", NEO_RAISED)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_test_exposure_write(vm, incoming, "heartbeat-interval", (neo_value){.kind=NEO_INTEGER,.integer=32}); }
    if (status == NEO_OK) { status = neo_test_exposure_write(vm, incoming, "idle-limit", (neo_value){.kind=NEO_INTEGER,.integer=-1}); }
    if (status == NEO_OK && !neo_test_message_call(vm, incoming, "configuration", NEO_RAISED)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_test_exposure_write(vm, incoming, "idle-limit", (neo_value){.kind=NEO_INTEGER,.integer=0}); }
    if (status == NEO_OK) { status = neo_test_exposure_write(vm, subscriber, "acknowledgements", (neo_value){.kind=NEO_BOOLEAN,.boolean=false}); }
    if (status == NEO_OK) { status = neo_test_exposure_write(vm, subscriber, "heartbeats", (neo_value){.kind=NEO_BOOLEAN,.boolean=false}); }
    const neo_network_backend *network = NULL;
    neo_network_socket *probe = NULL;
    uint16_t port = 0;
    if (status == NEO_OK) { status = neo_platform_network(vm, &network); }
    if (status == NEO_OK) { status = network->listen("127.0.0.1", 0, &probe); }
    if (status == NEO_OK) { status = network->local_port(probe, &port); }
    if (probe != NULL) { network->close(probe); }
    if (status == NEO_OK) { status = neo_network_endpoint_create(vm, incoming_image, "listener", "127.0.0.1", port, true, &listen_grant); }
    if (status == NEO_OK) { status = neo_network_endpoint_create(vm, root, "connector", "127.0.0.1", port, false, &connect_grant); }
    if (status == NEO_OK) { status = neo_object_connect(vm, outgoing, "network", connect_grant, NEO_READ | NEO_ACT); }
    if (status == NEO_OK) { status = neo_object_connect(vm, incoming, "network", listen_grant, NEO_READ | NEO_ACT); }
    if (status == NEO_OK) { status = neo_test_exposure_write(vm, incoming, "role", (neo_value){.kind=NEO_TEXT,.text="subscriber"}); }
    const neo_capability *workers[] = {incoming,outgoing};
    const neo_capability *schedulers[] = {incoming_scheduler,scheduler};
    for (size_t i = 0; status == NEO_OK && i < 2; ++i) {
        status = neo_object_connect(vm, schedulers[i], "candidate", workers[i], NEO_READ | NEO_WRITE | NEO_DELEGATE | NEO_ACT);
        if (status == NEO_OK) { status = neo_test_exposure_write(vm, schedulers[i], "candidate-name", (neo_value){.kind=NEO_TEXT,.text=i == 0 ? "incoming" : "outgoing"}); }
        if (status == NEO_OK) { status = neo_test_exposure_write(vm, schedulers[i], "candidate-periodic", (neo_value){.kind=NEO_BOOLEAN,.boolean=true}); }
        if (status == NEO_OK && !neo_test_message_call(vm, schedulers[i], "register", NEO_OK)) { status = NEO_BAD_STATE; }
        if (status == NEO_OK) { status = neo_object_disconnect(vm, schedulers[i], "candidate"); }
        if (status == NEO_OK && !neo_test_message_call(vm, workers[i], "start", NEO_OK)) { status = NEO_BAD_STATE; }
    }
    if (status == NEO_OK) { status = neo_object_child(vm, subscriber, "inbox", &inbox); }
    bool arrived = false;
    for (size_t i = 0; status == NEO_OK && !arrived && i < 1000; ++i) {
        status = neo_test_session_pair(vm, scheduler, incoming_scheduler);
        arrived = neo_object_child(vm, inbox, "candidate", &candidate) == NEO_OK;
    }
    if (status == NEO_OK && !arrived) { status = NEO_BUSY; }
    if (status == NEO_OK && !neo_test_message_call(vm, subscriber, "accept", NEO_OK)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_object_write(vm, field, (neo_value){.kind=NEO_INTEGER,.integer=43}); }
    const neo_capability *broken_peer = NULL;
    if (status == NEO_OK) { status = neo_object_child(vm, outgoing, "peer", &broken_peer); }
    const uint8_t fragment = 'N';
    neo_io_result fragment_result = {0};
    if (status == NEO_OK) { status = neo_stream_write(vm, broken_peer, &fragment, 1, &fragment_result); }
    if (status == NEO_OK && fragment_result.count != 1) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_message_call(vm, outgoing, "stop", NEO_OK)) { status = NEO_BAD_STATE; }
    for (size_t i = 0; status == NEO_OK && !neo_test_session_phase(vm, incoming, "failed") && i < 1000; ++i) {
        status = neo_test_session_pair(vm, scheduler, incoming_scheduler);
    }
    if (status == NEO_OK && !neo_test_session_phase(vm, incoming, "failed")) { status = NEO_BAD_STATE; }
    /* Default failed session stays failed across turns; retries are explicit. */
    if (status == NEO_OK) { status = neo_test_session_pair(vm, scheduler, incoming_scheduler); }
    if (status == NEO_OK && !neo_test_session_phase(vm, incoming, "failed")) { status = NEO_BAD_STATE; }
    for (size_t i = 0; status == NEO_OK && i < 2; ++i) {
        status = neo_test_exposure_write(vm, workers[i], "automatic-retry", (neo_value){.kind=NEO_BOOLEAN,.boolean=true});
        if (status == NEO_OK && !neo_test_message_call(vm, workers[i], "retry", NEO_OK)) { status = NEO_BAD_STATE; }
    }
    if (status == NEO_OK) { status = neo_test_session_pair(vm, scheduler, incoming_scheduler); }
    if (status == NEO_OK && (!neo_test_session_phase(vm, incoming, "backoff") || !neo_test_session_phase(vm, outgoing, "backoff"))) { status = NEO_BAD_STATE; }
    arrived = false;
    for (size_t i = 0; status == NEO_OK && !arrived && i < 1000; ++i) {
        status = neo_test_session_pair(vm, scheduler, incoming_scheduler);
        arrived = neo_object_child(vm, inbox, "candidate", &candidate) == NEO_OK;
    }
    if (status == NEO_OK && !arrived) { status = NEO_BUSY; }
    const neo_capability *revision = NULL, *state = NULL, *copied = NULL;
    neo_value value = {0};
    if (status == NEO_OK) { status = neo_object_child(vm, subscriber, "revision", &revision); }
    if (status == NEO_OK) { status = neo_object_read(vm, revision, &value); }
    if (status == NEO_OK && (value.kind != NEO_INTEGER || value.integer != 1)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_object_child(vm, candidate, "revision", &revision); }
    if (status == NEO_OK) { status = neo_object_read(vm, revision, &value); }
    if (status == NEO_OK && (value.kind != NEO_INTEGER || value.integer <= 1)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_object_child(vm, candidate, "state", &state); }
    if (status == NEO_OK) { status = neo_object_child(vm, state, "value", &copied); }
    if (status == NEO_OK) { status = neo_object_read(vm, copied, &value); }
    if (status == NEO_OK && (value.kind != NEO_INTEGER || value.integer != 43)) { status = NEO_BAD_STATE; }
    /* A pending candidate causes no further receive/accept on later turns. */
    if (status == NEO_OK) { status = neo_test_session_pair(vm, scheduler, incoming_scheduler); }
    if (status == NEO_OK && !neo_test_session_phase(vm, incoming, "active")) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_message_call(vm, outgoing, "stop", NEO_OK)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_message_call(vm, incoming, "stop", NEO_OK)) { status = NEO_BAD_STATE; }
    neo_object_id before = 0, after = 0;
    if (status == NEO_OK) { status = neo_object_identity(vm, candidate, &before); }
    for (size_t i = 0; status == NEO_OK && i < 2; ++i) {
        if (!neo_test_message_call(vm, workers[i], "start", NEO_OK)) { status = NEO_BAD_STATE; }
    }
    for (size_t i = 0; status == NEO_OK && i < 1000 &&
         (!neo_test_session_phase(vm, incoming, "active") || !neo_test_session_phase(vm, outgoing, "active")); ++i) {
        status = neo_test_session_pair(vm, scheduler, incoming_scheduler);
    }
    if (status == NEO_OK && (!neo_test_session_phase(vm, incoming, "active") || !neo_test_session_phase(vm, outgoing, "active"))) {
        status = NEO_BUSY;
    }
    for (size_t i = 0; status == NEO_OK && i < 12; ++i) { status = neo_test_session_pair(vm, scheduler, incoming_scheduler); }
    if (status == NEO_OK) { status = neo_object_child(vm, inbox, "candidate", &candidate); }
    if (status == NEO_OK) { status = neo_object_identity(vm, candidate, &after); }
    if (status == NEO_OK && before != after) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_object_child(vm, subscriber, "revision", &revision); }
    if (status == NEO_OK) { status = neo_object_read(vm, revision, &value); }
    if (status == NEO_OK && (value.kind != NEO_INTEGER || value.integer != 1)) { status = NEO_BAD_STATE; }
    for (size_t i = 0; status == NEO_OK && i < 2; ++i) {
        if (!neo_test_message_call(vm, workers[i], "stop", NEO_OK)) { status = NEO_BAD_STATE; }
    }
    /* Image finish drains an already prepared publication, then stops locally. */
    for (size_t i = 0; status == NEO_OK && i < 2; ++i) {
        if (!neo_test_message_call(vm, workers[i], "start", NEO_OK)) { status = NEO_BAD_STATE; }
    }
    for (size_t i = 0; status == NEO_OK && i < 1000 &&
         (!neo_test_session_phase(vm, incoming, "active") || !neo_test_session_phase(vm, outgoing, "active")); ++i) {
        status = neo_test_session_pair(vm, scheduler, incoming_scheduler);
    }
    if (status == NEO_OK && (!neo_test_session_phase(vm, incoming, "active") || !neo_test_session_phase(vm, outgoing, "active"))) { status = NEO_BUSY; }
    if (status == NEO_OK) { status = neo_test_session_pair(vm, scheduler, incoming_scheduler); }
    if (status == NEO_OK && !neo_test_message_call(vm, outgoing, "finish", NEO_OK)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_session_phase(vm, outgoing, "closing")) { status = NEO_BAD_STATE; }
    for (size_t i = 0; status == NEO_OK && !neo_test_session_phase(vm, outgoing, "stopped") && i < 1000; ++i) {
        status = neo_test_session_pair(vm, scheduler, incoming_scheduler);
    }
    if (status == NEO_OK && !neo_test_session_phase(vm, outgoing, "stopped")) { status = NEO_BUSY; }
    if (status == NEO_OK && !neo_test_message_call(vm, outgoing, "finish", NEO_RAISED)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_object_child(vm, inbox, "candidate", &candidate); }
    if (status == NEO_OK) { status = neo_object_identity(vm, candidate, &after); }
    if (status == NEO_OK && before != after) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_message_call(vm, incoming, "stop", NEO_OK)) { status = NEO_BAD_STATE; }
    /* Authentication-required policy rejects raw peers before channel creation. */
    for (size_t i = 0; status == NEO_OK && i < 2; ++i) {
        status = neo_test_exposure_write(vm, workers[i], "require-authentication", (neo_value){.kind=NEO_BOOLEAN,.boolean=true});
        if (status == NEO_OK) { status = neo_test_exposure_write(vm, workers[i], "automatic-retry", (neo_value){.kind=NEO_BOOLEAN,.boolean=false}); }
        if (status == NEO_OK && !neo_test_message_call(vm, workers[i], "start", NEO_OK)) { status = NEO_BAD_STATE; }
    }
    for (size_t i = 0; status == NEO_OK && i < 1000 &&
         (!neo_test_session_phase(vm, incoming, "failed") || !neo_test_session_phase(vm, outgoing, "failed")); ++i) {
        status = neo_test_session_pair(vm, scheduler, incoming_scheduler);
    }
    for (size_t i = 0; status == NEO_OK && i < 2; ++i) {
        const neo_capability *unexpected = NULL;
        if (!neo_test_session_phase(vm, workers[i], "failed") ||
            neo_object_child(vm, workers[i], "channel", &unexpected) != NEO_UNAVAILABLE) { status = NEO_BAD_STATE; }
        if (status == NEO_OK && !neo_test_message_call(vm, workers[i], "stop", NEO_OK)) { status = NEO_BAD_STATE; }
    }
    /* Bound idle connection attempts without a clock or an automatic retry. */
    if (status == NEO_OK) { status = neo_test_exposure_write(vm, incoming, "connection-limit", (neo_value){.kind=NEO_INTEGER,.integer=-1}); }
    if (status == NEO_OK && !neo_test_message_call(vm, incoming, "start", NEO_RAISED)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_test_exposure_write(vm, incoming, "connection-limit", (neo_value){.kind=NEO_INTEGER,.integer=3}); }
    if (status == NEO_OK && !neo_test_message_call(vm, incoming, "start", NEO_OK)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_test_session_pair(vm, scheduler, incoming_scheduler); }
    for (size_t i = 0; status == NEO_OK && i < 3; ++i) {
        if (!neo_test_session_phase(vm, incoming, "connecting")) { status = NEO_BAD_STATE; }
        if (status == NEO_OK) { status = neo_test_session_pair(vm, scheduler, incoming_scheduler); }
    }
    if (status == NEO_OK && !neo_test_session_phase(vm, incoming, "failed")) { status = NEO_BAD_STATE; }
    uint16_t closed_port = 0;
    if (status == NEO_OK && neo_network_endpoint_port(vm, listen_grant, &closed_port) != NEO_UNAVAILABLE) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_object_child(vm, incoming, "connection-remaining", &revision); }
    if (status == NEO_OK) { status = neo_object_read(vm, revision, &value); }
    if (status == NEO_OK && (value.kind != NEO_INTEGER || value.integer != 0)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_object_child(vm, inbox, "candidate", &candidate); }
    if (status == NEO_OK) { status = neo_object_identity(vm, candidate, &after); }
    if (status == NEO_OK && before != after) { status = NEO_BAD_STATE; }
    /* Explicit restart rearms the attempt. Zero retains indefinite listening. */
    if (status == NEO_OK) { status = neo_test_exposure_write(vm, incoming, "connection-limit", (neo_value){.kind=NEO_INTEGER,.integer=0}); }
    if (status == NEO_OK && !neo_test_message_call(vm, incoming, "start", NEO_OK)) { status = NEO_BAD_STATE; }
    for (size_t i = 0; status == NEO_OK && i < 8; ++i) { status = neo_test_session_pair(vm, scheduler, incoming_scheduler); }
    if (status == NEO_OK && !neo_test_session_phase(vm, incoming, "connecting")) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_message_call(vm, incoming, "stop", NEO_OK)) { status = NEO_BAD_STATE; }
    if (status != NEO_OK) {
        for (size_t i = 0; i < 2; ++i) {
            const char *names[] = {"phase", "error"};
            for (size_t j = 0; j < 2; ++j) {
                const neo_capability *diagnostic_field = NULL;
                neo_value diagnostic_value = {0};
                if (workers[i] != NULL && neo_object_child(vm, workers[i], names[j], &diagnostic_field) == NEO_OK &&
                    neo_object_read(vm, diagnostic_field, &diagnostic_value) == NEO_OK && diagnostic_value.kind == NEO_TEXT) {
                    fprintf(stderr, "session %zu %s: %s\n", i, names[j], diagnostic_value.text);
                }
            }
        }
    }
    if (root != NULL) { (void)neo_image_unload(vm, root); }
    if (incoming_image != NULL) { (void)neo_image_unload(vm, incoming_image); }
    if (status != NEO_OK) { fprintf(stderr, "FAIL scheduled network reconnect/resync: %s (%s)\n", neo_status_name(status), error.message); }
    return status == NEO_OK;
#endif
}


static bool neo_test_headless_display(neo_vm *vm, neo_test_allocator *allocator) {
    const neo_capability *root = NULL, *actor = NULL, *window = NULL, *buffer = NULL, *readonly = NULL;
    neo_headless_display *control = NULL;
    neo_diagnostic error = {0};
    bool okay = neo_image_parse(vm, "(image (actor (handlers "
        "(frame (body (do (buffer-fill (target \"buffer\") (value 17)) (window-present (target \"window\") (buffer \"buffer\"))))) "
        "(input (body (window-next-event (target \"window\")))))))", &root, &error) == NEO_OK;
    if (okay) { okay = neo_object_child(vm, root, "actor", &actor) == NEO_OK; }
    if (okay) { okay = neo_headless_open(vm, root, "window", 2, 1, &control, &window) == NEO_OK; }
    if (okay) { okay = neo_buffer_create(vm, root, "buffer", 2, 1, &buffer) == NEO_OK &&
        neo_object_connect(vm, actor, "window", window, NEO_READ | NEO_WRITE) == NEO_OK &&
        neo_object_connect(vm, actor, "buffer", buffer, NEO_READ | NEO_WRITE) == NEO_OK; }
    uint8_t bytes[8] = {0}; size_t count = 0, width = 0, height = 0; uint64_t frames = 0;
    if (okay) { okay = neo_headless_frame(control, bytes, sizeof(bytes), &count, &width, &height, &frames) == NEO_UNAVAILABLE; }
    neo_execution report = {0};
    if (okay) { okay = neo_behavior_run(vm, actor, "frame", NULL, 1000, &report) == NEO_OK; }
    neo_execution_release(vm, &report);
    if (okay) { okay = neo_headless_frame(control, bytes, sizeof(bytes), &count, &width, &height, &frames) == NEO_OK &&
        count == 8 && width == 2 && height == 1 && frames == 1; }
    for (size_t i = 0; okay && i < sizeof(bytes); ++i) { okay = bytes[i] == 17; }
    if (okay) { okay = neo_buffer_fill(vm, buffer, 99) == NEO_OK &&
        neo_headless_frame(control, bytes, sizeof(bytes), &count, &width, &height, &frames) == NEO_OK && bytes[0] == 17 &&
        neo_headless_present_status(control, NEO_UNAVAILABLE) == NEO_OK && neo_window_present(vm, window, buffer) == NEO_UNAVAILABLE &&
        neo_headless_frame(control, bytes, sizeof(bytes), &count, &width, &height, &frames) == NEO_OK && frames == 1 && bytes[0] == 17 &&
        neo_headless_present_status(control, NEO_OK) == NEO_OK && neo_window_present(vm, window, buffer) == NEO_OK &&
        neo_headless_frame(control, bytes, sizeof(bytes), &count, &width, &height, &frames) == NEO_OK && frames == 2 && bytes[0] == 99; }
    if (okay) { okay = neo_headless_frame(control, bytes, 1, &count, &width, &height, &frames) == NEO_LIMIT &&
        count == 0 && width == 0 && height == 0 && frames == 0; }
    neo_input_event event = {0}; neo_window_state state = {0};
    if (okay) { okay = neo_capability_restrict(vm, window, NEO_READ, &readonly) == NEO_OK &&
        neo_window_present(vm, readonly, buffer) == NEO_DENIED && neo_window_next_event(vm, readonly, &event) == NEO_DENIED; }
    if (okay) { okay = neo_window_poll(vm, window, &state) == NEO_OK &&
        neo_window_next_event(vm, window, &event) == NEO_OK && event.kind == NEO_INPUT_SURFACE; }
    if (okay) { okay = neo_headless_push(control, &(neo_input_event){.kind=NEO_INPUT_RESIZE,.width=4,.height=3}) == NEO_OK &&
        neo_headless_push(control, &(neo_input_event){.kind=NEO_INPUT_FOCUS,.pressed=false}) == NEO_OK &&
        neo_headless_push(control, &(neo_input_event){.kind=NEO_INPUT_KEY,.pressed=false,.key="a"}) == NEO_OK &&
        neo_window_poll(vm, window, &state) == NEO_OK && state.width == 4 && state.height == 3 && state.redraw &&
        neo_window_poll(vm, window, &state) == NEO_OK && !state.redraw &&
        neo_headless_frame(control, bytes, sizeof(bytes), &count, &width, &height, &frames) == NEO_OK && width == 2 && height == 1; }
    if (okay) { okay = neo_behavior_run(vm, actor, "input", NULL, 1000, &report) == NEO_OK &&
        report.result.kind == NEO_TEXT && strcmp(report.result.text, "resize") == 0; }
    neo_execution_release(vm, &report);
    if (okay) { okay = neo_window_next_event(vm, window, &event) == NEO_OK && event.kind == NEO_INPUT_FOCUS && !event.pressed &&
        neo_window_get_event(vm, readonly, &event) == NEO_OK && event.kind == NEO_INPUT_FOCUS &&
        neo_window_next_event(vm, window, &event) == NEO_OK && event.kind == NEO_INPUT_KEY && strcmp(event.key,"a") == 0 && !event.pressed &&
        neo_window_next_event(vm, window, &event) == NEO_OK && event.kind == NEO_INPUT_NONE; }
    for (size_t i = 0; okay && i < NEO_HEADLESS_EVENT_CAPACITY; ++i) {
        okay = neo_headless_push(control, &(neo_input_event){.kind=NEO_INPUT_POINTER,.x=(int64_t)i}) == NEO_OK;
    }
    if (okay) { okay = neo_headless_push(control, &(neo_input_event){.kind=NEO_INPUT_RESIZE,.width=9,.height=7}) == NEO_LIMIT &&
        neo_headless_push(control, &(neo_input_event){.kind=NEO_INPUT_KEY,.key="enter"}) == NEO_LIMIT &&
        neo_headless_push(control, &(neo_input_event){.kind=NEO_INPUT_CLOSE}) == NEO_LIMIT &&
        neo_window_poll(vm, window, &state) == NEO_OK && state.closed && state.width == 9 && state.height == 7 &&
        neo_window_next_event(vm, window, &event) == NEO_OK && event.kind == NEO_INPUT_RESET &&
        neo_window_next_event(vm, window, &event) == NEO_OK && event.kind == NEO_INPUT_OVERFLOW && event.lost == 3; }
    /* Old-generation queued input is suppressed after the reset, not replayed. */
    if (okay) { okay = neo_window_next_event(vm, window, &event) == NEO_OK && event.kind == NEO_INPUT_NONE &&
        neo_window_present(vm, window, buffer) == NEO_UNAVAILABLE &&
        neo_headless_push(control, &(neo_input_event){.kind=NEO_INPUT_EXPOSE}) == NEO_UNAVAILABLE; }
    char *text = NULL;
    if (okay) { okay = neo_image_format(vm, root, &text) == NEO_UNSUPPORTED && text == NULL; }
    neo_image_text_free(vm, text);
    if (root != NULL) { (void)neo_image_unload(vm, root); root = NULL; }
    if (okay) { okay = neo_headless_frame(control, bytes, sizeof(bytes), &count, &width, &height, &frames) == NEO_UNAVAILABLE &&
        neo_headless_push(control, &(neo_input_event){.kind=NEO_INPUT_EXPOSE}) == NEO_UNAVAILABLE; }
    neo_headless_release(control); control = NULL;
    /* Creation failures retain no controller; caller can release its reference first. */
    if (okay) { okay = neo_image_parse(vm, "(image)", &root, &error) == NEO_OK; }
    if (okay) {
        allocator->fail_at = allocator->calls + 1;
        neo_status status = neo_headless_open(vm, root, "failed", 2, 1, &control, &window);
        allocator->fail_at = 0;
        okay = status == NEO_OUT_OF_MEMORY && control == NULL && window == NULL;
    }
    if (okay) { okay = neo_capability_restrict(vm, root, NEO_READ, &readonly) == NEO_OK &&
        neo_headless_open(vm, readonly, "denied", 2, 1, &control, &window) == NEO_DENIED && control == NULL && window == NULL &&
        neo_headless_open(vm, root, "window", 2, 1, &control, &window) == NEO_OK; }
    if (okay) {
        neo_input_event malformed = {.kind=NEO_INPUT_KEY}; memset(malformed.key,'x',sizeof(malformed.key));
        okay = neo_headless_push(control, &malformed) == NEO_INVALID &&
            neo_headless_push(control, &(neo_input_event){.kind=NEO_INPUT_RESIZE,.width=-1}) == NEO_INVALID &&
            neo_headless_push(control, &(neo_input_event){.kind=NEO_INPUT_OVERFLOW}) == NEO_INVALID &&
            neo_headless_push(control, &(neo_input_event){.kind=NEO_INPUT_RESIZE,.width=0,.height=0}) == NEO_OK &&
            neo_window_poll(vm, window, &state) == NEO_OK && state.width == 0 && state.height == 0;
    }
    neo_headless_release(control); control = NULL;
    if (okay) { okay = neo_window_poll(vm, window, &state) == NEO_OK; }
    if (root != NULL) { (void)neo_image_unload(vm, root); }
    if (!okay) { fprintf(stderr, "FAIL deterministic headless presentation/input/lifetime\n"); }
    return okay;
}


static bool neo_test_input_reset(neo_vm *vm) {
    const neo_capability *root = NULL, *actor = NULL, *window = NULL, *readonly = NULL;
    neo_headless_display *control = NULL;
    neo_diagnostic error = {0};
    bool okay = neo_image_parse(vm, "(image (actor (dragging true) (handlers "
        "(capture (body (window-capture (target \"window\") (value true)))) "
        "(release (body (window-capture (target \"window\") (value false)))) "
        "(cancel (body (if (== (left (window-next-event (target \"window\"))) (right \"input-reset\")) "
        "(then (write (slot \"dragging\") (value false))) (else (fail))))) "
        "(epoch (body (window-state (target \"window\") (field \"input-epoch\")))))))", &root, &error) == NEO_OK;
    if (okay) { okay = neo_object_child(vm, root, "actor", &actor) == NEO_OK &&
        neo_headless_open(vm, root, "window", 2, 2, &control, &window) == NEO_OK &&
        neo_object_connect(vm, actor, "window", window, NEO_READ | NEO_WRITE) == NEO_OK; }
    neo_window_state state = {0}; neo_input_event event = {0};
    if (okay) { okay = neo_window_capture(vm, window, true) == NEO_UNAVAILABLE &&
        neo_headless_push(control, &(neo_input_event){.kind=NEO_INPUT_FOCUS,.pressed=true}) == NEO_OK &&
        neo_window_poll(vm, window, &state) == NEO_OK && state.focused && state.input_epoch == 0 &&
        neo_window_next_event(vm, window, &event) == NEO_OK && event.kind == NEO_INPUT_FOCUS; }
    if (okay) { okay = neo_test_matrix_call(vm, actor, "capture", NEO_OK) &&
        neo_window_get_state(vm, window, &state) == NEO_OK && state.captured &&
        neo_capability_restrict(vm, window, NEO_READ, &readonly) == NEO_OK &&
        neo_window_capture(vm, readonly, false) == NEO_DENIED; }
    if (okay) { okay = neo_headless_push(control, &(neo_input_event){.kind=NEO_INPUT_KEY,.key="a",.pressed=true}) == NEO_OK &&
        neo_headless_push(control, &(neo_input_event){.kind=NEO_INPUT_FOCUS,.pressed=false}) == NEO_OK &&
        neo_window_poll(vm, window, &state) == NEO_OK && !state.focused && !state.captured && state.input_epoch == 1 &&
        neo_test_matrix_call(vm, actor, "cancel", NEO_OK) &&
        neo_test_matrix_value(vm, actor, "dragging", (neo_value){.kind=NEO_BOOLEAN,.boolean=false}) &&
        neo_window_next_event(vm, window, &event) == NEO_OK && event.kind == NEO_INPUT_FOCUS && !event.pressed && event.epoch == 1 &&
        neo_window_next_event(vm, window, &event) == NEO_OK && event.kind == NEO_INPUT_NONE && event.epoch == 1; }
    neo_execution report = {0};
    if (okay) { okay = neo_behavior_run(vm, actor, "epoch", NULL, 1000, &report) == NEO_OK &&
        report.result.kind == NEO_INTEGER && report.result.integer == 1; }
    neo_execution_release(vm, &report);
    if (okay) { okay = neo_headless_push(control, &(neo_input_event){.kind=NEO_INPUT_FOCUS,.pressed=true}) == NEO_OK &&
        neo_window_poll(vm, window, &state) == NEO_OK &&
        neo_window_next_event(vm, window, &event) == NEO_OK && event.kind == NEO_INPUT_FOCUS &&
        neo_test_matrix_call(vm, actor, "capture", NEO_OK) &&
        neo_headless_push(control, &(neo_input_event){.kind=NEO_INPUT_BUTTON,.button=1,.pressed=true}) == NEO_OK &&
        neo_headless_push(control, &(neo_input_event){.kind=NEO_INPUT_CAPTURE,.pressed=false}) == NEO_OK &&
        neo_window_poll(vm, window, &state) == NEO_OK && !state.captured && state.focused && state.input_epoch == 2 &&
        neo_window_next_event(vm, window, &event) == NEO_OK && event.kind == NEO_INPUT_RESET && event.epoch == 2 &&
        neo_window_next_event(vm, window, &event) == NEO_OK && event.kind == NEO_INPUT_CAPTURE &&
        neo_window_next_event(vm, window, &event) == NEO_OK && event.kind == NEO_INPUT_NONE; }
    if (okay) { okay = neo_test_matrix_call(vm, actor, "capture", NEO_OK) && neo_test_matrix_call(vm, actor, "release", NEO_OK) &&
        neo_window_get_state(vm, window, &state) == NEO_OK && !state.captured && state.input_epoch == 3 &&
        neo_window_next_event(vm, window, &event) == NEO_OK && event.kind == NEO_INPUT_RESET &&
        neo_test_matrix_call(vm, actor, "release", NEO_OK) &&
        neo_window_get_state(vm, window, &state) == NEO_OK && state.input_epoch == 3; }
    if (root != NULL) { (void)neo_image_unload(vm, root); }
    neo_headless_release(control);
    if (!okay) { fprintf(stderr, "FAIL focus/capture/input reset/stale-event suppression: %s\n", error.message); }
    return okay;
}

static bool neo_test_point_renderer(neo_vm *vm) {
    const neo_capability *root = NULL, *artifact = NULL, *contents = NULL, *renderer = NULL;
    const neo_capability *view = NULL, *geometry = NULL, *buffer = NULL, *window = NULL, *package = NULL;
    neo_headless_display *control = NULL; neo_diagnostic diagnostic = {0};
    neo_status status = neo_platform_load_image(vm, "neo/packages/point-renderer.neo", &root, &diagnostic);
    if (status == NEO_OK) { status = neo_package_validate(vm, root); }
    const neo_capability *catalogue_root = NULL, *catalogue = NULL, *record = NULL, *prototype = NULL, *instance = NULL;
    if (status == NEO_OK) { status = neo_image_parse(vm, "(image (catalogue))", &catalogue_root, &diagnostic); }
    if (status == NEO_OK) { status = neo_object_child(vm, catalogue_root, "catalogue", &catalogue); }
    if (status == NEO_OK) { status = neo_package_install(vm, catalogue, root, false, &record); }
    if (status == NEO_OK) { status = neo_package_resolve(vm, catalogue, "neo.render.points", "renderer", NEO_READ | NEO_COPY, &prototype); }
    if (status == NEO_OK) { status = neo_object_copy(vm, prototype, catalogue_root, "instance", &instance); }
    if (status == NEO_OK && !neo_test_matrix_value(vm, instance, "drawn-count", (neo_value){.kind=NEO_INTEGER,.integer=0})) { status = NEO_BAD_STATE; }
    if (catalogue_root != NULL) { (void)neo_image_unload(vm, catalogue_root); }
    if (status == NEO_OK) { status = neo_object_child(vm, root, "contents", &contents); }
    if (status == NEO_OK) { status = neo_object_child(vm, contents, "renderer", &renderer); }
    const char *paths[] = {"neo/packages/projection.neo", "neo/packages/geometry.neo"};
    const char *names[] = {"view-package", "geometry-package"};
    const char *exports[] = {"projection", "geometry"};
    for (size_t i = 0; status == NEO_OK && i < 2; ++i) {
        status = neo_platform_load_image(vm, paths[i], &artifact, &diagnostic);
        if (status == NEO_OK) { status = neo_object_import(vm, artifact, root, names[i], &package); }
        if (artifact != NULL) { (void)neo_image_unload(vm, artifact); artifact = NULL; }
        if (status == NEO_OK) { status = neo_object_child(vm, package, "contents", &contents); }
        if (status == NEO_OK) { status = neo_object_child(vm, contents, exports[i], i == 0 ? &view : &geometry); }
    }
    if (status == NEO_OK) { status = neo_object_connect(vm, view, "source", geometry, NEO_READ | NEO_WRITE); }
    if (status == NEO_OK) { status = neo_object_connect(vm, renderer, "view", view, NEO_READ); }
    if (status == NEO_OK) { status = neo_headless_open(vm, root, "window", 5, 5, &control, &window); }
    if (status == NEO_OK) { status = neo_buffer_create(vm, root, "buffer", 5, 5, &buffer); }
    if (status == NEO_OK) { status = neo_object_connect(vm, renderer, "window", window, NEO_WRITE); }
    if (status == NEO_OK) { status = neo_object_connect(vm, renderer, "buffer", buffer, NEO_READ | NEO_WRITE); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, renderer, "origin-x", (neo_value){.kind=NEO_INTEGER,.integer=2}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, renderer, "origin-y", (neo_value){.kind=NEO_INTEGER,.integer=2}); }
    const int64_t dimensions[] = {2,3,4,33};
    int64_t vertices[132] = {0};
    const int64_t xs[] = {-1000000,0,2000000,20000000}, ys[] = {1000000,0,-1000000,0};
    uint8_t expected[100] = {0}, pixels[100] = {0};
    const size_t offsets[] = {24,48,76};
    for (size_t i = 0; i < 3; ++i) { memset(expected + offsets[i], 255, 4); }
    neo_object_id original = 0, current = 0;
    size_t maximum_steps = 0;
    if (status == NEO_OK) { status = neo_object_identity(vm, geometry, &original); }
    for (size_t d = 0; status == NEO_OK && d < 4; ++d) {
        size_t n = (size_t)dimensions[d];
        for (size_t row = 0; row < 4; ++row) {
            for (size_t axis = 0; axis < n; ++axis) { vertices[row*n+axis] = (int64_t)axis + 123; }
            vertices[row*n] = xs[row]; vertices[row*n+n-1] = ys[row];
        }
        status = neo_test_matrix_write(vm, geometry, "dimension", (neo_value){.kind=NEO_INTEGER,.integer=(int64_t)n});
        if (status == NEO_OK) { status = neo_test_matrix_write(vm, geometry, "vertex-count", (neo_value){.kind=NEO_INTEGER,.integer=4}); }
        if (status == NEO_OK) { status = neo_test_matrix_write(vm, geometry, "vertices", (neo_value){.kind=NEO_INTEGERS,.integers=vertices,.count=4*n}); }
        if (status == NEO_OK) { status = neo_test_matrix_write(vm, view, "axes", (neo_value){.kind=NEO_INTEGERS,.integers=(int64_t[]){0,(int64_t)n-1},.count=2}); }
        if (status == NEO_OK && !neo_test_matrix_call(vm, view, "project", NEO_OK)) { status = NEO_BAD_STATE; }
        neo_execution report = {0};
        if (status == NEO_OK) { status = neo_behavior_run(vm, renderer, "frame", NULL, 100000, &report); }
        if (report.steps > maximum_steps) { maximum_steps = report.steps; }
        neo_execution_release(vm, &report);
        size_t count = 0, width = 0, height = 0; uint64_t frames = 0;
        if (status == NEO_OK) { status = neo_headless_frame(control, pixels, sizeof(pixels), &count, &width, &height, &frames); }
        if (status == NEO_OK && (count != 100 || width != 5 || height != 5 || memcmp(pixels, expected, sizeof(pixels)) != 0 ||
            !neo_test_matrix_value(vm, renderer, "drawn-count", (neo_value){.kind=NEO_INTEGER,.integer=3}) ||
            !neo_test_matrix_value(vm, renderer, "screen-indices", (neo_value){.kind=NEO_INTEGERS,.integers=(int64_t[]){24,48,76,-1},.count=4}) ||
            !neo_test_matrix_value(vm, geometry, "vertices", (neo_value){.kind=NEO_INTEGERS,.integers=vertices,.count=4*n}))) { status = NEO_BAD_STATE; }
        if (status == NEO_OK) { status = neo_object_identity(vm, geometry, &current); }
        if (status == NEO_OK && current != original) { status = NEO_BAD_STATE; }
    }
    /* Edit the selected coordinates through the view; hidden coordinates survive. */
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, view, "vertex-index", (neo_value){.kind=NEO_INTEGER,.integer=0}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, view, "edit", (neo_value){.kind=NEO_INTEGERS,.integers=(int64_t[]){0,1000000},.count=2}); }
    if (status == NEO_OK && (!neo_test_matrix_call(vm, view, "edit-vertex", NEO_OK) || !neo_test_matrix_call(vm, view, "project", NEO_OK) ||
        !neo_test_matrix_call(vm, renderer, "frame", NEO_OK))) { status = NEO_BAD_STATE; }
    vertices[0] = 0; memset(expected + 24, 0, 4); memset(expected + 28, 255, 4);
    if (status == NEO_OK) { status = neo_buffer_read(vm, buffer, 0, pixels, sizeof(pixels)); }
    if (status == NEO_OK && (memcmp(pixels, expected, sizeof(pixels)) != 0 ||
        !neo_test_matrix_value(vm, geometry, "vertices", (neo_value){.kind=NEO_INTEGERS,.integers=vertices,.count=132}))) { status = NEO_BAD_STATE; }
    /* Select a second view of that same source; no duplicated scene or hidden-axis loss. */
    const neo_capability *other_view = NULL;
    if (status == NEO_OK) { status = neo_object_copy(vm, view, root, "alternate-view", &other_view); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, other_view, "axes", (neo_value){.kind=NEO_INTEGERS,.integers=(int64_t[]){32,0},.count=2}); }
    if (status == NEO_OK && !neo_test_matrix_call(vm, other_view, "project", NEO_OK)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_object_disconnect(vm, renderer, "view"); }
    if (status == NEO_OK) { status = neo_object_connect(vm, renderer, "view", other_view, NEO_READ); }
    if (status == NEO_OK && !neo_test_matrix_call(vm, renderer, "frame", NEO_OK)) { status = NEO_BAD_STATE; }
    memset(expected, 0, sizeof(expected)); memset(expected + 52, 255, 4); memset(expected + 48, 255, 4); memset(expected + 4, 255, 4);
    if (status == NEO_OK) { status = neo_buffer_read(vm, buffer, 0, pixels, sizeof(pixels)); }
    if (status == NEO_OK) { status = neo_object_identity(vm, geometry, &current); }
    if (status == NEO_OK && (memcmp(pixels, expected, sizeof(pixels)) != 0 || current != original ||
        !neo_test_matrix_value(vm, geometry, "vertices", (neo_value){.kind=NEO_INTEGERS,.integers=vertices,.count=132}))) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, other_view, "result-vertex-count", (neo_value){.kind=NEO_INTEGER,.integer=0}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, other_view, "result", (neo_value){.kind=NEO_INTEGERS}); }
    if (status == NEO_OK && !neo_test_matrix_call(vm, renderer, "render", NEO_OK)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_buffer_read(vm, buffer, 0, pixels, sizeof(pixels)); }
    for (size_t i = 0; status == NEO_OK && i < sizeof(pixels); ++i) { if (pixels[i] != 0) { status = NEO_BAD_STATE; } }
    if (status == NEO_OK && (!neo_test_matrix_call(vm, other_view, "project", NEO_OK) || !neo_test_matrix_call(vm, renderer, "frame", NEO_OK))) { status = NEO_BAD_STATE; }
    /* A late transform overflow must occur before clear, too. */
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, renderer, "pixels-per-unit", (neo_value){.kind=NEO_INTEGER,.integer=INT64_MAX}); }
    if (status == NEO_OK && !neo_test_matrix_call(vm, renderer, "render", NEO_OVERFLOW)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, renderer, "pixels-per-unit", (neo_value){.kind=NEO_INTEGER,.integer=1}); }
    /* Ordinary validation and denied authority must leave the previous buffer intact. */
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, renderer, "red", (neo_value){.kind=NEO_INTEGER,.integer=256}); }
    if (status == NEO_OK && !neo_test_matrix_call(vm, renderer, "render", NEO_RAISED)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, renderer, "red", (neo_value){.kind=NEO_INTEGER,.integer=255}); }
    if (status == NEO_OK) { status = neo_object_disconnect(vm, renderer, "buffer"); }
    if (status == NEO_OK) { status = neo_object_connect(vm, renderer, "buffer", buffer, NEO_READ); }
    if (status == NEO_OK && !neo_test_matrix_call(vm, renderer, "render", NEO_DENIED)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_buffer_read(vm, buffer, 0, pixels, sizeof(pixels)); }
    if (status == NEO_OK && memcmp(pixels, expected, sizeof(pixels)) != 0) { status = NEO_BAD_STATE; }
    /* Persist the single ordinary graph after removing external resource grants. */
    if (status == NEO_OK) { status = neo_object_disconnect(vm, renderer, "buffer"); }
    if (status == NEO_OK) { status = neo_object_disconnect(vm, renderer, "window"); }
    if (status == NEO_OK) { status = neo_object_delete(vm, buffer); }
    if (status == NEO_OK) { status = neo_object_delete(vm, window); }
    char *text = NULL; const neo_capability *restored = NULL;
    if (status == NEO_OK) { status = neo_image_format(vm, root, &text); }
    if (status == NEO_OK) { status = neo_image_parse(vm, text, &restored, &diagnostic); }
    neo_image_text_free(vm, text);
    const neo_capability *saved = NULL, *source = NULL;
    if (status == NEO_OK) { status = neo_object_child(vm, restored, "view-package", &saved); }
    if (status == NEO_OK) { status = neo_object_child(vm, saved, "contents", &saved); }
    if (status == NEO_OK) { status = neo_object_child(vm, saved, "projection", &saved); }
    if (status == NEO_OK) { status = neo_object_connection(vm, saved, "source", &source); }
    if (status == NEO_OK && (!neo_test_matrix_call(vm, saved, "project", NEO_OK) ||
        !neo_test_matrix_value(vm, source, "vertices", (neo_value){.kind=NEO_INTEGERS,.integers=vertices,.count=132}))) { status = NEO_BAD_STATE; }
    if (restored != NULL) { (void)neo_image_unload(vm, restored); }
    if (root != NULL) { (void)neo_image_unload(vm, root); }
    neo_headless_release(control);
    if (status != NEO_OK) { fprintf(stderr, "FAIL image point renderer/projection/edit/checkpoint: %s %s\n", neo_status_name(status), diagnostic.message); }
    else { printf("PASS image point renderer: 4 vertices, 5x5 pixels, max %zu steps/frame (budget100000)\n", maximum_steps); }
    return status == NEO_OK;
}

static bool neo_test_line_renderer(neo_vm *vm) {
    const neo_capability *root = NULL, *renderer = NULL, *view = NULL, *geometry = NULL;
    const neo_capability *artifact = NULL, *package = NULL, *contents = NULL, *buffer = NULL, *window = NULL;
    neo_headless_display *control = NULL; neo_diagnostic diagnostic = {0};
    neo_status status = neo_platform_load_image(vm, "neo/packages/line-renderer.neo", &root, &diagnostic);
    if (status == NEO_OK) { status = neo_package_validate(vm, root); }
    if (status == NEO_OK) { status = neo_object_child(vm, root, "contents", &contents); }
    if (status == NEO_OK) { status = neo_object_child(vm, contents, "renderer", &renderer); }
    const char *paths[] = {"neo/packages/projection.neo", "neo/packages/geometry.neo"};
    for (size_t i = 0; status == NEO_OK && i < 2; ++i) {
        status = neo_platform_load_image(vm, paths[i], &artifact, &diagnostic);
        if (status == NEO_OK) { status = neo_object_import(vm, artifact, root, i == 0 ? "view-package" : "geometry-package", &package); }
        if (artifact != NULL) { (void)neo_image_unload(vm, artifact); artifact = NULL; }
        if (status == NEO_OK) { status = neo_object_child(vm, package, "contents", &contents); }
        if (status == NEO_OK) { status = neo_object_child(vm, contents, i == 0 ? "projection" : "geometry", i == 0 ? &view : &geometry); }
    }
    if (status == NEO_OK) { status = neo_object_connect(vm, view, "source", geometry, NEO_READ); }
    if (status == NEO_OK) { status = neo_object_connect(vm, renderer, "view", view, NEO_READ); }
    if (status == NEO_OK) { status = neo_headless_open(vm, root, "window", 5, 5, &control, &window); }
    if (status == NEO_OK) { status = neo_buffer_create(vm, root, "buffer", 5, 5, &buffer); }
    if (status == NEO_OK) { status = neo_object_connect(vm, renderer, "buffer", buffer, NEO_READ | NEO_WRITE); }
    if (status == NEO_OK) { status = neo_object_connect(vm, renderer, "window", window, NEO_WRITE); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, geometry, "segments", (neo_value){.kind=NEO_INTEGERS,.integers=(int64_t[]){0,1},.count=2}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, geometry, "vertex-count", (neo_value){.kind=NEO_INTEGER,.integer=2}); }
    const struct { int64_t x0,y0,x1,y1; const char *mask; } cases[] = {
        {-2,2,6,2,"00000" "00000" "11111" "00000" "00000"},
        {2,-2,2,6,"00100" "00100" "00100" "00100" "00100"},
        {-2,-2,6,6,"10000" "01000" "00100" "00010" "00001"},
        {6,6,-2,-2,"10000" "01000" "00100" "00010" "00001"},
        {2,2,2,2,"00000" "00000" "00100" "00000" "00000"},
        {-3,-2,-1,-2,"00000" "00000" "00000" "00000" "00000"},
        {-1000000000,2,1000000000,2,"00000" "00000" "11111" "00000" "00000"},
        {0,4,4,0,"00001" "00010" "00100" "01000" "10000"}
    };
    const int64_t dimensions[] = {2,3,4,33}; int64_t vertices[66]; uint8_t pixels[100] = {0}; size_t max_steps = 0;
    for (size_t d = 0; status == NEO_OK && d < 4; ++d) {
        size_t dimension = (size_t)dimensions[d];
        status = neo_test_matrix_write(vm, geometry, "dimension", (neo_value){.kind=NEO_INTEGER,.integer=(int64_t)dimension});
        if (status == NEO_OK) { status = neo_test_matrix_write(vm, view, "axes", (neo_value){.kind=NEO_INTEGERS,.integers=(int64_t[]){0,(int64_t)dimension-1},.count=2}); }
        for (size_t c = 0; status == NEO_OK && c < sizeof(cases)/sizeof(cases[0]); ++c) {
            for (size_t i = 0; i < 2*dimension; ++i) { vertices[i] = 777; }
            vertices[0] = cases[c].x0*1000000; vertices[dimension-1] = -cases[c].y0*1000000;
            vertices[dimension] = cases[c].x1*1000000; vertices[2*dimension-1] = -cases[c].y1*1000000;
            status = neo_test_matrix_write(vm, geometry, "vertices", (neo_value){.kind=NEO_INTEGERS,.integers=vertices,.count=2*dimension});
            if (status == NEO_OK && !neo_test_matrix_call(vm, view, "project", NEO_OK)) { status = NEO_BAD_STATE; }
            neo_execution report = {0};
            if (status == NEO_OK) { status = neo_behavior_run(vm, renderer, "frame", NULL, 100000, &report); }
            if (report.steps > max_steps) { max_steps = report.steps; }
            neo_execution_release(vm, &report);
            size_t count = 0, width = 0, height = 0; uint64_t frames = 0;
            if (status == NEO_OK) { status = neo_headless_frame(control, pixels, sizeof(pixels), &count, &width, &height, &frames); }
            for (size_t i = 0; status == NEO_OK && i < sizeof(pixels); ++i) {
                if (pixels[i] != (cases[c].mask[i/4] == '1' ? 255 : 0)) { status = NEO_BAD_STATE; }
            }
            if (status == NEO_OK && !neo_test_matrix_value(vm, geometry, "vertices", (neo_value){.kind=NEO_INTEGERS,.integers=vertices,.count=2*dimension})) { status = NEO_BAD_STATE; }
        }
    }
    uint8_t previous[100]; memcpy(previous, pixels, sizeof(previous));
    /* All ordinary topology/shape/transform failures precede buffer clear. */
    for (size_t trial = 0; status == NEO_OK && trial < 4; ++trial) {
        int64_t indices[] = {0, trial == 0 ? -1 : 2};
        if (trial < 3) { status = neo_test_matrix_write(vm, geometry, "segments", (neo_value){.kind=NEO_INTEGERS,.integers=indices,.count=trial == 2 ? 1 : 2}); }
        else {
            status = neo_test_matrix_write(vm, geometry, "segments", (neo_value){.kind=NEO_INTEGERS,.integers=(int64_t[]){0,1},.count=2});
            if (status == NEO_OK) { status = neo_test_matrix_write(vm, view, "result-vertex-count", (neo_value){.kind=NEO_INTEGER,.integer=1}); }
        }
        if (status == NEO_OK && !neo_test_matrix_call(vm, renderer, "render", NEO_RAISED)) { status = NEO_BAD_STATE; }
        if (status == NEO_OK) { status = neo_buffer_read(vm, buffer, 0, pixels, sizeof(pixels)); }
        if (status == NEO_OK && memcmp(pixels, previous, sizeof(pixels)) != 0) { status = NEO_BAD_STATE; }
    }
    if (status == NEO_OK && !neo_test_matrix_call(vm, view, "project", NEO_OK)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, view, "result", (neo_value){.kind=NEO_INTEGERS,.integers=(int64_t[]){-10000000000000000,-10000000000000000,10000000000000000,10000000000000000},.count=4}); }
    if (status == NEO_OK && !neo_test_matrix_call(vm, renderer, "render", NEO_OVERFLOW)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_buffer_read(vm, buffer, 0, pixels, sizeof(pixels)); }
    if (status == NEO_OK && memcmp(pixels, previous, sizeof(pixels)) != 0) { status = NEO_BAD_STATE; }
    if (root != NULL) { (void)neo_image_unload(vm, root); }
    neo_headless_release(control);
    if (status != NEO_OK) { fprintf(stderr, "FAIL image line renderer/topology/viewport: %s %s\n", neo_status_name(status), diagnostic.message); }
    else { printf("PASS image line renderer: 32 reference frames, max %zu steps/frame (budget100000)\n", max_steps); }
    return status == NEO_OK;
}

static bool neo_test_segment_clipper(neo_vm *vm) {
    const neo_capability *root = NULL, *clipper = NULL, *view = NULL, *geometry = NULL, *renderer = NULL;
    const neo_capability *artifact = NULL, *package = NULL, *contents = NULL, *buffer = NULL, *window = NULL;
    neo_headless_display *control = NULL; neo_diagnostic diagnostic = {0};
    neo_status status = neo_platform_load_image(vm, "neo/packages/segment-clipper.neo", &root, &diagnostic);
    if (status == NEO_OK) { status = neo_package_validate(vm, root); }
    if (status == NEO_OK) { status = neo_object_child(vm, root, "contents", &contents); }
    if (status == NEO_OK) { status = neo_object_child(vm, contents, "clipper", &clipper); }
    const char *paths[] = {"neo/packages/perspective.neo", "neo/packages/geometry.neo", "neo/packages/line-renderer.neo"};
    const char *names[] = {"perspective", "geometry", "lines"};
    const char *exports[] = {"view", "geometry", "renderer"};
    for (size_t i = 0; status == NEO_OK && i < 3; ++i) {
        status = neo_platform_load_image(vm, paths[i], &artifact, &diagnostic);
        if (status == NEO_OK) { status = neo_object_import(vm, artifact, root, names[i], &package); }
        if (artifact != NULL) { (void)neo_image_unload(vm, artifact); artifact = NULL; }
        if (status == NEO_OK) { status = neo_object_child(vm, package, "contents", &contents); }
        if (status == NEO_OK) { status = neo_object_child(vm, contents, exports[i], i == 0 ? &view : i == 1 ? &geometry : &renderer); }
    }
    if (status == NEO_OK) { status = neo_object_connect(vm, view, "source", geometry, NEO_READ); }
    if (status == NEO_OK) { status = neo_object_connect(vm, clipper, "input", view, NEO_READ); }
    if (status == NEO_OK) { status = neo_object_connect(vm, renderer, "view", clipper, NEO_READ); }
    if (status == NEO_OK) { status = neo_headless_open(vm, root, "window", 5, 5, &control, &window); }
    if (status == NEO_OK) { status = neo_buffer_create(vm, root, "buffer", 5, 5, &buffer); }
    if (status == NEO_OK) { status = neo_object_connect(vm, renderer, "buffer", buffer, NEO_READ | NEO_WRITE); }
    if (status == NEO_OK) { status = neo_object_connect(vm, renderer, "window", window, NEO_WRITE); }
    const char *settings[] = {"origin-x", "origin-y", "pixels-per-unit"};
    for (size_t i = 0; status == NEO_OK && i < 3; ++i) { status = neo_test_matrix_write(vm, renderer, settings[i], (neo_value){.kind=NEO_INTEGER,.integer=2}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, geometry, "vertex-count", (neo_value){.kind=NEO_INTEGER,.integer=2}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, geometry, "segments", (neo_value){.kind=NEO_INTEGERS,.integers=(int64_t[]){0,1},.count=2}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, view, "view-dimension", (neo_value){.kind=NEO_INTEGER,.integer=2}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, view, "offset", (neo_value){.kind=NEO_INTEGERS,.integers=(int64_t[]){0,0,0},.count=3}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, view, "near", (neo_value){.kind=NEO_INTEGER,.integer=1000000}); }
    const struct { int64_t h[6], far, output[4]; size_t count; const char *mask; } cases[] = {
        {{-2,0,1,2,0,1},0,{-1000000,0,1000000,0},4,"00000" "00000" "11111" "00000" "00000"},
        {{0,0,0,0,0,2},0,{0,0,0,0},4,"00000" "00000" "00100" "00000" "00000"},
        {{2,0,1,3,0,1},0,{0},0,"00000" "00000" "00000" "00000" "00000"},
        {{-1,-1,1,1,1,1},0,{-1000000,-1000000,1000000,1000000},4,"00001" "00010" "00100" "01000" "10000"},
        {{0,0,1,3,0,3},2,{0,0,750000,0},4,"00000" "00000" "00110" "00000" "00000"},
        {{0,0,-2,0,0,-1},0,{0},0,"00000" "00000" "00000" "00000" "00000"}
    };
    const int64_t dimensions[] = {3,4,7,33}; int64_t vertices[66], map[99]; size_t max_steps = 0;
    for (size_t d = 0; status == NEO_OK && d < 4; ++d) {
        size_t dimension = (size_t)dimensions[d]; memset(map, 0, sizeof(map));
        map[0] = 1000000; map[dimension+1] = 1000000; map[3*dimension-1] = 1000000;
        status = neo_test_matrix_write(vm, geometry, "dimension", (neo_value){.kind=NEO_INTEGER,.integer=(int64_t)dimension});
        if (status == NEO_OK) { status = neo_test_matrix_write(vm, view, "map", (neo_value){.kind=NEO_INTEGERS,.integers=map,.count=3*dimension}); }
        for (size_t c = 0; status == NEO_OK && c < sizeof(cases)/sizeof(cases[0]); ++c) {
            for (size_t i = 0; i < 2*dimension; ++i) { vertices[i] = 777; }
            for (size_t row = 0; row < 2; ++row) {
                vertices[row*dimension] = cases[c].h[3*row]*1000000;
                vertices[row*dimension+1] = cases[c].h[3*row+1]*1000000;
                vertices[row*dimension+dimension-1] = cases[c].h[3*row+2]*1000000;
            }
            status = neo_test_matrix_write(vm, geometry, "vertices", (neo_value){.kind=NEO_INTEGERS,.integers=vertices,.count=2*dimension});
            if (status == NEO_OK) { status = neo_test_matrix_write(vm, view, "far", (neo_value){.kind=NEO_INTEGER,.integer=cases[c].far*1000000}); }
            if (status == NEO_OK && !neo_test_matrix_call(vm, view, "project", NEO_OK)) { status = NEO_BAD_STATE; }
            neo_execution report = {0};
            if (status == NEO_OK) { status = neo_behavior_run(vm, clipper, "clip", NULL, 100000, &report); }
            if (report.steps > max_steps) { max_steps = report.steps; }
            neo_execution_release(vm, &report);
            if (status == NEO_OK && (!neo_test_matrix_value(vm, clipper, "result", (neo_value){.kind=NEO_INTEGERS,.integers=cases[c].output,.count=cases[c].count}) ||
                !neo_test_matrix_value(vm, geometry, "vertices", (neo_value){.kind=NEO_INTEGERS,.integers=vertices,.count=2*dimension}) ||
                !neo_test_matrix_call(vm, renderer, "frame", NEO_OK))) { status = NEO_BAD_STATE; }
            uint8_t pixels[100];
            if (status == NEO_OK) { status = neo_buffer_read(vm, buffer, 0, pixels, sizeof(pixels)); }
            for (size_t i = 0; status == NEO_OK && i < sizeof(pixels); ++i) { if (pixels[i] != (cases[c].mask[i/4] == '1' ? 255 : 0)) { status = NEO_BAD_STATE; } }
            if (status == NEO_OK && c == 0 && (!neo_test_matrix_value(vm, clipper, "parameter-numerators", (neo_value){.kind=NEO_INTEGERS,.integers=(int64_t[]){1,3},.count=2}) ||
                !neo_test_matrix_value(vm, clipper, "parameter-denominators", (neo_value){.kind=NEO_INTEGERS,.integers=(int64_t[]){4,4},.count=2}))) { status = NEO_BAD_STATE; }
        }
    }
    /* Rejection must preserve the last published output; no native divide by w. */
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, view, "near", (neo_value){.kind=NEO_INTEGER,.integer=0}); }
    if (status == NEO_OK && !neo_test_matrix_call(vm, clipper, "clip", NEO_RAISED)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, view, "near", (neo_value){.kind=NEO_INTEGER,.integer=1}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, view, "homogeneous", (neo_value){.kind=NEO_INTEGERS,.integers=(int64_t[]){INT64_MAX,0,1000000,INT64_MAX,0,1000000},.count=6}); }
    if (status == NEO_OK && !neo_test_matrix_call(vm, clipper, "clip", NEO_OVERFLOW)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK && !neo_test_matrix_value(vm, clipper, "result", (neo_value){.kind=NEO_INTEGERS})) { status = NEO_BAD_STATE; }
    if (root != NULL) { (void)neo_image_unload(vm, root); }
    neo_headless_release(control);
    if (status != NEO_OK) { fprintf(stderr, "FAIL homogeneous segment clipping/generated lines: %s %s\n", neo_status_name(status), diagnostic.message); }
    else { printf("PASS homogeneous segments: 24 reference frames, max %zu steps/clip (budget100000)\n", max_steps); }
    return status == NEO_OK;
}

static bool neo_test_triangle_renderer(neo_vm *vm) {
    const neo_capability *root = NULL, *renderer = NULL, *view = NULL, *geometry = NULL;
    const neo_capability *artifact = NULL, *package = NULL, *contents = NULL, *buffer = NULL, *window = NULL;
    neo_headless_display *control = NULL; neo_diagnostic diagnostic = {0};
    neo_status status = neo_platform_load_image(vm, "neo/packages/triangle-renderer.neo", &root, &diagnostic);
    if (status == NEO_OK) { status = neo_package_validate(vm, root); }
    if (status == NEO_OK) { status = neo_object_child(vm, root, "contents", &contents); }
    if (status == NEO_OK) { status = neo_object_child(vm, contents, "renderer", &renderer); }
    const char *paths[] = {"neo/packages/projection.neo", "neo/packages/geometry.neo"};
    for (size_t i = 0; status == NEO_OK && i < 2; ++i) {
        status = neo_platform_load_image(vm, paths[i], &artifact, &diagnostic);
        if (status == NEO_OK) { status = neo_object_import(vm, artifact, root, i == 0 ? "view" : "geometry", &package); }
        if (artifact != NULL) { (void)neo_image_unload(vm, artifact); artifact = NULL; }
        if (status == NEO_OK) { status = neo_object_child(vm, package, "contents", &contents); }
        if (status == NEO_OK) { status = neo_object_child(vm, contents, i == 0 ? "projection" : "geometry", i == 0 ? &view : &geometry); }
    }
    if (status == NEO_OK) { status = neo_object_connect(vm, view, "source", geometry, NEO_READ); }
    if (status == NEO_OK) { status = neo_object_connect(vm, renderer, "view", view, NEO_READ); }
    if (status == NEO_OK) { status = neo_headless_open(vm, root, "window", 5, 5, &control, &window); }
    if (status == NEO_OK) { status = neo_buffer_create(vm, root, "buffer", 5, 5, &buffer); }
    if (status == NEO_OK) { status = neo_object_connect(vm, renderer, "buffer", buffer, NEO_READ | NEO_WRITE); }
    if (status == NEO_OK) { status = neo_object_connect(vm, renderer, "window", window, NEO_WRITE); }
    const int64_t dimensions[] = {2,3,4,33}; int64_t vertices[198]; size_t max_steps = 0;
    uint8_t pixels[100] = {0}, expected[100] = {0};
    for (size_t d = 0; status == NEO_OK && d < 4; ++d) {
        size_t dimension = (size_t)dimensions[d];
        for (size_t i = 0; i < 6*dimension; ++i) { vertices[i] = 777; }
        for (size_t row = 0; row < 6; ++row) {
            vertices[row*dimension] = row%3 == 1 ? 4000000 : 0;
            vertices[row*dimension+dimension-1] = row%3 == 2 ? -4000000 : 0;
        }
        status = neo_test_matrix_write(vm, geometry, "dimension", (neo_value){.kind=NEO_INTEGER,.integer=(int64_t)dimension});
        if (status == NEO_OK) { status = neo_test_matrix_write(vm, geometry, "vertex-count", (neo_value){.kind=NEO_INTEGER,.integer=6}); }
        if (status == NEO_OK) { status = neo_test_matrix_write(vm, geometry, "vertices", (neo_value){.kind=NEO_INTEGERS,.integers=vertices,.count=6*dimension}); }
        if (status == NEO_OK) { status = neo_test_matrix_write(vm, view, "axes", (neo_value){.kind=NEO_INTEGERS,.integers=(int64_t[]){0,(int64_t)dimension-1},.count=2}); }
        if (status == NEO_OK && !neo_test_matrix_call(vm, view, "project", NEO_OK)) { status = NEO_BAD_STATE; }
        for (size_t variant = 0; status == NEO_OK && variant < 3; ++variant) {
            const int64_t *faces = variant == 1 ? (int64_t[]){3,4,5,0,1,2} : (int64_t[]){0,1,2,3,4,5};
            const int64_t *colours = variant == 1 ? (int64_t[]){0,0,255,255,255,0,0,255} : (int64_t[]){255,0,0,255,0,0,255,255};
            const int64_t *depths = variant == 2 ? (int64_t[]){0,12,0,5,5,5} : (int64_t[]){10,10,10,5,5,5};
            status = neo_test_matrix_write(vm, geometry, "triangles", (neo_value){.kind=NEO_INTEGERS,.integers=faces,.count=6});
            if (status == NEO_OK) { status = neo_test_matrix_write(vm, renderer, "face-colours", (neo_value){.kind=NEO_INTEGERS,.integers=colours,.count=8}); }
            if (status == NEO_OK) { status = neo_test_matrix_write(vm, renderer, "depths", (neo_value){.kind=NEO_INTEGERS,.integers=depths,.count=6}); }
            neo_execution report = {0};
            if (status == NEO_OK) { status = neo_behavior_run(vm, renderer, "frame", NULL, 100000, &report); }
            if (report.steps > max_steps) { max_steps = report.steps; }
            neo_execution_release(vm, &report);
            memset(expected, 0, sizeof(expected)); int64_t expected_depth[25] = {0};
            for (size_t y = 0; y < 3; ++y) { for (size_t x = 0; x+y < 3; ++x) {
                size_t pixel = y*5+x;
                expected[pixel*4+(variant == 2 && x < 2 ? 0u : 2u)] = 255; expected[pixel*4+3] = 255;
                expected_depth[pixel] = variant == 2 && x == 0 ? 2 : 5;
            } }
            if (status == NEO_OK) { status = neo_buffer_read(vm, buffer, 0, pixels, sizeof(pixels)); }
            if (status == NEO_OK && (memcmp(pixels, expected, sizeof(pixels)) != 0 ||
                !neo_test_matrix_value(vm, renderer, "drawn-count", (neo_value){.kind=NEO_INTEGER,.integer=6}) ||
                !neo_test_matrix_value(vm, renderer, "depth-buffer", (neo_value){.kind=NEO_INTEGERS,.integers=expected_depth,.count=25}))) { status = NEO_BAD_STATE; }
        }
        if (status == NEO_OK && !neo_test_matrix_value(vm, geometry, "vertices", (neo_value){.kind=NEO_INTEGERS,.integers=vertices,.count=6*dimension})) { status = NEO_BAD_STATE; }
    }
    /* Adjacent triangles tile a square with exactly one owner on their diagonal. */
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, geometry, "dimension", (neo_value){.kind=NEO_INTEGER,.integer=2}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, geometry, "vertex-count", (neo_value){.kind=NEO_INTEGER,.integer=4}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, geometry, "vertices", (neo_value){.kind=NEO_INTEGERS,.integers=(int64_t[]){0,0,4000000,0,0,-4000000,4000000,-4000000},.count=8}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, geometry, "triangles", (neo_value){.kind=NEO_INTEGERS,.integers=(int64_t[]){0,1,2,1,3,2},.count=6}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, renderer, "depths", (neo_value){.kind=NEO_INTEGERS,.integers=(int64_t[]){10,10,10,10},.count=4}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, view, "axes", (neo_value){.kind=NEO_INTEGERS,.integers=(int64_t[]){0,1},.count=2}); }
    if (status == NEO_OK && (!neo_test_matrix_call(vm, view, "project", NEO_OK) || !neo_test_matrix_call(vm, renderer, "frame", NEO_OK))) { status = NEO_BAD_STATE; }
    memset(expected, 0, sizeof(expected));
    for (size_t y = 0; y < 4; ++y) { for (size_t x = 0; x < 4; ++x) { expected[(y*5+x)*4+(x+y < 3 ? 0u : 2u)] = 255; expected[(y*5+x)*4+3] = 255; } }
    if (status == NEO_OK) { status = neo_buffer_read(vm, buffer, 0, pixels, sizeof(pixels)); }
    if (status == NEO_OK && (memcmp(pixels, expected, sizeof(pixels)) != 0 ||
        !neo_test_matrix_value(vm, renderer, "fragment-count", (neo_value){.kind=NEO_INTEGER,.integer=16}) ||
        !neo_test_matrix_value(vm, renderer, "drawn-count", (neo_value){.kind=NEO_INTEGER,.integer=16}))) { status = NEO_BAD_STATE; }
    for (size_t trial = 0; status == NEO_OK && trial < 3; ++trial) {
        status = neo_test_matrix_write(vm, renderer, "depths", (neo_value){.kind=NEO_INTEGERS,.integers=trial == 1 ? (int64_t[]){INT64_MAX,INT64_MAX,INT64_MAX,INT64_MAX} : (int64_t[]){10,10,10,10},.count=trial == 0 ? 3 : 4});
        if (status == NEO_OK && trial == 2) { status = neo_object_disconnect(vm, renderer, "buffer"); }
        if (status == NEO_OK && trial == 2) { status = neo_object_connect(vm, renderer, "buffer", buffer, NEO_READ); }
        if (status == NEO_OK && !neo_test_matrix_call(vm, renderer, "render", trial == 0 ? NEO_RAISED : trial == 1 ? NEO_OVERFLOW : NEO_DENIED)) { status = NEO_BAD_STATE; }
        if (status == NEO_OK) { status = neo_buffer_read(vm, buffer, 0, pixels, sizeof(pixels)); }
        if (status == NEO_OK && memcmp(pixels, expected, sizeof(pixels)) != 0) { status = NEO_BAD_STATE; }
    }
    if (root != NULL) { (void)neo_image_unload(vm, root); }
    neo_headless_release(control);
    if (status != NEO_OK) { fprintf(stderr, "FAIL triangle coverage/depth/RGBA: %s %s\n", neo_status_name(status), diagnostic.message); }
    else { printf("PASS image triangles: 12 overlap frames plus shared-edge quad; max %zu steps/overlap frame (budget100000)\n", max_steps); }
    return status == NEO_OK;
}

static bool neo_test_ui_canvas(neo_vm *vm) {
    const neo_capability *root = NULL, *contents = NULL, *canvas = NULL, *buffer = NULL;
    neo_diagnostic diagnostic = {0};
    neo_status status = neo_platform_load_image(vm, "neo/packages/canvas.neo", &root, &diagnostic);
    if (status == NEO_OK) { status = neo_package_validate(vm, root); }
    if (status == NEO_OK) { status = neo_object_child(vm, root, "contents", &contents); }
    if (status == NEO_OK) { status = neo_object_child(vm, contents, "canvas", &canvas); }
    if (status == NEO_OK) { status = neo_buffer_create(vm, root, "buffer", 16, 16, &buffer); }
    if (status == NEO_OK) { status = neo_object_connect(vm, canvas, "buffer", buffer, NEO_READ | NEO_WRITE); }
    if (status == NEO_OK && !neo_test_matrix_call(vm, canvas, "setup", NEO_OK)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, canvas, "text", (neo_value){.kind=NEO_TEXT,.text="A"}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, canvas, "x", (neo_value){.kind=NEO_INTEGER,.integer=1}); }
    if (status == NEO_OK) { status = neo_test_matrix_write(vm, canvas, "y", (neo_value){.kind=NEO_INTEGER,.integer=1}); }
    const unsigned rows[] = {14,17,17,31,17,17,17}; uint8_t pixels[1024], expected[1024];
    for (size_t scale = 1; status == NEO_OK && scale <= 2; ++scale) {
        status = neo_buffer_fill(vm, buffer, 0);
        if (status == NEO_OK) { status = neo_test_matrix_write(vm, canvas, "scale", (neo_value){.kind=NEO_INTEGER,.integer=(int64_t)scale}); }
        if (status == NEO_OK && !neo_test_matrix_call(vm, canvas, "text", NEO_OK)) { status = NEO_BAD_STATE; }
        memset(expected, 0, sizeof(expected));
        for (size_t y = 0; y < 7; ++y) { for (size_t x = 0; x < 5; ++x) {
            if ((rows[y] & (16u >> x)) != 0) {
                for (size_t sy = 0; sy < scale; ++sy) { for (size_t sx = 0; sx < scale; ++sx) {
                    size_t index = ((1+y*scale+sy)*16+1+x*scale+sx)*4; memset(expected+index,255,4);
                } }
            }
        } }
        if (status == NEO_OK) { status = neo_buffer_read(vm, buffer, 0, pixels, sizeof(pixels)); }
        if (status == NEO_OK && (memcmp(pixels,expected,sizeof(pixels)) != 0 ||
            !neo_test_matrix_value(vm, canvas, "drawn", (neo_value){.kind=NEO_INTEGER,.integer=(int64_t)(18*scale*scale)}))) { status = NEO_BAD_STATE; }
    }
    const char *names[] = {"clip-left","clip-top","clip-right","clip-bottom","x","y","rect-width","rect-height"};
    const int64_t values[] = {2,2,5,6,0,0,8,8};
    for (size_t i = 0; status == NEO_OK && i < 8; ++i) { status = neo_test_matrix_write(vm, canvas, names[i], (neo_value){.kind=NEO_INTEGER,.integer=values[i]}); }
    if (status == NEO_OK) { status = neo_buffer_fill(vm, buffer, 0); }
    if (status == NEO_OK && !neo_test_matrix_call(vm, canvas, "rect", NEO_OK)) { status = NEO_BAD_STATE; }
    memset(expected,0,sizeof(expected));
    for (size_t y = 2; y < 6; ++y) { for (size_t x = 2; x < 5; ++x) { memset(expected+(y*16+x)*4,255,4); } }
    if (status == NEO_OK) { status = neo_buffer_read(vm, buffer, 0, pixels, sizeof(pixels)); }
    if (status == NEO_OK && (memcmp(pixels,expected,sizeof(pixels)) != 0 ||
        !neo_test_matrix_value(vm, canvas, "drawn", (neo_value){.kind=NEO_INTEGER,.integer=12}))) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_object_disconnect(vm, canvas, "buffer"); }
    if (status == NEO_OK) { status = neo_object_connect(vm, canvas, "buffer", buffer, NEO_READ); }
    if (status == NEO_OK && !neo_test_matrix_call(vm, canvas, "rect", NEO_DENIED)) { status = NEO_BAD_STATE; }
    if (root != NULL) { (void)neo_image_unload(vm, root); }
    if (status != NEO_OK) { fprintf(stderr, "FAIL UI text/clipping foundation: %s\n", neo_status_name(status)); }
    return status == NEO_OK;
}

static bool neo_test_gui_operation(neo_vm *vm, const neo_capability *object, neo_object_id wanted) {
    neo_object_id id = 0; size_t count = 0;
    if (neo_object_identity(vm,object,&id) != NEO_OK) { return false; }
    bool found = id == wanted;
    if (!found && neo_object_child_count(vm,object,&count) == NEO_OK) {
        for (size_t i = 0; i < count && !found; ++i) {
            const neo_capability *child = NULL;
            if (neo_object_child_at(vm,object,i,&child) == NEO_OK) { found = neo_test_gui_operation(vm,child,wanted); }
        }
    }
    if (found) { const char *name = NULL; if (neo_object_name(vm,object,&name) == NEO_OK) { fprintf(stderr," /%s",name); } }
    return found;
}

static bool neo_test_gui_frame(neo_vm *vm, const neo_capability *browser, size_t *maximum) {
    neo_execution report = {0};
    neo_status status = neo_behavior_run(vm, browser, "frame", NULL, 1000000, &report);
    if (report.steps > *maximum) { *maximum = report.steps; }
    if (status != NEO_OK) { fprintf(stderr, "FAIL GUI frame: %s operation=%llu steps=%zu", neo_status_name(status), (unsigned long long)report.operation, report.steps); (void)neo_test_gui_operation(vm,browser,report.operation); fputc('\n',stderr); }
    neo_execution_release(vm, &report);
    return status == NEO_OK;
}

static bool neo_test_editing_foundations(neo_vm *vm, neo_test_allocator *allocator) {
    const neo_test_case cases[]={
        {"bounded infinite callback", "(do (attempt (budget 100) (body (while (== (left 1) (right 1)) (body (result 1)))) (recover (result 7))) (result 42))", NEO_OK, {.kind=NEO_INTEGER,.integer=42}},
        {"outer limit cannot recover", "(attempt (budget 20000) (body (while (== (left 1) (right 1)) (body (self)))) (recover (result 1)))", NEO_LIMIT, {0}},
        {"source validation", "(source-check (value \"(x)\"))", NEO_OK, {.kind=NEO_TEXT,.text=""}},
        {"active source protected", "(source-apply (target (child (target (child (target (self)) (name \"handlers\"))) (name \"main\"))) (value \"(main (body (do)))\") (expected \"\"))", NEO_DENIED, {0}},
        {"multiline splice", "(text-edit (value \"a\\nb\") (start 2) (count 1) (insert \"xy\"))", NEO_OK, {.kind=NEO_TEXT,.text="a\nxy"}},
        {"UTF8 next", "(text-offset (value \"A\xc4\xb1\") (start 1) (direction 1))", NEO_OK, {.kind=NEO_INTEGER,.integer=3}},
        {"UTF8 interior splice", "(text-edit (value \"A\xc4\xb1\") (start 2) (count 0) (insert \"x\"))", NEO_INVALID, {0}},
        {"invalid UTF8", "(text-from-bytes (value [192 128]))", NEO_INVALID, {0}},
        {"text embedded NUL", "(text-from-bytes (value [65 0]))", NEO_INVALID, {0}},
        {"line lookup", "(text-line (value \"a\\nb\") (index 1))", NEO_OK, {.kind=NEO_TEXT,.text="b"}},
        {"clamped UTF8 column", "(text-index (value \"A\xc4\xb1\") (line 0) (column 2))", NEO_OK, {.kind=NEO_INTEGER,.integer=1}}
    };
    bool okay=true;
    for (size_t i=0; i<sizeof(cases)/sizeof(cases[0]); ++i) { if (!neo_test_expression(vm,&cases[i])) { okay=false; } }
    const neo_capability *root=NULL,*actor=NULL,*handlers=NULL,*target=NULL,*state=NULL,*restricted=NULL;
    neo_diagnostic diagnostic={0}; char *baseline=NULL,*after=NULL; neo_object_id before_id=0,after_id=0;
    okay=okay && neo_image_parse(vm,"(image (actor (state 9) (handlers (main (body (result 1))))))",&root,&diagnostic) == NEO_OK &&
        neo_object_child(vm,root,"actor",&actor) == NEO_OK && neo_object_child(vm,actor,"handlers",&handlers) == NEO_OK &&
        neo_object_child(vm,handlers,"main",&target) == NEO_OK && neo_object_child(vm,actor,"state",&state) == NEO_OK &&
        neo_object_identity(vm,target,&before_id) == NEO_OK && neo_image_source(vm,target,&baseline) == NEO_OK &&
        strstr(baseline,"#") == NULL && neo_image_apply_source(vm,target,"(main (body",baseline,&diagnostic) != NEO_OK &&
        neo_image_source(vm,target,&after) == NEO_OK && strcmp(baseline,after) == 0;
    neo_image_text_free(vm,after); after=NULL;
    if (okay) { okay=neo_capability_restrict(vm,target,NEO_READ,&restricted) == NEO_OK &&
        neo_image_apply_source(vm,restricted,"(main (body (return (value 2))))",baseline,&diagnostic) == NEO_DENIED &&
        neo_image_apply_source(vm,target,"(main (body (return (value 2))))",baseline,&diagnostic) == NEO_OK &&
        neo_object_identity(vm,target,&after_id) == NEO_OK && before_id == after_id &&
        neo_test_matrix_value(vm,actor,"state",(neo_value){.kind=NEO_INTEGER,.integer=9}) &&
        neo_image_apply_source(vm,target,"(main (body (result 3)))",baseline,&diagnostic) == NEO_CONFLICT; }
    neo_execution report={0};
    if (okay) { neo_status run=neo_behavior_run(vm,actor,"main",NULL,10000,&report); okay=run == NEO_OK && report.result.kind == NEO_INTEGER && report.result.integer == 2; }
    neo_execution_release(vm,&report); neo_image_text_free(vm,baseline);
    if (root != NULL) { (void)neo_image_unload(vm,root); }
    root=NULL; baseline=NULL;
    const neo_capability *edge=NULL;
    bool committed=false;
    if (okay) { okay=neo_image_parse(vm,"(scope #s @self #s 1 (child #c 5) @child #c 1)",&root,&diagnostic) == NEO_OK && neo_image_source(vm,root,&baseline) == NEO_OK && neo_object_identity(vm,root,&before_id) == NEO_OK; }
    for (size_t offset=1; okay && !committed && offset<256; ++offset) {
        allocator->fail_at=allocator->calls+offset;
        neo_status applied=neo_image_apply_source(vm,root,"(scope #s @self #s 1 (child #c 6) @child #c 1)",baseline,&diagnostic);
        allocator->fail_at=0;
        if (applied == NEO_OK) { committed=true; }
        else { okay=applied == NEO_OUT_OF_MEMORY && neo_image_source(vm,root,&after) == NEO_OK && strcmp(after,baseline) == 0; neo_image_text_free(vm,after); after=NULL; }
    }
    if (okay) { okay=committed && neo_object_connection(vm,root,"self",&edge) == NEO_OK && neo_object_identity(vm,edge,&after_id) == NEO_OK && before_id == after_id && neo_object_connection(vm,root,"child",&edge) == NEO_OK; }
    neo_value edge_value={0};
    if (okay) { okay=neo_object_read(vm,edge,&edge_value) == NEO_OK && edge_value.kind == NEO_INTEGER && edge_value.integer == 6; }
    neo_image_text_free(vm,baseline); if (root != NULL) { (void)neo_image_unload(vm,root); }
    if (!okay) { fprintf(stderr,"FAIL editing foundations: %s\n",diagnostic.message); }
    else { puts("PASS editing foundations: bounded callback, UTF8 splice/cursor, validated atomic replacement, identity/state preservation, stale draft and authority"); }
    return okay;
}

static bool neo_test_desktop_model(neo_vm *vm) {
    const neo_capability *root = NULL, *contents = NULL, *desktop = NULL, *windows = NULL, *staging = NULL;
    const neo_capability *first = NULL, *second = NULL, *record = NULL, *linked = NULL;
    neo_diagnostic diagnostic = {0};
    bool okay = neo_platform_load_image(vm,"neo/packages/desktop-model.neo",&root,&diagnostic) == NEO_OK &&
        neo_package_validate(vm,root) == NEO_OK && neo_object_child(vm,root,"contents",&contents) == NEO_OK &&
        neo_object_child(vm,contents,"desktop",&desktop) == NEO_OK && neo_object_child(vm,desktop,"windows",&windows) == NEO_OK &&
        neo_object_child(vm,desktop,"staging",&staging) == NEO_OK &&
        neo_object_create(vm,root,"browser-app",(neo_value){0},&first) == NEO_OK &&
        neo_object_create(vm,root,"sketch-app",(neo_value){0},&second) == NEO_OK &&
        neo_object_connect(vm,desktop,"candidate",first,NEO_READ) == NEO_OK;
    const char *fields[] = {"x","y","width","height"}; const int64_t initial[] = {-50,20,100,80};
    for (size_t i = 0; okay && i < 4; ++i) { okay = neo_test_matrix_write(vm,desktop,fields[i],(neo_value){.kind=NEO_INTEGER,.integer=initial[i]}) == NEO_OK; }
    if (okay) { okay = neo_test_matrix_write(vm,desktop,"name",(neo_value){.kind=NEO_TEXT,.text="browser"}) == NEO_OK &&
        neo_test_matrix_write(vm,desktop,"title",(neo_value){.kind=NEO_TEXT,.text="System browser"}) == NEO_OK &&
        neo_test_matrix_call(vm,desktop,"open",NEO_DENIED); }
    size_t count = 0;
    if (okay) { okay = neo_object_child_count(vm,windows,&count) == NEO_OK && count == 0 &&
        neo_object_child_count(vm,staging,&count) == NEO_OK && count == 0 && neo_object_disconnect(vm,desktop,"candidate") == NEO_OK &&
        neo_object_connect(vm,desktop,"candidate",first,NEO_ALL & ~(unsigned)NEO_SEND) == NEO_OK &&
        neo_test_matrix_call(vm,desktop,"open",NEO_OK) && neo_object_child(vm,windows,"browser",&record) == NEO_OK &&
        neo_object_connection(vm,record,"app",&linked) == NEO_OK; }
    neo_object_id original = 0, current = 0;
    if (okay) { okay = neo_object_identity(vm,first,&original) == NEO_OK && neo_object_identity(vm,linked,&current) == NEO_OK && original == current &&
        neo_object_disconnect(vm,desktop,"candidate") == NEO_OK && neo_object_connect(vm,desktop,"candidate",second,127) == NEO_OK &&
        neo_test_matrix_write(vm,desktop,"name",(neo_value){.kind=NEO_TEXT,.text="sketch"}) == NEO_OK &&
        neo_test_matrix_write(vm,desktop,"x",(neo_value){.kind=NEO_INTEGER,.integer=0}) == NEO_OK &&
        neo_test_matrix_write(vm,desktop,"y",(neo_value){.kind=NEO_INTEGER,.integer=40}) == NEO_OK &&
        neo_test_matrix_call(vm,desktop,"open",NEO_OK) &&
        neo_test_matrix_write(vm,desktop,"pointer-x",(neo_value){.kind=NEO_INTEGER,.integer=20}) == NEO_OK &&
        neo_test_matrix_write(vm,desktop,"pointer-y",(neo_value){.kind=NEO_INTEGER,.integer=60}) == NEO_OK &&
        neo_test_matrix_call(vm,desktop,"hit",NEO_OK) && neo_test_matrix_value(vm,desktop,"hit-name",(neo_value){.kind=NEO_TEXT,.text="sketch"}); }
    if (okay) { okay = neo_test_matrix_write(vm,desktop,"name",(neo_value){.kind=NEO_TEXT,.text="browser"}) == NEO_OK &&
        neo_test_matrix_call(vm,desktop,"focus",NEO_OK) && neo_test_matrix_call(vm,desktop,"hit",NEO_OK) &&
        neo_test_matrix_value(vm,desktop,"hit-name",(neo_value){.kind=NEO_TEXT,.text="browser"}) &&
        neo_object_child(vm,record,"minimized",&linked) == NEO_UNAVAILABLE &&
        neo_test_matrix_call(vm,desktop,"minimize",NEO_UNAVAILABLE); }
    if (okay) { okay = neo_test_matrix_write(vm,desktop,"pan-x",(neo_value){.kind=NEO_INTEGER,.integer=100}) == NEO_OK &&
        neo_test_matrix_write(vm,desktop,"pan-y",(neo_value){.kind=NEO_INTEGER,.integer=-30}) == NEO_OK &&
        neo_test_matrix_call(vm,desktop,"pan",NEO_OK) && neo_test_matrix_call(vm,desktop,"screen",NEO_OK) &&
        neo_test_matrix_value(vm,desktop,"screen-x",(neo_value){.kind=NEO_INTEGER,.integer=-150}) &&
        neo_test_matrix_value(vm,desktop,"screen-y",(neo_value){.kind=NEO_INTEGER,.integer=50}) &&
        neo_test_matrix_write(vm,desktop,"pointer-x",(neo_value){.kind=NEO_INTEGER,.integer=-140}) == NEO_OK &&
        neo_test_matrix_write(vm,desktop,"pointer-y",(neo_value){.kind=NEO_INTEGER,.integer=60}) == NEO_OK &&
        neo_test_matrix_call(vm,desktop,"hit",NEO_OK) && neo_test_matrix_value(vm,desktop,"hit-name",(neo_value){.kind=NEO_TEXT,.text="browser"}) &&
        neo_test_matrix_write(vm,desktop,"pointer-x",(neo_value){.kind=NEO_INTEGER,.integer=-50}) == NEO_OK &&
        neo_test_matrix_call(vm,desktop,"hit",NEO_OK) && neo_test_matrix_value(vm,desktop,"hit-name",(neo_value){.kind=NEO_TEXT,.text=""}); }
    if (okay) { okay = neo_test_matrix_write(vm,desktop,"x",(neo_value){.kind=NEO_INTEGER,.integer=150}) == NEO_OK &&
        neo_test_matrix_write(vm,desktop,"y",(neo_value){.kind=NEO_INTEGER,.integer=70}) == NEO_OK &&
        neo_test_matrix_call(vm,desktop,"move",NEO_OK) &&
        neo_test_matrix_write(vm,desktop,"width",(neo_value){.kind=NEO_INTEGER,.integer=80}) == NEO_OK &&
        neo_test_matrix_write(vm,desktop,"height",(neo_value){.kind=NEO_INTEGER,.integer=60}) == NEO_OK &&
        neo_test_matrix_call(vm,desktop,"resize",NEO_OK) && neo_test_matrix_call(vm,desktop,"screen",NEO_OK) &&
        neo_test_matrix_value(vm,desktop,"screen-x",(neo_value){.kind=NEO_INTEGER,.integer=50}) &&
        neo_test_matrix_value(vm,desktop,"screen-y",(neo_value){.kind=NEO_INTEGER,.integer=100}) &&
        neo_test_matrix_write(vm,desktop,"width",(neo_value){.kind=NEO_INTEGER,.integer=0}) == NEO_OK &&
        neo_test_matrix_call(vm,desktop,"resize",NEO_RAISED) && neo_test_matrix_value(vm,record,"width",(neo_value){.kind=NEO_INTEGER,.integer=80}); }
    if (okay) { okay = neo_test_matrix_write(vm,desktop,"x",(neo_value){.kind=NEO_INTEGER,.integer=INT64_MAX}) == NEO_OK &&
        neo_test_matrix_call(vm,desktop,"move",NEO_OVERFLOW) && neo_test_matrix_value(vm,record,"x",(neo_value){.kind=NEO_INTEGER,.integer=150}) &&
        neo_test_matrix_write(vm,desktop,"pan-x",(neo_value){.kind=NEO_INTEGER,.integer=1}) == NEO_OK &&
        neo_test_matrix_write(vm,desktop,"pan-y",(neo_value){.kind=NEO_INTEGER,.integer=INT64_MIN}) == NEO_OK &&
        neo_test_matrix_call(vm,desktop,"pan",NEO_OVERFLOW) && neo_test_matrix_value(vm,desktop,"viewport-x",(neo_value){.kind=NEO_INTEGER,.integer=100}) &&
        neo_test_matrix_call(vm,desktop,"close",NEO_OK) && neo_object_identity(vm,first,&current) == NEO_OK && current == original &&
        neo_test_matrix_value(vm,desktop,"focused-name",(neo_value){.kind=NEO_TEXT,.text="sketch"}); }
    char *source = NULL; const neo_capability *restored = NULL, *restored_app = NULL, *restored_desktop = NULL;
    if (okay) { okay = neo_image_format(vm,root,&source) == NEO_OK && neo_image_parse(vm,source,&restored,&diagnostic) == NEO_OK &&
        neo_object_child(vm,restored,"contents",&contents) == NEO_OK && neo_object_child(vm,contents,"desktop",&restored_desktop) == NEO_OK &&
        neo_object_child(vm,restored_desktop,"windows",&windows) == NEO_OK && neo_object_child(vm,windows,"sketch",&record) == NEO_OK &&
        neo_object_connection(vm,record,"app",&linked) == NEO_OK && neo_object_child(vm,restored,"sketch-app",&restored_app) == NEO_OK &&
        neo_object_identity(vm,linked,&original) == NEO_OK && neo_object_identity(vm,restored_app,&current) == NEO_OK && original == current &&
        neo_test_matrix_value(vm,restored_desktop,"viewport-x",(neo_value){.kind=NEO_INTEGER,.integer=100}); }
    neo_image_text_free(vm,source);
    if (restored != NULL) { (void)neo_image_unload(vm,restored); }
    if (root != NULL) { (void)neo_image_unload(vm,root); }
    if (!okay) { fprintf(stderr,"FAIL Silk-inspired desktop model: %s\n",diagnostic.message); }
    return okay;
}

typedef struct neo_test_display_provider {
    neo_window_state state;
    neo_input_event event;
    neo_status status;
    size_t destroyed;
} neo_test_display_provider;

static neo_status neo_test_display_present(void *context, const uint8_t *pixels, size_t width, size_t height) {
    (void)context; (void)pixels; (void)width; (void)height;
    return NEO_OK;
}
static neo_status neo_test_display_poll(void *context, neo_window_state *out) {
    neo_test_display_provider *provider = context;
    *out = provider->state;
    return provider->status;
}
static neo_status neo_test_display_next(void *context, neo_input_event *out) {
    neo_test_display_provider *provider = context;
    *out = provider->event;
    return provider->status;
}
static void neo_test_display_destroy(void *context) {
    neo_test_display_provider *provider = context;
    ++provider->destroyed;
}

static bool neo_test_display_provider_boundary(neo_vm *vm) {
    const neo_capability *root = NULL, *window = NULL;
    neo_diagnostic error = {0};
    neo_test_display_provider provider = {.state={.width=2,.height=1,.input_epoch=1,.surface_epoch=1,
        .scale_numerator=3,.scale_denominator=2}};
    const neo_window_backend backend = {neo_test_display_present, neo_test_display_poll,
        neo_test_display_destroy, neo_test_display_next, NULL};
    bool okay = neo_image_parse(vm, "(image)", &root, &error) == NEO_OK &&
        neo_window_create(vm, root, "window", &backend, &provider, &window) == NEO_OK;
    neo_window_state state = {0}; neo_input_event event = {0};
    if (okay) { okay = neo_window_poll(vm, window, &state) == NEO_OK &&
        neo_window_next_event(vm, window, &event) == NEO_OK && event.kind == NEO_INPUT_RESET &&
        neo_window_next_event(vm, window, &event) == NEO_OK && event.kind == NEO_INPUT_SURFACE; }
    neo_window_state accepted = provider.state;
    for (size_t i = 0; okay && i < 9; ++i) {
        provider.state = accepted;
        if (i == 0) { provider.state.scale_denominator = 0; }
        else if (i == 1) { provider.state.surface_epoch = 0; }
        else if (i == 2) { provider.state.surface_epoch = (uint64_t)INT64_MAX + 1u; }
        else if (i == 3) { provider.state.scale_numerator = 4; }
        else if (i == 4) { provider.state.presentation_lost = true; }
        else if (i == 5) { provider.state.input_epoch = 0; }
        else if (i == 6) { provider.state.input_epoch = (uint64_t)INT64_MAX + 1u; }
        else if (i == 7) { provider.state.captured = true; }
        else { provider.state.width = 99; provider.status = NEO_UNAVAILABLE; }
        okay = neo_window_poll(vm, window, &state) == (i == 8 ? NEO_UNAVAILABLE : NEO_BAD_STATE) &&
            state.width == 0 && neo_window_get_state(vm, window, &state) == NEO_OK && state.width == 2 &&
            state.scale_numerator == 3 && state.scale_denominator == 2 && !state.presentation_lost &&
            state.surface_epoch == 1 && state.input_epoch == 1 && !state.captured &&
            neo_window_get_event(vm, window, &event) == NEO_OK && event.kind == NEO_INPUT_SURFACE;
        provider.status = NEO_OK;
    }
    provider.state = accepted;
    provider.event = (neo_input_event){.kind=NEO_INPUT_KEY,.key="enter",.epoch=1};
    if (okay) { okay = neo_window_next_event(vm, window, &event) == NEO_OK && event.kind == NEO_INPUT_KEY; }
    for (size_t i = 0; okay && i < 4; ++i) {
        provider.event = (neo_input_event){.kind=NEO_INPUT_TEXT,.epoch=1};
        if (i == 0) { memcpy(provider.event.text, "\xed\xa0\x80", 4); }
        else if (i == 1) { memset(provider.event.text, 'x', sizeof(provider.event.text)); }
        else if (i == 2) { provider.event.kind = NEO_INPUT_KEY; memset(provider.event.key, 'x', sizeof(provider.event.key)); }
        else { provider.status = NEO_IO_ERROR; }
        okay = neo_window_next_event(vm, window, &event) == (i == 3 ? NEO_IO_ERROR : NEO_BAD_STATE) &&
            event.kind == NEO_INPUT_NONE && neo_window_get_event(vm, window, &event) == NEO_OK &&
            event.kind == NEO_INPUT_KEY && strcmp(event.key, "enter") == 0;
        provider.status = NEO_OK;
    }
    provider.state.closed = true; provider.state.input_epoch = 2;
    provider.event = (neo_input_event){0};
    if (okay) { okay = neo_window_poll(vm, window, &state) == NEO_OK &&
        neo_window_next_event(vm, window, &event) == NEO_OK && event.kind == NEO_INPUT_RESET; }
    provider.state.closed = false;
    if (okay) { okay = neo_window_poll(vm, window, &state) == NEO_BAD_STATE &&
        neo_window_get_state(vm, window, &state) == NEO_OK && state.closed; }
    if (root != NULL) { (void)neo_image_unload(vm, root); }
    if (provider.destroyed != (window == NULL ? 0u : 1u)) { okay = false; }
    if (!okay) { fprintf(stderr, "FAIL display provider validation/snapshot preservation\n"); }
    return okay;
}

static bool neo_test_surface(neo_vm *vm) {
    const neo_capability *root = NULL, *actor = NULL, *window = NULL, *buffer = NULL, *readonly = NULL;
    neo_headless_display *control = NULL; neo_diagnostic error = {0};
    bool okay = neo_image_parse(vm, "(image (actor (ratio 0) (lost false) (generation 0) (handlers "
        "(surface (body (if (== (left (window-next-event (target \"window\"))) (right \"surface\")) "
        "(then (do (write (slot \"ratio\") (value (window-state (target \"window\") (field \"scale-numerator\")))) "
        "(write (slot \"lost\") (value (window-state (target \"window\") (field \"presentation-lost\")))) "
        "(write (slot \"generation\") (value (window-state (target \"window\") (field \"surface-epoch\")))))) "
        "(else (fail))))))))", &root, &error) == NEO_OK;
    if (okay) { okay = neo_object_child(vm, root, "actor", &actor) == NEO_OK &&
        neo_headless_open(vm, root, "window", 4, 3, &control, &window) == NEO_OK &&
        neo_buffer_create(vm, root, "buffer", 2, 1, &buffer) == NEO_OK &&
        neo_object_connect(vm, actor, "window", window, NEO_READ | NEO_WRITE) == NEO_OK &&
        neo_capability_restrict(vm, window, NEO_READ, &readonly) == NEO_OK; }
    neo_window_state state = {0}; neo_input_event event = {0};
    if (okay) { okay = neo_window_get_state(vm, readonly, &state) == NEO_OK && state.scale_numerator == 1 && state.scale_denominator == 1 &&
        neo_headless_scale(control, 0, 1) == NEO_INVALID && neo_headless_scale(control, 1, 0) == NEO_INVALID &&
        neo_headless_scale(control, 144, 96) == NEO_OK && neo_headless_scale(control, 3, 2) == NEO_OK &&
        neo_window_poll(vm, readonly, &state) == NEO_DENIED &&
        neo_window_poll(vm, window, &state) == NEO_OK && state.scale_numerator == 3 && state.scale_denominator == 2 &&
        state.width == 4 && state.height == 3 && state.surface_epoch == 1 && state.redraw &&
        neo_test_matrix_call(vm, actor, "surface", NEO_OK) &&
        neo_test_matrix_value(vm, actor, "ratio", (neo_value){.kind=NEO_INTEGER,.integer=3}) &&
        neo_test_matrix_value(vm, actor, "generation", (neo_value){.kind=NEO_INTEGER,.integer=1}) &&
        neo_window_next_event(vm, window, &event) == NEO_OK && event.kind == NEO_INPUT_NONE; }
    uint8_t bytes[8] = {0}; size_t count = 0, width = 0, height = 0; uint64_t frames = 0;
    if (okay) { okay = neo_buffer_fill(vm, buffer, 42) == NEO_OK && neo_window_present(vm, window, buffer) == NEO_OK &&
        neo_headless_present_status(control, NEO_UNAVAILABLE) == NEO_OK &&
        neo_headless_present_status(control, NEO_UNAVAILABLE) == NEO_OK &&
        neo_window_present(vm, window, buffer) == NEO_UNAVAILABLE &&
        neo_window_poll(vm, window, &state) == NEO_OK && state.presentation_lost && !state.closed && state.surface_epoch == 2 &&
        neo_test_matrix_call(vm, actor, "surface", NEO_OK) &&
        neo_test_matrix_value(vm, actor, "lost", (neo_value){.kind=NEO_BOOLEAN,.boolean=true}) &&
        neo_headless_frame(control, bytes, sizeof(bytes), &count, &width, &height, &frames) == NEO_OK &&
        frames == 1 && width == 2 && height == 1 && bytes[0] == 42; }
    for (size_t i = 0; okay && i < NEO_HEADLESS_EVENT_CAPACITY; ++i) {
        okay = neo_headless_push(control, &(neo_input_event){.kind=NEO_INPUT_POINTER}) == NEO_OK;
    }
    if (okay) { okay = neo_headless_push(control, &(neo_input_event){.kind=NEO_INPUT_POINTER}) == NEO_LIMIT &&
        neo_headless_scale(control, 2, 1) == NEO_OK && neo_headless_present_status(control, NEO_OK) == NEO_OK &&
        neo_window_poll(vm, window, &state) == NEO_OK && !state.presentation_lost && state.surface_epoch == 4 &&
        neo_window_next_event(vm, window, &event) == NEO_OK && event.kind == NEO_INPUT_RESET &&
        neo_test_matrix_call(vm, actor, "surface", NEO_OK) &&
        neo_test_matrix_value(vm, actor, "ratio", (neo_value){.kind=NEO_INTEGER,.integer=2}) &&
        neo_test_matrix_value(vm, actor, "lost", (neo_value){.kind=NEO_BOOLEAN,.boolean=false}) &&
        neo_test_matrix_value(vm, actor, "generation", (neo_value){.kind=NEO_INTEGER,.integer=4}) &&
        neo_window_next_event(vm, window, &event) == NEO_OK && event.kind == NEO_INPUT_OVERFLOW && event.lost == 1 &&
        neo_window_next_event(vm, window, &event) == NEO_OK && event.kind == NEO_INPUT_NONE &&
        neo_window_present(vm, window, buffer) == NEO_OK &&
        neo_headless_frame(control, bytes, sizeof(bytes), &count, &width, &height, &frames) == NEO_OK && frames == 2 && bytes[0] == 42; }
    neo_window_state exhausted = {.scale_numerator=1,.scale_denominator=1,.surface_epoch=INT64_MAX};
    if (okay) { okay = neo_window_surface_change(&exhausted, 2, 1, true) == NEO_LIMIT &&
        exhausted.scale_numerator == 1 && !exhausted.presentation_lost &&
        neo_window_surface_change(&exhausted, 96, 96, false) == NEO_OK; }
    if (root != NULL) { (void)neo_image_unload(vm, root); }
    if (okay) { okay = neo_headless_scale(control, 1, 1) == NEO_UNAVAILABLE; }
    neo_headless_release(control);
    if (!okay) { fprintf(stderr, "FAIL scale/surface loss/recovery/image contract: %s\n", error.message); }
    return okay;
}

static bool neo_test_text_input(neo_vm *vm) {
    const char *valid[] = {"hello", "\xc4\xb1", "e\xcc\x81", "\xf0\x9f\x99\x82", "\xf4\x8f\xbf\xbf"};
    const char *invalid[] = {"", "\n", "\x7f", "\xc0\xaf", "\xe0\x80\x80", "\xf0\x80\x80\x80",
        "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xc2", "\xe2\x28\xa1", "\x80"};
    bool okay = true;
    for (size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); ++i) {
        okay = neo_text_valid(valid[i], strlen(valid[i]) + 1) && okay;
    }
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        okay = !neo_text_valid(invalid[i], strlen(invalid[i]) + 1) && okay;
    }
    uint32_t scalars[] = {32, 126, 128, 2047, 2048, 55295, 57344, 65535, 65536, 128578, 1114111};
    char encoded[5];
    for (size_t i = 0; i < sizeof(scalars) / sizeof(scalars[0]); ++i) {
        okay = neo_text_encode(scalars[i], encoded) == NEO_OK && neo_text_valid(encoded, sizeof(encoded)) && okay;
    }
    okay = neo_text_encode(128578, encoded) == NEO_OK && strcmp(encoded, valid[3]) == 0 &&
        neo_text_encode(55296, encoded) == NEO_INVALID && neo_text_encode(1114112, encoded) == NEO_INVALID && okay;
    neo_text_decoder decoder = {0}; uint32_t decoded[2] = {0};
    okay = neo_text_utf16(&decoder, 0xd83d, decoded) == 0 &&
        neo_text_utf16(&decoder, 0xde42, decoded) == 1 && decoded[0] == 128578 &&
        neo_text_utf16(&decoder, 0xdc00, decoded) == 1 && decoded[0] == 65533 &&
        neo_text_utf16(&decoder, 0xd800, decoded) == 0 &&
        neo_text_utf16(&decoder, 'A', decoded) == 2 && decoded[0] == 65533 && decoded[1] == 'A' &&
        neo_text_utf16(&decoder, 0xd800, decoded) == 0 &&
        neo_text_utf16(&decoder, 0xdbff, decoded) == 1 && decoded[0] == 65533 &&
        neo_text_utf16(&decoder, 0xdfff, decoded) == 1 && decoded[0] == 1114111 && okay;
    (void)neo_text_utf16(&decoder, 0xd83d, decoded); decoder.high = 0;
    okay = neo_text_utf16(&decoder, 0xde42, decoded) == 1 && decoded[0] == 65533 && okay;
    const neo_capability *root = NULL, *actor = NULL, *window = NULL;
    neo_headless_display *control = NULL; neo_diagnostic error = {0};
    if (okay) { okay = neo_image_parse(vm, "(image (actor (value \"\") (handlers (read (body "
        "(if (== (left (window-next-event (target \"window\"))) (right \"text\")) "
        "(then (write (slot \"value\") (value (window-event (target \"window\") (field \"text\"))))) "
        "(else (fail))))))))", &root, &error) == NEO_OK; }
    if (okay) { okay = neo_object_child(vm, root, "actor", &actor) == NEO_OK &&
        neo_headless_open(vm, root, "window", 2, 2, &control, &window) == NEO_OK &&
        neo_object_connect(vm, actor, "window", window, NEO_READ | NEO_WRITE) == NEO_OK; }
    neo_input_event input = {.kind=NEO_INPUT_TEXT,.text="\xc4\xb1" "e\xcc\x81" "\xf0\x9f\x99\x82"};
    neo_window_state state = {0}; neo_input_event event = {0};
    if (okay) { okay = neo_headless_push(control, &input) == NEO_OK && neo_window_poll(vm, window, &state) == NEO_OK &&
        neo_test_matrix_call(vm, actor, "read", NEO_OK) &&
        neo_test_matrix_value(vm, actor, "value", (neo_value){.kind=NEO_TEXT,.text=input.text}); }
    for (size_t i = 0; okay && i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        neo_input_event malformed = {.kind=NEO_INPUT_TEXT};
        memcpy(malformed.text, invalid[i], strlen(invalid[i]) + 1);
        okay = neo_headless_push(control, &malformed) == NEO_INVALID;
    }
    if (okay) { okay = neo_window_next_event(vm, window, &event) == NEO_OK && event.kind == NEO_INPUT_NONE; }
    memset(input.text, 'x', sizeof(input.text));
    if (okay) { okay = neo_headless_push(control, &input) == NEO_INVALID; }
    input.text[63] = '\0';
    if (okay) { okay = neo_headless_push(control, &input) == NEO_OK &&
        neo_window_poll(vm, window, &state) == NEO_OK && neo_window_next_event(vm, window, &event) == NEO_OK &&
        event.kind == NEO_INPUT_TEXT && strlen(event.text) == 63; }
    if (okay) { okay = neo_headless_push(control, &(neo_input_event){.kind=NEO_INPUT_KEY,.key="enter",.pressed=true}) == NEO_OK &&
        neo_window_poll(vm, window, &state) == NEO_OK && neo_window_next_event(vm, window, &event) == NEO_OK &&
        event.kind == NEO_INPUT_KEY && event.text[0] == '\0' &&
        neo_headless_push(control, &(neo_input_event){.kind=NEO_INPUT_FOCUS,.pressed=true}) == NEO_OK &&
        neo_window_poll(vm, window, &state) == NEO_OK && neo_window_next_event(vm, window, &event) == NEO_OK &&
        event.kind == NEO_INPUT_FOCUS && neo_headless_push(control, &input) == NEO_OK &&
        neo_headless_push(control, &(neo_input_event){.kind=NEO_INPUT_FOCUS,.pressed=false}) == NEO_OK &&
        neo_window_poll(vm, window, &state) == NEO_OK && neo_window_next_event(vm, window, &event) == NEO_OK &&
        event.kind == NEO_INPUT_RESET && neo_window_next_event(vm, window, &event) == NEO_OK &&
        event.kind == NEO_INPUT_FOCUS && neo_window_next_event(vm, window, &event) == NEO_OK && event.kind == NEO_INPUT_NONE; }
    if (root != NULL) { (void)neo_image_unload(vm, root); }
    neo_headless_release(control);
    if (!okay) { fprintf(stderr, "FAIL committed UTF-8 text/UTF-16 decoding/image delivery: %s\n", error.message); }
    return okay;
}

static bool neo_test_image_capacity(neo_vm *vm) {
    size_t capacity = (size_t)NEO_IMAGE_MAX_OBJECTS * 24u + 64u;
    char *source = malloc(capacity);
    if (source == NULL) { return false; }
    size_t used = 6;
    memcpy(source, "(image", used);
    /* Exercise the object bound without a single maximum-width namespace.
     * Each group contributes one container and up to32 leaf objects. */
    bool group_open = false;
    for (size_t i = 1; i < NEO_IMAGE_MAX_OBJECTS; ++i) {
        bool group = (i - 1u) % 33u == 0;
        if (group && group_open) { source[used++] = ')'; }
        int written = snprintf(source + used, capacity - used, group ? " (b%zu" : " (n%zu)", i);
        if (written <= 0 || (size_t)written >= capacity - used) { free(source); return false; }
        used += (size_t)written;
        group_open = true;
    }
    if (group_open) { source[used++] = ')'; }
    source[used] = ')'; source[used + 1] = '\0';
    const neo_capability *root = NULL, *trial = NULL, *small = NULL, *extra = NULL;
    neo_diagnostic diagnostic = {0};
    neo_status status = neo_image_parse(vm, source, &root, &diagnostic);
    char *formatted = NULL;
    if (status == NEO_OK) { status = neo_image_format(vm, root, &formatted); }
    neo_image_text_free(vm, formatted); formatted = NULL;
    if (status == NEO_OK) { status = neo_image_parse(vm, "(source (child))", &small, &diagnostic); }
    if (status == NEO_OK && (neo_object_import(vm, small, root, "overflow", &trial) != NEO_LIMIT || trial != NULL)) { status = NEO_BAD_STATE; }
    if (status == NEO_OK) { status = neo_object_create(vm, root, "extra", (neo_value){0}, &extra); }
    if (status == NEO_OK && (neo_image_format(vm, root, &formatted) != NEO_LIMIT || formatted != NULL)) { status = NEO_BAD_STATE; }
    neo_image_text_free(vm, formatted);
    memcpy(source + used, " (extra))", 10);
    if (status == NEO_OK && (neo_image_parse(vm, source, &trial, &diagnostic) != NEO_LIMIT || trial != NULL)) { status = NEO_BAD_STATE; }
    if (small != NULL) { (void)neo_image_unload(vm, small); }
    if (root != NULL) { (void)neo_image_unload(vm, root); }
    free(source);
    source = malloc((size_t)NEO_IMAGE_MAX_SOURCE_BYTES + 2u);
    if (source == NULL) { return false; }
    memset(source, ' ', (size_t)NEO_IMAGE_MAX_SOURCE_BYTES + 1u);
    memcpy(source, "(image)", 7);
    source[(size_t)NEO_IMAGE_MAX_SOURCE_BYTES + 1u] = '\0';
    if (status == NEO_OK && (neo_image_parse(vm, source, &trial, &diagnostic) != NEO_LIMIT || trial != NULL)) { status = NEO_BAD_STATE; }
    free(source);
    if (status != NEO_OK) { fprintf(stderr, "FAIL shared image capacity boundaries: %s\n", neo_status_name(status)); }
    return status == NEO_OK;
}

static bool neo_test_desktop_call(neo_vm *vm, const neo_capability *desktop, const char *selector) {
    neo_execution report={0};
    neo_status status=neo_behavior_run(vm,desktop,selector,NULL,1000000,&report);
    if (status != NEO_OK) { fprintf(stderr,"FAIL desktop %s: %s steps=%zu",selector,neo_status_name(status),report.steps); (void)neo_test_gui_operation(vm,desktop,report.operation); fputc('\n',stderr); }
    neo_execution_release(vm,&report);
    return status == NEO_OK;
}

static bool neo_test_desktop_pump(neo_vm *vm, const neo_capability *desktop,
                                  const neo_capability *window, size_t turns, size_t *maximum) {
    neo_window_state state={0};
    for (size_t i=0; i<turns; ++i) {
        if (neo_window_poll(vm,window,&state) != NEO_OK || !neo_test_gui_frame(vm,desktop,maximum)) { return false; }
    }
    return true;
}

static bool neo_test_desktop_click(neo_headless_display *control, int64_t x, int64_t y) {
    return neo_headless_push(control,&(neo_input_event){.kind=NEO_INPUT_BUTTON,.button=1,.pressed=true,.x=x,.y=y}) == NEO_OK &&
        neo_headless_push(control,&(neo_input_event){.kind=NEO_INPUT_BUTTON,.button=1,.pressed=false,.x=x,.y=y}) == NEO_OK;
}

static int64_t neo_test_desktop_integer(neo_vm *vm, const neo_capability *object, const char *name) {
    const neo_capability *slot=NULL; neo_value value={0};
    if (neo_object_child(vm,object,name,&slot) != NEO_OK || neo_object_read(vm,slot,&value) != NEO_OK || value.kind != NEO_INTEGER) { return INT64_MIN; }
    return value.integer;
}

static void neo_test_gui_capture(neo_vm *vm, const neo_capability *buffer) {
    FILE *capture=fopen("build/x86_64/Windows/desktop-editor.ppm","wb");
    if (capture == NULL) { return; }
    fputs("P6\n1100 760\n255\n",capture); uint8_t rgba[4400],rgb[3300];
    for (size_t y=0; y<760; ++y) {
        if (neo_buffer_read(vm,buffer,y*4400u,rgba,sizeof(rgba)) != NEO_OK) { break; }
        for (size_t x=0; x<1100; ++x) { memcpy(rgb+x*3u,rgba+x*4u,3); }
        (void)fwrite(rgb,1,sizeof(rgb),capture);
    }
    (void)fclose(capture);
}

static bool neo_test_desktop_session(neo_vm *vm) {
    const neo_capability *root=NULL,*desktop=NULL,*window=NULL,*buffer=NULL,*model=NULL,*windows=NULL,*workspace=NULL,*manager=NULL,*catalogue=NULL,*requests=NULL,*documents=NULL;
    neo_diagnostic error={0}; neo_headless_display *control=NULL; size_t maximum=0,count=0; int stage=1;
    bool okay=neo_platform_load_image(vm,"neo/neo-os/image.neo",&root,&error) == NEO_OK &&
        neo_object_child(vm,root,"desktop",&desktop) == NEO_OK && neo_object_child(vm,desktop,"model",&model) == NEO_OK &&
        neo_object_child(vm,model,"windows",&windows) == NEO_OK && neo_object_child(vm,root,"workspace",&workspace) == NEO_OK &&
        neo_object_child(vm,root,"package-manager",&manager) == NEO_OK && neo_object_child(vm,manager,"catalogue",&catalogue) == NEO_OK &&
        neo_object_child(vm,desktop,"requests",&requests) == NEO_OK && neo_object_child(vm,desktop,"documents",&documents) == NEO_OK &&
        neo_headless_open(vm,root,"test-window",1100,760,&control,&window) == NEO_OK && neo_buffer_create(vm,root,"test-buffer",1100,760,&buffer) == NEO_OK &&
        neo_object_connect(vm,desktop,"window",window,35) == NEO_OK && neo_object_connect(vm,desktop,"buffer",buffer,35) == NEO_OK &&
        neo_headless_push(control,&(neo_input_event){.kind=NEO_INPUT_FOCUS,.pressed=true}) == NEO_OK && neo_test_desktop_pump(vm,desktop,window,4,&maximum);
    const char *packages[]={"canvas","package-browser","sketch","text-editor","task-manager"};
    for (size_t i=0; okay && i<5; ++i) {
        char path[128]; (void)snprintf(path,sizeof(path),"neo/packages/%s.neo",packages[i]);
        const neo_capability *artifact=NULL,*record=NULL;
        okay=neo_platform_load_image(vm,path,&artifact,&error) == NEO_OK && neo_package_install(vm,catalogue,artifact,false,&record) == NEO_OK;
        if (artifact != NULL) { (void)neo_image_unload(vm,artifact); }
    }
    /* The dropdown creates; it never resumes a singleton. */
    for (size_t i=0; okay && i<2; ++i) { okay=neo_test_desktop_click(control,24,20) && neo_test_desktop_pump(vm,desktop,window,2,&maximum) &&
        neo_test_desktop_click(control,40,94) && neo_test_desktop_pump(vm,desktop,window,16,&maximum); }
    const neo_capability *first=NULL,*second=NULL,*record=NULL,*absent=NULL;
    if (okay) { stage=2; okay=neo_object_child(vm,workspace,"Sketch-1",&first) == NEO_OK && neo_object_child(vm,workspace,"Sketch-2",&second) == NEO_OK &&
        neo_object_child_count(vm,windows,&count) == NEO_OK && count == 2 &&
        neo_test_matrix_write(vm,first,"point-count",(neo_value){.kind=NEO_INTEGER,.integer=7}) == NEO_OK &&
        neo_test_matrix_value(vm,second,"point-count",(neo_value){.kind=NEO_INTEGER,.integer=0}) &&
        neo_test_desktop_click(control,100,20) && neo_test_desktop_pump(vm,desktop,window,4,&maximum) &&
        neo_test_matrix_value(vm,model,"focused-name",(neo_value){.kind=NEO_TEXT,.text="Sketch-1"}) && neo_object_child_count(vm,windows,&count) == NEO_OK && count == 2 &&
        neo_object_child(vm,windows,"Sketch-1",&record) == NEO_OK; }
    if (okay) {
        int64_t x=neo_test_desktop_integer(vm,record,"x")-neo_test_desktop_integer(vm,model,"viewport-x");
        int64_t y=neo_test_desktop_integer(vm,record,"y")-neo_test_desktop_integer(vm,model,"viewport-y");
        okay=neo_test_desktop_click(control,x+628,y+12) && neo_test_desktop_pump(vm,desktop,window,6,&maximum) &&
            neo_object_child(vm,workspace,"Sketch-1",&absent) == NEO_UNAVAILABLE && neo_object_child(vm,windows,"Sketch-1",&absent) == NEO_UNAVAILABLE;
    }
    /* Fresh standalone document, multiline input, undo/redo, independent views. */
    const neo_capability *editor=NULL,*document=NULL;
    if (okay) { stage=3; okay=neo_test_desktop_click(control,24,20) && neo_test_desktop_pump(vm,desktop,window,2,&maximum) &&
        neo_test_desktop_click(control,40,130) && neo_test_desktop_pump(vm,desktop,window,50,&maximum) &&
        neo_object_child(vm,workspace,"TextEditor-3",&editor) == NEO_OK && neo_object_connection(vm,editor,"document",&document) == NEO_OK &&
        neo_headless_push(control,&(neo_input_event){.kind=NEO_INPUT_TEXT,.text="hello"}) == NEO_OK &&
        neo_headless_push(control,&(neo_input_event){.kind=NEO_INPUT_KEY,.key="enter",.pressed=true}) == NEO_OK &&
        neo_headless_push(control,&(neo_input_event){.kind=NEO_INPUT_TEXT,.text="world"}) == NEO_OK && neo_test_desktop_pump(vm,desktop,window,12,&maximum) &&
        neo_test_matrix_value(vm,document,"text",(neo_value){.kind=NEO_TEXT,.text="hello\nworld"}); }
    if (okay) { okay=neo_headless_push(control,&(neo_input_event){.kind=NEO_INPUT_KEY,.key="z",.pressed=true,.control=true}) == NEO_OK && neo_test_desktop_pump(vm,desktop,window,6,&maximum) &&
        neo_test_matrix_value(vm,document,"text",(neo_value){.kind=NEO_TEXT,.text="hello\n"}) &&
        neo_headless_push(control,&(neo_input_event){.kind=NEO_INPUT_KEY,.key="y",.pressed=true,.control=true}) == NEO_OK && neo_test_desktop_pump(vm,desktop,window,6,&maximum) &&
        neo_test_matrix_value(vm,document,"text",(neo_value){.kind=NEO_TEXT,.text="hello\nworld"}); }
    const neo_capability *browser=NULL,*mini=NULL,*code=NULL;
    if (okay) { stage=4; okay=neo_test_desktop_click(control,24,20) && neo_test_desktop_pump(vm,desktop,window,2,&maximum) &&
        neo_test_desktop_click(control,40,58) && neo_test_desktop_pump(vm,desktop,window,100,&maximum) &&
        neo_object_child(vm,workspace,"browser-4",&browser) == NEO_OK && neo_object_child(vm,browser,"editor",&mini) == NEO_OK &&
        neo_object_connection(vm,mini,"document",&code) == NEO_OK && neo_test_matrix_value(vm,browser,"u-document-ready",(neo_value){.kind=NEO_BOOLEAN,.boolean=true}); }
    if (okay) { neo_test_gui_capture(vm,buffer); }
    /* Pop out shares the exact document and history, not a copied text snapshot. */
    const neo_capability *popout=NULL,*shared=NULL;
    neo_object_id a=0,b=0;
    if (okay) { stage=5; okay=neo_test_desktop_call(vm,browser,"ui-edit") && neo_test_desktop_call(vm,browser,"ui-full") &&
        neo_test_desktop_pump(vm,desktop,window,80,&maximum) && neo_object_child(vm,workspace,"TextEditor-5",&popout) == NEO_OK &&
        neo_object_connection(vm,popout,"document",&shared) == NEO_OK && neo_object_identity(vm,code,&a) == NEO_OK && neo_object_identity(vm,shared,&b) == NEO_OK && a == b; }
    /* Close browser: shared document survives while popout owns it. */
    if (okay) { stage=6; okay=neo_test_matrix_write(vm,requests,"name",(neo_value){.kind=NEO_TEXT,.text="browser-4"}) == NEO_OK &&
        neo_test_matrix_write(vm,requests,"action",(neo_value){.kind=NEO_TEXT,.text="close"}) == NEO_OK && neo_test_desktop_pump(vm,desktop,window,8,&maximum) &&
        neo_object_child(vm,workspace,"browser-4",&absent) == NEO_UNAVAILABLE && neo_object_identity(vm,shared,&b) == NEO_OK && a == b; }
    /* Last editor close releases its unkept document and owned pixel/glyph resources. */
    if (okay) { stage=7; okay=neo_test_matrix_write(vm,requests,"name",(neo_value){.kind=NEO_TEXT,.text="TextEditor-5"}) == NEO_OK &&
        neo_test_matrix_write(vm,requests,"action",(neo_value){.kind=NEO_TEXT,.text="close"}) == NEO_OK && neo_test_desktop_pump(vm,desktop,window,8,&maximum) &&
        neo_object_child(vm,workspace,"TextEditor-5",&absent) == NEO_UNAVAILABLE && neo_object_identity(vm,shared,&b) != NEO_OK &&
        neo_test_matrix_value(vm,desktop,"d-error",(neo_value){.kind=NEO_TEXT,.text=""}); }
    /* A small package makes live-instance editing and future-instance publication observable. */
    const neo_capability *fixture=NULL,*installed=NULL,*live=NULL,*stale_draft=NULL;
    if (okay) { stage=8; okay=neo_image_parse(vm,
        "(package (manifest (schema 1) (id \"neo.test.editing\") (version (major 0) (minor 1) (patch 0)) (dependencies) "
        "(exports (app (path \"contents/app\") (mode \"copy\") (rights 9))) (imports) (bindings)) "
        "(contents (app (title \"Edit test\") (state 7) (handlers (frame (body (do (result 0)))) (main (body (return (value 10))))))))",&fixture,&error) == NEO_OK &&
        neo_package_install(vm,catalogue,fixture,false,&installed) == NEO_OK &&
        neo_test_matrix_write(vm,requests,"name",(neo_value){.kind=NEO_TEXT,.text="export"}) == NEO_OK &&
        neo_test_matrix_write(vm,requests,"action",(neo_value){.kind=NEO_TEXT,.text="export"}) == NEO_OK &&
        neo_test_matrix_write(vm,requests,"package-id",(neo_value){.kind=NEO_TEXT,.text="neo.test.editing"}) == NEO_OK &&
        neo_test_matrix_write(vm,requests,"export-name",(neo_value){.kind=NEO_TEXT,.text="app"}) == NEO_OK &&
        neo_test_desktop_pump(vm,desktop,window,8,&maximum) && neo_object_child(vm,workspace,"app-6",&live) == NEO_OK &&
        neo_test_desktop_click(control,24,20) && neo_test_desktop_pump(vm,desktop,window,2,&maximum) &&
        neo_test_desktop_click(control,40,58) && neo_test_desktop_pump(vm,desktop,window,80,&maximum) &&
        neo_object_child(vm,workspace,"browser-7",&browser) == NEO_OK && neo_test_matrix_write(vm,browser,"u-mode",(neo_value){.kind=NEO_INTEGER,.integer=1}) == NEO_OK &&
        neo_test_matrix_write(vm,browser,"u-selected",(neo_value){.kind=NEO_INTEGER,.integer=2}) == NEO_OK &&
        neo_test_matrix_write(vm,browser,"u-handler",(neo_value){.kind=NEO_INTEGER,.integer=-1}) == NEO_OK &&
        neo_test_desktop_call(vm,browser,"ui-source") && neo_object_connection(vm,browser,"document",&code) == NEO_OK &&
        neo_test_matrix_value(vm,code,"mode",(neo_value){.kind=NEO_TEXT,.text="code"}) && neo_test_desktop_call(vm,browser,"ui-edit"); }
    if (fixture != NULL) { (void)neo_image_unload(vm,fixture); }
    /* Editing through the mini editor submits an Apply request after input returns. */
    if (okay) { stage=9; okay=neo_headless_push(control,&(neo_input_event){.kind=NEO_INPUT_KEY,.key="a",.pressed=true,.control=true}) == NEO_OK &&
        neo_headless_push(control,&(neo_input_event){.kind=NEO_INPUT_TEXT,.text="(main (body"}) == NEO_OK &&
        neo_headless_push(control,&(neo_input_event){.kind=NEO_INPUT_KEY,.key="enter",.pressed=true,.control=true}) == NEO_OK && neo_test_desktop_pump(vm,desktop,window,16,&maximum) &&
        neo_test_matrix_value(vm,code,"apply-ok",(neo_value){.kind=NEO_BOOLEAN,.boolean=false}); }
    neo_execution execution={0};
    if (okay) { okay=neo_behavior_run(vm,live,"main",NULL,10000,&execution) == NEO_OK && execution.result.kind == NEO_INTEGER && execution.result.integer == 10; }
    neo_execution_release(vm,&execution);
    if (okay) { stage=10; okay=neo_headless_push(control,&(neo_input_event){.kind=NEO_INPUT_KEY,.key="a",.pressed=true,.control=true}) == NEO_OK &&
        neo_headless_push(control,&(neo_input_event){.kind=NEO_INPUT_TEXT,.text="(main (body (return (value 42))))"}) == NEO_OK &&
        neo_headless_push(control,&(neo_input_event){.kind=NEO_INPUT_KEY,.key="enter",.pressed=true,.control=true}) == NEO_OK && neo_test_desktop_pump(vm,desktop,window,16,&maximum) &&
        neo_test_matrix_value(vm,code,"apply-ok",(neo_value){.kind=NEO_BOOLEAN,.boolean=true}) &&
        neo_behavior_run(vm,live,"main",NULL,10000,&execution) == NEO_OK && execution.result.kind == NEO_INTEGER && execution.result.integer == 42 &&
        neo_test_matrix_value(vm,live,"state",(neo_value){.kind=NEO_INTEGER,.integer=7}); }
    neo_execution_release(vm,&execution);
    /* The package draft is separate; publishing it changes only future copies. */
    if (okay) { stage=11; okay=neo_test_matrix_write(vm,browser,"u-mode",(neo_value){.kind=NEO_INTEGER,.integer=0}) == NEO_OK &&
        neo_test_matrix_write(vm,browser,"u-selected",(neo_value){.kind=NEO_INTEGER,.integer=5}) == NEO_OK &&
        neo_test_matrix_write(vm,browser,"u-handler",(neo_value){.kind=NEO_INTEGER,.integer=-1}) == NEO_OK && neo_test_desktop_call(vm,browser,"ui-source") &&
        neo_object_connection(vm,browser,"document",&code) == NEO_OK && neo_object_copy(vm,code,documents,"stale-package",&stale_draft) == NEO_OK &&
        neo_test_matrix_write(vm,code,"text",(neo_value){.kind=NEO_TEXT,.text="(main (body (return (value 99))))"}) == NEO_OK &&
        neo_test_matrix_write(vm,code,"publish-request",(neo_value){.kind=NEO_BOOLEAN,.boolean=true}) == NEO_OK && neo_test_desktop_pump(vm,desktop,window,16,&maximum) &&
        neo_test_matrix_value(vm,code,"status",(neo_value){.kind=NEO_TEXT,.text="PACKAGE PUBLISHED - EXISTING INSTANCES UNCHANGED"}) &&
        neo_test_desktop_call(vm,browser,"ui-launch") && neo_test_desktop_pump(vm,desktop,window,12,&maximum) &&
        neo_object_child(vm,workspace,"app-8",&second) == NEO_OK && neo_behavior_run(vm,second,"main",NULL,10000,&execution) == NEO_OK &&
        execution.result.kind == NEO_INTEGER && execution.result.integer == 99; }
    neo_execution_release(vm,&execution);
    if (okay) { okay=neo_behavior_run(vm,live,"main",NULL,10000,&execution) == NEO_OK && execution.result.kind == NEO_INTEGER && execution.result.integer == 42; }
    neo_execution_release(vm,&execution);
    if (okay) { okay=neo_test_matrix_write(vm,stale_draft,"publish-request",(neo_value){.kind=NEO_BOOLEAN,.boolean=true}) == NEO_OK && neo_test_desktop_pump(vm,desktop,window,8,&maximum) &&
        neo_test_matrix_value(vm,stale_draft,"status",(neo_value){.kind=NEO_TEXT,.text="PUBLISH FAILED (REOPEN IF PACKAGE CHANGED): explicit failure"}); }
    /* All app classes use the same factory; each has its own state and window. */
    if (okay) { stage=12; okay=neo_test_matrix_write(vm,requests,"name",(neo_value){.kind=NEO_TEXT,.text="app-8"}) == NEO_OK &&
        neo_test_matrix_write(vm,requests,"action",(neo_value){.kind=NEO_TEXT,.text="close"}) == NEO_OK && neo_test_desktop_pump(vm,desktop,window,6,&maximum); }
    const char *classes[]={"TaskManager","TaskManager","browser","TextEditor"};
    for (size_t i=0; okay && i<4; ++i) {
        okay=neo_test_matrix_write(vm,requests,"name",(neo_value){.kind=NEO_TEXT,.text=classes[i]}) == NEO_OK &&
            neo_test_matrix_write(vm,requests,"action",(neo_value){.kind=NEO_TEXT,.text="new"}) == NEO_OK && neo_test_desktop_pump(vm,desktop,window,100,&maximum);
    }
    const neo_capability *tasks1=NULL,*tasks2=NULL,*second_editor=NULL,*fresh=NULL,*model_read=NULL;
    if (okay) { okay=neo_object_child(vm,workspace,"TaskManager-9",&tasks1) == NEO_OK && neo_object_child(vm,workspace,"TaskManager-10",&tasks2) == NEO_OK &&
        neo_object_child(vm,workspace,"browser-11",&second) == NEO_OK && neo_object_child(vm,workspace,"TextEditor-12",&second_editor) == NEO_OK &&
        neo_object_connection(vm,second_editor,"document",&fresh) == NEO_OK && neo_test_matrix_value(vm,fresh,"text",(neo_value){.kind=NEO_TEXT,.text=""}) &&
        neo_object_identity(vm,document,&a) == NEO_OK && neo_object_identity(vm,fresh,&b) == NEO_OK && a != b &&
        neo_test_matrix_write(vm,tasks1,"t-selected",(neo_value){.kind=NEO_INTEGER,.integer=3}) == NEO_OK &&
        neo_test_matrix_value(vm,tasks2,"t-selected",(neo_value){.kind=NEO_INTEGER,.integer=0}) && neo_object_connection(vm,tasks2,"desktop-model",&model_read) == NEO_OK &&
        neo_object_write(vm,model_read,(neo_value){0}) == NEO_DENIED && neo_object_child_count(vm,windows,&count) == NEO_OK && count == 8; }
    /* Internal clipboard fallback uses explicit text sharing; documents stay independent. */
    const neo_capability *clipboard=NULL;
    if (okay) { stage=13; okay=neo_object_child(vm,desktop,"clipboard",&clipboard) == NEO_OK &&
        neo_test_matrix_write(vm,clipboard,"text",(neo_value){.kind=NEO_TEXT,.text="copied\ntext"}) == NEO_OK &&
        neo_headless_push(control,&(neo_input_event){.kind=NEO_INPUT_KEY,.key="v",.pressed=true,.control=true}) == NEO_OK && neo_test_desktop_pump(vm,desktop,window,20,&maximum) &&
        neo_test_matrix_value(vm,fresh,"text",(neo_value){.kind=NEO_TEXT,.text="copied\ntext"}) &&
        neo_headless_push(control,&(neo_input_event){.kind=NEO_INPUT_KEY,.key="a",.pressed=true,.control=true}) == NEO_OK &&
        neo_headless_push(control,&(neo_input_event){.kind=NEO_INPUT_KEY,.key="c",.pressed=true,.control=true}) == NEO_OK && neo_test_desktop_pump(vm,desktop,window,10,&maximum) &&
        neo_test_matrix_value(vm,clipboard,"text",(neo_value){.kind=NEO_TEXT,.text="copied\ntext"}); }
    /* Kept data survives app termination without retaining that app or its native buffers. */
    if (okay) { stage=14; okay=neo_test_desktop_call(vm,document,"keep") &&
        neo_test_matrix_write(vm,requests,"name",(neo_value){.kind=NEO_TEXT,.text="TextEditor-3"}) == NEO_OK &&
        neo_test_matrix_write(vm,requests,"action",(neo_value){.kind=NEO_TEXT,.text="close"}) == NEO_OK && neo_test_desktop_pump(vm,desktop,window,12,&maximum) &&
        neo_object_child(vm,workspace,"TextEditor-3",&absent) == NEO_UNAVAILABLE && neo_test_matrix_value(vm,document,"text",(neo_value){.kind=NEO_TEXT,.text="hello\nworld"}) &&
        neo_test_matrix_value(vm,desktop,"d-error",(neo_value){.kind=NEO_TEXT,.text=""}); }
    /* Wheel over editor text scrolls; Ctrl+wheel remains a viewport transform. */
    if (okay) { stage=15;
        char lines[256]; size_t used=0;
        for (size_t i=0;i<45;++i) { memcpy(lines+used,"row\n",4); used+=4; } lines[used]='\0';
        okay=neo_test_matrix_write(vm,fresh,"text",(neo_value){.kind=NEO_TEXT,.text=lines}) == NEO_OK &&
            neo_test_matrix_write(vm,fresh,"revision",(neo_value){.kind=NEO_INTEGER,.integer=20}) == NEO_OK &&
            neo_test_matrix_write(vm,second_editor,"e-caret",(neo_value){.kind=NEO_INTEGER,.integer=0}) == NEO_OK &&
            neo_test_matrix_write(vm,second_editor,"e-anchor",(neo_value){.kind=NEO_INTEGER,.integer=0}) == NEO_OK &&
            neo_test_desktop_pump(vm,desktop,window,40,&maximum) && neo_object_child(vm,windows,"TextEditor-12",&record) == NEO_OK;
        int64_t x=neo_test_desktop_integer(vm,record,"x")-neo_test_desktop_integer(vm,model,"viewport-x");
        int64_t y=neo_test_desktop_integer(vm,record,"y")-neo_test_desktop_integer(vm,model,"viewport-y");
        if (okay) { okay=neo_headless_push(control,&(neo_input_event){.kind=NEO_INPUT_BUTTON,.button=5,.pressed=true,.x=x+300,.y=y+150}) == NEO_OK &&
            neo_test_desktop_pump(vm,desktop,window,20,&maximum) && neo_test_desktop_integer(vm,second_editor,"e-scroll") == 3 &&
            neo_test_desktop_integer(vm,desktop,"d-zoom") == 1000 &&
            neo_headless_push(control,&(neo_input_event){.kind=NEO_INPUT_BUTTON,.button=4,.pressed=true,.control=true,.x=x+300,.y=y+150}) == NEO_OK &&
            neo_test_desktop_pump(vm,desktop,window,8,&maximum) && neo_test_desktop_integer(vm,desktop,"d-zoom") == 1250; }
    }
    /* A runaway app exhausts only its child callback budget, leaving the shell alive. */
    const neo_capability *live_handlers=NULL,*frame=NULL,*view_resources=NULL,*view_windows=NULL,*failed_view=NULL;
    char *frame_source=NULL;
    if (okay) { stage=16; okay=neo_object_child(vm,live,"handlers",&live_handlers) == NEO_OK && neo_object_child(vm,live_handlers,"frame",&frame) == NEO_OK &&
        neo_image_source(vm,frame,&frame_source) == NEO_OK && neo_image_apply_source(vm,frame,"(frame (body (while (== (left 1) (right 1)) (body (self)))))",frame_source,&error) == NEO_OK &&
        neo_test_desktop_pump(vm,desktop,window,40,&maximum) && neo_object_child(vm,desktop,"surfaces",&view_resources) == NEO_OK &&
        neo_object_child(vm,view_resources,"windows",&view_windows) == NEO_OK && neo_object_child(vm,view_windows,"app-6",&failed_view) == NEO_OK &&
        neo_test_matrix_value(vm,failed_view,"failed",(neo_value){.kind=NEO_BOOLEAN,.boolean=true}); }
    neo_image_text_free(vm,frame_source);
    /* Repair from Browser after the callback returned; document service resumes it. */
    if (okay) { stage=17; okay=neo_test_matrix_write(vm,browser,"u-mode",(neo_value){.kind=NEO_INTEGER,.integer=1}) == NEO_OK &&
        neo_test_matrix_write(vm,browser,"u-selected",(neo_value){.kind=NEO_INTEGER,.integer=1}) == NEO_OK &&
        neo_test_matrix_write(vm,browser,"u-handler",(neo_value){.kind=NEO_INTEGER,.integer=0}) == NEO_OK && neo_test_desktop_call(vm,browser,"ui-source") &&
        neo_object_connection(vm,browser,"document",&code) == NEO_OK && neo_test_matrix_write(vm,code,"text",(neo_value){.kind=NEO_TEXT,.text="(frame (body (do (result 0))))"}) == NEO_OK &&
        neo_test_matrix_write(vm,code,"apply-request",(neo_value){.kind=NEO_BOOLEAN,.boolean=true}) == NEO_OK && neo_test_desktop_pump(vm,desktop,window,16,&maximum) &&
        neo_test_matrix_value(vm,code,"apply-ok",(neo_value){.kind=NEO_BOOLEAN,.boolean=true}) && neo_test_matrix_value(vm,failed_view,"failed",(neo_value){.kind=NEO_BOOLEAN,.boolean=false}); }
    if (!okay) {
        const neo_capability *slot=NULL; neo_value value={0};
        if (desktop != NULL && neo_object_child(vm,desktop,"d-error",&slot) == NEO_OK && neo_object_read(vm,slot,&value) == NEO_OK && value.kind == NEO_TEXT) { fprintf(stderr,"Desktop error=%s\n",value.text); }
        if (browser != NULL && neo_object_child(vm,browser,"u-status",&slot)==NEO_OK && neo_object_read(vm,slot,&value)==NEO_OK && value.kind==NEO_TEXT) {fprintf(stderr,"Browser=%s\n",value.text);}
        if (browser != NULL && neo_object_child(vm,browser,"u-count",&slot)==NEO_OK && neo_object_read(vm,slot,&value)==NEO_OK) {fprintf(stderr,"Browser count=%lld\n",(long long)value.integer);}
        if (code != NULL && neo_object_child(vm,code,"status",&slot)==NEO_OK && neo_object_read(vm,slot,&value)==NEO_OK && value.kind==NEO_TEXT) {fprintf(stderr,"Code=%s\n",value.text);}
        if (stale_draft != NULL && neo_object_child(vm,stale_draft,"status",&slot)==NEO_OK && neo_object_read(vm,slot,&value)==NEO_OK && value.kind==NEO_TEXT) {fprintf(stderr,"Stale=%s\n",value.text);}
        if (document != NULL) { const char *names[]={"text","status"}; for(size_t i=0;i<2;++i) { if(neo_object_child(vm,document,names[i],&slot)==NEO_OK&&neo_object_read(vm,slot,&value)==NEO_OK&&value.kind==NEO_TEXT) {fprintf(stderr,"Doc %s=%s\n",names[i],value.text);} } }
        fprintf(stderr,"FAIL live editing desktop stage%d: %s editor=%p count=%zu\n",stage,error.message,(const void*)editor,count);
    } else { printf("PASS desktop lifecycle/editor: fresh dropdown instances, taskbar navigation, destructive close, multiline undo/redo, embedded source, shared popout, last-owner cleanup, live Apply, versioned publish, all app classes, clipboard, kept data, scroll/zoom and runaway repair; max %zu steps/frame\n",maximum); }
    if (root != NULL) { (void)neo_image_unload(vm,root); }
    neo_headless_release(control); return okay;
}

int neo_cli_gui_selftest(void) {
    neo_test_allocator memory = {0};
    neo_allocator allocator = {&memory,neo_test_allocate,neo_test_release};
    neo_vm *vm = NULL; neo_platform_services platform;
    neo_status status = neo_vm_create(&allocator,&vm);
    if (status == NEO_OK) { status = neo_platform_native_services(NEO_PLATFORM_HOST_FILES,&platform); }
    if (status == NEO_OK) { status = neo_vm_install_platform(vm,&platform); }
    bool okay = status == NEO_OK && neo_test_ui_canvas(vm) && neo_test_editing_foundations(vm,&memory) && neo_test_desktop_model(vm) && neo_test_desktop_session(vm);
    neo_vm_destroy(vm);
    return okay && memory.live == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

int neo_cli_selftest(void) {
    neo_test_allocator memory = {0};
    neo_allocator allocator = {&memory, neo_test_allocate, neo_test_release};
    neo_vm *vm = NULL;
    if (neo_vm_create(&allocator, &vm) != NEO_OK) { return EXIT_FAILURE; }
    neo_platform_services platform;
    size_t idle_waits = 0;
    unsigned platform_flags = NEO_PLATFORM_HOST_FILES | NEO_PLATFORM_WAIT;
#ifdef NEO_PLATFORM_WITH_WIN32
    platform_flags |= NEO_PLATFORM_NETWORK;
#endif
    neo_status setup = neo_platform_native_services(platform_flags, &platform);
    if (setup == NEO_OK) { platform.backend.wait = neo_test_idle_wait; platform.context = &idle_waits; }
    if (setup == NEO_OK) { setup = neo_vm_install_platform(vm, &platform); }
    if (setup != NEO_OK) { neo_vm_destroy(vm); return EXIT_FAILURE; }
    size_t failed = 0;
    for (size_t i = 0; i < sizeof(neo_cases) / sizeof(neo_cases[0]); ++i) {
        if (!neo_test_expression(vm, &neo_cases[i])) { ++failed; }
    }
    if (!neo_test_lifetime(vm, &memory)) { ++failed; }
    if (!neo_test_sequence_namespace(vm)) { ++failed; }
    if (!neo_launch_selftest(vm)) { ++failed; }
    if (!neo_test_closed_import(vm, &memory)) { ++failed; }
    if (!neo_test_packages(vm, &memory)) { ++failed; }
    if (!neo_live_package_policy_selftest(vm)) { ++failed; }
    if (!neo_live_environment_selftest(vm)) { ++failed; }
    if (!neo_test_graph_scheduler(vm)) { ++failed; }
    if (!neo_test_message_language(vm)) { ++failed; }
    if (!neo_test_message_checkpoint(vm, &memory)) { ++failed; }
    if (!neo_test_message_order(vm)) { ++failed; }
    if (!neo_test_dispatch_recovery(vm)) { ++failed; }
    for (int state = NEO_MESSAGE_CREATED; state <= NEO_MESSAGE_PROCESSED; ++state) {
        for (unsigned loss = 1; loss <= 3; ++loss) {
            if (!neo_test_message_loss(vm, (neo_message_state)state, loss)) { ++failed; }
        }
    }
    if (!neo_live_selftest(vm)) { ++failed; }
    if (!neo_live_graph_selftest(vm)) { ++failed; }
    if (!neo_live_scheduler_selftest(vm)) { ++failed; }
    if (idle_waits != 1) { fprintf(stderr, "FAIL idle wait count: %zu\n", idle_waits); ++failed; }
    if (!neo_test_os_image(vm)) { ++failed; }
#ifdef NEO_PLATFORM_WITH_WIN32
    if (!neo_win32_tls_selftest(vm)) { ++failed; }
    if (!neo_win32_window_selftest(vm)) { ++failed; }
#endif
    if (!neo_test_image_capacity(vm)) { ++failed; }
    if (!neo_test_headless_display(vm, &memory)) { ++failed; }
    if (!neo_test_input_reset(vm)) { ++failed; }
    if (!neo_test_text_input(vm)) { ++failed; }
    if (!neo_test_surface(vm)) { ++failed; }
    if (!neo_test_point_renderer(vm)) { ++failed; }
    if (!neo_test_line_renderer(vm)) { ++failed; }
    if (!neo_test_segment_clipper(vm)) { ++failed; }
    if (!neo_test_triangle_renderer(vm)) { ++failed; }
    if (!neo_test_ui_canvas(vm)) { ++failed; }
    if (!neo_test_editing_foundations(vm,&memory)) { ++failed; }
    if (!neo_test_desktop_model(vm)) { ++failed; }
    if (!neo_test_desktop_session(vm)) { ++failed; }
    if (!neo_test_display_provider_boundary(vm)) { ++failed; }
    if (!neo_test_network_sessions(vm)) { ++failed; }
    if (!neo_test_exposure(vm)) { ++failed; }
    if (!neo_test_acknowledgements(vm)) { ++failed; }
    if (!neo_test_network_package_capacity(vm, "neo/packages/exposure.neo", "neo.network.exposure", "publisher", "subscriber")) { ++failed; }
    if (!neo_test_network_package_capacity(vm, "neo/packages/session.neo", "neo.network.session", "session", NULL)) { ++failed; }
    if (!neo_test_replica_invalid(vm)) { ++failed; }
    if (!neo_test_replicas(vm, &memory)) { ++failed; }
    if (!neo_test_network_endpoints(vm)) { ++failed; }
    if (!neo_test_network(vm, &memory)) { ++failed; }
    if (!neo_test_vector_package(vm)) { ++failed; }
    if (!neo_test_vector_edges(vm)) { ++failed; }
    if (!neo_test_matrix_package(vm)) { ++failed; }
    if (!neo_test_matrix_edges(vm)) { ++failed; }
    if (!neo_test_affine_package(vm)) { ++failed; }
    if (!neo_test_frames_package(vm)) { ++failed; }
    if (!neo_test_geometry_package(vm)) { ++failed; }
    if (!neo_test_projection_package(vm)) { ++failed; }
    if (!neo_test_section_package(vm)) { ++failed; }
    if (!neo_test_linearview_package(vm)) { ++failed; }
    if (!neo_test_perspective_package(vm)) { ++failed; }
    if (!neo_test_boot_image(vm)) { ++failed; }
    neo_vm_destroy(vm);
    if (memory.live != 0) { fprintf(stderr, "FAIL: %zu unreleased allocations\n", memory.live); ++failed; }
    if (failed != 0) { fprintf(stderr, "%zu selftest groups failed\n", failed); return EXIT_FAILURE; }
    printf("PASS: %zu expression cases; authority, lifetime, copy remapping, allocation failure, persistence, balanced ownership; OS browser, partial writes, dimensional maths; message lifecycle/failures/checkpoints; image scheduling cutoff/rotation/snapshot/membership/recovery/idle; environment services/shared clients/save policy/recovery; package schema/staging/bindings/export/update/removal; image entrypoints\n", sizeof(neo_cases) / sizeof(neo_cases[0]));
    return EXIT_SUCCESS;
}
