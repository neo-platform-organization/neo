#include "neo_platform.h"
#include "neo_image.h"
#include "neo_execution.h"
#include "neo_io.h"
#include "neo_display.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(EXIT_FAILURE); } } while (0)
#define OK(x) CHECK((x) == NEO_OK)

static const char *neo_source =
    "(image (machine (host-os \"pretend\") (handlers"
    " (environment (body (platform-info (field \"environment\"))))"
    " (architecture (body (platform-info (field \"architecture\"))))"
    " (host (body (platform-info (field \"host-os\"))))"
    " (virtualization (body (platform-info (field \"virtualization\"))))"
    " (backend (body (platform-info (field \"backend\"))))"
    " (files (body (platform-info (field \"host-files\"))))"
    " (streams (body (platform-info (field \"byte-streams\"))))"
    " (windows (body (platform-info (field \"pixel-windows\"))))"
    " (input (body (platform-info (field \"input-events\"))))"
    " (invalid (body (platform-info (field \"bogus\"))))"
    " (kind (body (platform-info (field 3))))"
    " (early (body (platform-info (field (return (value 7))))))"
    " (nogrant (body (stream-read (target \"stdin\")))))))";

static const neo_capability *neo_load(neo_vm *vm) {
    const neo_capability *root, *actor;
    neo_diagnostic error;
    OK(neo_image_parse(vm, neo_source, &root, &error));
    OK(neo_object_child(vm, root, "machine", &actor));
    return actor;
}
static void neo_expect(neo_vm *vm, const neo_capability *actor, const char *handler,
                        neo_status expected, const char *text, bool boolean) {
    neo_execution report;
    CHECK(neo_behavior_run(vm, actor, handler, NULL, 100, &report) == expected);
    if (expected == NEO_OK) {
        if (text != NULL) { CHECK(report.result.kind == NEO_TEXT && strcmp(report.result.text, text) == 0); }
        else { CHECK(report.result.kind == NEO_BOOLEAN && report.result.boolean == boolean); }
    }
    neo_execution_release(vm, &report);
}

int main(void) {
    neo_vm *vm;
    neo_platform_info info, snapshot;
    OK(neo_vm_create(NULL, &vm));
    CHECK(neo_vm_platform_info(vm, &snapshot) == NEO_UNAVAILABLE);
    CHECK(snapshot.environment == NEO_ENVIRONMENT_UNKNOWN && snapshot.services == 0);
    CHECK(neo_vm_platform_info(NULL, &snapshot) == NEO_INVALID);
    CHECK(neo_vm_platform_info(vm, NULL) == NEO_INVALID);
    CHECK(neo_platform_native(0, NULL) == NEO_INVALID);
    CHECK(neo_platform_native(1u << 20, &info) == NEO_INVALID);
    CHECK(info.services == 0 && info.backend[0] == '\0');
    OK(neo_platform_native(NEO_PLATFORM_HOST_FILES | NEO_PLATFORM_BYTE_STREAMS, &info));
    CHECK(info.environment == NEO_ENVIRONMENT_HOSTED);
    CHECK(info.virtualization == NEO_VIRTUALIZATION_UNKNOWN);
    CHECK(strcmp(info.host_os, "linux") == 0 && strcmp(info.architecture, "x86_64") == 0);
    CHECK(neo_vm_configure_platform(NULL, &info) == NEO_INVALID);
    CHECK(neo_vm_configure_platform(vm, NULL) == NEO_INVALID);
    snapshot = info; snapshot.services = 1u << 20;
    CHECK(neo_vm_configure_platform(vm, &snapshot) == NEO_INVALID);
    snapshot = info; memset(snapshot.host_os, 'x', sizeof(snapshot.host_os));
    CHECK(neo_vm_configure_platform(vm, &snapshot) == NEO_INVALID);
    snapshot = info; snapshot.environment = (neo_environment)99;
    CHECK(neo_vm_configure_platform(vm, &snapshot) == NEO_INVALID);
    snapshot = info; snapshot.virtualization = (neo_virtualization)99;
    CHECK(neo_vm_configure_platform(vm, &snapshot) == NEO_INVALID);
    OK(neo_vm_configure_platform(vm, &info));
    CHECK(neo_vm_configure_platform(vm, &info) == NEO_BUSY);
    info.host_os[0] = 'X';
    OK(neo_vm_platform_info(vm, &snapshot)); CHECK(strcmp(snapshot.host_os, "linux") == 0);
    const neo_capability *actor = neo_load(vm);
    neo_expect(vm, actor, "environment", NEO_OK, "hosted", false);
    neo_expect(vm, actor, "architecture", NEO_OK, "x86_64", false);
    neo_expect(vm, actor, "host", NEO_OK, "linux", false);
    neo_expect(vm, actor, "backend", NEO_OK, "linux-x86_64", false);
    neo_expect(vm, actor, "virtualization", NEO_OK, "unknown", false);
    neo_expect(vm, actor, "files", NEO_OK, NULL, true);
    neo_expect(vm, actor, "streams", NEO_OK, NULL, true);
    neo_expect(vm, actor, "windows", NEO_OK, NULL, false);
    neo_expect(vm, actor, "input", NEO_OK, NULL, false);
    neo_expect(vm, actor, "invalid", NEO_INVALID, NULL, false);
    neo_expect(vm, actor, "kind", NEO_WRONG_KIND, NULL, false);
    neo_expect(vm, actor, "nogrant", NEO_UNAVAILABLE, NULL, false);
    neo_execution report;
    OK(neo_behavior_run(vm, actor, "early", NULL, 100, &report));
    CHECK(report.result.kind == NEO_INTEGER && report.result.integer == 7);
    neo_execution_release(vm, &report);
    CHECK(neo_behavior_run(vm, actor, "host", NULL, 1, &report) == NEO_LIMIT);
    neo_execution_release(vm, &report);
    /* Platform metadata does not turn an ordinary object into a resource. */
    uint8_t byte; neo_io_result transfer; neo_window_state state;
    CHECK(neo_stream_read(vm, actor, &byte, 1, &transfer) == NEO_WRONG_KIND);
    CHECK(neo_window_poll(vm, actor, &state) == NEO_WRONG_KIND);
    neo_vm_destroy(vm);

    OK(neo_vm_create(NULL, &vm));
    actor = neo_load(vm);
    neo_expect(vm, actor, "host", NEO_UNAVAILABLE, NULL, false);
    CHECK(neo_vm_configure_platform(vm, &snapshot) == NEO_BUSY);
    neo_vm_destroy(vm);

    /* Synthetic descriptor only: proves generic contract, not a hardware port. */
    OK(neo_vm_create(NULL, &vm));
    info = (neo_platform_info){.environment = NEO_ENVIRONMENT_BARE_METAL,
        .virtualization = NEO_VIRTUALIZATION_VIRTUAL, .backend = "test-backend",
        .architecture = "test-arch", .host_os = "none", .services = NEO_PLATFORM_INPUT_EVENTS};
    snapshot = info; snapshot.services |= NEO_PLATFORM_HOST_FILES;
    CHECK(neo_vm_configure_platform(vm, &snapshot) == NEO_INVALID);
    OK(neo_vm_configure_platform(vm, &info));
    actor = neo_load(vm);
    neo_expect(vm, actor, "environment", NEO_OK, "bare-metal", false);
    neo_expect(vm, actor, "virtualization", NEO_OK, "virtualized", false);
    neo_expect(vm, actor, "files", NEO_OK, NULL, false);
    neo_expect(vm, actor, "input", NEO_OK, NULL, true);
    neo_vm_destroy(vm);
    neo_platform_services native;
    const neo_capability *resource = NULL;
    CHECK(neo_platform_native_services(NEO_PLATFORM_PIXEL_WINDOWS, &native) == NEO_UNSUPPORTED);
    CHECK(native.backend.open_window == NULL && native.info.services == 0);
    OK(neo_platform_native_services(NEO_PLATFORM_HOST_FILES | NEO_PLATFORM_WAIT, &native));
    OK(neo_vm_create(NULL, &vm));
    OK(neo_vm_install_platform(vm, &native));
    OK(neo_platform_wait(vm, 0));
    CHECK(neo_platform_open_stream(vm, NULL, "s", NEO_STANDARD_INPUT, &resource) == NEO_UNSUPPORTED);
    neo_vm_destroy(vm);
    puts("PASS: platform descriptor, independent environment axes, immutable setup, queries, no grants");
    return EXIT_SUCCESS;
}
