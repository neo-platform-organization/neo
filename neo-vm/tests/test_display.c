#include "neo_display.h"
#include "neo_execution.h"
#include "neo_image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(EXIT_FAILURE); } } while (0)
#define OK(x) CHECK((x) == NEO_OK)

typedef struct neo_fake_window {
    unsigned destroyed;
    unsigned presented;
    bool closed;
    neo_status failure;
} neo_fake_window;

static neo_status neo_fake_present(void *context, const uint8_t *pixels, size_t width, size_t height) {
    neo_fake_window *fake = context;
    CHECK(width == 2 && height == 1 && pixels[0] == 255 && pixels[7] == 0);
    ++fake->presented;
    return fake->failure;
}

static neo_status neo_fake_poll(void *context, neo_window_state *out) {
    neo_fake_window *fake = context;
    *out = (neo_window_state){.width = 80, .height = 60, .closed = fake->closed, .redraw = true};
    return fake->failure;
}

static void neo_fake_destroy(void *context) {
    neo_fake_window *fake = context;
    ++fake->destroyed;
}

static const neo_window_backend neo_fake_backend = {neo_fake_present, neo_fake_poll, neo_fake_destroy, NULL};

static void neo_test_display(void) {
    neo_vm *vm;
    OK(neo_vm_create(NULL, &vm));
    const neo_capability *root, *actor, *buffer, *window, *read_only, *copy;
    neo_diagnostic diagnostic;
    OK(neo_image_parse(vm,
        "(image (actor (handlers"
        " (frame (body (do"
        "  (store (buffer-write (target \"buffer\") (index 0) (value 255)))"
        "  (show (window-present (target \"window\") (buffer \"buffer\"))))))"
        " (fetch (body (buffer-read (target \"buffer\") (index 0))))"
        " (size (body (buffer-size (target \"buffer\"))))"
        " (poll (body (window-poll (target \"window\"))))"
        " (width (body (window-width (target \"window\"))))"
        " (height (body (window-height (target \"window\"))))"
        " (bad (body (buffer-write (target \"buffer\") (index 8) (value 1))))"
        " (negative (body (buffer-write (target \"buffer\") (index -1) (value 1))))"
        " (byte (body (buffer-write (target \"buffer\") (index 0) (value 256))))"
        " (kind (body (buffer-write (target \"buffer\") (index 0) (value true))))"
        " (early (body (buffer-write (target \"buffer\") (index (return (value 9))) (value 0))))"
        ")))", &root, &diagnostic));
    OK(neo_object_child(vm, root, "actor", &actor));
    CHECK(neo_buffer_create(vm, root, "invalid", SIZE_MAX, 2, &buffer) == NEO_LIMIT);
    CHECK(buffer == NULL);
    OK(neo_buffer_create(vm, root, "pixels", 2, 1, &buffer));
    neo_fake_window fake = {0};
    OK(neo_window_create(vm, root, "screen", &neo_fake_backend, &fake, &window));
    CHECK(neo_window_create(vm, root, "screen", &neo_fake_backend, &fake, &copy) == NEO_CONFLICT);
    CHECK(copy == NULL && fake.destroyed == 0);
    uint8_t bytes[8];
    memset(bytes, 42, sizeof(bytes));
    OK(neo_buffer_read(vm, buffer, 0, bytes, sizeof(bytes)));
    for (size_t i = 0; i < sizeof(bytes); ++i) { CHECK(bytes[i] == 0); }
    CHECK(neo_buffer_read(vm, buffer, SIZE_MAX, bytes, 1) == NEO_LIMIT);
    CHECK(neo_buffer_write(vm, buffer, 7, bytes, SIZE_MAX) == NEO_LIMIT);
    OK(neo_buffer_write(vm, buffer, 8, NULL, 0));
    OK(neo_capability_restrict(vm, buffer, NEO_READ, &read_only));
    CHECK(neo_buffer_write(vm, read_only, 0, bytes, 1) == NEO_DENIED);
    CHECK(neo_buffer_fill(vm, read_only, 42) == NEO_DENIED);
    OK(neo_buffer_fill(vm, buffer, 42));
    OK(neo_buffer_read(vm, buffer, 0, bytes, sizeof(bytes)));
    for (size_t i = 0; i < sizeof(bytes); ++i) { CHECK(bytes[i] == 42); }
    OK(neo_buffer_fill(vm, buffer, 0));
    CHECK(neo_buffer_read(vm, window, 0, bytes, 1) == NEO_WRONG_KIND);
    neo_execution report;
    CHECK(neo_behavior_run(vm, actor, "frame", NULL, 100, &report) == NEO_UNAVAILABLE);
    neo_execution_release(vm, &report);
    OK(neo_object_connect(vm, actor, "buffer", buffer, NEO_READ | NEO_WRITE));
    OK(neo_object_connect(vm, actor, "window", window, NEO_READ | NEO_WRITE));
    OK(neo_behavior_run(vm, actor, "frame", NULL, 100, &report));
    neo_execution_release(vm, &report);
    CHECK(fake.presented == 1);
    const char *selectors[] = {"fetch", "size", "width", "height", "early"};
    const int64_t values[] = {255, 8, 80, 60, 9};
    OK(neo_behavior_run(vm, actor, "poll", NULL, 100, &report));
    CHECK(report.result.kind == NEO_BOOLEAN && !report.result.boolean);
    neo_execution_release(vm, &report);
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        OK(neo_behavior_run(vm, actor, selectors[i], NULL, 100, &report));
        CHECK(report.result.kind == NEO_INTEGER && report.result.integer == values[i]);
        neo_execution_release(vm, &report);
    }
    const char *bad[] = {"bad", "negative", "byte", "kind"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        CHECK(neo_behavior_run(vm, actor, bad[i], NULL, 100, &report) ==
              (i == 3 ? NEO_WRONG_KIND : NEO_LIMIT));
        neo_execution_release(vm, &report);
    }
    bytes[0] = 0;
    OK(neo_buffer_read(vm, buffer, 0, bytes, 1));
    CHECK(bytes[0] == 255); /* Rejected stores and nested return did not write. */
    char *text = NULL;
    CHECK(neo_image_format(vm, root, &text) == NEO_UNSUPPORTED && text == NULL);
    CHECK(neo_image_duplicate(vm, root, "copy", &copy) == NEO_UNSUPPORTED && copy == NULL);
    CHECK(neo_object_move(vm, buffer, actor, "moved", &copy) == NEO_UNSUPPORTED);
    neo_window_state state;
    fake.failure = NEO_IO_ERROR;
    CHECK(neo_window_present(vm, window, buffer) == NEO_IO_ERROR);
    CHECK(neo_window_poll(vm, window, &state) == NEO_IO_ERROR);
    fake.failure = NEO_OK;
    fake.closed = true;
    OK(neo_window_poll(vm, window, &state));
    CHECK(state.closed);
    CHECK(neo_window_present(vm, window, buffer) == NEO_UNAVAILABLE);
    OK(neo_object_delete(vm, buffer));
    CHECK(neo_buffer_read(vm, read_only, 0, bytes, 1) == NEO_UNAVAILABLE);
    OK(neo_image_unload(vm, root));
    CHECK(fake.destroyed == 1);
    CHECK(neo_window_poll(vm, window, &state) == NEO_UNAVAILABLE);
    neo_vm_destroy(vm);
    CHECK(fake.destroyed == 1);
}

typedef struct neo_faults { size_t live; size_t call; size_t fail; } neo_faults;
static void *neo_fault_allocate(void *context, size_t size) {
    neo_faults *faults = context;
    if (faults->call++ == faults->fail) { return NULL; }
    void *memory = malloc(size);
    if (memory != NULL) { ++faults->live; }
    return memory;
}
static void neo_fault_release(void *context, void *memory) {
    neo_faults *faults = context;
    CHECK(faults->live > 0);
    --faults->live;
    free(memory);
}
static void neo_test_allocations(void) {
    for (unsigned kind = 0; kind < 2; ++kind) {
        bool completed = false;
        for (size_t failure = 0; failure < 20 && !completed; ++failure) {
            neo_faults faults = {.fail = SIZE_MAX};
            neo_allocator allocator = {&faults, neo_fault_allocate, neo_fault_release};
            neo_vm *vm;
            OK(neo_vm_create(&allocator, &vm));
            const neo_capability *root, *resource = NULL;
            OK(neo_image_create(vm, "root", &root));
            size_t before = faults.live;
            faults.call = 0;
            faults.fail = failure;
            neo_fake_window fake = {0};
            neo_status status = kind == 0 ? neo_buffer_create(vm, root, "resource", 2, 1, &resource) :
                neo_window_create(vm, root, "resource", &neo_fake_backend, &fake, &resource);
            if (status == NEO_OUT_OF_MEMORY) {
                CHECK(resource == NULL && faults.live == before && fake.destroyed == 0);
            } else { CHECK(status == NEO_OK); completed = true; }
            neo_vm_destroy(vm);
            CHECK(faults.live == 0);
            CHECK(fake.destroyed == (unsigned)(kind == 1 && completed));
        }
        CHECK(completed);
    }
}

static neo_status neo_test_next_event(void *context, neo_input_event *out) {
    neo_fake_window *fake = context;
    if (fake->failure != NEO_OK) { return fake->failure; }
    *out = (neo_input_event){.kind = NEO_INPUT_KEY, .pressed = true, .key = "enter"};
    return NEO_OK;
}

static void neo_test_events(void) {
    neo_vm *vm;
    OK(neo_vm_create(NULL, &vm));
    const neo_capability *root, *actor, *window, *restricted, *buffer;
    neo_diagnostic error;
    OK(neo_image_parse(vm,
        "(image (actor (handlers"
        " (next (body (window-next-event (target \"window\"))))"
        " (key (body (window-event (target \"window\") (field \"key\"))))"
        " (pressed (body (window-event (target \"window\") (field \"pressed\"))))"
        " (width (body (buffer-width (target \"buffer\"))))"
        " (height (body (buffer-height (target \"buffer\"))))"
        ")))", &root, &error));
    OK(neo_object_child(vm, root, "actor", &actor));
    neo_fake_window fake = {0};
    neo_window_backend backend = neo_fake_backend;
    backend.next_event = neo_test_next_event;
    OK(neo_window_create(vm, root, "window", &backend, &fake, &window));
    OK(neo_buffer_create(vm, root, "buffer", 3, 2, &buffer));
    OK(neo_object_connect(vm, actor, "window", window, NEO_READ | NEO_WRITE));
    OK(neo_object_connect(vm, actor, "buffer", buffer, NEO_READ));
    OK(neo_capability_restrict(vm, window, NEO_READ, &restricted));
    neo_input_event event;
    CHECK(neo_window_next_event(vm, restricted, &event) == NEO_DENIED);
    neo_execution report;
    OK(neo_behavior_run(vm, actor, "next", NULL, 100, &report));
    CHECK(report.result.kind == NEO_TEXT && strcmp(report.result.text, "key") == 0);
    neo_execution_release(vm, &report);
    OK(neo_behavior_run(vm, actor, "key", NULL, 100, &report));
    CHECK(report.result.kind == NEO_TEXT && strcmp(report.result.text, "enter") == 0);
    neo_execution_release(vm, &report);
    OK(neo_behavior_run(vm, actor, "pressed", NULL, 100, &report));
    CHECK(report.result.kind == NEO_BOOLEAN && report.result.boolean);
    neo_execution_release(vm, &report);
    OK(neo_behavior_run(vm, actor, "width", NULL, 100, &report));
    CHECK(report.result.kind == NEO_INTEGER && report.result.integer == 3);
    neo_execution_release(vm, &report);
    OK(neo_behavior_run(vm, actor, "height", NULL, 100, &report));
    CHECK(report.result.kind == NEO_INTEGER && report.result.integer == 2);
    neo_execution_release(vm, &report);
    fake.failure = NEO_IO_ERROR;
    CHECK(neo_window_next_event(vm, window, &event) == NEO_IO_ERROR);
    OK(neo_window_get_event(vm, restricted, &event));
    CHECK(event.kind == NEO_INPUT_KEY && strcmp(event.key, "enter") == 0);
    neo_vm_destroy(vm);
    CHECK(fake.destroyed == 1);
}

int main(void) {
    neo_test_events();
    neo_test_display();
    neo_test_allocations();
    puts("PASS: buffer bounds, display authority, neo primitives, lifecycle, backend failures, allocation rollback");
    return EXIT_SUCCESS;
}
