#ifndef NEO_LANGUAGE_H
#define NEO_LANGUAGE_H
#include "jit.h"
#include "weft.h"
/* Surface-owned versioned language contract. Implementations are trusted native plugins. */
enum { NEO_LANGUAGE_ABI=1 };
typedef struct {
    uint32_t abi; size_t bytes;
    const char * (*neo_run)(const char *source, const neo_code_platform *platform, int32_t *result);
    const char * (*neo_roundtrip)(const char *source, const neo_code_platform *platform, int32_t *result);
    const char * (*neo_snapshot)(const char *source, const neo_code_platform *platform, char *output, size_t capacity);
    const char * (*neo_session_program_config)(const neo_session *, const char *, neo_program_config *);
    void (*neo_session_entry)(const neo_session *, char [64], char [64]);
    const char * (*neo_session_package_image)(const neo_session *, const char *, char *, size_t);
    const char * (*neo_session_arguments)(neo_session *, const char *, const char *const *, size_t);
    void (*neo_session_io)(neo_session *, neo_io_sink *, void *);
    void (*neo_session_schedule)(neo_session *, neo_deferred_sink *, void *);
    const char * (*neo_session_grant_buffer)(neo_session *, const char *, neo_buffer_sink *, void *);
    const char * (*neo_session_object_package)(const neo_session *, const char *, char [64]);
    const char * (*neo_session_clone)(neo_session **session, uint64_t revision, const char *prototype, const char *name);
    const char * (*neo_session_delete)(neo_session **session, uint64_t revision, const char *name);
    const char * (*neo_session_open)(const char *source, neo_session **session);
    void (*neo_session_close)(neo_session *session);
    void (*neo_session_grant_document)(neo_session *session, int32_t *document, unsigned rights);
    void (*neo_session_document_sink)(neo_session *, neo_document_sink *, void *);
    const char * (*neo_session_authorize_import)(neo_session *session, const char *object, const char *alias, bool allow);
    const char * (*neo_session_grant_package_document)(neo_session *session, const char *package, int32_t *document, unsigned rights);
    uint64_t (*neo_session_revision)(const neo_session *session);
    const char * (*neo_session_send)(neo_session *session, const char *object, const char *selector, const neo_code_platform *platform, int32_t *result);
    const char * (*neo_session_send_value)(neo_session *session, const char *object, const char *selector, int32_t argument, const neo_code_platform *platform, int32_t *result);
    const char * (*neo_session_send_args)(neo_session *session, const char *object, const char *selector, const int32_t *arguments, size_t count, const neo_code_platform *platform, int32_t *result);
    const char * (*neo_session_receive_sequence)(neo_session *session, const char *object, const char *selector, const int32_t *arguments, size_t count, const neo_code_platform *platform, char kind, char result[NEO_TEXT_RESULT_CAPACITY]);
    const char * (*neo_session_send_text)(neo_session *session, const char *object, const char *selector, const char *literal, const neo_code_platform *platform, int32_t *result);
    const char * (*neo_session_send_sequence_args)(neo_session *, const char *, const char *, const char *, const int32_t *, size_t, const neo_code_platform *, int32_t *);
    const char * (*neo_session_trace)(neo_session *session, const char *object, const char *selector, const int32_t *arguments, size_t count, const neo_code_platform *platform, int32_t *result, neo_trace_sink *sink, void *context);
    const char * (*neo_session_reload)(neo_session **session, uint64_t expected, const char *source);
    const char * (*neo_session_snapshot)(const neo_session *session, char *output, size_t capacity);
    const char * (*neo_session_weft)(const neo_session *session, char *text, size_t capacity);
    const char * (*neo_session_inspect)(const neo_session *session, const char *object, const char *member, neo_inspect_view view, char *text, size_t capacity);
    const char * (*neo_session_evaluate_knight)(const neo_session *session, uint64_t revision, const char *object, const char *handler, int32_t state, const int32_t *arguments, size_t count, const neo_code_platform *platform, int32_t *next, int32_t *result);
    const char * (*neo_session_probe_knight)(const neo_session *session, uint64_t revision, const char *object, const char *function, const char *source, const int32_t *arguments, size_t count, const neo_code_platform *platform, int32_t *result);
    const char * (*neo_session_publish_knight)(neo_session **session, uint64_t revision, const char *object, const char *function, const char *source);
    const char * (*neo_session_transform_knight)(const neo_session *session, uint64_t revision, const char *object, const char *function, const char *from, const char *to, char *output, size_t capacity);
    const char * (*neo_weft_lower)(const char *source, char *warp, size_t capacity);
} neo_language_plugin;
const char *neo_language_validate(const neo_language_plugin *);
#endif
