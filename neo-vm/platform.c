#include "depth.h"

static const neo_depth_plugin *neo_depth_active;
const char *neo_depth_validate(const neo_depth_plugin *plugin) {
    if (!plugin || plugin->abi!=NEO_DEPTH_ABI || plugin->bytes<sizeof(*plugin)) { return "incompatible depth plugin ABI"; }
    if (!plugin->name || !*plugin->name || !plugin->allocate || !plugin->malloc_bytes || !plugin->calloc_bytes ||
        !plugin->realloc_bytes || !plugin->free_bytes || !plugin->execute || !plugin->worker || !plugin->begin ||
        !plugin->end || !plugin->worker_id || !plugin->events || !plugin->services || !plugin->selftest ||
        !plugin->console_open || !plugin->console_close || !plugin->read_line) {
        return "incomplete depth plugin interface";
    }
    return NULL;
}
const char *neo_depth_install(const neo_depth_plugin *plugin) {
    const char *failure=neo_depth_validate(plugin);
    if (failure) { return failure; }
    if (neo_depth_active && neo_depth_active!=plugin) { return "depth plugin already installed"; }
    neo_depth_active=plugin; return NULL;
}
static const neo_depth_plugin *neo_depth_current(void) {
    return neo_depth_active;
}
void *neo_platform_allocate(size_t bytes, size_t alignment) {
    const neo_depth_plugin *p=neo_depth_current(); return p ? p->allocate(bytes,alignment) : NULL;
}
void *neo_platform_malloc(size_t bytes) {
    const neo_depth_plugin *p=neo_depth_current(); return p ? p->malloc_bytes(bytes) : NULL;
}
void *neo_platform_calloc(size_t count, size_t bytes) {
    const neo_depth_plugin *p=neo_depth_current(); return p ? p->calloc_bytes(count,bytes) : NULL;
}
void *neo_platform_realloc(void *memory, size_t bytes) {
    const neo_depth_plugin *p=neo_depth_current(); return p ? p->realloc_bytes(memory,bytes) : NULL;
}
void neo_platform_free(void *memory) { const neo_depth_plugin *p=neo_depth_current(); if (p) { p->free_bytes(memory); } }
const char *neo_native_execute(const unsigned char *code, size_t length, int32_t *slots, int64_t *result) {
    const neo_depth_plugin *p=neo_depth_current();
    return p ? p->execute(code,length,slots,result) : "depth execution unavailable";
}
int neo_native_worker(const char *text) { const neo_depth_plugin *p=neo_depth_current(); return p ? p->worker(text) : 1; }
void neo_native_begin(void) { const neo_depth_plugin *p=neo_depth_current(); if (p) { p->begin(); } }
void neo_native_end(void) { const neo_depth_plugin *p=neo_depth_current(); if (p) { p->end(); } }
uint32_t neo_native_worker_id(void) { const neo_depth_plugin *p=neo_depth_current(); return p ? p->worker_id() : 0; }
neo_event_platform neo_native_event_platform(void) {
    const neo_depth_plugin *p=neo_depth_current(); return p ? p->events() : (neo_event_platform){0};
}
neo_status neo_platform_services(bool visible, neo_services **services) {
    const neo_depth_plugin *p=neo_depth_current(); *services=NULL;
    return p ? p->services(visible,services) : NEO_UNSUPPORTED;
}
bool neo_platform_services_selftest(void) { const neo_depth_plugin *p=neo_depth_current(); return p && p->selftest(); }
neo_status neo_console_open(neo_console **console) {
    const neo_depth_plugin *p=neo_depth_current(); *console=NULL; return p ? p->console_open(console) : NEO_UNSUPPORTED;
}
void neo_console_close(neo_console *console) { const neo_depth_plugin *p=neo_depth_current(); if (p) { p->console_close(console); } }
const char *neo_platform_read_line(neo_console *console, char *line, size_t capacity, bool *received,
                                  neo_platform_idle *idle, void *context) {
    const neo_depth_plugin *p=neo_depth_current(); *received=false;
    return p ? p->read_line(console,line,capacity,received,idle,context) : "depth console unavailable";
}
