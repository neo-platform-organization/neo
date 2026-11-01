#ifndef NEO_PLATFORM_H
#define NEO_PLATFORM_H

#include "objects/objects.h"
#include "image/image.h"
#include "platform/network.h"

/** Execution environment and underlying hardware status are independent axes.
 * These describe a backend, not image-selectable switches or resource grants. */
typedef enum neo_environment {
    NEO_ENVIRONMENT_UNKNOWN, NEO_ENVIRONMENT_HOSTED, NEO_ENVIRONMENT_BARE_METAL
} neo_environment;
typedef enum neo_virtualization {
    NEO_VIRTUALIZATION_UNKNOWN, NEO_VIRTUALIZATION_PHYSICAL, NEO_VIRTUALIZATION_VIRTUAL
} neo_virtualization;
typedef enum neo_platform_service {
    NEO_PLATFORM_BYTE_STREAMS = 1u << 0,
    NEO_PLATFORM_HOST_FILES = 1u << 1,
    NEO_PLATFORM_PIXEL_WINDOWS = 1u << 2,
    NEO_PLATFORM_INPUT_EVENTS = 1u << 3,
    NEO_PLATFORM_WAIT = 1u << 4,
    NEO_PLATFORM_NETWORK = 1u << 5,
    NEO_PLATFORM_SERVICES_ALL = (1u << 6) - 1u
} neo_platform_service;

#define NEO_PLATFORM_NAME_SIZE 32u
/** Fixed owned strings: no backend pointer lifetime or native ABI leaks into VM.
 * Services describe providers selected by trusted bootstrap, not successful
 * device connections or permissions. Opening a resource can still fail.
 * No GUI mode or GPU capability is implemented by this descriptor. */
typedef struct neo_platform_info {
    neo_environment environment;
    neo_virtualization virtualization;
    char backend[NEO_PLATFORM_NAME_SIZE];
    char architecture[NEO_PLATFORM_NAME_SIZE];
    char host_os[NEO_PLATFORM_NAME_SIZE];
    unsigned services;
} neo_platform_info;

/** Configure once, before any image/object allocation. Copies the descriptor.
 * No system probing, resource creation, or authority grants. Plain neo_vm_create
 * deliberately leaves the platform unconfigured for embedding and parser tools. */
neo_status neo_vm_configure_platform(neo_vm *vm, const neo_platform_info *info);
/** Output is zeroed on failure; an unconfigured VM reports NEO_UNAVAILABLE. */
neo_status neo_vm_platform_info(const neo_vm *vm, neo_platform_info *out);
const char *neo_environment_name(neo_environment value);
const char *neo_virtualization_name(neo_virtualization value);

/** Selected host adapter entry point. Linux and Windows x86_64 report
 * HOSTED, their host OS, x86_64, and UNKNOWN virtualization. Bootstrap declares the enabled
 * provider mask. Unsupported build targets return NEO_UNSUPPORTED, never guess.
 * Adapter is linked into runners separately from the portable core library. */
neo_status neo_platform_native(unsigned services, neo_platform_info *out);

/** Host bootstrap interface, not directly callable by image code. No native
 * descriptor, pathname syntax, display API, or clock type crosses this boundary. */
typedef enum neo_standard_stream {
    NEO_STANDARD_INPUT, NEO_STANDARD_OUTPUT, NEO_STANDARD_ERROR
} neo_standard_stream;

typedef struct neo_platform_backend {
    /** Read the complete source into borrowed storage. Return NEO_LIMIT if it
     * exceeds capacity; do not truncate successfully. Set count only on success.
     * Source need not be terminated. The core checks NULs and parses inertly. */
    neo_status (*read_source)(void *context, const char *location, char *bytes,
                              size_t capacity, size_t *out_count);
    /** On success publish one resource via neo_stream_create/neo_window_create.
     * On failure free partial backend resources and leave *out NULL. These are
     * trusted host callbacks; they do not run neo behavior or retain arguments. */
    neo_status (*open_stream)(void *context, neo_vm *vm, const neo_capability *parent,
                              const char *name, neo_standard_stream stream,
                              const neo_capability **out);
    neo_status (*open_window)(void *context, neo_vm *vm, const neo_capability *parent,
                              const char *name, const char *title, size_t width,
                              size_t height, const neo_capability **out);
    neo_status (*wait)(void *context, uint32_t milliseconds);
    /** Optional immutable transport table; must outlive the VM. Trusted host
     * bootstrap only, not an ambient image socket-opening capability. */
    const neo_network_backend *network;
    /** Optional atomic source replacement; no partial destination on failure. */
    neo_status (*write_source)(void *context, const char *location, const char *text);
    /** Optional text-snapshot stream. Opening is an explicit host grant. */
    neo_status (*open_clipboard)(void *context, neo_vm *vm, const neo_capability *parent,
                                 const char *name, const neo_capability **out);
} neo_platform_backend;

typedef struct neo_platform_services {
    neo_platform_info info;
    neo_platform_backend backend;
    void *context;
} neo_platform_services;

/** Copies descriptor/table; context remains host-owned and must outlive the VM.
 * Enabled flags must match callbacks; INPUT_EVENTS annotates window support.
 * Configure once, before images. Descriptor-only configure remains available
 * for metadata-only embeddings, but cannot dispatch any host services. */
neo_status neo_vm_install_platform(neo_vm *vm, const neo_platform_services *services);
/** Selected adapter builds only the requested subset. Unsupported providers
 * fail explicitly. No streams/windows/files are opened by this factory. */
neo_status neo_platform_native_services(unsigned requested, neo_platform_services *out);

/** Synchronous bootstrap operations. Missing setup -> UNAVAILABLE, missing
 * provider -> UNSUPPORTED. Loading never runs behavior. Opening requires INSERT
 * on parent; returned resources still require an explicit connection grant. */
neo_status neo_platform_load_image(neo_vm *vm, const char *location,
                                   const neo_capability **out, neo_diagnostic *error);
neo_status neo_platform_open_stream(neo_vm *vm, const neo_capability *parent,
                                    const char *name, neo_standard_stream stream,
                                    const neo_capability **out);
neo_status neo_platform_open_window(neo_vm *vm, const neo_capability *parent,
                                    const char *name, const char *title,
                                    size_t width, size_t height, const neo_capability **out);
neo_status neo_platform_wait(neo_vm *vm, uint32_t milliseconds);
neo_status neo_platform_open_clipboard(neo_vm *vm, const neo_capability *parent,
                                       const char *name, const neo_capability **out);


/** Format first, then atomically replace a host file through the backend. */
neo_status neo_platform_save_image(neo_vm *vm, const neo_capability *root, const char *location);
/** Host-only text export through the same protected atomic file backend. */
neo_status neo_platform_write_text(neo_vm *vm, const char *location, const char *text);

/** Obtain the explicitly installed transport provider; no endpoint is opened.
 * Output is NULL on failure. Host must close its handles before provider teardown. */
neo_status neo_platform_network(neo_vm *vm, const neo_network_backend **out);

#endif
