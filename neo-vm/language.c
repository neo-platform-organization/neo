#include "depth.h"
const char *neo_language_validate(const neo_language_plugin *p) {
    if (!p || p->abi!=NEO_LANGUAGE_ABI || p->bytes<sizeof(*p)) { return "incompatible language plugin ABI"; }
    if (!p->neo_run) { return "incomplete language plugin interface"; }
    if (!p->neo_roundtrip) { return "incomplete language plugin interface"; }
    if (!p->neo_snapshot) { return "incomplete language plugin interface"; }
    if (!p->neo_session_program_config) { return "incomplete language plugin interface"; }
    if (!p->neo_session_entry) { return "incomplete language plugin interface"; }
    if (!p->neo_session_package_image) { return "incomplete language plugin interface"; }
    if (!p->neo_session_arguments) { return "incomplete language plugin interface"; }
    if (!p->neo_session_io) { return "incomplete language plugin interface"; }
    if (!p->neo_session_schedule) { return "incomplete language plugin interface"; }
    if (!p->neo_session_grant_buffer) { return "incomplete language plugin interface"; }
    if (!p->neo_session_object_package) { return "incomplete language plugin interface"; }
    if (!p->neo_session_clone) { return "incomplete language plugin interface"; }
    if (!p->neo_session_delete) { return "incomplete language plugin interface"; }
    if (!p->neo_session_open) { return "incomplete language plugin interface"; }
    if (!p->neo_session_close) { return "incomplete language plugin interface"; }
    if (!p->neo_session_grant_document) { return "incomplete language plugin interface"; }
    if (!p->neo_session_document_sink) { return "incomplete language plugin interface"; }
    if (!p->neo_session_authorize_import) { return "incomplete language plugin interface"; }
    if (!p->neo_session_grant_package_document) { return "incomplete language plugin interface"; }
    if (!p->neo_session_revision) { return "incomplete language plugin interface"; }
    if (!p->neo_session_send) { return "incomplete language plugin interface"; }
    if (!p->neo_session_send_value) { return "incomplete language plugin interface"; }
    if (!p->neo_session_send_args) { return "incomplete language plugin interface"; }
    if (!p->neo_session_receive_sequence) { return "incomplete language plugin interface"; }
    if (!p->neo_session_send_text) { return "incomplete language plugin interface"; }
    if (!p->neo_session_send_sequence_args) { return "incomplete language plugin interface"; }
    if (!p->neo_session_trace) { return "incomplete language plugin interface"; }
    if (!p->neo_session_reload) { return "incomplete language plugin interface"; }
    if (!p->neo_session_snapshot) { return "incomplete language plugin interface"; }
    if (!p->neo_session_weft) { return "incomplete language plugin interface"; }
    if (!p->neo_session_inspect) { return "incomplete language plugin interface"; }
    if (!p->neo_session_evaluate_knight) { return "incomplete language plugin interface"; }
    if (!p->neo_session_probe_knight) { return "incomplete language plugin interface"; }
    if (!p->neo_session_publish_knight) { return "incomplete language plugin interface"; }
    if (!p->neo_session_transform_knight) { return "incomplete language plugin interface"; }
    if (!p->neo_weft_lower) { return "incomplete language plugin interface"; }
    return NULL;
}
const char * neo_run(const char * a0, const neo_code_platform * a1, int32_t * a2) {
    const neo_language_plugin *p=neo_depth_language();
    return p ? p->neo_run(a0,a1,a2) : "language plugin unavailable";
}
const char * neo_roundtrip(const char * a0, const neo_code_platform * a1, int32_t * a2) {
    const neo_language_plugin *p=neo_depth_language();
    return p ? p->neo_roundtrip(a0,a1,a2) : "language plugin unavailable";
}
const char * neo_snapshot(const char * a0, const neo_code_platform * a1, char * a2, size_t a3) {
    const neo_language_plugin *p=neo_depth_language();
    return p ? p->neo_snapshot(a0,a1,a2,a3) : "language plugin unavailable";
}
const char * neo_session_program_config(const neo_session * a0, const char * a1, neo_program_config * a2) {
    const neo_language_plugin *p=neo_depth_language();
    return p ? p->neo_session_program_config(a0,a1,a2) : "language plugin unavailable";
}
void neo_session_entry(const neo_session * a0, char a1[64], char a2[64]) {
    const neo_language_plugin *p=neo_depth_language();
    if (p) { p->neo_session_entry(a0,a1,a2); }
}
const char * neo_session_package_image(const neo_session * a0, const char * a1, char * a2, size_t a3) {
    const neo_language_plugin *p=neo_depth_language();
    return p ? p->neo_session_package_image(a0,a1,a2,a3) : "language plugin unavailable";
}
const char * neo_session_arguments(neo_session * a0, const char * a1, const char *const * a2, size_t a3) {
    const neo_language_plugin *p=neo_depth_language();
    return p ? p->neo_session_arguments(a0,a1,a2,a3) : "language plugin unavailable";
}
void neo_session_io(neo_session * a0, neo_io_sink * a1, void * a2) {
    const neo_language_plugin *p=neo_depth_language();
    if (p) { p->neo_session_io(a0,a1,a2); }
}
void neo_session_schedule(neo_session * a0, neo_deferred_sink * a1, void * a2) {
    const neo_language_plugin *p=neo_depth_language();
    if (p) { p->neo_session_schedule(a0,a1,a2); }
}
const char * neo_session_grant_buffer(neo_session * a0, const char * a1, neo_buffer_sink * a2, void * a3) {
    const neo_language_plugin *p=neo_depth_language();
    return p ? p->neo_session_grant_buffer(a0,a1,a2,a3) : "language plugin unavailable";
}
const char * neo_session_object_package(const neo_session * a0, const char * a1, char a2[64]) {
    const neo_language_plugin *p=neo_depth_language();
    return p ? p->neo_session_object_package(a0,a1,a2) : "language plugin unavailable";
}
const char * neo_session_clone(neo_session ** a0, uint64_t a1, const char * a2, const char * a3) {
    const neo_language_plugin *p=neo_depth_language();
    return p ? p->neo_session_clone(a0,a1,a2,a3) : "language plugin unavailable";
}
const char * neo_session_delete(neo_session ** a0, uint64_t a1, const char * a2) {
    const neo_language_plugin *p=neo_depth_language();
    return p ? p->neo_session_delete(a0,a1,a2) : "language plugin unavailable";
}
const char * neo_session_open(const char * a0, neo_session ** a1) {
    const neo_language_plugin *p=neo_depth_language();
    return p ? p->neo_session_open(a0,a1) : "language plugin unavailable";
}
void neo_session_close(neo_session * a0) {
    const neo_language_plugin *p=neo_depth_language();
    if (p) { p->neo_session_close(a0); }
}
void neo_session_grant_document(neo_session * a0, int32_t * a1, unsigned a2) {
    const neo_language_plugin *p=neo_depth_language();
    if (p) { p->neo_session_grant_document(a0,a1,a2); }
}
void neo_session_document_sink(neo_session * a0, neo_document_sink * a1, void * a2) {
    const neo_language_plugin *p=neo_depth_language();
    if (p) { p->neo_session_document_sink(a0,a1,a2); }
}
const char * neo_session_authorize_import(neo_session * a0, const char * a1, const char * a2, bool a3) {
    const neo_language_plugin *p=neo_depth_language();
    return p ? p->neo_session_authorize_import(a0,a1,a2,a3) : "language plugin unavailable";
}
const char * neo_session_grant_package_document(neo_session * a0, const char * a1, int32_t * a2, unsigned a3) {
    const neo_language_plugin *p=neo_depth_language();
    return p ? p->neo_session_grant_package_document(a0,a1,a2,a3) : "language plugin unavailable";
}
uint64_t neo_session_revision(const neo_session * a0) {
    const neo_language_plugin *p=neo_depth_language();
    return p ? p->neo_session_revision(a0) : 0;
}
const char * neo_session_send(neo_session * a0, const char * a1, const char * a2, const neo_code_platform * a3, int32_t * a4) {
    const neo_language_plugin *p=neo_depth_language();
    return p ? p->neo_session_send(a0,a1,a2,a3,a4) : "language plugin unavailable";
}
const char * neo_session_send_value(neo_session * a0, const char * a1, const char * a2, int32_t a3, const neo_code_platform * a4, int32_t * a5) {
    const neo_language_plugin *p=neo_depth_language();
    return p ? p->neo_session_send_value(a0,a1,a2,a3,a4,a5) : "language plugin unavailable";
}
const char * neo_session_send_args(neo_session * a0, const char * a1, const char * a2, const int32_t * a3, size_t a4, const neo_code_platform * a5, int32_t * a6) {
    const neo_language_plugin *p=neo_depth_language();
    return p ? p->neo_session_send_args(a0,a1,a2,a3,a4,a5,a6) : "language plugin unavailable";
}
const char * neo_session_receive_sequence(neo_session * a0, const char * a1, const char * a2, const int32_t * a3, size_t a4, const neo_code_platform * a5, char a6, char a7[NEO_TEXT_RESULT_CAPACITY]) {
    const neo_language_plugin *p=neo_depth_language();
    return p ? p->neo_session_receive_sequence(a0,a1,a2,a3,a4,a5,a6,a7) : "language plugin unavailable";
}
const char * neo_session_send_text(neo_session * a0, const char * a1, const char * a2, const char * a3, const neo_code_platform * a4, int32_t * a5) {
    const neo_language_plugin *p=neo_depth_language();
    return p ? p->neo_session_send_text(a0,a1,a2,a3,a4,a5) : "language plugin unavailable";
}
const char * neo_session_send_sequence_args(neo_session * a0, const char * a1, const char * a2, const char * a3, const int32_t * a4, size_t a5, const neo_code_platform * a6, int32_t * a7) {
    const neo_language_plugin *p=neo_depth_language();
    return p ? p->neo_session_send_sequence_args(a0,a1,a2,a3,a4,a5,a6,a7) : "language plugin unavailable";
}
const char * neo_session_trace(neo_session * a0, const char * a1, const char * a2, const int32_t * a3, size_t a4, const neo_code_platform * a5, int32_t * a6, neo_trace_sink * a7, void * a8) {
    const neo_language_plugin *p=neo_depth_language();
    return p ? p->neo_session_trace(a0,a1,a2,a3,a4,a5,a6,a7,a8) : "language plugin unavailable";
}
const char * neo_session_reload(neo_session ** a0, uint64_t a1, const char * a2) {
    const neo_language_plugin *p=neo_depth_language();
    return p ? p->neo_session_reload(a0,a1,a2) : "language plugin unavailable";
}
const char * neo_session_snapshot(const neo_session * a0, char * a1, size_t a2) {
    const neo_language_plugin *p=neo_depth_language();
    return p ? p->neo_session_snapshot(a0,a1,a2) : "language plugin unavailable";
}
const char * neo_session_weft(const neo_session * a0, char * a1, size_t a2) {
    const neo_language_plugin *p=neo_depth_language();
    return p ? p->neo_session_weft(a0,a1,a2) : "language plugin unavailable";
}
const char * neo_session_inspect(const neo_session * a0, const char * a1, const char * a2, neo_inspect_view a3, char * a4, size_t a5) {
    const neo_language_plugin *p=neo_depth_language();
    return p ? p->neo_session_inspect(a0,a1,a2,a3,a4,a5) : "language plugin unavailable";
}
const char * neo_session_evaluate_knight(const neo_session * a0, uint64_t a1, const char * a2, const char * a3, int32_t a4, const int32_t * a5, size_t a6, const neo_code_platform * a7, int32_t * a8, int32_t * a9) {
    const neo_language_plugin *p=neo_depth_language();
    return p ? p->neo_session_evaluate_knight(a0,a1,a2,a3,a4,a5,a6,a7,a8,a9) : "language plugin unavailable";
}
const char * neo_session_probe_knight(const neo_session * a0, uint64_t a1, const char * a2, const char * a3, const char * a4, const int32_t * a5, size_t a6, const neo_code_platform * a7, int32_t * a8) {
    const neo_language_plugin *p=neo_depth_language();
    return p ? p->neo_session_probe_knight(a0,a1,a2,a3,a4,a5,a6,a7,a8) : "language plugin unavailable";
}
const char * neo_session_publish_knight(neo_session ** a0, uint64_t a1, const char * a2, const char * a3, const char * a4) {
    const neo_language_plugin *p=neo_depth_language();
    return p ? p->neo_session_publish_knight(a0,a1,a2,a3,a4) : "language plugin unavailable";
}
const char * neo_session_transform_knight(const neo_session * a0, uint64_t a1, const char * a2, const char * a3, const char * a4, const char * a5, char * a6, size_t a7) {
    const neo_language_plugin *p=neo_depth_language();
    return p ? p->neo_session_transform_knight(a0,a1,a2,a3,a4,a5,a6,a7) : "language plugin unavailable";
}
const char * neo_weft_lower(const char * a0, char * a1, size_t a2) {
    const neo_language_plugin *p=neo_depth_language();
    return p ? p->neo_weft_lower(a0,a1,a2) : "language plugin unavailable";
}
