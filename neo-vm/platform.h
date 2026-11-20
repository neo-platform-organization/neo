#ifndef NEO_PLATFORM_H
#define NEO_PLATFORM_H
#include "jit.h"
#include "events.h"
#include "services.h"

const char *neo_native_execute(const unsigned char *code, size_t length,
                               int32_t *slots, int64_t *result);
int neo_native_worker(const char *handle_text);
void neo_native_begin(void);
void neo_native_end(void);
uint32_t neo_native_worker_id(void);
neo_event_platform neo_native_event_platform(void);
/* Console transport pumps idle work while waiting. EOF is not an error.
   The callback must not start another read. Its context is borrowed. */
typedef void neo_platform_idle(void *);
typedef struct neo_console neo_console;
neo_status neo_console_open(neo_console **);
void neo_console_close(neo_console *);
const char *neo_platform_read_line(neo_console *, char *, size_t, bool *, neo_platform_idle *, void *);
neo_status neo_platform_services(bool, neo_services **);
bool neo_platform_services_selftest(void);
void *neo_platform_allocate(size_t, size_t);
void *neo_platform_malloc(size_t);
void *neo_platform_calloc(size_t, size_t);
void *neo_platform_realloc(void *, size_t);
void neo_platform_free(void *);
#endif
