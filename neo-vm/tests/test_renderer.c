#include "neo_bootstrap.h"
#include "neo_display.h"
#include "neo_execution.h"
#include "neo_image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); exit(EXIT_FAILURE); } } while (0)
#define OK(x) CHECK((x) == NEO_OK)

static const neo_capability *neo_load_test(neo_vm *vm, const char *path) {
    FILE *file = fopen(path, "rb");
    CHECK(file != NULL);
    char source[65536];
    size_t length = fread(source, 1, sizeof(source) - 1, file);
    CHECK(!ferror(file) && fgetc(file) == EOF && fclose(file) == 0);
    source[length] = '\0';
    const neo_capability *root;
    neo_diagnostic error;
    OK(neo_image_parse(vm, source, &root, &error));
    return root;
}
static const neo_capability *neo_field(neo_vm *vm, const neo_capability *actor, const char *name) {
    const neo_capability *field;
    OK(neo_object_child(vm, actor, name, &field));
    return field;
}
static void neo_set(neo_vm *vm, const neo_capability *actor, const char *name, int64_t value) {
    OK(neo_object_write(vm, neo_field(vm, actor, name), (neo_value){.kind = NEO_INTEGER, .integer = value}));
}
static int64_t neo_get(neo_vm *vm, const neo_capability *actor, const char *name) {
    neo_value value;
    OK(neo_object_read(vm, neo_field(vm, actor, name), &value));
    CHECK(value.kind == NEO_INTEGER);
    return value.integer;
}
static int64_t neo_projected(neo_vm *vm, const neo_capability *actor, size_t index) {
    int64_t value;
    OK(neo_array_get(vm, neo_field(vm, actor, "projected"), index, &value));
    return value;
}
static double neo_frame_seconds;
static size_t neo_frame_count;
static size_t neo_run(neo_vm *vm, const neo_capability *actor, const char *handler) {
    neo_execution report;
    clock_t start = clock();
    neo_status status = neo_behavior_run(vm, actor, handler, NULL, 1000000, &report);
    clock_t end = clock();
    if (strcmp(handler, "frame") == 0 && start != (clock_t)-1 && end != (clock_t)-1) {
        neo_frame_seconds += (double)(end - start) / CLOCKS_PER_SEC;
        ++neo_frame_count;
    }
    if (status != NEO_OK) { fprintf(stderr, "%s: %s after %zu steps\n", handler, neo_status_name(status), report.steps); }
    CHECK(status == NEO_OK);
    size_t steps = report.steps;
    neo_execution_release(vm, &report);
    return steps;
}
static neo_status neo_capture(void *context, const uint8_t *pixels, size_t width, size_t height) {
    CHECK(width == 640 && height == 480);
    memcpy(context, pixels, width * height * 4);
    return NEO_OK;
}
static neo_status neo_poll_test(void *context, neo_window_state *out) {
    (void)context;
    *out = (neo_window_state){.width = 640, .height = 480};
    return NEO_OK;
}
static void neo_destroy_test(void *context) { (void)context; }
static void neo_save(const char *path, const uint8_t *pixels) {
    FILE *file = fopen(path, "wb");
    CHECK(file != NULL && fputs("P6\n640 480\n255\n", file) >= 0);
    for (size_t i = 0; i < 640u * 480u; ++i) { CHECK(fwrite(pixels + i * 4, 1, 3, file) == 3); }
    CHECK(fclose(file) == 0);
}

int main(void) {
    neo_vm *vm;
    OK(neo_vm_create(NULL, &vm));
    const neo_capability *root = neo_load_test(vm, "neo/cube.neo");
    const neo_capability *actor = neo_field(vm, root, "cube");
    const neo_capability *prototype = neo_load_test(vm, "neo/software-renderer.neo");
    OK(neo_actor_install(vm, actor, prototype));
    /* Copies remain executable after removing the template image. */
    OK(neo_image_unload(vm, prototype));
    const neo_capability *buffer, *window;
    OK(neo_buffer_create(vm, root, "pixels", 640, 480, &buffer));
    uint8_t *pixels = calloc(640u * 480u, 4), *previous = calloc(640u * 480u, 4);
    CHECK(pixels != NULL && previous != NULL);
    const neo_window_backend backend = {neo_capture, neo_poll_test, neo_destroy_test, NULL};
    OK(neo_window_create(vm, root, "window", &backend, pixels, &window));
    OK(neo_object_connect(vm, actor, "buffer", buffer, NEO_READ | NEO_WRITE));
    OK(neo_object_connect(vm, actor, "window", window, NEO_WRITE));
    (void)neo_run(vm, actor, "r-init");
    /* All octants, endpoints, reversed direction, and a degenerate line. */
    const int ends[][2] = {{26,22},{22,26},{18,26},{14,22},{14,18},{18,14},{22,14},{26,18},{20,20}};
    for (size_t i = 0; i < sizeof(ends) / sizeof(ends[0]); ++i) {
        (void)neo_run(vm, actor, "r-clear");
        neo_set(vm, actor, "r-x0", 20); neo_set(vm, actor, "r-y0", 20);
        neo_set(vm, actor, "r-x1", ends[i][0]); neo_set(vm, actor, "r-y1", ends[i][1]);
        (void)neo_run(vm, actor, "r-line");
        uint8_t pixel[4];
        OK(neo_buffer_read(vm, buffer, (20u * 640u + 20u) * 4, pixel, 4));
        CHECK(pixel[0] == 64 && pixel[1] == 200 && pixel[2] == 255 && pixel[3] == 255);
        OK(neo_buffer_read(vm, buffer, ((size_t)ends[i][1] * 640u + (size_t)ends[i][0]) * 4, pixel, 4));
        CHECK(pixel[3] == 255);
        OK(neo_buffer_read(vm, buffer, 0, pixels, 640u * 480u * 4));
        size_t covered = 0;
        for (size_t j = 0; j < 640u * 480u; ++j) { if (pixels[j * 4 + 3] != 0) { ++covered; } }
        CHECK(covered == (i == 8 ? 1u : 7u));
    }
    (void)neo_run(vm, actor, "r-clear");
    neo_set(vm, actor, "r-x0", -3); neo_set(vm, actor, "r-y0", 5);
    neo_set(vm, actor, "r-x1", 3); neo_set(vm, actor, "r-y1", 5);
    (void)neo_run(vm, actor, "r-line");
    OK(neo_buffer_read(vm, buffer, 0, pixels, 640u * 480u * 4));
    size_t count = 0;
    for (size_t i = 0; i < 640u * 480u; ++i) { if (pixels[i * 4 + 3] != 0) { ++count; } }
    CHECK(count == 4);
    /* Cardinal trig values and wraparound do not drift between frames. */
    const int angles[] = {0, 90, 180, 270, 360, -90};
    const int values[] = {0, 10000, 0, -10000, 0, -10000};
    for (size_t i = 0; i < 6; ++i) {
        neo_set(vm, actor, "c-angle", angles[i]);
        (void)neo_run(vm, actor, "c-sine");
        CHECK(neo_get(vm, actor, "c-trig") == values[i]);
    }
    neo_set(vm, actor, "c-yaw", 0); neo_set(vm, actor, "c-pitch", 0);
    size_t steps = neo_run(vm, actor, "frame");
    CHECK(neo_projected(vm, actor, 0) == 245 && neo_projected(vm, actor, 1) == 315);
    CHECK(neo_projected(vm, actor, 8) == 270 && neo_projected(vm, actor, 9) == 290);
    CHECK(neo_get(vm, actor, "c-yaw") == 3 && neo_get(vm, actor, "c-pitch") == 2);
    memcpy(previous, pixels, 640u * 480u * 4);
    (void)neo_run(vm, actor, "frame");
    CHECK(memcmp(previous, pixels, 640u * 480u * 4) != 0);
    neo_set(vm, actor, "c-yaw", 0); neo_set(vm, actor, "c-pitch", 0);
    (void)neo_run(vm, actor, "frame");
    CHECK(memcmp(previous, pixels, 640u * 480u * 4) == 0); /* No stale trails. */
    const int poses[][2] = {{25,20},{55,40},{95,70},{175,110}};
    for (size_t pose = 0; pose < 4; ++pose) {
        neo_set(vm, actor, "c-yaw", poses[pose][0]); neo_set(vm, actor, "c-pitch", poses[pose][1]);
        size_t frame_steps = neo_run(vm, actor, "frame");
        if (frame_steps > steps) { steps = frame_steps; }
        for (size_t i = 0; i < 8; ++i) {
            CHECK(neo_projected(vm, actor, 2 * i) >= 0 && neo_projected(vm, actor, 2 * i) < 640);
            CHECK(neo_projected(vm, actor, 2 * i + 1) >= 0 && neo_projected(vm, actor, 2 * i + 1) < 480);
        }
        char path[80];
        (void)snprintf(path, sizeof(path), "build/cube-%zu.ppm", pose);
        neo_save(path, pixels);
    }
    neo_vm_destroy(vm);
    free(pixels); free(previous);
    printf("PASS: software line renderer, clipping, fixed-point rotation, perspective, clear/redraw; max %zu steps/frame\n", steps);
    if (neo_frame_count != 0) {
        printf("Headless frame CPU time: %.2f ms average over %zu frames (no pacing or X11)\n",
               1000 * neo_frame_seconds / (double)neo_frame_count, neo_frame_count);
    }
    return EXIT_SUCCESS;
}
