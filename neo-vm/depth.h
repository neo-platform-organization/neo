#ifndef NEO_DEPTH_H
#define NEO_DEPTH_H
#include "platform.h"
#include "language.h"

/* Native, trusted plugin ABI. No allocator, page, clock, OS or device SDK
   implementation belongs in the surface VM. Structures must stay valid for
   the entire process; hot replacement while resources exist is unsupported. */
enum { NEO_DEPTH_ABI=1 };
typedef struct {
    uint32_t abi;
    size_t bytes;
    const char *name;
    void *(*allocate)(size_t, size_t);
    void *(*malloc_bytes)(size_t);
    void *(*calloc_bytes)(size_t, size_t);
    void *(*realloc_bytes)(void *, size_t);
    void (*free_bytes)(void *);
    const char *(*execute)(const unsigned char *, size_t, int32_t *, int64_t *);
    int (*worker)(const char *);
    void (*begin)(void);
    void (*end)(void);
    uint32_t (*worker_id)(void);
    neo_event_platform (*events)(void);
    neo_status (*services)(bool, neo_services **);
    bool (*selftest)(void);
    neo_status (*console_open)(neo_console **);
    void (*console_close)(neo_console *);
    const char *(*read_line)(neo_console *, char *, size_t, bool *, neo_platform_idle *, void *);
    int (*run)(int, char **);
    const neo_language_plugin *language;
} neo_depth_plugin;

/* Selected by the depth repository at link time, including standalone boot.
   A dynamic loader can provide this later without changing language semantics. */
const neo_depth_plugin *neo_depth_provider(void);
const char *neo_depth_validate(const neo_depth_plugin *);
const char *neo_depth_install(const neo_depth_plugin *);
const neo_language_plugin *neo_depth_language(void);
#endif
