#include "events.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool neo_event_number(const char *input, uint64_t *number) {
    if (!input || *input<'0' || *input>'9') { return false; }
    errno=0;
    char *end;
    uintmax_t value=strtoumax(input,&end,10);
    if (errno || *end || value>UINT64_MAX) { return false; }
    *number=(uint64_t)value;
    return true;
}

static void neo_event_close(neo_events *events, neo_event *event) {
    if (event->read) { events->platform.read_close(event->read); }
    *event=(neo_event){0};
}

void neo_events_close(neo_events *events) {
    for (size_t i=0; i<NEO_EVENT_LIMIT; ++i) { neo_event_close(events,&events->items[i]); }
}

const char *neo_events_schedule(void *context, uint64_t revision,
                               const neo_deferred *requests, size_t count) {
    neo_events *events=context;
    size_t available=0;
    for (size_t i=0; i<NEO_EVENT_LIMIT; ++i) { if (!events->items[i].active) { ++available; } }
    if (count>available) { return "event queue full"; }
    if (count>UINT64_MAX-events->sequence) { return "event limit exceeded"; }
    uint64_t now=events->platform.clock_ms();
    for (size_t i=0; i<count; ++i) {
        if (requests[i].delay_ms>UINT64_MAX-now) { return "event deadline overflow"; }
    }
    size_t next=0;
    for (size_t i=0; i<NEO_EVENT_LIMIT && next<count; ++i) {
        if (events->items[i].active) { continue; }
        const neo_deferred *request=&requests[next++];
        neo_event *event=&events->items[i];
        *event=(neo_event){0};
        event->active=true;
        event->sequence=++events->sequence;
        event->revision=revision;
        event->due=now+request->delay_ms;
        snprintf(event->id,sizeof(event->id),"image-%" PRIu64,event->sequence);
        strcpy(event->object,request->object);
        strcpy(event->selector,request->selector);
        event->count=request->count;
        memcpy(event->arguments,request->arguments,event->count*sizeof(int32_t));
    }
    return NULL;
}

void neo_events_poll(neo_events *events, neo_session *session, const neo_code_platform *native) {
    /* Each poll visits its initial queue once. Sequence order also orders equal deadlines. */
    uint64_t previous=0, last=events->sequence;
    for (size_t visited=0; visited<NEO_EVENT_LIMIT; ++visited) {
        neo_event *event=NULL;
        for (size_t i=0; i<NEO_EVENT_LIMIT; ++i) {
            neo_event *candidate=&events->items[i];
            if (candidate->active && candidate->sequence>previous && candidate->sequence<=last &&
                (!event || candidate->sequence<event->sequence)) { event=candidate; }
        }
        if (!event) { return; }
        previous=event->sequence;
        if (event->due>events->platform.clock_ms()) { continue; }
        const char *failure=NULL;
        char literal[64];
        bool ready=true;
        if (event->read) {
            failure=events->platform.read_poll(event->read,&ready,literal,sizeof(literal));
        }
        if (!ready && !failure) { continue; }
        int32_t result=0;
        if (!failure && event->revision!=neo_session_revision(session)) {
            failure="stale graph revision";
        }
        if (!failure) {
            failure=event->read ?
                neo_session_send_text(session,event->object,event->selector,literal,native,&result) :
                neo_session_send_args(session,event->object,event->selector,event->arguments,event->count,
                                      native,&result);
        }
        if (events->complete) {
            events->complete(events->completion_context,event->id,failure,result);
        }
        neo_event_close(events,event);
    }
}

static bool neo_events_pending(const neo_events *events) {
    for (size_t i=0; i<NEO_EVENT_LIMIT; ++i) {
        if (events->items[i].active) { return true; }
    }
    return false;
}

const char *neo_events_command(neo_events *events, neo_session *session,
                               const neo_code_platform *native, char *command) {
    const char *operation=strtok(command," \t");
    if (!operation) { return "expected an event command"; }
    if (!strcmp(operation,"poll") || !strcmp(operation,"await")) {
        bool await=!strcmp(operation,"await");
        if (strtok(NULL," \t")) { return "unexpected event argument"; }
        do {
            neo_events_poll(events,session,native);
            if (!await || !neo_events_pending(events)) { break; }
            events->platform.wait_ms(10);
        } while (true);
        return NULL;
    }
    const char *id=strtok(NULL," \t");
    if (!id || strlen(id)>=sizeof(events->items[0].id)) { return "expected event ID"; }
    neo_event *available=NULL, *existing=NULL;
    for (size_t i=0; i<NEO_EVENT_LIMIT; ++i) {
        neo_event *event=&events->items[i];
        if (!event->active && !available) { available=event; }
        if (event->active && !strcmp(event->id,id)) { existing=event; }
    }
    if (!strcmp(operation,"cancel")) {
        if (strtok(NULL," \t")) { return "unexpected cancel argument"; }
        if (!existing) { return "unknown pending event"; }
        neo_event_close(events,existing);
        printf("cancelled %s\n",id);
        return NULL;
    }
    if (!strncmp(id,"image-",6)) { return "image- event IDs are reserved"; }
    bool read=!strcmp(operation,"read-text"), after=!strcmp(operation,"after");
    if (!read && !after && strcmp(operation,"post")) { return "unknown event command"; }
    if (existing) { return "event ID already pending"; }
    if (!available) { return "event queue full"; }
    uint64_t delay=0;
    if (after && !neo_event_number(strtok(NULL," \t"),&delay)) { return "expected delay in milliseconds"; }
    uint64_t now=events->platform.clock_ms();
    if (delay>UINT64_MAX-now || events->sequence==UINT64_MAX) { return "event limit exceeded"; }
    const char *object=strtok(NULL," \t"), *selector=strtok(NULL," \t");
    if (!object || !selector || strlen(object)>=64 || strlen(selector)>=64) {
        return "expected receiver and selector";
    }
    neo_event staged={0};
    strcpy(staged.id,id);
    strcpy(staged.object,object);
    strcpy(staged.selector,selector);
    staged.due=now+delay;
    staged.revision=neo_session_revision(session);
    if (read) {
        char *path=strtok(NULL,"\r\n");
        if (!path) { return "expected text file path"; }
        path+=strspn(path," \t");
        size_t length=strlen(path);
        if (length>=2 && path[0]=='"' && path[length-1]=='"') {
            path[length-1]=0;
            ++path;
        }
        const char *failure=events->platform.read_start(path,&staged.read);
        if (failure) { return failure; }
    } else {
        for (const char *arg=strtok(NULL," \t"); arg; arg=strtok(NULL," \t")) {
            if (staged.count==NEO_MAX_ARGUMENTS) { return "too many event arguments"; }
            errno=0;
            char *end;
            intmax_t value=strtoimax(arg,&end,10);
            if (errno || *end || value<INT32_MIN || value>INT32_MAX) {
                return "expected a signed 32-bit integer";
            }
            staged.arguments[staged.count++]=(int32_t)value;
        }
    }
    staged.active=true;
    staged.sequence=++events->sequence;
    *available=staged;
    printf("queued %s\n",id);
    return NULL;
}
