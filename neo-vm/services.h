#ifndef NEO_SERVICES_H
#define NEO_SERVICES_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    NEO_OK, NEO_PENDING, NEO_UNSUPPORTED, NEO_DENIED, NEO_CLOSED,
    NEO_EXHAUSTED, NEO_CANCELLED, NEO_TIMEOUT, NEO_EOF, NEO_CONFLICT,
    NEO_INVALID, NEO_IO_ERROR, NEO_DEVICE_LOST, NEO_TRUNCATED, NEO_UNCERTAIN
} neo_status;
const char *neo_status_name(neo_status);
enum { NEO_SERVICE_RESOURCES=64, NEO_SERVICE_REQUESTS=32,
       NEO_SERVICE_BYTES=65536, NEO_SERVICE_EVENTS=128,
       NEO_RIGHT_READ=1, NEO_RIGHT_WRITE=2, NEO_RIGHT_ACCEPT=4,
       NEO_RIGHT_PRESENT=8, NEO_RIGHT_INPUT=16 };
typedef enum { NEO_RESOURCE_STREAM=1, NEO_RESOURCE_SURFACE, NEO_RESOURCE_ROOT,
               NEO_RESOURCE_CONNECTION, NEO_RESOURCE_LISTENER, NEO_RESOURCE_DATAGRAM,
               NEO_RESOURCE_AUDIO, NEO_RESOURCE_CLIPBOARD } neo_resource_kind;
typedef struct { uint64_t owner, generation; uint32_t slot, kind; } neo_handle;
typedef enum { NEO_INPUT_KEY, NEO_INPUT_TEXT, NEO_INPUT_POINTER, NEO_INPUT_BUTTON,
               NEO_INPUT_WHEEL, NEO_INPUT_FOCUS, NEO_INPUT_RESIZE, NEO_INPUT_CLOSE } neo_input_kind;
typedef struct {
    neo_input_kind kind;
    neo_handle surface;
    uint64_t time_ns;
    int32_t code, modifiers;
    bool pressed;
    double x, y, dx, dy;
    uint32_t width, height;
    double scale;
    char text[32];
} neo_input;
typedef struct {
    neo_handle surface;
    uint64_t generation;
    uint32_t width, height;
    size_t stride;
    unsigned char *pixels;
} neo_frame;
typedef enum { NEO_IO_READ, NEO_IO_WRITE, NEO_IO_ACCEPT, NEO_IO_STORE_READ, NEO_IO_STORE_REPLACE, NEO_IO_STORE_DELETE } neo_io_kind;
typedef struct {
    uint64_t id;
    neo_status status;
    size_t length;
    const unsigned char *bytes;
    neo_handle accepted;
    uint64_t revision;
    size_t required_length;
} neo_completion;
typedef struct neo_services neo_services;
typedef struct {
    bool host_windows, input, surfaces, storage, network, entropy, native_isolation;
} neo_capabilities;
typedef struct { uint32_t page_size, processors, word_bits; uint64_t clock_resolution_ns; } neo_machine;
typedef enum { NEO_RENDER_AUTO, NEO_RENDER_SOFTWARE, NEO_RENDER_OPENGL } neo_renderer;
typedef struct { uint32_t rate, channels; bool capture; } neo_audio_format;
typedef struct { uint64_t frames, starvations; } neo_audio_stats;

/* Backend objects and tickets never cross this host-only boundary into an image.
   step() is nonblocking; cancel() must stop access before returning. */
typedef struct {
    uint64_t (*clock_ns)(void *);
    void (*wait)(void *, uint32_t);
    void (*wake)(void *);
    neo_status (*machine)(void *, neo_machine *);
    neo_status (*entropy)(void *, void *, size_t);
    void (*close)(void *, void *, neo_resource_kind);
    void (*bind)(void *, void *, neo_handle);
    neo_status (*step)(void *, void *, neo_io_kind, void **, unsigned char *, size_t, size_t *, void **);
    void (*cancel)(void *, void *);
    neo_status (*surface_open)(void *, uint32_t, uint32_t, void **);
    neo_status (*present)(void *, void *, const neo_frame *);
    neo_status (*input)(void *, neo_input *);
    neo_status (*root_open)(void *, const char *, void **);
    neo_status (*store_read)(void *, void *, const char *, void *, size_t, size_t *, uint64_t *);
    neo_status (*store_replace)(void *, void *, const char *, uint64_t, const void *, size_t, uint64_t *);
    neo_status (*store_list)(void *, void *, char *, size_t);
    neo_status (*store_delete)(void *, void *, const char *, uint64_t, uint64_t *);
    neo_status (*store_step)(void *, void *, neo_io_kind, const char *, uint64_t, void **, unsigned char *, size_t, size_t *, uint64_t *);
    neo_status (*endpoint)(void *, neo_resource_kind, bool, uint16_t *, void **);
    neo_status (*standard)(void *, unsigned, void **, bool *, uint32_t *, uint32_t *);
    neo_status (*renderer)(void *, neo_renderer);
    neo_status (*renderer_name)(void *, void *, char *, size_t);
    neo_status (*audio)(void *, const neo_audio_format *, void **);
    neo_status (*audio_stats)(void *, void *, neo_audio_stats *);
    neo_status (*clipboard)(void *, void **);
    neo_status (*position)(void *, void *, int32_t, int32_t);
    void (*destroy)(void *);
} neo_service_backend;

/* The caller owns the instance. Backend context is transferred on successful
   creation and destroyed on close. Calls are serialized by its owner; only
   wake may run concurrently with wait. Times/deadlines use monotonic ns.
   Handles and trusted resource constructors are host APIs, not image literals. */
neo_status neo_services_open(const neo_service_backend *, void *, neo_capabilities, neo_services **);
void neo_services_close(neo_services *);
neo_capabilities neo_services_capabilities(const neo_services *);
uint64_t neo_services_clock(neo_services *);
neo_status neo_services_entropy(neo_services *, void *, size_t);
neo_status neo_services_memory_stream(neo_services *, unsigned, const void *, size_t, neo_handle *);
neo_status neo_services_adopt(neo_services *, neo_resource_kind, unsigned, void *, neo_handle *);
neo_status neo_services_revoke(neo_services *, neo_handle);
neo_status neo_services_submit(neo_services *, neo_handle, neo_io_kind, const void *, size_t, uint64_t, uint64_t *);
neo_status neo_services_cancel(neo_services *, uint64_t);
void neo_services_poll(neo_services *);
void neo_services_wait(neo_services *, uint32_t);
void neo_services_wake(neo_services *);
neo_status neo_services_machine(neo_services *, neo_machine *);
neo_status neo_services_renderer(neo_services *, neo_renderer);
neo_status neo_services_renderer_name(neo_services *, neo_handle, char *, size_t);
/* Audio buffers contain interleaved signed 16-bit little-endian frames. */
neo_status neo_services_audio(neo_services *, neo_audio_format, neo_handle *);
neo_status neo_services_audio_stats(neo_services *, neo_handle, neo_audio_stats *);
neo_status neo_services_clipboard(neo_services *, unsigned, neo_handle *);
neo_status neo_services_position(neo_services *, neo_handle, int32_t, int32_t);
/* A completion retains its buffer until release. No callback may reenter.
   Shutdown reports every not-yet-reported completion through the supplied sink. */
neo_status neo_services_next(neo_services *, neo_completion *);
neo_status neo_services_release(neo_services *, uint64_t);
void neo_services_shutdown(neo_services *, void (*)(void *, const neo_completion *), void *);
neo_status neo_services_surface(neo_services *, uint32_t, uint32_t, neo_handle *);
neo_status neo_services_resize(neo_services *, neo_handle, uint32_t, uint32_t);
neo_status neo_services_acquire(neo_services *, neo_handle, neo_frame *);
/* RGBA8, straight alpha, sRGB, top-left origin. Frame lease remains owned by
   the instance; resize is rejected until it is presented or abandoned. */
neo_status neo_services_present(neo_services *, neo_frame *);
neo_status neo_services_abandon(neo_services *, neo_frame *);
neo_status neo_services_capture(neo_services *, neo_handle, void *, size_t, size_t *);
neo_status neo_services_push_input(neo_services *, const neo_input *);
neo_status neo_services_input(neo_services *, neo_input *);
neo_status neo_services_root(neo_services *, const char *, unsigned, neo_handle *);
/* The store is an opaque key/byte store, not the image filesystem schema.
   Keys are single names. Replacement uses a provider revision under one lock. */
neo_status neo_services_store_read(neo_services *, neo_handle, const char *, void *, size_t, size_t *, uint64_t *);
neo_status neo_services_store_replace(neo_services *, neo_handle, const char *, uint64_t, const void *, size_t, uint64_t *);
neo_status neo_services_store_list(neo_services *, neo_handle, char *, size_t);
neo_status neo_services_store_delete(neo_services *, neo_handle, const char *, uint64_t, uint64_t *);
neo_status neo_services_store_submit(neo_services *, neo_handle, const char *, neo_io_kind,
                                      uint64_t, const void *, size_t, uint64_t, uint64_t *);
/* Initial network grant is strictly IPv4 loopback. No DNS or ambient peers. */
neo_status neo_services_endpoint(neo_services *, neo_resource_kind, bool, uint16_t *, unsigned, neo_handle *);
neo_status neo_services_standard(neo_services *, unsigned, neo_handle *, bool *, uint32_t *, uint32_t *);
neo_status neo_services_headless(neo_services **);
neo_status neo_services_advance(neo_services *, uint64_t);
typedef struct {
    uint64_t ready_ns;
    neo_io_kind operation;
    neo_status status;
    size_t length;
    unsigned char bytes[256];
} neo_script_step;
neo_status neo_services_script_stream(neo_services *, const neo_script_step *, size_t, unsigned, neo_handle *);
bool neo_services_selftest(void);
bool neo_optional_services_selftest(void);
#endif
