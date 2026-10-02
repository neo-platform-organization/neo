#include "neo_display.h"
#include "neo_execution.h"
#include "neo_image.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(EXIT_FAILURE); } } while (0)
#define OK(x) CHECK((x) == NEO_OK)

static neo_status neo_triangle_present(void *context, const uint8_t *pixels, size_t width, size_t height) {
    unsigned *frames = context;
    CHECK(pixels != NULL && width == 640 && height == 480);
    ++*frames;
    return NEO_OK;
}
static neo_status neo_triangle_poll(void *context, neo_window_state *out) {
    (void)context;
    *out = (neo_window_state){.width = 640, .height = 480};
    return NEO_OK;
}
static void neo_triangle_destroy(void *context) { (void)context; }

int main(void) {
    FILE *file = fopen("neo/triangle.neo", "rb");
    CHECK(file != NULL);
    char source[16384];
    size_t length = fread(source, 1, sizeof(source) - 1, file);
    CHECK(!ferror(file) && fgetc(file) == EOF);
    CHECK(fclose(file) == 0);
    source[length] = '\0';
    neo_vm *vm;
    OK(neo_vm_create(NULL, &vm));
    const neo_capability *root, *actor, *buffer, *window;
    neo_diagnostic diagnostic;
    OK(neo_image_parse(vm, source, &root, &diagnostic));
    OK(neo_object_child(vm, root, "triangle", &actor));
    OK(neo_buffer_create(vm, root, "pixels", 640, 480, &buffer));
    unsigned frames = 0;
    const neo_window_backend backend = {neo_triangle_present, neo_triangle_poll, neo_triangle_destroy};
    OK(neo_window_create(vm, root, "screen", &backend, &frames, &window));
    OK(neo_object_connect(vm, actor, "buffer", buffer, NEO_READ | NEO_WRITE));
    OK(neo_object_connect(vm, actor, "window", window, NEO_READ | NEO_WRITE));
    size_t max_steps = 0;
    /* 321 rows in batches of eight, then one presentation-only frame. */
    for (unsigned frame = 0; frame < 42; ++frame) {
        neo_execution report;
        OK(neo_behavior_run(vm, actor, "frame", NULL, 1000000, &report));
        if (report.steps > max_steps) { max_steps = report.steps; }
        if (frame == 41) { CHECK(report.steps < 100); }
        neo_execution_release(vm, &report);
    }
    CHECK(frames == 42);
    uint8_t *pixels = malloc(640u * 480u * 4u);
    CHECK(pixels != NULL);
    OK(neo_buffer_read(vm, buffer, 0, pixels, 640u * 480u * 4u));
    size_t covered = 0;
    for (int y = 0; y < 480; ++y) {
        for (int x = 0; x < 640; ++x) {
            /* Independent half-plane test, including the boundary. */
            bool inside = y <= 400 && y - 80 >= 2 * abs(x - 320);
            size_t offset = ((size_t)y * 640u + (size_t)x) * 4u;
            CHECK(pixels[offset] == (inside ? 64 : 0));
            CHECK(pixels[offset + 1] == (inside ? 200 : 0));
            CHECK(pixels[offset + 2] == (inside ? 255 : 0));
            CHECK(pixels[offset + 3] == (inside ? 255 : 0));
            if (inside) { ++covered; }
        }
    }
    /* Optional viewable artifact generated from actual VM output. */
    file = fopen("build/triangle.ppm", "wb");
    CHECK(file != NULL);
    CHECK(fputs("P6\n640 480\n255\n", file) >= 0);
    for (size_t i = 0; i < 640u * 480u; ++i) { CHECK(fwrite(pixels + i * 4u, 1, 3, file) == 3); }
    CHECK(fclose(file) == 0);
    free(pixels);
    neo_vm_destroy(vm);
    printf("PASS: neo triangle, %zu covered pixels; max %zu steps/frame; stable completed frame\n", covered, max_steps);
    return EXIT_SUCCESS;
}
