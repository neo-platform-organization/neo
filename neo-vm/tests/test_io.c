#define _POSIX_C_SOURCE 200809L
#include "neo_io_posix.h"
#include "neo_execution.h"
#include "neo_image.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); exit(EXIT_FAILURE); } } while (0)
#define OK(x) CHECK((x) == NEO_OK)

typedef struct neo_test_stream { size_t destroyed; uint8_t byte; bool block; } neo_test_stream;
static neo_status neo_short_write(void *context, const uint8_t *bytes, size_t count, neo_io_result *out) {
    neo_test_stream *stream = context;
    CHECK(count > 0);
    if (stream->block) { *out = (neo_io_result){NEO_IO_WOULD_BLOCK, 0}; }
    else { stream->byte = bytes[0]; *out = (neo_io_result){NEO_IO_TRANSFERRED, 1}; }
    return NEO_OK;
}
static void neo_test_destroy(void *context) { ++((neo_test_stream *)context)->destroyed; }

typedef struct neo_io_faults { size_t calls; size_t fail; size_t live; } neo_io_faults;
static void *neo_io_allocate(void *context, size_t size) {
    neo_io_faults *faults = context;
    if (faults->calls++ == faults->fail) { return NULL; }
    void *memory = malloc(size);
    if (memory != NULL) { ++faults->live; }
    return memory;
}
static void neo_io_release(void *context, void *memory) {
    neo_io_faults *faults = context;
    CHECK(faults->live > 0);
    --faults->live;
    free(memory);
}
static void neo_test_io_allocations(void) {
    bool completed = false;
    for (size_t i = 0; i < 16 && !completed; ++i) {
        neo_io_faults faults = {.fail = SIZE_MAX};
        neo_allocator allocator = {&faults, neo_io_allocate, neo_io_release};
        neo_vm *vm;
        OK(neo_vm_create(&allocator, &vm));
        const neo_capability *root, *stream;
        OK(neo_image_create(vm, "root", &root));
        size_t before = faults.live;
        faults.calls = 0; faults.fail = i;
        neo_test_stream fake = {0};
        const neo_stream_backend backend = {NULL, neo_short_write, neo_test_destroy};
        neo_status status = neo_stream_create(vm, root, "stream", &backend, &fake, &stream);
        if (status == NEO_OUT_OF_MEMORY) {
            CHECK(stream == NULL && faults.live == before && fake.destroyed == 0);
        } else { CHECK(status == NEO_OK); completed = true; }
        neo_vm_destroy(vm);
        CHECK(faults.live == 0 && fake.destroyed == (size_t)completed);
    }
    CHECK(completed);
}

int main(void) {
    neo_test_io_allocations();
    neo_vm *vm;
    OK(neo_vm_create(NULL, &vm));
    const neo_capability *root, *input, *output, *actor, *restricted, *copy;
    neo_diagnostic error;
    OK(neo_image_parse(vm,
        "(image (actor (handlers"
        " (get (body (stream-read (target \"input\"))))"
        " (put (body (stream-write (target \"output\") (value \"abc\") (offset 1))))"
        " (early (body (stream-write (target \"output\") (value (return (value 7))))))"
        " (bad (body (stream-write (target \"output\") (value 256))))"
        ")))", &root, &error));
    OK(neo_object_child(vm, root, "actor", &actor));
    int descriptors[2];
    CHECK(pipe(descriptors) == 0);
    int flags = fcntl(descriptors[0], F_GETFL);
    CHECK(fcntl(descriptors[0], F_SETFL, flags | O_NONBLOCK) == 0);
    OK(neo_stream_posix_create(vm, root, "input", descriptors[0], true, false, &input));
    CHECK(fcntl(descriptors[0], F_GETFL) == (flags | O_NONBLOCK));
    OK(neo_object_connect(vm, actor, "input", input, NEO_READ));
    neo_execution report;
    OK(neo_behavior_run(vm, actor, "get", NULL, 100, &report));
    CHECK(report.result.kind == NEO_TEXT && strcmp(report.result.text, "would-block") == 0);
    neo_execution_release(vm, &report);
    uint8_t bytes[] = {0, 255};
    CHECK(write(descriptors[1], bytes, 2) == 2);
    for (size_t i = 0; i < 2; ++i) {
        OK(neo_behavior_run(vm, actor, "get", NULL, 100, &report));
        CHECK(report.result.kind == NEO_INTEGER && report.result.integer == bytes[i]);
        neo_execution_release(vm, &report);
    }
    CHECK(close(descriptors[1]) == 0);
    OK(neo_behavior_run(vm, actor, "get", NULL, 100, &report));
    CHECK(report.result.kind == NEO_TEXT && strcmp(report.result.text, "eof") == 0);
    neo_execution_release(vm, &report);
    neo_io_result result;
    OK(neo_capability_restrict(vm, input, 0, &restricted));
    CHECK(neo_stream_read(vm, restricted, bytes, 1, &result) == NEO_DENIED);
    CHECK(neo_stream_write(vm, input, bytes, 1, &result) == NEO_UNSUPPORTED);
    neo_test_stream fake = {0};
    const neo_stream_backend backend = {NULL, neo_short_write, neo_test_destroy};
    OK(neo_stream_create(vm, root, "output", &backend, &fake, &output));
    CHECK(neo_stream_create(vm, root, "output", &backend, &fake, &copy) == NEO_CONFLICT);
    CHECK(fake.destroyed == 0);
    OK(neo_object_connect(vm, actor, "output", output, NEO_WRITE));
    OK(neo_behavior_run(vm, actor, "put", NULL, 100, &report));
    CHECK(report.result.kind == NEO_INTEGER && report.result.integer == 1 && fake.byte == 'b');
    neo_execution_release(vm, &report);
    OK(neo_behavior_run(vm, actor, "early", NULL, 100, &report));
    CHECK(report.result.integer == 7 && fake.byte == 'b');
    neo_execution_release(vm, &report);
    CHECK(neo_behavior_run(vm, actor, "bad", NULL, 100, &report) == NEO_LIMIT);
    neo_execution_release(vm, &report);
    fake.block = true;
    OK(neo_behavior_run(vm, actor, "put", NULL, 100, &report));
    CHECK(report.result.kind == NEO_TEXT && strcmp(report.result.text, "would-block") == 0);
    neo_execution_release(vm, &report);
    char *text;
    CHECK(neo_image_format(vm, root, &text) == NEO_UNSUPPORTED && text == NULL);
    CHECK(neo_image_duplicate(vm, root, "copy", &copy) == NEO_UNSUPPORTED);
    OK(neo_object_delete(vm, input));
    CHECK(neo_stream_read(vm, input, bytes, 1, &result) == NEO_UNAVAILABLE);
    CHECK(fcntl(descriptors[0], F_GETFL) == (flags | O_NONBLOCK));
    CHECK(close(descriptors[0]) == 0);
    /* Broken pipe must report an error without delivering fatal SIGPIPE. */
    CHECK(pipe(descriptors) == 0);
    OK(neo_stream_posix_create(vm, root, "broken", descriptors[1], false, true, &input));
    CHECK(close(descriptors[0]) == 0);
    CHECK(neo_stream_write(vm, input, bytes, 1, &result) == NEO_IO_ERROR);
    CHECK(close(descriptors[1]) == 0);
    neo_vm_destroy(vm);
    CHECK(fake.destroyed == 1);
    puts("PASS: byte streams, partial writes, would-block, EOF, rights, broken pipe, lifetime, neo primitives");
    return EXIT_SUCCESS;
}
