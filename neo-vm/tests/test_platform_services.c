#include "neo_platform.h"
#include "neo_io.h"
#include "neo_display.h"
#include "neo_execution.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(EXIT_FAILURE); } } while (0)
#define OK(x) CHECK((x) == NEO_OK)

typedef struct neo_fake {
    unsigned reads, streams, windows, waits, destroyed, presented;
    uint32_t milliseconds;
    neo_standard_stream selected;
    neo_status failure;
    const char *source;
    size_t size;
    bool excessive;
} neo_fake;

static neo_status neo_read(void *context, const char *location, char *bytes,
                            size_t capacity, size_t *out_count) {
    neo_fake *fake = context;
    CHECK(strcmp(location, "memory:image") == 0);
    ++fake->reads;
    *out_count = 0;
    if (fake->failure != NEO_OK) { return fake->failure; }
    if (fake->excessive) { *out_count = capacity + 1; return NEO_OK; }
    CHECK(fake->size <= capacity);
    memcpy(bytes, fake->source, fake->size);
    *out_count = fake->size;
    return NEO_OK;
}
static neo_status neo_write(void *context, const uint8_t *bytes, size_t count, neo_io_result *out) {
    (void)context;
    CHECK(count == 2 && bytes[0] == 'o' && bytes[1] == 'k');
    *out = (neo_io_result){NEO_IO_TRANSFERRED, 1}; /* Preserve partial writes. */
    return NEO_OK;
}
static void neo_destroy(void *context) { ++((neo_fake *)context)->destroyed; }
static neo_status neo_open_stream(void *context, neo_vm *vm, const neo_capability *parent,
                                   const char *name, neo_standard_stream stream,
                                   const neo_capability **out) {
    neo_fake *fake = context;
    ++fake->streams; fake->selected = stream;
    if (fake->failure != NEO_OK) { return fake->failure; }
    neo_stream_backend backend = {.write = neo_write, .destroy = neo_destroy};
    return neo_stream_create(vm, parent, name, &backend, context, out);
}
static neo_status neo_present(void *context, const uint8_t *pixels, size_t width, size_t height) {
    CHECK(width == 2 && height == 2 && pixels[0] == 42);
    ++((neo_fake *)context)->presented;
    return NEO_OK;
}
static neo_status neo_poll(void *context, neo_window_state *out) {
    (void)context;
    *out = (neo_window_state){.width = 2, .height = 2};
    return NEO_OK;
}
static neo_status neo_next(void *context, neo_input_event *out) {
    (void)context;
    *out = (neo_input_event){.kind = NEO_INPUT_POINTER, .x = 1, .y = 1};
    return NEO_OK;
}
static neo_status neo_open_window(void *context, neo_vm *vm, const neo_capability *parent,
                                   const char *name, const char *title, size_t width, size_t height,
                                   const neo_capability **out) {
    neo_fake *fake = context;
    CHECK(strcmp(title, "test") == 0 && width == 2 && height == 2);
    ++fake->windows;
    if (fake->failure != NEO_OK) { return fake->failure; }
    neo_window_backend backend = {.present = neo_present, .poll = neo_poll,
        .next_event = neo_next, .destroy = neo_destroy};
    return neo_window_create(vm, parent, name, &backend, context, out);
}
static neo_status neo_wait(void *context, uint32_t milliseconds) {
    neo_fake *fake = context;
    ++fake->waits; fake->milliseconds = milliseconds;
    return fake->failure;
}

typedef struct neo_heap { size_t calls, fail, live; } neo_heap;
static void *neo_allocate(void *context, size_t size) {
    neo_heap *heap = context;
    if (heap->calls++ == heap->fail) { return NULL; }
    void *memory = malloc(size);
    if (memory != NULL) { ++heap->live; }
    return memory;
}
static void neo_release(void *context, void *memory) {
    --((neo_heap *)context)->live;
    free(memory);
}

int main(void) {
    neo_vm *vm;
    const neo_capability *root = NULL, *stream = NULL, *window = NULL, *restricted, *buffer;
    neo_diagnostic error;
    neo_fake fake = {.source = "(image)", .size = 7};
    neo_platform_services services = {
        .info = {.environment = NEO_ENVIRONMENT_HOSTED, .backend = "fake",
            .architecture = "test", .host_os = "test", .services = NEO_PLATFORM_SERVICES_ALL},
        .backend = {.read_source = neo_read, .open_stream = neo_open_stream,
            .open_window = neo_open_window, .wait = neo_wait}, .context = &fake
    };
    OK(neo_vm_create(NULL, &vm));
    CHECK(neo_platform_load_image(vm, "memory:image", &root, &error) == NEO_UNAVAILABLE);
    CHECK(root == NULL && error.message[0] == '\0' && fake.reads == 0);
    CHECK(neo_platform_wait(vm, 0) == NEO_UNAVAILABLE);
    neo_platform_services broken = services;
    broken.backend.wait = NULL;
    CHECK(neo_vm_install_platform(vm, &broken) == NEO_INVALID);
    OK(neo_vm_install_platform(vm, &services));
    CHECK(neo_vm_install_platform(vm, &services) == NEO_BUSY);
    services.backend.read_source = NULL; /* Stored table is an independent copy. */
    fake.failure = NEO_IO_ERROR;
    CHECK(neo_platform_load_image(vm, "memory:image", &root, &error) == NEO_IO_ERROR && root == NULL);
    fake.failure = NEO_OK; fake.excessive = true;
    CHECK(neo_platform_load_image(vm, "memory:image", &root, &error) == NEO_LIMIT && root == NULL);
    fake.excessive = false; fake.source = "(x)\0suffix"; fake.size = 10;
    CHECK(neo_platform_load_image(vm, "memory:image", &root, &error) == NEO_PARSE_ERROR && root == NULL);
    fake.source = "(x"; fake.size = 2;
    CHECK(neo_platform_load_image(vm, "memory:image", &root, &error) == NEO_PARSE_ERROR);
    CHECK(error.line == 1 && error.message[0] != '\0');
    fake.source = "(image)"; fake.size = 7;
    OK(neo_platform_load_image(vm, "memory:image", &root, &error));
    CHECK(error.message[0] == '\0');
    OK(neo_capability_restrict(vm, root, NEO_READ, &restricted));
    CHECK(neo_platform_open_stream(vm, restricted, "s", NEO_STANDARD_OUTPUT, &stream) == NEO_DENIED);
    CHECK(neo_platform_open_window(vm, restricted, "w", "test", 2, 2, &window) == NEO_DENIED);
    CHECK(fake.streams == 0 && fake.windows == 0 && stream == NULL && window == NULL);
    CHECK(neo_platform_open_stream(vm, root, "s", (neo_standard_stream)99, &stream) == NEO_INVALID);
    CHECK(neo_platform_open_window(vm, root, "w", "test", 0, 2, &window) == NEO_INVALID);
    fake.failure = NEO_IO_ERROR;
    CHECK(neo_platform_open_stream(vm, root, "s", NEO_STANDARD_OUTPUT, &stream) == NEO_IO_ERROR && stream == NULL);
    CHECK(neo_platform_open_window(vm, root, "w", "test", 2, 2, &window) == NEO_IO_ERROR && window == NULL);
    CHECK(neo_platform_wait(vm, 16) == NEO_IO_ERROR);
    fake.failure = NEO_OK;
    OK(neo_platform_open_stream(vm, root, "s", NEO_STANDARD_ERROR, &stream));
    CHECK(fake.selected == NEO_STANDARD_ERROR);
    neo_io_result result;
    OK(neo_stream_write(vm, stream, (const uint8_t *)"ok", 2, &result));
    CHECK(result.state == NEO_IO_TRANSFERRED && result.count == 1);
    OK(neo_capability_restrict(vm, stream, NEO_READ, &restricted));
    CHECK(neo_stream_write(vm, restricted, (const uint8_t *)"ok", 2, &result) == NEO_DENIED);
    OK(neo_platform_open_window(vm, root, "w", "test", 2, 2, &window));
    OK(neo_buffer_create(vm, root, "buffer", 2, 2, &buffer));
    OK(neo_buffer_fill(vm, buffer, 42));
    OK(neo_window_present(vm, window, buffer)); CHECK(fake.presented == 1);
    neo_input_event event;
    OK(neo_window_next_event(vm, window, &event));
    CHECK(event.kind == NEO_INPUT_POINTER && event.x == 1);
    OK(neo_platform_wait(vm, 16)); CHECK(fake.milliseconds == 16 && fake.waits == 2);
    OK(neo_image_unload(vm, root)); CHECK(fake.destroyed == 2);
    CHECK(neo_platform_open_stream(vm, root, "gone", NEO_STANDARD_OUTPUT, &stream) == NEO_UNAVAILABLE);
    neo_vm_destroy(vm); CHECK(fake.destroyed == 2);

    OK(neo_vm_create(NULL, &vm));
    /* Declaration-only metadata cannot accidentally dispatch a service. */
    OK(neo_vm_configure_platform(vm, &services.info));
    CHECK(neo_platform_load_image(vm, "memory:image", &root, NULL) == NEO_UNSUPPORTED);
    CHECK(neo_platform_open_stream(vm, NULL, "s", NEO_STANDARD_INPUT, &stream) == NEO_UNSUPPORTED);
    CHECK(neo_platform_open_window(vm, NULL, "w", "test", 2, 2, &window) == NEO_UNSUPPORTED);
    CHECK(neo_platform_wait(vm, 0) == NEO_UNSUPPORTED);
    neo_vm_destroy(vm);
    services.backend.read_source = neo_read;
    bool completed = false;
    for (size_t fail = 0; fail < 64; ++fail) {
        neo_heap heap = {.fail = SIZE_MAX};
        neo_allocator allocator = {&heap, neo_allocate, neo_release};
        OK(neo_vm_create(&allocator, &vm));
        OK(neo_vm_install_platform(vm, &services));
        heap.calls = 0; heap.fail = fail;
        unsigned reads = fake.reads;
        neo_status status = neo_platform_load_image(vm, "memory:image", &root, &error);
        CHECK(status == NEO_OK || status == NEO_OUT_OF_MEMORY);
        if (status != NEO_OK) { CHECK(root == NULL); }
        if (fail == 0) { CHECK(fake.reads == reads); }
        neo_vm_destroy(vm); CHECK(heap.live == 0);
        if (status == NEO_OK) { completed = true; break; }
    }
    CHECK(completed);
    puts("PASS: platform service dispatch, inert loading, resource rights, errors, ownership, fake presentation");
    return EXIT_SUCCESS;
}
