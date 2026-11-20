#ifndef NEO_EVENTS_H
#define NEO_EVENTS_H
#include "jit.h"

enum { NEO_EVENT_LIMIT=32 };
typedef struct {
    uint64_t (*clock_ms)(void);
    void (*wait_ms)(unsigned);
    const char *(*read_start)(const char *, void **);
    const char *(*read_poll)(void *, bool *, char *, size_t);
    void (*read_close)(void *);
} neo_event_platform;

typedef struct {
    bool active;
    uint64_t sequence, due, revision;
    char id[64], object[64], selector[64];
    int32_t arguments[NEO_MAX_ARGUMENTS];
    size_t count;
    void *read;
} neo_event;
typedef struct {
    neo_event items[NEO_EVENT_LIMIT];
    uint64_t sequence;
    neo_event_platform platform;
    /* Borrowed completion data is valid only during the call. Do not reenter. */
    void (*complete)(void *, const char *, const char *, int32_t);
    void *completion_context;
} neo_events;

void neo_events_close(neo_events *);
const char *neo_events_schedule(void *, uint64_t, const neo_deferred *, size_t);
void neo_events_poll(neo_events *, neo_session *, const neo_code_platform *);
const char *neo_events_command(neo_events *, neo_session *, const neo_code_platform *, char *);
#endif
