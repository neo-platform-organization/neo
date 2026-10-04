#include "cli/cli.h"
#include "execution/execution.h"
#include "platform/platform.h"
#include "display/display.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static neo_status neo_grant_terminal(neo_vm *vm, const neo_capability *root,
                                     const neo_capability *actor) {
    const neo_standard_stream streams[] = {NEO_STANDARD_INPUT, NEO_STANDARD_OUTPUT, NEO_STANDARD_ERROR};
    const char *names[] = {"stdin", "stdout", "stderr"};
    const char *objects[] = {"host-stdin", "host-stdout", "host-stderr"};
    neo_status status = NEO_OK;
    for (size_t i = 0; status == NEO_OK && i < 3; ++i) {
        const neo_capability *stream = NULL;
        status = neo_platform_open_stream(vm, root, objects[i], streams[i], &stream);
        if (status == NEO_OK) {
            status = neo_object_connect(vm, actor, names[i], stream, i == 0 ? NEO_READ : NEO_WRITE);
        }
    }
    return status;
}

/* Both modes share this VM, image, receiver, and resource lifetime. */
int neo_cli_launch(int argc, char **argv) {
    bool terminal = false, gui = false;
    int at = 1;
    while (at < argc && argv[at][0] == '-') {
        if (strcmp(argv[at], "--cli") == 0) { terminal = true; }
        else if (strcmp(argv[at], "--gui") == 0) { gui = true; }
        else { fprintf(stderr, "unknown option: %s\n", argv[at]); return EXIT_FAILURE; }
        ++at;
    }
    if (!terminal && !gui) { terminal = true; }
    if (argc - at < 2) {
        fputs("usage: neo [--cli] [--gui] IMAGE ACTOR [HANDLER]\n", stderr);
        return EXIT_FAILURE;
    }
    const char *path = argv[at++];
    const char *actor_name = argv[at++];
    const char *handler = gui ? "frame" : "main";
    if (at < argc && argv[at][0] != '-') { handler = argv[at++]; }
    if (at != argc) {
        fputs("unexpected arguments after handler\n", stderr); return EXIT_FAILURE;
    }
    unsigned flags = NEO_PLATFORM_HOST_FILES;
    if (terminal) { flags |= NEO_PLATFORM_BYTE_STREAMS; }
    if (gui) { flags |= NEO_PLATFORM_PIXEL_WINDOWS | NEO_PLATFORM_INPUT_EVENTS | NEO_PLATFORM_WAIT; }
    neo_vm *vm = NULL;
    neo_status status = neo_vm_create(NULL, &vm);
    neo_platform_services platform;
    if (status == NEO_OK) { status = neo_platform_native_services(flags, &platform); }
    if (status == NEO_OK) { status = neo_vm_install_platform(vm, &platform); }
    const neo_capability *root = NULL, *actor = NULL, *buffer = NULL, *window = NULL;
    neo_diagnostic error = {0};
    if (status == NEO_OK) { status = neo_platform_load_image(vm, path, &root, &error); }
    if (status != NEO_OK && error.message[0] != '\0') {
        fprintf(stderr, "%s:%zu:%zu: %s\n", path, error.line, error.column, error.message);
    }
    if (status == NEO_OK) {
        actor = root;
        if (strcmp(actor_name, ".") != 0) { status = neo_object_child(vm, root, actor_name, &actor); }
    }
    if (status == NEO_OK && terminal) { status = neo_grant_terminal(vm, root, actor); }
    if (status == NEO_OK && gui) { status = neo_buffer_create(vm, root, "host-buffer", 640, 480, &buffer); }
    if (status == NEO_OK && gui) {
        status = neo_platform_open_window(vm, root, "host-window", "neo", 640, 480, &window);
    }
    if (status == NEO_OK && gui) { status = neo_object_connect(vm, actor, "buffer", buffer, NEO_READ | NEO_WRITE); }
    if (status == NEO_OK && gui) { status = neo_object_connect(vm, actor, "window", window, NEO_READ | NEO_WRITE); }
    while (status == NEO_OK) {
        if (gui) {
            neo_window_state state;
            status = neo_window_poll(vm, window, &state);
            if (status != NEO_OK || state.closed) { break; }
        }
        neo_execution execution = {0};
        status = neo_behavior_run(vm, actor, handler, NULL, 1000000, &execution);
        neo_execution_release(vm, &execution);
        if (!gui) { break; }
        /* Temporary host pacing, not language scheduling policy. */
        if (status == NEO_OK) { status = neo_platform_wait(vm, 16); }
    }
    if (status != NEO_OK) { fprintf(stderr, "neo: %s\n", neo_status_name(status)); }
    neo_vm_destroy(vm);
    return status == NEO_OK ? EXIT_SUCCESS : EXIT_FAILURE;
}
