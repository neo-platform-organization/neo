#include "cli/cli.h"
#include "internal.h"
#include "execution/execution.h"
#include "io/io.h"
#include "packages/packages.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Trusted operator interface. Commands execute between bounded behavior turns;
 * root authority is never passed implicitly to invoked image behavior. */
typedef struct neo_live {
    neo_vm *vm;
    const neo_capability *root;
    bool running;
    bool quit;
    const neo_capability *driver;
    const neo_capability *service;
} neo_live;

static char *neo_word(char **cursor) {
    char *p = *cursor;
    while (isspace((unsigned char)*p)) { ++p; }
    char *word = p;
    while (*p != '\0' && !isspace((unsigned char)*p)) { ++p; }
    if (*p != '\0') { *p++ = '\0'; }
    *cursor = p;
    return word;
}

static neo_status neo_live_path(neo_live *live, const char *path, const neo_capability **out) {
    *out = live->root;
    if (strcmp(path, ".") == 0 || strcmp(path, "/") == 0) { return NEO_OK; }
    if (*path == '\0') { return NEO_INVALID; }
    char copy[4096];
    if (strlen(path) >= sizeof(copy)) { return NEO_LIMIT; }
    strcpy(copy, path);
    char *p = copy;
    if (*p == '/') { ++p; }
    while (*p != '\0') {
        char *end = strchr(p, '/');
        if (end != NULL) { *end = '\0'; }
        neo_status status = neo_object_child(live->vm, *out, p, out);
        if (status != NEO_OK) { return status; }
        if (end == NULL) { break; }
        p = end + 1;
    }
    return NEO_OK;
}

static void neo_live_value(neo_value value) {
    switch (value.kind) {
        case NEO_OBJECT: fputs("object", stdout); break;
        case NEO_REFERENCE: fputs("reference", stdout); break;
        case NEO_INTEGER: printf("%" PRId64, value.integer); break;
        case NEO_BOOLEAN: fputs(value.boolean ? "true" : "false", stdout); break;
        case NEO_TEXT: printf("\"%s\"", value.text); break;
        case NEO_PRIMITIVE: printf("primitive %s", value.text); break;
        case NEO_INTEGERS:
            putchar('[');
            for (size_t i = 0; i < value.count; ++i) { printf("%s%" PRId64, i ? " " : "", value.integers[i]); }
            putchar(']'); break;
    }
}

static neo_status neo_live_show(neo_live *live, const neo_capability *cap, size_t depth) {
    neo_object *node = NULL;
    neo_status status = neo_resolve(live->vm, cap, NEO_READ, &node);
    if (status != NEO_OK) { return status; }
    printf("%*s%s = ", (int)(depth * 2), "", node->name);
    neo_live_value(node->value);
    putchar('\n');
    for (neo_edge *edge = node->edges; edge != NULL; edge = edge->next) {
        neo_object *target = neo_lookup(live->vm, edge->target);
        printf("%*s@%s -> %s [rights %u]\n", (int)(depth * 2 + 2), "", edge->name,
               target == NULL ? "unavailable" : target->name, edge->rights);
    }
    if (depth < 1) {
        size_t count = 0;
        status = neo_object_child_count(live->vm, cap, &count);
        for (size_t i = 0; status == NEO_OK && i < count; ++i) {
            const neo_capability *child = NULL;
            status = neo_object_child_at(live->vm, cap, i, &child);
            if (status == NEO_OK) { status = neo_live_show(live, child, depth + 1); }
        }
    }
    return status;
}

/* Import inert syntax without external capabilities. Roll back a partial tree.
 * Full package import with connection remapping is a separate operation. */
static neo_status neo_live_import(neo_vm *vm, const neo_capability *source,
                                  const neo_capability *parent, const neo_capability **out) {
    neo_object *node = NULL;
    neo_status status = neo_resolve(vm, source, NEO_READ, &node);
    if (status != NEO_OK) { return status; }
    if (node->edges != NULL) { return NEO_UNSUPPORTED; }
    status = neo_object_create(vm, parent, node->name, node->value, out);
    if (status != NEO_OK) { return status; }
    size_t count = 0;
    status = neo_object_child_count(vm, source, &count);
    for (size_t i = 0; status == NEO_OK && i < count; ++i) {
        const neo_capability *child = NULL, *created = NULL;
        status = neo_object_child_at(vm, source, i, &child);
        if (status == NEO_OK) { status = neo_live_import(vm, child, *out, &created); }
    }
    if (status != NEO_OK) { (void)neo_object_delete(vm, *out); *out = NULL; }
    return status;
}

static void neo_live_report(neo_execution *report) {
    printf("%s; receiver=%" PRIu64 " operation=%" PRIu64 " steps=%zu; result=",
           neo_status_name(report->status), report->receiver, report->operation, report->steps);
    neo_live_value(report->result);
    putchar('\n');
}

static neo_status neo_live_tick(neo_live *live) {
    neo_execution report = {0};
    size_t turns = 0;
    neo_status status = live->driver == NULL ? neo_scheduler_tick(live->vm, 100000, &turns, &report)
        : neo_behavior_run(live->vm, live->driver, "tick", NULL, 100000, &report);
    if (status != NEO_OK) {
        live->running = false;
        neo_live_report(&report);
        puts(live->driver == NULL ? "Activity paused. Repair, then retry/discard the receiver and resume." :
             "Activity paused. Inspect the image driver; repair and explicitly reset/recover it before resuming.");
    }
    bool idle = live->driver == NULL ? turns == 0 :
        report.result.kind == NEO_INTEGER && report.result.integer == 0;
    neo_execution_release(live->vm, &report);
    if (status == NEO_OK && idle && live->running) { status = neo_platform_wait(live->vm, 1); }
    return status;
}

static neo_status neo_live_command(neo_live *live, char *line) {
    char *cursor = line;
    char *command = neo_word(&cursor);
    if (*command == '\0') { return NEO_OK; }
    if (strcmp(command, "help") == 0) {
        puts("Paths start at image root: /packages/system/environment; / means root.\n"
             "ls PATH | show PATH          inspect values, children and connections\n"
             "set PATH VALUE               change payload (7, true, \"text\", [1 2])\n"
             "add PARENT (name ...)        create an inert subtree (one line)\n"
             "copy SOURCE PARENT NAME | move SOURCE PARENT NAME | delete PATH\n"
             "rename PATH NAME             preserve identity under a new name\n"
             "send SENDER RECEIVER HANDLER  submit a text-selector message\n"
             "call PATH HANDLER            run updated behavior, bounded to 100000 steps\n"
             "register PATH | unregister PATH | tick | resume | pause\n"
             "drive PATH | drive native    choose a graph scheduler or the legacy scheduler\n"
             "service PATH | service native select image environment policy or recovery mode\n"
             "request ACTION               invoke selected service through its execute handler\n"
             "pkg-load FILE | pkg ACTION | pkg-export FILE | pkg-list | pkg-info ID\n"
             "                             stage, manage and browse local packages\n"
             "ether PATH                   grant READ access to image ETHER\n"
             "net-grant OWNER listen|connect ADDRESS PORT  grant a fixed endpoint; does not open it\n"
             "net-grant-tls OWNER listen|connect ADDRESS PORT LOCAL_SHA256 PEER_SHA256\n"
             "link OWNER NAME TARGET RIGHTS | unlink OWNER NAME\n"
             "retry PATH | discard PATH    explicit scheduled-failure decision\n"
             "save FILE                    save quiescent graph; unsupported state rejected\n"
             "quit                         exit; does not automatically save");
        return NEO_OK;
    }
    if (strcmp(command, "quit") == 0) { live->quit = true; return NEO_OK; }
    if (strcmp(command, "pause") == 0) { live->running = false; return NEO_OK; }
    if (strcmp(command, "resume") == 0) { live->running = true; return NEO_OK; }
    if (strcmp(command, "tick") == 0) { return neo_live_tick(live); }
    char *path = neo_word(&cursor);
    if (strcmp(command, "pkg-load") == 0 || strcmp(command, "pkg") == 0 ||
        strcmp(command, "pkg-export") == 0 || strcmp(command, "pkg-list") == 0 || strcmp(command, "pkg-info") == 0) {
        const neo_capability *manager = NULL;
        neo_status package_status = neo_live_path(live, "/package-manager", &manager);
        if (package_status != NEO_OK) { return package_status; }
        if (strcmp(command, "pkg-load") == 0) {
            const neo_capability *artifact = NULL, *staging = NULL, *candidate = NULL;
            neo_diagnostic error = {0};
            package_status = neo_platform_load_image(live->vm, path, &artifact, &error);
            if (package_status == NEO_OK) { package_status = neo_package_validate(live->vm, artifact); }
            if (package_status == NEO_OK) { package_status = neo_object_child(live->vm, manager, "staging", &staging); }
            if (package_status == NEO_OK) { package_status = neo_object_import(live->vm, artifact, staging, "candidate", &candidate); }
            if (artifact != NULL) { (void)neo_image_unload(live->vm, artifact); }
            if (error.message[0] != '\0') { printf("%zu:%zu %s\n", error.line, error.column, error.message); }
            return package_status;
        }
        if (strcmp(command, "pkg-list") == 0) {
            const neo_capability *catalogue = NULL;
            package_status = neo_object_child(live->vm, manager, "catalogue", &catalogue);
            return package_status == NEO_OK ? neo_live_show(live, catalogue, 0) : package_status;
        }
        if (strcmp(command, "pkg-info") == 0) {
            const neo_capability *catalogue = NULL, *record = NULL, *bundle = NULL, *manifest = NULL;
            package_status = neo_object_child(live->vm, manager, "catalogue", &catalogue);
            if (package_status == NEO_OK) { package_status = neo_object_child(live->vm, catalogue, path, &record); }
            if (package_status == NEO_OK) { package_status = neo_object_child(live->vm, record, "bundle", &bundle); }
            if (package_status == NEO_OK) { package_status = neo_object_child(live->vm, bundle, "manifest", &manifest); }
            if (package_status == NEO_OK) { package_status = neo_live_show(live, manifest, 0); }
            const char *sections[] = {"version", "dependencies", "exports", "imports", "bindings"};
            for (size_t i = 0; package_status == NEO_OK && i < 5; ++i) {
                const neo_capability *section = NULL;
                package_status = neo_object_child(live->vm, manifest, sections[i], &section);
                if (package_status == NEO_OK) { package_status = neo_live_show(live, section, 0); }
                if (i == 0) { continue; }
                size_t count = 0;
                if (package_status == NEO_OK) { package_status = neo_object_child_count(live->vm, section, &count); }
                for (size_t j = 0; package_status == NEO_OK && j < count; ++j) {
                    const neo_capability *entry = NULL;
                    package_status = neo_object_child_at(live->vm, section, j, &entry);
                    if (package_status == NEO_OK) { package_status = neo_live_show(live, entry, 0); }
                }
            }
            return package_status;
        }
        neo_execution report = {0};
        package_status = neo_behavior_run(live->vm, manager, strcmp(command, "pkg-export") == 0 ? "export" : path, NULL, 100000, &report);
        if (strcmp(command, "pkg-export") == 0) {
            if (package_status == NEO_OK) { package_status = report.result.kind == NEO_TEXT ?
                neo_platform_write_text(live->vm, path, report.result.text) : NEO_WRONG_KIND; }
            if (package_status != NEO_OK) { report.status = package_status; neo_live_report(&report); }
        } else { neo_live_report(&report); }
        neo_execution_release(live->vm, &report);
        return package_status;
    }
    if (strcmp(command, "save") == 0) {
        if (live->running) { return NEO_BUSY; }
        if (live->service != NULL) {
            neo_execution report = {0};
            neo_status ready = neo_behavior_run(live->vm, live->service, "save-ready", NULL, 100000, &report);
            if (ready == NEO_OK) {
                ready = report.result.kind != NEO_BOOLEAN ? NEO_WRONG_KIND :
                    report.result.boolean ? NEO_OK : NEO_DENIED;
            }
            if (ready != NEO_OK) { report.status = ready; neo_live_report(&report); }
            neo_execution_release(live->vm, &report);
            if (ready != NEO_OK) { return ready; }
        }
        return neo_platform_save_image(live->vm, live->root, path);
    }
    if (strcmp(command, "service") == 0 && strcmp(path, "native") == 0) { live->service = NULL; return NEO_OK; }
    if (strcmp(command, "request") == 0) {
        if (live->service == NULL || *path == '\0') { return NEO_INVALID; }
        const neo_capability *action = NULL;
        neo_status requested = neo_object_child(live->vm, live->service, "action", &action);
        if (requested == NEO_OK) { requested = neo_object_write(live->vm, action, (neo_value){.kind=NEO_TEXT,.text=path}); }
        neo_execution report = {0};
        if (requested == NEO_OK) {
            requested = neo_behavior_run(live->vm, live->service, "execute", NULL, 100000, &report);
            neo_live_report(&report);
        }
        neo_execution_release(live->vm, &report);
        return requested;
    }
    if (strcmp(command, "drive") == 0 && strcmp(path, "native") == 0) { live->driver = NULL; live->running = false; return NEO_OK; }
    const neo_capability *target = NULL;
    neo_status status = neo_live_path(live, path, &target);
    if (status != NEO_OK) { return status; }
    if (strcmp(command, "service") == 0) {
        neo_object *node = NULL;
        status = neo_resolve(live->vm, target, NEO_READ | NEO_WRITE | NEO_ACT, &node);
        if (status == NEO_OK) { live->service = target; }
        return status;
    }
    if (strcmp(command, "drive") == 0) {
        if (neo_scheduler_any_active(live->vm)) { return NEO_BUSY; }
        live->driver = target; live->running = false; return NEO_OK;
    }
    if (strcmp(command, "net-grant") == 0 || strcmp(command, "net-grant-tls") == 0) {
        bool secure = strcmp(command, "net-grant-tls") == 0;
        char *mode = neo_word(&cursor), *address = neo_word(&cursor), *port_text = neo_word(&cursor);
        char *local = secure ? neo_word(&cursor) : NULL;
        char *peer = secure ? neo_word(&cursor) : NULL;
        bool listening = strcmp(mode, "listen") == 0;
        if ((!listening && strcmp(mode, "connect") != 0) || *address == '\0' || *port_text == '\0' ||
            *neo_word(&cursor) != '\0') { return NEO_INVALID; }
        unsigned port = 0;
        for (const char *p = port_text; *p != '\0'; ++p) {
            if (*p < '0' || *p > '9') { return NEO_INVALID; }
            port = port * 10u + (unsigned)(*p - '0');
            if (port > UINT16_MAX) { return NEO_INVALID; }
        }
        if (!listening && port == 0) { return NEO_INVALID; }
        const neo_capability *endpoint = NULL;
        status = secure ? neo_network_endpoint_secure_create(live->vm, target, "host-network", address,
            (uint16_t)port, listening, local, peer, &endpoint) :
            neo_network_endpoint_create(live->vm, target, "host-network", address, (uint16_t)port, listening, &endpoint);
        if (status == NEO_OK) {
            status = neo_object_connect(live->vm, target, "network", endpoint, NEO_READ | NEO_ACT);
            if (status != NEO_OK) { (void)neo_object_delete(live->vm, endpoint); }
        }
        return status;
    }
    if (strcmp(command, "ether") == 0) {
        const neo_capability *ether = NULL;
        status = neo_object_child(live->vm, live->root, "ETHER", &ether);
        if (status == NEO_UNAVAILABLE) { status = neo_ether_create(live->vm, live->root, &ether); }
        if (status == NEO_OK) { status = neo_object_connect(live->vm, target, "ether", ether, NEO_READ); }
        return status;
    }
    if (strcmp(command, "unlink") == 0) { return neo_object_disconnect(live->vm, target, neo_word(&cursor)); }
    if (strcmp(command, "link") == 0) {
        char *name = neo_word(&cursor);
        const neo_capability *destination = NULL;
        status = neo_live_path(live, neo_word(&cursor), &destination);
        char *rights = neo_word(&cursor), *end = NULL;
        unsigned long number = strtoul(rights, &end, 10);
        if (*rights == '\0' || *rights == '-' || *end != '\0' || number > NEO_ALL) { return NEO_INVALID; }
        if (status == NEO_OK) { status = neo_object_connect(live->vm, target, name, destination, (unsigned)number); }
        return status;
    }
    if (strcmp(command, "ls") == 0 || strcmp(command, "show") == 0) { return neo_live_show(live, target, 0); }
    if (strcmp(command, "rename") == 0) { return neo_object_rename(live->vm, target, neo_word(&cursor)); }
    if (strcmp(command, "send") == 0) {
        const neo_capability *receiver = NULL, *ether = NULL;
        const neo_context *context = NULL;
        const neo_message *message = NULL;
        status = neo_live_path(live, neo_word(&cursor), &receiver);
        char *selector = neo_word(&cursor);
        if (*selector == '\0') { return NEO_INVALID; }
        if (status == NEO_OK) {
            status = neo_object_child(live->vm, live->root, "ETHER", &ether);
            if (status == NEO_UNAVAILABLE) { status = neo_ether_create(live->vm, live->root, &ether); }
        }
        if (status == NEO_OK) { status = neo_context_create(live->vm, target, &context); }
        if (status == NEO_OK) { status = neo_message_create(live->vm, context, ether, receiver, (neo_value){.kind=NEO_TEXT,.text=selector}, &message); }
        if (status == NEO_OK) { status = neo_message_submit(live->vm, context, message); }
        return status;
    }
    if (strcmp(command, "delete") == 0) {
        if (target == live->root) { return NEO_DENIED; }
        return neo_object_delete(live->vm, target);
    }
    if (strcmp(command, "register") == 0) { if (live->driver != NULL) { return NEO_BUSY; } return neo_scheduler_register(live->vm, target); }
    if (strcmp(command, "unregister") == 0) { return neo_scheduler_unregister(live->vm, target); }
    if (strcmp(command, "retry") == 0 || strcmp(command, "discard") == 0) {
        neo_object_id id = 0;
        status = neo_object_identity(live->vm, target, &id);
        if (status != NEO_OK) { return status; }
        bool retry = strcmp(command, "retry") == 0;
        if (neo_scheduler_contains(live->vm, id)) { return neo_scheduler_recover(live->vm, target, retry); }
        const neo_context *context = NULL;
        const neo_message *message = NULL;
        status = neo_context_create(live->vm, target, &context);
        if (status == NEO_OK) { status = neo_message_active(live->vm, context, &message); }
        if (status == NEO_OK) { status = retry ? neo_message_retry(live->vm, context) : neo_message_finish(live->vm, context, message); }
        return status;
    }
    if (strcmp(command, "call") == 0) {
        neo_execution report = {0};
        status = neo_behavior_run(live->vm, target, neo_word(&cursor), NULL, 100000, &report);
        neo_live_report(&report);
        neo_execution_release(live->vm, &report);
        return status;
    }
    if (strcmp(command, "copy") == 0 || strcmp(command, "move") == 0) {
        const neo_capability *parent = NULL, *created = NULL;
        status = neo_live_path(live, neo_word(&cursor), &parent);
        char *name = neo_word(&cursor);
        if (status == NEO_OK) {
            status = strcmp(command, "copy") == 0 ? neo_object_copy(live->vm, target, parent, name, &created)
                : neo_object_move(live->vm, target, parent, name, &created);
        }
        return status;
    }
    if (strcmp(command, "set") == 0 || strcmp(command, "add") == 0) {
        char source[16384];
        bool setting = strcmp(command, "set") == 0;
        int length = snprintf(source, sizeof(source), setting ? "(value %s)" : "%s", cursor);
        if (length < 0 || (size_t)length >= sizeof(source)) { return NEO_LIMIT; }
        const neo_capability *parsed = NULL, *created = NULL;
        neo_diagnostic diagnostic = {0};
        status = neo_image_parse(live->vm, source, &parsed, &diagnostic);
        if (status != NEO_OK) { printf("%zu:%zu %s\n", diagnostic.line, diagnostic.column, diagnostic.message); return status; }
        if (setting) {
            neo_value value;
            size_t count = 0;
            status = neo_object_child_count(live->vm, parsed, &count);
            if (status == NEO_OK && count != 0) { status = NEO_INVALID; }
            if (status == NEO_OK) { status = neo_object_read(live->vm, parsed, &value); }
            if (status == NEO_OK) { status = neo_object_write(live->vm, target, value); }
        } else { status = neo_live_import(live->vm, parsed, target, &created); }
        (void)neo_image_unload(live->vm, parsed);
        return status;
    }
    return NEO_INVALID;
}

int neo_cli_live(int argc, char **argv) {
    if (argc != 3) { fputs("usage: neo live IMAGE\n", stderr); return EXIT_FAILURE; }
    neo_live live = {0};
    neo_platform_services platform;
    neo_status status = neo_vm_create(NULL, &live.vm);
    unsigned services = NEO_PLATFORM_HOST_FILES | NEO_PLATFORM_BYTE_STREAMS | NEO_PLATFORM_WAIT;
#ifdef NEO_PLATFORM_WITH_WIN32
    services |= NEO_PLATFORM_NETWORK;
#endif
    if (status == NEO_OK) { status = neo_platform_native_services(services, &platform); }
    if (status == NEO_OK) { status = neo_vm_install_platform(live.vm, &platform); }
    neo_diagnostic diagnostic = {0};
    if (status == NEO_OK) { status = neo_platform_load_image(live.vm, argv[2], &live.root, &diagnostic); }
    /* Operator input is a separate host image, never serialized into user data. */
    const neo_capability *host = NULL, *input = NULL;
    if (status == NEO_OK) { status = neo_image_create(live.vm, "operator", &host); }
    if (status == NEO_OK) { status = neo_platform_open_stream(live.vm, host, "input", NEO_STANDARD_INPUT, &input); }
    if (status == NEO_OK) { puts("neo live image — edits apply between turns. Type help. No automatic save."); }
    char line[8192];
    size_t used = 0;
    bool overflow = false;
    while (status == NEO_OK && !live.quit) {
        if (used == 0 && !overflow) { fputs("neo> ", stdout); (void)fflush(stdout); }
        bool complete = false;
        while (status == NEO_OK && !complete) {
            uint8_t byte;
            neo_io_result result;
            status = neo_stream_read(live.vm, input, &byte, 1, &result);
            if (status != NEO_OK) { break; }
            if (result.state == NEO_IO_EOF) { live.quit = true; break; }
            if (result.state == NEO_IO_WOULD_BLOCK) {
                if (live.running) { (void)neo_live_tick(&live); }
                else { status = neo_platform_wait(live.vm, 10); }
                continue;
            }
            if (byte == '\n') { complete = true; }
            else if (byte == 0 || used + 1 >= sizeof(line)) { overflow = true; }
            else if (!overflow) { line[used++] = (char)byte; }
        }
        if (status != NEO_OK || live.quit) { break; }
        line[used] = '\0';
        neo_status command_status = overflow ? NEO_LIMIT : neo_live_command(&live, line);
        if (command_status != NEO_OK) { printf("neo: %s\n", neo_status_name(command_status)); }
        used = 0; overflow = false;
    }
    if (status != NEO_OK) { fprintf(stderr, "neo live: %s %s\n", neo_status_name(status), diagnostic.message); }
    neo_vm_destroy(live.vm);
    return status == NEO_OK ? EXIT_SUCCESS : EXIT_FAILURE;
}

/* Runs inside the universal selftest VM, including its tracked allocator. */
bool neo_live_selftest(neo_vm *vm) {
    neo_live live = {.vm=vm};
    neo_diagnostic error = {0};
    if (neo_image_parse(vm, "(live (workspace))", &live.root, &error) != NEO_OK) { return false; }
    const char *commands[] = {
        "add /workspace (counter (n 0) (handlers (tick (body (write (slot \"n\") (value (add (left (read (slot \"n\"))) (right 1))))))))",
        "add /workspace/counter/handlers (sequence (body (do (write (slot \"n\") (value 1)) (write (slot \"n\") (value 0)))))",
        "call /workspace/counter sequence",
        "register /workspace/counter", "tick", "tick", "unregister /workspace/counter",
        "set /workspace/counter/n 40", "copy /workspace/counter /workspace other",
        "rename /workspace/other renamed", "call /workspace/renamed tick",
        "set /workspace/counter/handlers/tick/body/write/value/add/right 2",
        "call /workspace/counter tick"
    };
    bool okay = true;
    for (size_t i = 0; okay && i < sizeof(commands)/sizeof(commands[0]); ++i) {
        char line[1024]; strcpy(line, commands[i]);
        neo_status status = neo_live_command(&live, line);
        if (status != NEO_OK) { fprintf(stderr, "FAIL live command %zu: %s\n", i, neo_status_name(status)); okay = false; }
    }
    const neo_capability *value = NULL;
    neo_value actual = {0};
    if (okay) { okay = neo_live_path(&live, "/workspace/counter/n", &value) == NEO_OK && neo_object_read(vm, value, &actual) == NEO_OK && actual.integer == 42; }
    if (okay) { okay = neo_live_path(&live, "/workspace/renamed/n", &value) == NEO_OK && neo_object_read(vm, value, &actual) == NEO_OK && actual.integer == 41; }
    char invalid[] = "set /workspace/counter/n (child 8)";
    if (okay) { okay = neo_live_command(&live, invalid) == NEO_INVALID; }
    char malformed[] = "add /workspace (broken (child)";
    if (okay) { okay = neo_live_command(&live, malformed) == NEO_PARSE_ERROR; }
    char *text = NULL;
    if (okay) { okay = neo_image_format(vm, live.root, &text) == NEO_OK; }
    const neo_capability *reopened = NULL;
    if (okay) { okay = neo_image_parse(vm, text, &reopened, &error) == NEO_OK; }
    if (reopened != NULL) { (void)neo_image_unload(vm, reopened); }
    neo_image_text_free(vm, text);
    const char *activity[] = {
        "add /workspace (receiver (n 0) (handlers (open (body (write (slot \"n\") (value 9))))))",
        "register /workspace/receiver",
        "send /workspace/counter /workspace/receiver open", "tick"
    };
    for (size_t i = 0; okay && i < sizeof(activity)/sizeof(activity[0]); ++i) {
        char line[1024]; strcpy(line, activity[i]);
        okay = neo_live_command(&live, line) == NEO_OK;
    }
    if (okay) { okay = neo_live_path(&live, "/workspace/receiver/n", &value) == NEO_OK && neo_object_read(vm, value, &actual) == NEO_OK && actual.integer == 9; }
    const char *failure_setup[] = {
        "add /workspace/receiver/handlers (broken (body (fail)))",
        "send /workspace/counter /workspace/receiver broken"
    };
    for (size_t i = 0; okay && i < sizeof(failure_setup)/sizeof(failure_setup[0]); ++i) {
        char line[1024]; strcpy(line, failure_setup[i]);
        okay = neo_live_command(&live, line) == NEO_OK;
    }
    char failed_tick[] = "tick";
    if (okay) { okay = neo_live_command(&live, failed_tick) == NEO_RAISED; }
    const char *repair[] = {
        "delete /workspace/receiver/handlers/broken/body",
        "add /workspace/receiver/handlers/broken (body 3)",
        "retry /workspace/receiver", "tick"
    };
    for (size_t i = 0; okay && i < sizeof(repair)/sizeof(repair[0]); ++i) {
        char line[1024]; strcpy(line, repair[i]);
        okay = neo_live_command(&live, line) == NEO_OK;
    }
    text = NULL;
    if (okay) { okay = neo_image_format(vm, live.root, &text) == NEO_UNSUPPORTED; }
    neo_image_text_free(vm, text);
    (void)neo_image_unload(vm, live.root);
    if (!okay) { fputs("FAIL live editing/roundtrip\n", stderr); }
    return okay;
}

/* Test workers are fixtures, never objects in the boot image. */
static bool neo_live_test_worker(neo_live *live, const char *name) {
    const neo_capability *fixture = NULL, *workspace = NULL, *worker = NULL;
    neo_diagnostic error = {0};
    neo_status status = neo_platform_load_image(live->vm, "neo/fixtures/counter.neo", &fixture, &error);
    if (status == NEO_OK) { status = neo_live_path(live, "/workspace", &workspace); }
    if (status == NEO_OK) { status = neo_object_import(live->vm, fixture, workspace, name, &worker); }
    if (fixture != NULL) { (void)neo_image_unload(live->vm, fixture); }
    return status == NEO_OK;
}

/* Exercise the same operator commands documented for the image scheduler. */
bool neo_live_graph_selftest(neo_vm *vm) {
    neo_live live = {.vm=vm};
    neo_diagnostic error = {0};
    if (neo_platform_load_image(vm, "neo/neo-os/image.neo", &live.root, &error) != NEO_OK) { return false; }
    if (!neo_live_test_worker(&live, "clock")) { (void)neo_image_unload(vm, live.root); return false; }
    const char *commands[] = {
        "copy /packages/system/scheduler /workspace scheduler",
        "add /workspace/scheduler/entries (clock (enabled true) (periodic true) (error \"\"))",
        "link /workspace/scheduler/entries/clock target /workspace/clock 67",
        "drive /workspace/scheduler", "tick", "tick",
        "set /workspace/clock/increment \"broken\"", "tick"
    };
    bool okay = true;
    for (size_t i = 0; okay && i < sizeof(commands)/sizeof(commands[0]); ++i) {
        char line[256]; strcpy(line, commands[i]);
        neo_status status = neo_live_command(&live, line);
        if (status != NEO_OK) { fprintf(stderr, "FAIL graph command %zu: %s\n", i, neo_status_name(status)); okay = false; }
    }
    const neo_capability *field = NULL;
    neo_value value = {0};
    if (okay) { okay = neo_live_path(&live, "/workspace/clock/value", &field) == NEO_OK && neo_object_read(vm, field, &value) == NEO_OK && value.kind == NEO_INTEGER && value.integer == 2; }
    if (okay) { okay = neo_live_path(&live, "/workspace/scheduler/entries/clock/enabled", &field) == NEO_OK && neo_object_read(vm, field, &value) == NEO_OK && value.kind == NEO_BOOLEAN && !value.boolean; }
    if (okay) { okay = neo_live_path(&live, "/workspace/scheduler/entries/clock/error", &field) == NEO_OK && neo_object_read(vm, field, &value) == NEO_OK && value.kind == NEO_TEXT && strcmp(value.text, neo_status_name(NEO_WRONG_KIND)) == 0; }
    char registration[] = "register /workspace/clock";
    if (okay) { okay = neo_live_command(&live, registration) == NEO_BUSY; }
    const char *repair[] = {
        "set /workspace/clock/increment 10",
        "set /workspace/scheduler/entries/clock/enabled true", "tick",
        "send /workspace/clock /workspace/clock reset", "tick"
    };
    for (size_t i = 0; okay && i < sizeof(repair)/sizeof(repair[0]); ++i) {
        char line[256]; strcpy(line, repair[i]);
        okay = neo_live_command(&live, line) == NEO_OK;
        if (okay && i == 2) {
            okay = neo_live_path(&live, "/workspace/clock/value", &field) == NEO_OK && neo_object_read(vm, field, &value) == NEO_OK && value.kind == NEO_INTEGER && value.integer == 12;
        }
    }
    if (okay) { okay = neo_live_path(&live, "/workspace/clock/value", &field) == NEO_OK && neo_object_read(vm, field, &value) == NEO_OK && value.kind == NEO_INTEGER && value.integer == 10; }
    (void)neo_image_unload(vm, live.root);
    if (!okay) { fputs("FAIL live graph-driver workflow\n", stderr); }
    return okay;
}

static bool neo_live_test_script(neo_live *live, const char *const *commands, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        char line[2048];
        if (strlen(commands[i]) >= sizeof(line)) { return false; }
        strcpy(line, commands[i]);
        neo_status status = neo_live_command(live, line);
        if (status != NEO_OK) {
            fprintf(stderr, "FAIL scheduling command %zu (%s): %s\n", i, commands[i], neo_status_name(status));
            return false;
        }
    }
    return true;
}

static bool neo_live_test_integer(neo_live *live, const char *path, int64_t expected) {
    const neo_capability *field = NULL;
    neo_value value = {0};
    bool okay = neo_live_path(live, path, &field) == NEO_OK && neo_object_read(live->vm, field, &value) == NEO_OK &&
        value.kind == NEO_INTEGER && value.integer == expected;
    if (!okay) { fprintf(stderr, "FAIL scheduling value %s, expected %" PRId64 "\n", path, expected); }
    return okay;
}

static bool neo_live_test_flag(neo_live *live, const char *path, bool expected) {
    const neo_capability *field = NULL;
    neo_value value = {0};
    return neo_live_path(live, path, &field) == NEO_OK && neo_object_read(live->vm, field, &value) == NEO_OK &&
        value.kind == NEO_BOOLEAN && value.boolean == expected;
}

static bool neo_live_test_members(neo_live *live, size_t expected) {
    const neo_capability *entries = NULL;
    size_t count = 0;
    return neo_live_path(live, "/workspace/scheduler/entries", &entries) == NEO_OK &&
        neo_object_child_count(live->vm, entries, &count) == NEO_OK && count == expected;
}


bool neo_live_environment_selftest(neo_vm *vm) {
    neo_live live = {.vm=vm};
    neo_diagnostic diagnostic = {0};
    if (neo_platform_load_image(vm, "neo/neo-os/image.neo", &live.root, &diagnostic) != NEO_OK) { return false; }
    const char *setup[] = {
        "copy /packages/system/environment /workspace environment",
        "add /workspace (subject (n 0))",
        "link /workspace/environment target /workspace/subject/n 1",
        "link /workspace/environment destination /workspace/subject 5",
        "service /workspace/environment", "request inspect-value",
        "set /workspace/environment/value 41"
    };
    bool okay = neo_live_test_script(&live, setup, sizeof(setup)/sizeof(setup[0]));
    char denied[] = "request edit";
    if (okay) { okay = neo_live_command(&live, denied) == NEO_DENIED && neo_live_test_integer(&live, "/workspace/subject/n", 0); }
    const neo_capability *field = NULL;
    neo_value value = {0};
    if (okay) { okay = neo_live_path(&live, "/workspace/environment/last-error", &field) == NEO_OK &&
        neo_object_read(vm, field, &value) == NEO_OK && value.kind == NEO_TEXT && strcmp(value.text, "authority denied") == 0; }
    const char *failure_inspection[] = {"request failure-status", "request clear-failure"};
    if (okay) { okay = neo_live_test_script(&live, failure_inspection, 2); }
    if (okay) { okay = neo_object_read(vm, field, &value) == NEO_OK && value.kind == NEO_TEXT && value.text[0] == '\0'; }
    const char *edit[] = {
        "unlink /workspace/environment target", "link /workspace/environment target /workspace/subject/n 3",
        "request edit", "set /workspace/environment/name \"created\"", "request create",
        "add /workspace (view (handlers (edit (body (do (object-write (target (child (target (connection (target (self)) (name \"environment\"))) (name \"value\"))) (value 73)) (object-write (target (child (target (connection (target (self)) (name \"environment\"))) (name \"action\"))) (value \"edit\")) (invoke (target (connection (target (self)) (name \"environment\"))) (selector \"execute\")))))))",
        "link /workspace/view environment /workspace/environment 67", "call /workspace/view edit"
    };
    if (okay) { okay = neo_live_test_script(&live, edit, sizeof(edit)/sizeof(edit[0])); }
    if (okay) { okay = neo_live_test_integer(&live, "/workspace/subject/n", 73) && neo_live_test_integer(&live, "/workspace/subject/created", 41); }
    const char *save_policy[] = {"set /workspace/environment/saving-enabled false"};
    if (okay) { okay = neo_live_test_script(&live, save_policy, 1); }
    char save[] = "save";
    if (okay) { okay = neo_live_command(&live, save) == NEO_DENIED; }
    const char *scheduler_policy[] = {
        "copy /packages/system/scheduler /workspace scheduler",
        "link /workspace/environment scheduler /workspace/scheduler 127",
        "set /workspace/environment/scheduler-attached true",
        "set /workspace/environment/saving-enabled true", "request tick",
        "set /workspace/scheduler/running true"
    };
    if (okay) { okay = neo_live_test_script(&live, scheduler_policy, sizeof(scheduler_policy)/sizeof(scheduler_policy[0])); }
    char partial[] = "save";
    if (okay) { okay = neo_live_command(&live, partial) == NEO_DENIED; }
    const char *reset_policy[] = {"call /workspace/scheduler reset"};
    if (okay) { okay = neo_live_test_script(&live, reset_policy, 1); }
    char allowed[] = "save";
    /* Policy now permits saving; the host rejects the missing filename. */
    if (okay) { okay = neo_live_command(&live, allowed) == NEO_INVALID; }
    /* A broken image service cannot remove native repair/escape access. */
    const char *break_service[] = {"delete /workspace/environment/handlers/execute"};
    if (okay) { okay = neo_live_test_script(&live, break_service, 1); }
    char broken[] = "request inspect-value";
    if (okay) { okay = neo_live_command(&live, broken) == NEO_UNAVAILABLE; }
    const char *escape[] = {"service native", "set /workspace/subject/n 99"};
    if (okay) { okay = neo_live_test_script(&live, escape, 2) && neo_live_test_integer(&live, "/workspace/subject/n", 99); }
    if (live.root != NULL) { (void)neo_image_unload(vm, live.root); }
    if (!okay) { fputs("FAIL image environment services/authority/shared-view/recovery\n", stderr); }
    return okay;
}

static bool neo_live_policy_rejected(neo_live *live, const char *action, const char *reason, size_t expected_count) {
    char command[128];
    (void)snprintf(command, sizeof(command), "pkg %s", action);
    const neo_capability *error = NULL, *catalogue = NULL, *scratch = NULL;
    neo_value value = {0};
    size_t count = 0;
    return neo_live_command(live, command) == NEO_RAISED &&
        neo_live_path(live, "/package-manager/policy-error", &error) == NEO_OK && neo_object_read(live->vm, error, &value) == NEO_OK &&
        value.kind == NEO_TEXT && strcmp(value.text, reason) == 0 &&
        neo_live_path(live, "/package-manager/catalogue", &catalogue) == NEO_OK && neo_object_child_count(live->vm, catalogue, &count) == NEO_OK && count == expected_count &&
        neo_live_path(live, "/package-manager/resolution", &scratch) == NEO_UNAVAILABLE;
}

bool neo_live_package_policy_selftest(neo_vm *vm) {
    neo_live live = {.vm=vm};
    neo_diagnostic error = {0};
    if (neo_platform_load_image(vm, "neo/neo-os/image.neo", &live.root, &error) != NEO_OK) { return false; }
    const char *missing[] = {"pkg-load neo/fixtures/packages/counter.neo"};
    bool okay = neo_live_test_script(&live, missing, 1) && neo_live_policy_rejected(&live, "install", "missing-dependency", 0);
    const char *dependency[] = {"pkg clear", "pkg-load neo/fixtures/packages/clock.neo", "pkg install", "pkg clear", "pkg-load neo/fixtures/packages/counter.neo",
        "set /package-manager/staging/candidate/manifest/dependencies/clock-package/version/patch 1"};
    if (okay) { okay = neo_live_test_script(&live, dependency, sizeof(dependency)/sizeof(dependency[0])) && neo_live_policy_rejected(&live, "install", "version-conflict", 1); }
    const char *cycle[] = {"set /package-manager/staging/candidate/manifest/dependencies/clock-package/version/patch 0",
        "set /package-manager/staging/candidate/manifest/dependencies/clock-package/id \"local.counter\""};
    if (okay) { okay = neo_live_test_script(&live, cycle, 2) && neo_live_policy_rejected(&live, "install", "dependency-cycle", 1); }
    const char *install[] = {"set /package-manager/staging/candidate/manifest/dependencies/clock-package/id \"local.clock\"", "pkg install",
        "set /package-manager/id \"local.clock\""};
    if (okay) { okay = neo_live_test_script(&live, install, 3) && neo_live_policy_rejected(&live, "remove", "required-by-package", 2); }
    if (okay) { okay = neo_live_policy_rejected(&live, "update", "version-not-newer", 2); }
    const char *resolve[] = {"set /package-manager/id \"local.counter\"", "set /package-manager/export-name \"counter\"", "pkg resolve"};
    if (okay) { okay = neo_live_test_script(&live, resolve, 3); }
    const char *transitive_cycle[] = {
        "add /package-manager/catalogue/local.clock/bundle/manifest/dependencies (loop (id \"local.counter\") (version (major 1) (minor 0) (patch 0)))"
    };
    if (okay) { okay = neo_live_test_script(&live, transitive_cycle, 1) && neo_live_policy_rejected(&live, "resolve", "dependency-cycle", 2); }
    const char *repair_cycle[] = {"delete /package-manager/catalogue/local.clock/bundle/manifest/dependencies/loop"};
    if (okay) { okay = neo_live_test_script(&live, repair_cycle, 1); }
    const neo_capability *manager = NULL;
    neo_execution report = {0};
    if (okay) { okay = neo_live_path(&live, "/package-manager", &manager) == NEO_OK && neo_behavior_run(vm, manager, "policy-prepare", NULL, 100, &report) == NEO_LIMIT; }
    neo_execution_release(vm, &report);
    const neo_capability *partial = NULL;
    if (okay) { okay = neo_live_path(&live, "/package-manager/resolution", &partial) == NEO_OK; }
    const char *recover[] = {"pkg resolve", "set /package-manager/staging/candidate/manifest/version/patch 1", "pkg update"};
    if (okay) { okay = neo_live_test_script(&live, recover, 3); }
    /* Editing image policy changes version eligibility without a C rebuild. */
    const char *custom_policy[] = {
        "delete /package-manager/handlers/version-newer/body", "add /package-manager/handlers/version-newer (body true)",
        "set /package-manager/staging/candidate/manifest/version/patch 0", "pkg update"
    };
    if (okay) { okay = neo_live_test_script(&live, custom_policy, 4) && neo_live_test_integer(&live, "/package-manager/catalogue/local.counter/bundle/manifest/version/patch", 0); }
    const char *unsafe_grant[] = {
        "delete /package-manager/handlers/version-newer/body",
        "add /package-manager/handlers/version-newer (body (do (object-write (target (child (target (child (target (child (target (child (target (call (selector \"candidate\"))) (name \"manifest\"))) (name \"imports\"))) (name \"clock\"))) (name \"rights\"))) (value 3)) (result true)))"
    };
    if (okay) { okay = neo_live_test_script(&live, unsafe_grant, 2); }
    char denied[] = "pkg update";
    if (okay) { okay = neo_live_command(&live, denied) == NEO_DENIED && neo_live_test_integer(&live, "/package-manager/catalogue/local.counter/bundle/manifest/version/patch", 0) &&
        neo_live_test_integer(&live, "/package-manager/staging/candidate/manifest/imports/clock/rights", 3); }
    const char *metadata_dependency[] = {
        "pkg clear",
        "add /package-manager/staging (candidate (manifest (schema 1) (id \"local.metadata\") (version (major 1) (minor 0) (patch 0)) (dependencies (clock-package (id \"local.clock\") (version (major 1) (minor 0) (patch 0)))) (exports) (imports) (bindings)) (contents))",
        "pkg install", "set /package-manager/id \"local.counter\"", "pkg remove", "set /package-manager/id \"local.clock\""
    };
    if (okay) { okay = neo_live_test_script(&live, metadata_dependency, 6) && neo_live_policy_rejected(&live, "remove", "required-by-package", 2); }
    if (live.root != NULL) { (void)neo_image_unload(vm, live.root); }
    if (!okay) { fputs("FAIL image package dependency/version/cycle/recovery policy\n", stderr); }
    return okay;
}

bool neo_live_scheduler_selftest(neo_vm *vm) {
    neo_live live = {.vm=vm};
    neo_diagnostic diagnostic = {0};
    if (neo_platform_load_image(vm, "neo/neo-os/image.neo", &live.root, &diagnostic) != NEO_OK) { return false; }
    if (!neo_live_test_worker(&live, "clock")) { (void)neo_image_unload(vm, live.root); return false; }
    const char *first[] = {
        "copy /packages/system/scheduler /workspace scheduler",
        "add /workspace (sender (observed 0) (handlers (tick (body (do (send (target \"receiver\") (value \"open\")) (write (slot \"observed\") (value (inspect (target (child (target (connection (target (self)) (name \"receiver\"))) (name \"n\")))))))))))",
        "add /workspace (receiver (n 0) (handlers (open (body (write (slot \"n\") (value (add (left (read (slot \"n\"))) (right 1))))))))",
        "link /workspace/sender receiver /workspace/receiver 129", "ether /workspace/sender",
        "add /workspace/scheduler/entries (sender (enabled true) (periodic true) (error \"\"))",
        "add /workspace/scheduler/entries (receiver (enabled true) (periodic false) (error \"\"))",
        "link /workspace/scheduler/entries/sender target /workspace/sender 67",
        "link /workspace/scheduler/entries/receiver target /workspace/receiver 67",
        "drive /workspace/scheduler", "tick"
    };
    bool okay = neo_live_test_script(&live, first, sizeof(first)/sizeof(first[0]));
    if (okay) { okay = neo_live_test_integer(&live, "/workspace/receiver/n", 0); }
    const char *second[] = {"tick"};
    if (okay) { okay = neo_live_test_script(&live, second, 1); }
    if (okay) { okay = neo_live_test_integer(&live, "/workspace/receiver/n", 1) && neo_live_test_integer(&live, "/workspace/sender/observed", 1); }
    const char *membership[] = {
        "link /workspace/scheduler candidate /workspace/clock 67",
        "set /workspace/scheduler/candidate-name \"clock\"",
        "set /workspace/scheduler/candidate-periodic true"
    };
    if (okay) { okay = neo_live_test_script(&live, membership, sizeof(membership)/sizeof(membership[0])); }
    char denied[] = "call /workspace/scheduler register";
    if (okay) { okay = neo_live_command(&live, denied) == NEO_DENIED && neo_live_test_members(&live, 2); }
    const char *grant[] = {
        "unlink /workspace/scheduler candidate", "link /workspace/scheduler candidate /workspace/clock 99",
        "call /workspace/scheduler register"
    };
    if (okay) { okay = neo_live_test_script(&live, grant, 3) && neo_live_test_members(&live, 3); }
    char duplicate[] = "call /workspace/scheduler register";
    if (okay) { okay = neo_live_command(&live, duplicate) == NEO_RAISED && neo_live_test_members(&live, 3); }
    const char *remove[] = {"set /workspace/scheduler/selected 2", "call /workspace/scheduler unregister"};
    if (okay) { okay = neo_live_test_script(&live, remove, 2) && neo_live_test_members(&live, 2); }
    char native[] = "register /workspace/receiver";
    if (okay) { okay = neo_live_command(&live, native) == NEO_BUSY; }
    const char *message_failure[] = {
        "set /workspace/receiver/n \"broken\"", "tick"
    };
    if (okay) { okay = neo_live_test_script(&live, message_failure, 2); }
    if (okay) { okay = neo_live_test_flag(&live, "/workspace/scheduler/entries/receiver/enabled", false); }
    const char *message_retry[] = {
        "set /workspace/receiver/n 0", "set /workspace/scheduler/selected 1",
        "call /workspace/scheduler retry", "tick"
    };
    if (okay) { okay = neo_live_test_script(&live, message_retry, 4) && neo_live_test_integer(&live, "/workspace/receiver/n", 1); }
    if (okay) { okay = neo_live_test_script(&live, message_failure, 2); }
    if (okay) { okay = neo_live_test_flag(&live, "/workspace/scheduler/entries/receiver/enabled", false); }
    const char *message_discard[] = {
        "call /workspace/scheduler discard", "set /workspace/receiver/n 0"
    };
    if (okay) { okay = neo_live_test_script(&live, message_discard, 2); }
    if (okay) { okay = neo_live_test_flag(&live, "/workspace/scheduler/entries/receiver/enabled", true); }
    if (live.root != NULL) { (void)neo_image_unload(vm, live.root); live.root = NULL; }

    if (okay) { okay = neo_platform_load_image(vm, "neo/neo-os/image.neo", &live.root, &diagnostic) == NEO_OK; }
    if (okay) { okay = neo_live_test_worker(&live, "late"); }
    const char *snapshot[] = {
        "copy /packages/system/scheduler /workspace scheduler",
        "add /workspace (mutator (handlers (tick (body (invoke (target (connection (target (self)) (name \"controller\"))) (selector \"unregister\"))))))",
        "link /workspace/mutator controller /workspace/scheduler 255",
        "add /workspace/scheduler/entries (mutator (enabled true) (periodic true) (error \"\"))",
        "add /workspace/scheduler/entries (late (enabled true) (periodic true) (error \"\"))",
        "link /workspace/scheduler/entries/mutator target /workspace/mutator 67",
        "link /workspace/scheduler/entries/late target /workspace/late 67",
        "set /workspace/scheduler/selected 1", "drive /workspace/scheduler", "tick"
    };
    if (okay) { okay = neo_live_test_script(&live, snapshot, sizeof(snapshot)/sizeof(snapshot[0])); }
    if (okay) { okay = neo_live_test_integer(&live, "/workspace/late/value", 1) && neo_live_test_members(&live, 1); }
    if (okay) { okay = neo_live_test_script(&live, second, 1) && neo_live_test_integer(&live, "/workspace/late/value", 1); }
    const char *reentrant[] = {
        "set /workspace/mutator/handlers/tick/body/invoke/selector \"tick\"",
        "set /workspace/scheduler/entries/mutator/enabled true", "tick"
    };
    if (okay) { okay = neo_live_test_script(&live, reentrant, sizeof(reentrant)/sizeof(reentrant[0])); }
    const char *exhaust[] = {
        "delete /workspace/mutator/handlers/tick/body",
        "add /workspace/mutator/handlers/tick (body (while (condition true) (body 0)))",
        "set /workspace/scheduler/entries/mutator/enabled true"
    };
    if (okay) { okay = neo_live_test_script(&live, exhaust, sizeof(exhaust)/sizeof(exhaust[0])); }
    neo_execution report = {0};
    if (okay) { okay = neo_behavior_run(vm, live.driver, "tick", NULL, 2000, &report) == NEO_LIMIT; }
    neo_execution_release(vm, &report);
    if (okay) { okay = neo_live_test_flag(&live, "/workspace/scheduler/running", true); }
    if (okay) { okay = neo_behavior_run(vm, live.driver, "tick", NULL, 2000, &report) == NEO_RAISED; }
    neo_execution_release(vm, &report);
    const char *recover[] = {
        "set /workspace/scheduler/entries/mutator/enabled false", "call /workspace/scheduler reset"
    };
    if (okay) { okay = neo_live_test_script(&live, recover, sizeof(recover)/sizeof(recover[0])); }
    if (okay) { okay = neo_live_test_flag(&live, "/workspace/scheduler/running", false); }
    live.running = true;
    if (okay) { okay = neo_live_tick(&live) == NEO_OK; }
    live.running = false;
    if (okay) { okay = neo_live_test_integer(&live, "/workspace/scheduler/work", 0); }
    if (live.root != NULL) { (void)neo_image_unload(vm, live.root); }
    if (!okay) { fputs("FAIL image scheduling boundary/membership/recovery\n", stderr); }
    return okay;
}
