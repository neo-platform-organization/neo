#include "internal.h"
#include "neo_platform.h"
#include <string.h>

const char *neo_environment_name(neo_environment value) {
    switch (value) {
        case NEO_ENVIRONMENT_HOSTED: return "hosted";
        case NEO_ENVIRONMENT_BARE_METAL: return "bare-metal";
        default: return "unknown";
    }
}

const char *neo_virtualization_name(neo_virtualization value) {
    switch (value) {
        case NEO_VIRTUALIZATION_PHYSICAL: return "physical";
        case NEO_VIRTUALIZATION_VIRTUAL: return "virtualized";
        default: return "unknown";
    }
}

static bool neo_platform_name_valid(const char *name) {
    const char *end = memchr(name, '\0', NEO_PLATFORM_NAME_SIZE);
    if (end == NULL || end == name) { return false; }
    for (const char *p = name; p != end; ++p) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') ||
              *p == '-' || *p == '_' || *p == '.')) { return false; }
    }
    return true;
}

neo_status neo_vm_configure_platform(neo_vm *vm, const neo_platform_info *info) {
    if (vm == NULL || info == NULL) { return NEO_INVALID; }
    if (vm->platform_configured || vm->next_id != 1) { return NEO_BUSY; }
    if ((info->environment != NEO_ENVIRONMENT_HOSTED &&
         info->environment != NEO_ENVIRONMENT_BARE_METAL) ||
        (info->virtualization != NEO_VIRTUALIZATION_UNKNOWN &&
         info->virtualization != NEO_VIRTUALIZATION_PHYSICAL &&
         info->virtualization != NEO_VIRTUALIZATION_VIRTUAL) ||
        !neo_platform_name_valid(info->backend) ||
        !neo_platform_name_valid(info->architecture) ||
        !neo_platform_name_valid(info->host_os) ||
        (info->services & ~((unsigned)NEO_PLATFORM_SERVICES_ALL)) != 0 ||
        (info->environment == NEO_ENVIRONMENT_BARE_METAL &&
         (strcmp(info->host_os, "none") != 0 || (info->services & NEO_PLATFORM_HOST_FILES) != 0)) ||
        (info->environment == NEO_ENVIRONMENT_HOSTED && strcmp(info->host_os, "none") == 0)) {
        return NEO_INVALID;
    }
    vm->platform = *info;
    vm->platform_configured = true;
    return NEO_OK;
}

neo_status neo_vm_platform_info(const neo_vm *vm, neo_platform_info *out) {
    if (out == NULL) { return NEO_INVALID; }
    *out = (neo_platform_info){0};
    if (vm == NULL) { return NEO_INVALID; }
    if (!vm->platform_configured) { return NEO_UNAVAILABLE; }
    *out = vm->platform;
    return NEO_OK;
}

neo_status neo_vm_install_platform(neo_vm *vm, const neo_platform_services *services) {
    if (vm == NULL || services == NULL) { return NEO_INVALID; }
    const neo_platform_backend *backend = &services->backend;
    unsigned supplied = (backend->read_source != NULL ? NEO_PLATFORM_HOST_FILES : 0u) |
        (backend->open_stream != NULL ? NEO_PLATFORM_BYTE_STREAMS : 0u) |
        (backend->open_window != NULL ? NEO_PLATFORM_PIXEL_WINDOWS : 0u) |
        (backend->wait != NULL ? NEO_PLATFORM_WAIT : 0u);
    if ((services->info.services & ~((unsigned)NEO_PLATFORM_INPUT_EVENTS)) != supplied ||
        ((services->info.services & NEO_PLATFORM_INPUT_EVENTS) != 0 && backend->open_window == NULL)) {
        return NEO_INVALID;
    }
    neo_status status = neo_vm_configure_platform(vm, &services->info);
    if (status == NEO_OK) {
        vm->platform_backend = *backend;
        vm->platform_context = services->context;
    }
    return status;
}

static neo_status neo_platform_require(neo_vm *vm, unsigned service) {
    if (vm == NULL) { return NEO_INVALID; }
    if (!vm->platform_configured) { return NEO_UNAVAILABLE; }
    return (vm->platform.services & service) != 0 ? NEO_OK : NEO_UNSUPPORTED;
}

neo_status neo_platform_load_image(neo_vm *vm, const char *location,
                                   const neo_capability **out, neo_diagnostic *error) {
    if (error != NULL) { *error = (neo_diagnostic){0}; }
    if (out == NULL) { return NEO_INVALID; }
    *out = NULL;
    if (location == NULL || location[0] == '\0') { return NEO_INVALID; }
    neo_status status = neo_platform_require(vm, NEO_PLATFORM_HOST_FILES);
    if (status != NEO_OK) { return status; }
    if (vm->platform_backend.read_source == NULL) { return NEO_UNSUPPORTED; }
    const size_t capacity = 1024u * 1024u;
    char *source = neo_alloc(vm, capacity + 1);
    if (source == NULL) { return NEO_OUT_OF_MEMORY; }
    size_t count = 0;
    status = vm->platform_backend.read_source(vm->platform_context, location, source, capacity, &count);
    if (status == NEO_OK && count > capacity) { status = NEO_LIMIT; }
    if (status == NEO_OK && memchr(source, '\0', count) != NULL) { status = NEO_PARSE_ERROR; }
    if (status == NEO_OK) {
        source[count] = '\0';
        status = neo_image_parse(vm, source, out, error);
    }
    neo_free(vm, source);
    return status;
}

neo_status neo_platform_open_stream(neo_vm *vm, const neo_capability *parent,
                                    const char *name, neo_standard_stream stream,
                                    const neo_capability **out) {
    if (out == NULL) { return NEO_INVALID; }
    *out = NULL;
    if (name == NULL || name[0] == '\0' ||
        (stream != NEO_STANDARD_INPUT && stream != NEO_STANDARD_OUTPUT && stream != NEO_STANDARD_ERROR)) {
        return NEO_INVALID;
    }
    neo_status status = neo_platform_require(vm, NEO_PLATFORM_BYTE_STREAMS);
    if (status != NEO_OK) { return status; }
    if (vm->platform_backend.open_stream == NULL) { return NEO_UNSUPPORTED; }
    neo_object *container = NULL;
    status = neo_resolve(vm, parent, NEO_INSERT, &container);
    if (status != NEO_OK) { return status; }
    status = vm->platform_backend.open_stream(vm->platform_context, vm, parent, name, stream, out);
    if (status != NEO_OK) { *out = NULL; }
    return status;
}

neo_status neo_platform_open_window(neo_vm *vm, const neo_capability *parent,
                                    const char *name, const char *title,
                                    size_t width, size_t height, const neo_capability **out) {
    if (out == NULL) { return NEO_INVALID; }
    *out = NULL;
    if (name == NULL || name[0] == '\0' || title == NULL || width == 0 || height == 0) {
        return NEO_INVALID;
    }
    neo_status status = neo_platform_require(vm, NEO_PLATFORM_PIXEL_WINDOWS);
    if (status != NEO_OK) { return status; }
    if (vm->platform_backend.open_window == NULL) { return NEO_UNSUPPORTED; }
    neo_object *container = NULL;
    status = neo_resolve(vm, parent, NEO_INSERT, &container);
    if (status != NEO_OK) { return status; }
    status = vm->platform_backend.open_window(vm->platform_context, vm, parent, name, title, width, height, out);
    if (status != NEO_OK) { *out = NULL; }
    return status;
}

neo_status neo_platform_wait(neo_vm *vm, uint32_t milliseconds) {
    neo_status status = neo_platform_require(vm, NEO_PLATFORM_WAIT);
    if (status != NEO_OK) { return status; }
    if (vm->platform_backend.wait == NULL) { return NEO_UNSUPPORTED; }
    return vm->platform_backend.wait(vm->platform_context, milliseconds);
}
