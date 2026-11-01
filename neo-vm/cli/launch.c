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

static neo_status neo_launch_entry(neo_vm *vm, const neo_capability *root,
                                    const char *explicit_name, const neo_capability **out) {
    const char *name = explicit_name;
    if (name == NULL) {
        const neo_capability *entry = NULL;
        neo_status status = neo_object_child(vm, root, "entry", &entry);
        if (status != NEO_OK && status != NEO_UNAVAILABLE) { return status; }
        if (status == NEO_OK) {
            neo_value value = {0};
            status = neo_object_read(vm, entry, &value);
            if (status != NEO_OK) { return status; }
            if (value.kind != NEO_TEXT || value.text[0] == '\0') { return NEO_WRONG_KIND; }
            name = value.text;
        }
    }
    *out = root;
    return name == NULL || strcmp(name, ".") == 0 ? NEO_OK : neo_object_child(vm, root, name, out);
}

bool neo_launch_selftest(neo_vm *vm) {
    const char *sources[] = {
        "(image (handlers (main (body 42))))",
        "(image (entry \"app\") (app (handlers (main (body 42)))))",
        "(image (entry 9) (app (handlers (main (body 42)))))",
        "(image (entry \"missing\"))",
        "(image (entry 9) (app (handlers (main (body 42)))))"
    };
    const neo_status expected[] = {NEO_OK, NEO_OK, NEO_WRONG_KIND, NEO_UNAVAILABLE, NEO_OK};
    bool okay = true;
    for (size_t i = 0; okay && i < 5; ++i) {
        const neo_capability *root = NULL, *actor = NULL;
        neo_diagnostic error = {0};
        neo_status status = neo_image_parse(vm, sources[i], &root, &error);
        if (status == NEO_OK) { status = neo_launch_entry(vm, root, i == 4 ? "app" : NULL, &actor); }
        okay = status == expected[i];
        neo_execution report = {0};
        if (okay && status == NEO_OK) { okay = neo_behavior_run(vm, actor, "main", NULL, 100, &report) == NEO_OK && report.result.kind == NEO_INTEGER && report.result.integer == 42; }
        neo_execution_release(vm, &report);
        if (root != NULL) { (void)neo_image_unload(vm, root); }
    }
    if (!okay) { fputs("FAIL image entry selection\n", stderr); }
    return okay;
}

/* Both modes share this VM, image, receiver, and resource lifetime. */
int neo_cli_launch(int argc, char **argv) {
    bool terminal = false, gui = false, listening = false;
    const char *network_address = NULL;
    uint16_t network_port = 0;
    int at = 1;
    while (at < argc && argv[at][0] == '-') {
        if (strcmp(argv[at], "--cli") == 0) { terminal = true; }
        else if (strcmp(argv[at], "--gui") == 0) { gui = true; }
        else if (strcmp(argv[at], "--listen") == 0 || strcmp(argv[at], "--connect") == 0) {
            if (network_address != NULL || at + 2 >= argc) { return EXIT_FAILURE; }
            listening = strcmp(argv[at], "--listen") == 0;
            network_address = argv[++at];
            const char *port_text = argv[++at];
            if (port_text[0] == '\0') { return EXIT_FAILURE; }
            unsigned port = 0;
            for (const char *p = port_text; *p != '\0'; ++p) {
                if (*p < '0' || *p > '9') { return EXIT_FAILURE; }
                port = port * 10u + (unsigned)(*p - '0');
                if (port > UINT16_MAX) { return EXIT_FAILURE; }
            }
            if (!listening && port == 0) { return EXIT_FAILURE; }
            network_port = (uint16_t)port;
        }
        else { fprintf(stderr, "unknown option: %s\n", argv[at]); return EXIT_FAILURE; }
        ++at;
    }
    if (!terminal && !gui) { terminal = true; }
    if (argc - at < 1) {
        fputs("usage: neo [--cli] [--gui] [--listen ADDRESS PORT | --connect ADDRESS PORT] IMAGE [ACTOR [HANDLER]]\n", stderr);
        return EXIT_FAILURE;
    }
    const char *path = argv[at++];
    const char *actor_name = at < argc && argv[at][0] != '-' ? argv[at++] : NULL;
    const char *handler = gui ? "frame" : "main";
    if (at < argc && argv[at][0] != '-') { handler = argv[at++]; }
    if (at != argc) {
        fputs("unexpected arguments after handler\n", stderr); return EXIT_FAILURE;
    }
    unsigned flags = NEO_PLATFORM_HOST_FILES;
    if (network_address != NULL) { flags |= NEO_PLATFORM_NETWORK; }
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
        status = neo_launch_entry(vm, root, actor_name, &actor);
    }
    /* Entry and drawable configuration are image data, not desktop policy. */
    size_t gui_width=640, gui_height=480;
    if (status == NEO_OK && gui && actor_name == NULL) {
        const neo_capability *entry=NULL;
        neo_status found=neo_object_child(vm,root,"gui-entry",&entry);
        if (found == NEO_OK) {
            neo_value value={0};
            status=neo_object_read(vm,entry,&value);
            if (status == NEO_OK) { status=value.kind == NEO_TEXT ? neo_object_child(vm,root,value.text,&actor) : NEO_WRONG_KIND; }
        } else if (found != NEO_UNAVAILABLE) { status=found; }
    }
    if (status == NEO_OK && gui) {
        const char *fields[]={"gui-width","gui-height"};
        size_t *sizes[]={&gui_width,&gui_height};
        for (size_t i=0; status == NEO_OK && i<2; ++i) {
            const neo_capability *field=NULL;
            neo_status found=neo_object_child(vm,actor,fields[i],&field);
            if (found == NEO_OK) {
                neo_value value={0}; status=neo_object_read(vm,field,&value);
                if (status == NEO_OK && (value.kind != NEO_INTEGER || value.integer <= 0 || (uint64_t)value.integer > SIZE_MAX)) { status=NEO_INVALID; }
                if (status == NEO_OK) { *sizes[i]=(size_t)value.integer; }
            } else if (found != NEO_UNAVAILABLE) { status=found; }
        }
    }
    const neo_capability *endpoint = NULL;
    if (status == NEO_OK && network_address != NULL) {
        status = neo_network_endpoint_create(vm, root, "host-network", network_address,
                                             network_port, listening, &endpoint);
        if (status == NEO_OK) { status = neo_object_connect(vm, actor, "network", endpoint, NEO_READ | NEO_ACT); }
    }
    if (status == NEO_OK && terminal) { status = neo_grant_terminal(vm, root, actor); }
    if (status == NEO_OK && gui) { status = neo_buffer_create(vm, root, "host-buffer", gui_width, gui_height, &buffer); }
    if (status == NEO_OK && gui) {
        status = neo_platform_open_window(vm, root, "host-window", "neo", gui_width, gui_height, &window);
    }
    if (status == NEO_OK && gui) { status = neo_object_connect(vm, actor, "buffer", buffer, NEO_READ | NEO_WRITE | NEO_DELEGATE); }
    if (status == NEO_OK && gui) { status = neo_object_connect(vm, actor, "window", window, NEO_READ | NEO_WRITE | NEO_DELEGATE); }
    if (status == NEO_OK && gui) {
        const neo_capability *clipboard=NULL;
        neo_status opened=neo_platform_open_clipboard(vm,root,"host-clipboard",&clipboard);
        if (opened == NEO_OK) { status=neo_object_connect(vm,actor,"host-clipboard",clipboard,NEO_READ|NEO_WRITE|NEO_DELEGATE); }
        else if (opened != NEO_UNSUPPORTED) { status=opened; }
    }
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
