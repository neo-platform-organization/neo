#include "exercise.h"
#include "presentation.h"
#include "events.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

typedef struct {
    neo_services *services;
    neo_handle root;
    uint64_t revision;
    int32_t value;
} neo_exercise_document;

/* Explicit little-endian binary payload, independent of C structure layout. */
static const char *neo_exercise_write(void *context, int32_t value) {
    neo_exercise_document *document=context;
    unsigned char bytes[8]={'N','E','O',1,0,0,0,0};
    uint32_t word=(uint32_t)value;
    for (unsigned i=0; i<4; ++i) { bytes[i+4]=(unsigned char)(word>>(8u*i)); }
    uint64_t revision=0;
    neo_status status=neo_services_store_replace(document->services,document->root,"document",
        document->revision,bytes,sizeof(bytes),&revision);
    if (status!=NEO_OK) { return neo_status_name(status); }
    document->revision=revision;
    return NULL;
}
static const char *neo_exercise_read(neo_exercise_document *document) {
    unsigned char bytes[8]; size_t length=0;
    neo_status status=neo_services_store_read(document->services,document->root,"document",
        bytes,sizeof(bytes),&length,&document->revision);
    if (status==NEO_EOF) { document->value=0; return NULL; }
    if (status!=NEO_OK) { return neo_status_name(status); }
    if (length!=8 || memcmp(bytes,"NEO\1",4)) { return "invalid exercise document"; }
    uint32_t value=0;
    for (unsigned i=0; i<4; ++i) { value|=(uint32_t)bytes[i+4]<<(8u*i); }
    document->value=value<=INT32_MAX ? (int32_t)value : -1-(int32_t)(UINT32_MAX-value);
    return NULL;
}
static uint64_t neo_exercise_time;
static uint64_t neo_exercise_clock(void) { return neo_exercise_time; }
static void neo_exercise_cancelled(void *context, const neo_completion *completion) {
    size_t *count=context;
    if (completion->status==NEO_CANCELLED) { ++*count; }
}
static const char *neo_exercise_input(neo_services *services, neo_session *session, const char *object,
                                     neo_input input, const neo_code_platform *platform) {
    neo_status status=neo_services_push_input(services,&input);
    if (status==NEO_OK) { status=neo_services_input(services,&input); }
    return status==NEO_OK ? neo_surface_input(session,object,&input,platform) : neo_status_name(status);
}

static const char *neo_exercise_send(neo_session *session, const char *object, const char *selector,
                                    const neo_code_platform *platform, int32_t expected) {
    int32_t result=0;
    const char *failure=neo_session_send(session,object,selector,platform,&result);
    if (!failure && result!=expected) {
        fprintf(stderr,"exercise %s: expected %" PRId32 ", received %" PRId32 "\n",selector,expected,result);
    }
    return failure ? failure : result==expected ? NULL : "unexpected exercise result";
}

static const char *neo_exercise_check(neo_session **active, const char *source, const char *object,
    neo_services *display, neo_handle surface, neo_exercise_document *document, neo_events *events,
    const neo_code_platform *platform) {
    neo_session *session=*active;
    int32_t initial=document->value, result=0;
    if (initial<0 || initial>10000) { return "exercise test counter exhausted; use a fresh directory"; }
    const char *failure=neo_exercise_send(session,object,"reading",platform,initial);
    if (failure) { return failure; }
    for (unsigned i=0; i<2; ++i) {
        neo_input input={.kind=NEO_INPUT_BUTTON,.surface=surface,.code=1,.pressed=true,.x=10,.y=10};
        failure=neo_exercise_input(display,session,object,input,platform);
        if (failure) { return failure; }
    }
    neo_input text={.kind=NEO_INPUT_TEXT,.surface=surface,.text="abc"};
    failure=neo_exercise_input(display,session,object,text,platform);
    if (failure) { return failure; }
    int32_t saved=initial+5;
    if ((failure=neo_exercise_send(session,object,"reading",platform,saved)) ||
        (failure=neo_exercise_send(session,object,"draw",platform,1))) { return failure; }
    unsigned char pixels[64*48*4]; size_t length=0;
    if (neo_services_capture(display,surface,pixels,sizeof(pixels),&length)!=NEO_OK ||
        length!=sizeof(pixels) || pixels[0]!=255 || pixels[3]!=255) { return "image framebuffer mismatch"; }
    neo_input save={.kind=NEO_INPUT_KEY,.surface=surface,.code=60,.pressed=true};
    if ((failure=neo_exercise_input(display,session,object,save,platform))) { return failure; }
    if ((failure=neo_exercise_send(session,object,"saved-value",platform,saved))) { return failure; }
    uint64_t saved_revision=document->revision;
    if (document->value!=saved) { return "document publication failed"; }
    failure=neo_session_send(session,object,"bad-save",platform,&result);
    if (!failure || strcmp(failure,"integer overflow") || document->revision!=saved_revision ||
        document->value!=saved) { return "failed send changed durable state"; }
    failure=neo_session_send(session,object,"mixed-save",platform,&result);
    if (!failure || strcmp(failure,"durable writes and other external effects require separate sends") ||
        document->revision!=saved_revision) { return "mixed effects were published"; }
    if ((failure=neo_exercise_send(session,object,"reading",platform,saved))) { return failure; }
    neo_handle root=document->root, readonly;
    /* Same resource with fewer rights, rather than a path chosen by the image. */
    unsigned char durable[8]; size_t durable_length=0; uint64_t revision=0;
    if (neo_services_store_read(document->services,root,"document",durable,sizeof(durable),
        &durable_length,&revision)!=NEO_OK) { return "durable read failed"; }
    if (neo_services_memory_stream(display,NEO_RIGHT_READ,NULL,0,&readonly)!=NEO_OK) {
        return "resource constructor failed";
    }
    /* A foreign handle must never grant access, even when its slot matches. */
    document->root=readonly;
    failure=neo_session_send(session,object,"increment",platform,&result);
    if (failure) { document->root=root; return failure; }
    failure=neo_session_send(session,object,"save",platform,&result);
    document->root=root;
    if (!failure || document->revision!=saved_revision || document->value!=saved) {
        return "denied save changed publication";
    }
    if ((failure=neo_exercise_send(session,object,"saved-value",platform,saved)) ||
        (failure=neo_exercise_send(session,object,"reading",platform,saved+1)) ||
        (failure=neo_exercise_send(session,object,"restore",platform,saved))) { return failure; }
    neo_services_revoke(display,readonly);
    failure=neo_session_send(session,"exercise-outsider","steal",platform,&result);
    if (!failure || strcmp(failure,"document access denied")) { return "package escaped document grant"; }
    uint64_t graph_revision=neo_session_revision(session);
    if (!neo_session_reload(active,graph_revision,"(warp 1 (entry missing start))") ||
        neo_session_revision(*active)!=graph_revision) { return "failed live edit replaced graph"; }
    session=*active;
    if ((failure=neo_exercise_send(session,object,"reading",platform,saved))) { return failure; }
    neo_exercise_time+=25;
    neo_events_poll(events,session,platform);
    if ((failure=neo_exercise_send(session,object,"pulse-count",platform,1))) { return failure; }
    if ((failure=neo_exercise_send(session,object,"stop",platform,0))) { return failure; }
    neo_events_close(events);
    for (size_t i=0; i<NEO_EVENT_LIMIT; ++i) {
        if (events->items[i].active) { return "shutdown left a scheduled message"; }
    }
    /* A successful live edit preserves the explicitly granted commit sink. */
    failure=neo_session_reload(active,neo_session_revision(session),source);
    if (failure) { return failure; }
    session=*active;
    saved_revision=document->revision;
    if ((failure=neo_exercise_send(session,object,"save",platform,saved)) ||
        document->revision!=saved_revision+1) { return failure ? failure : "reload lost durable commit grant"; }
    static char snapshot[NEO_SERVICE_BYTES], reopened[NEO_SERVICE_BYTES];
    failure=neo_session_snapshot(session,snapshot,sizeof(snapshot));
    if (failure) { return failure; }
    uint64_t checkpoint_revision=0;
    neo_status status=neo_services_store_read(document->services,root,"checkpoint",reopened,
        sizeof(reopened),&length,&checkpoint_revision);
    if (status!=NEO_OK && status!=NEO_EOF) { return neo_status_name(status); }
    status=neo_services_store_replace(document->services,root,"checkpoint",checkpoint_revision,
        snapshot,strlen(snapshot),&checkpoint_revision);
    if (status!=NEO_OK) { return neo_status_name(status); }
    status=neo_services_store_read(document->services,root,"checkpoint",reopened,sizeof(reopened)-1,
        &length,&revision);
    if (status!=NEO_OK) { return neo_status_name(status); }
    reopened[length]=0;
    neo_session *restored=NULL;
    failure=neo_session_open(reopened,&restored);
    if (!failure) { failure=neo_exercise_send(restored,object,"reading",platform,saved); }
    if (!failure) {
        const char *denied=neo_session_send(restored,object,"restore",platform,&result);
        if (!denied || strcmp(denied,"document access denied")) { failure="snapshot serialized authority"; }
    }
    neo_session_close(restored);
    if (failure) { return failure; }
    /* Discard unsaved edits and reopen the canonical source, not the snapshot. */
    if ((failure=neo_exercise_send(session,object,"increment",platform,saved+1))) { return failure; }
    neo_session_close(session); *active=NULL;
    if ((failure=neo_exercise_read(document)) || document->value!=saved) {
        return failure ? failure : "unsaved edit reached storage";
    }
    failure=neo_session_open(source,active);
    if (failure) { return failure; }
    failure=neo_session_grant_package_document(*active,"exercise",&document->value,NEO_DOCUMENT_READ|NEO_DOCUMENT_WRITE);
    if (!failure) {
        neo_session_document_sink(*active,neo_exercise_write,document);
        neo_session_schedule(*active,neo_events_schedule,events);
        failure=neo_exercise_send(*active,object,"start",platform,1);
    }
    if (!failure) { failure=neo_exercise_send(*active,object,"reading",platform,saved); }
    if (!failure) { failure=neo_exercise_send(*active,object,"stop",platform,0); }
    neo_events_close(events);
    if (failure) { return failure; }
    if (neo_services_revoke(display,surface)!=NEO_OK) { return "surface cleanup failed"; }
    neo_frame frame;
    if (neo_services_acquire(display,surface,&frame)==NEO_OK) { return "closed surface retained authority"; }
    neo_script_step delayed={.ready_ns=1000000000,.operation=NEO_IO_READ,.status=NEO_OK,.length=1,.bytes={1}};
    neo_handle pending; uint64_t request=0; size_t cancelled=0;
    if (neo_services_script_stream(display,&delayed,1,NEO_RIGHT_READ,&pending)!=NEO_OK ||
        neo_services_submit(display,pending,NEO_IO_READ,NULL,1,0,&request)!=NEO_OK) {
        return "pending shutdown setup failed";
    }
    neo_services_poll(display);
    neo_services_shutdown(display,neo_exercise_cancelled,&cancelled);
    if (cancelled!=1) { return "shutdown did not cancel pending I/O exactly once"; }
    printf("PASS exercise: restored %" PRId32 " saved %" PRId32 "; input, binary document, rollback, grants, idle timer, snapshot, and cleanup\n",initial,saved);
    return NULL;
}

int neo_image_exercise(const char *source, const char *object, const char *directory, bool test,
                       const neo_code_platform *platform) {
    neo_services *display=NULL, *storage=NULL;
    neo_session *session=NULL;
    neo_handle surface={0}; neo_buffer_grant buffer={0};
    neo_exercise_document document={0};
    neo_events events={0}; events.platform=neo_native_event_platform();
    if (test) { neo_exercise_time=0; events.platform.clock_ms=neo_exercise_clock; }
    neo_status status=neo_platform_services(false,&storage);
    document.services=storage;
    const char *failure=status==NEO_OK ? NULL : neo_status_name(status);
    if (!failure) {
        status=neo_services_root(storage,directory,NEO_RIGHT_READ|NEO_RIGHT_WRITE,&document.root);
        if (status!=NEO_OK) { failure=neo_status_name(status); }
    }
    if (!failure) { failure=neo_exercise_read(&document); }
    if (!failure) {
        status=test ? neo_services_headless(&display) : neo_platform_services(true,&display);
        if (status!=NEO_OK) { failure=neo_status_name(status); }
    }
    uint32_t width=test ? 64u : 640u, height=test ? 48u : 360u;
    if (!failure) {
        status=neo_services_surface(display,width,height,&surface);
        if (status!=NEO_OK) { failure=neo_status_name(status); }
    }
    if (!failure) { failure=neo_session_open(source,&session); }
    buffer=(neo_buffer_grant){display,surface};
    char package[64];
    if (!failure) { failure=neo_session_object_package(session,object,package); }
    if (!failure) { failure=neo_session_grant_package_document(session,package,&document.value,NEO_DOCUMENT_READ|NEO_DOCUMENT_WRITE); }
    if (!failure) {
        neo_session_document_sink(session,neo_exercise_write,&document);
        neo_session_schedule(session,neo_events_schedule,&events);
        failure=neo_session_grant_buffer(session,package,neo_buffer_apply,&buffer);
    }
    neo_native_begin();
    int32_t result=0;
    if (!failure) { failure=neo_session_send(session,object,"start",platform,&result); }
    if (!failure) { failure=neo_surface_configure(display,surface,session,object,width,height,1000,platform); }
    if (!failure && test) { failure=neo_exercise_check(&session,source,object,display,surface,&document,&events,platform); }
    if (!failure && !test) {
        puts("Exercise: click or type to change the value; F2 saves, F3 restores, Escape closes. Green means saved, orange means changed.");
        bool running=true;
        while (running) {
            neo_events_poll(&events,session,platform);
            neo_input input;
            status=neo_services_input(display,&input);
            if (status==NEO_PENDING) { neo_services_wait(display,10); continue; }
            if (status!=NEO_OK) { failure=neo_status_name(status); break; }
            if (input.kind==NEO_INPUT_CLOSE) { break; }
            if (input.kind==NEO_INPUT_RESIZE) {
                if (!input.width || !input.height) { continue; }
                failure=neo_surface_configure(display,surface,session,object,input.width,input.height,
                    (uint32_t)(input.scale*1000.0),platform);
            } else { failure=neo_surface_input(session,object,&input,platform); }
            if (failure) { fprintf(stderr,"neo exercise: %s (send rejected)\n",failure); failure=NULL; }
            failure=neo_session_send(session,object,"running",platform,&result);
            if (failure) { break; }
            running=result!=0;
            if (running) { failure=neo_session_send(session,object,"draw",platform,&result); }
            if (failure) { break; }
        }
    }
    if (session && !test) {
        const char *stopped=neo_session_send(session,object,"stop",platform,&result);
        if (!failure) { failure=stopped; }
    }
    if (failure) { fprintf(stderr,"neo exercise: %s\n",failure); }
    neo_events_close(&events);
    neo_native_end();
    neo_session_close(session);
    neo_services_close(display); neo_services_close(storage);
    return failure ? 1 : 0;
}

int neo_exercise_checkpoint(const char *directory) {
    neo_services *services=NULL; neo_handle root;
    neo_status status=neo_platform_services(false,&services);
    if (status==NEO_OK) { status=neo_services_root(services,directory,NEO_RIGHT_READ,&root); }
    static char source[NEO_SERVICE_BYTES]; size_t length=0; uint64_t revision=0;
    if (status==NEO_OK) {
        status=neo_services_store_read(services,root,"checkpoint",source,sizeof(source)-1,&length,&revision);
    }
    neo_session *session=NULL;
    const char *failure=status==NEO_OK ? NULL : neo_status_name(status);
    if (!failure) {
        source[length]=0;
        if (strlen(source)!=length) { failure="invalid checkpoint text"; }
        else { failure=neo_session_open(source,&session); }
    }
    if (!failure && puts(source)==EOF) { failure="checkpoint output failed"; }
    if (failure) { fprintf(stderr,"neo checkpoint: %s\n",failure); }
    neo_session_close(session); neo_services_close(services);
    return failure ? 1 : 0;
}
