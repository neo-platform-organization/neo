#ifndef NEO_JIT_H
#define NEO_JIT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { NEO_DEFERRED_LIMIT=8, NEO_DEFERRED_WORDS=1+NEO_DEFERRED_LIMIT*8,
       NEO_NATIVE_CODE_LIMIT=65536, NEO_TRACE_LIMIT=512, NEO_MAX_ARGUMENTS=4, NEO_GRAPH_CELLS=2048,
       NEO_TEXT_WORDS=80, NEO_TEXT_FRAMES=48, NEO_PROGRAM_ARGUMENTS=16,
       NEO_NATIVE_SLOTS=NEO_GRAPH_CELLS+4+NEO_MAX_ARGUMENTS+
           2*NEO_TRACE_LIMIT+(NEO_GRAPH_CELLS+NEO_TEXT_FRAMES*(NEO_MAX_ARGUMENTS+1)+1+
           NEO_DEFERRED_LIMIT+NEO_PROGRAM_ARGUMENTS)*NEO_TEXT_WORDS+NEO_DEFERRED_WORDS };
typedef struct {
    const char *(*execute)(const unsigned char *, size_t, int32_t *, int64_t *);
} neo_code_platform;
/* Source is a borrowed NUL-terminated Warp document. */
const char *neo_run(const char *source, const neo_code_platform *platform, int32_t *result);
const char *neo_roundtrip(const char *source, const neo_code_platform *platform, int32_t *result);
const char *neo_snapshot(const char *source, const neo_code_platform *platform,
                         char *output, size_t capacity);
typedef struct neo_session neo_session;
/* Inert launcher metadata, read from ordinary slots on a program object.
   This describes intent; it neither executes code nor grants resources. */
typedef enum { NEO_PRESENT_INTERNAL, NEO_PRESENT_HOST, NEO_PRESENT_HEADLESS } neo_program_presentation;
typedef struct {
    char object[64], package[64], selector[64];
    neo_program_presentation presentation;
    bool multiple_instances;
} neo_program_config;
const char *neo_session_program_config(const neo_session *, const char *, neo_program_config *);
void neo_session_entry(const neo_session *, char [64], char [64]);
/* Export the selected package into a minimal image with the declared entry.
   No dependency closure or host authority is silently imported. */
const char *neo_session_package_image(const neo_session *, const char *, char *, size_t);
/* Launch arguments are inert quoted text tokens. They are visible only to the
   named package, never serialized, and borrowed only during this call. */
const char *neo_session_arguments(neo_session *, const char *, const char *const *, size_t);
typedef struct {
    const char *package, *channel, *object, *selector;
    bool write;
    size_t length;
    unsigned char bytes[64];
} neo_io_message;
/* Accept the whole request batch without doing external I/O, or change nothing.
   Requests/payloads are borrowed. No callback may reenter the session. */
typedef const char *neo_io_sink(void *, uint64_t, const neo_io_message *, size_t);
void neo_session_io(neo_session *, neo_io_sink *, void *);
typedef struct {
    const char *object, *selector;
    uint32_t delay_ms;
    int32_t arguments[NEO_MAX_ARGUMENTS];
    size_t count;
} neo_deferred;
/* Host sink accepts the entire batch or leaves its queue unchanged.
   Borrowed requests are valid only during the callback. Do not reenter the session. */
typedef const char *neo_deferred_sink(void *, uint64_t, const neo_deferred *, size_t);
void neo_session_schedule(neo_session *, neo_deferred_sink *, void *);
typedef struct { bool present; int32_t offset, count, rgba; } neo_buffer_command;
typedef const char *neo_buffer_sink(void *, const neo_buffer_command *, size_t);
const char *neo_session_grant_buffer(neo_session *, const char *, neo_buffer_sink *, void *);
const char *neo_session_object_package(const neo_session *, const char *, char [64]);
const char *neo_session_clone(neo_session **session, uint64_t revision,
                               const char *prototype, const char *name);
const char *neo_session_delete(neo_session **session, uint64_t revision, const char *name);
const char *neo_session_open(const char *source, neo_session **session);
void neo_session_close(neo_session *session);
enum { NEO_DOCUMENT_READ=1, NEO_DOCUMENT_WRITE=2 };
/* Trusted host only. Storage must outlive the grant. NULL revokes it. */
void neo_session_grant_document(neo_session *session, int32_t *document, unsigned rights);
/* Optional durable commit. Runs before graph publication; do not reenter.
   A failure must leave the previous durable value intact or report uncertainty. */
typedef const char *neo_document_sink(void *, int32_t);
void neo_session_document_sink(neo_session *, neo_document_sink *, void *);
const char *neo_session_authorize_import(neo_session *session, const char *object,
                                          const char *alias, bool allow);
const char *neo_session_grant_package_document(neo_session *session, const char *package,
                                               int32_t *document, unsigned rights);
uint64_t neo_session_revision(const neo_session *session);
const char *neo_session_send(neo_session *session, const char *object, const char *selector,
                             const neo_code_platform *platform, int32_t *result);
const char *neo_session_send_value(neo_session *session, const char *object, const char *selector,
                                   int32_t argument, const neo_code_platform *platform,
                                   int32_t *result);
const char *neo_session_send_args(neo_session *session, const char *object, const char *selector,
                                  const int32_t *arguments, size_t count,
                                  const neo_code_platform *platform, int32_t *result);
enum { NEO_TEXT_RESULT_CAPACITY=64 };
const char *neo_session_receive_sequence(neo_session *session, const char *object, const char *selector,
                                     const int32_t *arguments, size_t count,
                                     const neo_code_platform *platform,
                                     char kind, char result[NEO_TEXT_RESULT_CAPACITY]);
const char *neo_session_send_text(neo_session *session, const char *object, const char *selector,
                                  const char *literal, const neo_code_platform *platform,
                                  int32_t *result);
/* First argument is a text or byte literal; up to three integer arguments follow. */
const char *neo_session_send_sequence_args(neo_session *, const char *, const char *, const char *,
    const int32_t *, size_t, const neo_code_platform *, int32_t *);
typedef enum { NEO_TRACE_SEND, NEO_TRACE_KNIGHT, NEO_TRACE_WRITE, NEO_TRACE_LIMIT_REACHED } neo_trace_kind;
typedef struct {
    neo_trace_kind kind;
    const char *sender, *receiver, *name, *owner;
    int32_t value;
    bool committed;
} neo_trace_event;
typedef void neo_trace_sink(const neo_trace_event *, void *);
/* Trusted inspector. The sink borrows names and must not reenter the session. */
const char *neo_session_trace(neo_session *session, const char *object, const char *selector,
                              const int32_t *arguments, size_t count, const neo_code_platform *platform,
                              int32_t *result, neo_trace_sink *sink, void *context);
const char *neo_session_reload(neo_session **session, uint64_t expected, const char *source);
const char *neo_session_snapshot(const neo_session *session, char *output, size_t capacity);
const char *neo_session_weft(const neo_session *session, char *text, size_t capacity);
typedef enum { NEO_INSPECT_WARP, NEO_INSPECT_OBJECT, NEO_INSPECT_KNIGHT } neo_inspect_view;
/* Trusted operator view. Targets are resolved in the session's current revision. */
const char *neo_session_inspect(const neo_session *session, const char *object,
                                const char *member, neo_inspect_view view,
                                char *text, size_t capacity);
const char *neo_session_evaluate_knight(const neo_session *session, uint64_t revision,
                                        const char *object, const char *handler, int32_t state,
                                        const int32_t *arguments, size_t count,
                                        const neo_code_platform *platform, int32_t *next,
                                        int32_t *result);
/* NULL source runs the original body. A candidate borrows its parameter names. */
const char *neo_session_probe_knight(const neo_session *session, uint64_t revision,
                                     const char *object, const char *function, const char *source,
                                     const int32_t *arguments, size_t count,
                                     const neo_code_platform *platform, int32_t *result);
const char *neo_session_publish_knight(neo_session **session, uint64_t revision,
                                       const char *object, const char *function,
                                       const char *source);
const char *neo_session_transform_knight(const neo_session *session, uint64_t revision,
                                         const char *object, const char *function,
                                         const char *from, const char *to,
                                         char *output, size_t capacity);
#endif
