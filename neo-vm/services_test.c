#include "services.h"
#include "platform.h"
#include "presentation.h"
#include <stdio.h>
#include <string.h>

#define NEO_CHECK(expression) do { if (!(expression)) { fprintf(stderr,"platform selftest line %d: %s\n",__LINE__,#expression); goto done; } } while (0)
static void neo_shutdown_completion(void *context, const neo_completion *completion) {
    size_t *count=context;
    if (completion->status==NEO_CANCELLED) { ++*count; }
}
bool neo_services_selftest(void) {
    neo_services *services=NULL, *other=NULL;
    bool passed=false;
    neo_handle stream, replacement, foreign, root, readonly, surface;
    uint64_t id=0, revision=0; size_t length=0;
    neo_completion completion;
    NEO_CHECK(neo_services_headless(&services)==NEO_OK);
    neo_machine machine;
    NEO_CHECK(neo_services_machine(services,&machine)==NEO_OK && machine.word_bits==64 && machine.page_size==4096);
    neo_services_wake(services); neo_services_wait(services,50);
    NEO_CHECK(neo_services_clock(services)==0);
    void *aligned=neo_platform_allocate(128,64);
    NEO_CHECK(aligned && (uintptr_t)aligned%64u==0);
    neo_platform_free(aligned);
    NEO_CHECK(!neo_platform_allocate(1,3) && !neo_platform_calloc(SIZE_MAX,2));
    NEO_CHECK(neo_services_headless(&other)==NEO_OK);
    NEO_CHECK(neo_services_memory_stream(services,NEO_RIGHT_READ|NEO_RIGHT_WRITE,"abcd",4,&stream)==NEO_OK);
    NEO_CHECK(neo_services_submit(services,stream,NEO_IO_READ,NULL,2,0,&id)==NEO_OK);
    neo_services_poll(services);
    NEO_CHECK(neo_services_next(services,&completion)==NEO_OK && completion.id==id &&
              completion.status==NEO_OK && completion.length==2 && !memcmp(completion.bytes,"ab",2));
    NEO_CHECK(neo_services_cancel(services,id)==NEO_CLOSED);
    NEO_CHECK(neo_services_release(services,id)==NEO_OK);
    char owned[]="ef";
    NEO_CHECK(neo_services_submit(services,stream,NEO_IO_WRITE,owned,2,0,&id)==NEO_OK);
    memset(owned,'x',2); neo_services_poll(services);
    NEO_CHECK(neo_services_next(services,&completion)==NEO_OK && completion.length==2);
    NEO_CHECK(neo_services_release(services,id)==NEO_OK);
    NEO_CHECK(neo_services_submit(services,stream,NEO_IO_READ,NULL,8,0,&id)==NEO_OK);
    neo_services_poll(services);
    NEO_CHECK(neo_services_next(services,&completion)==NEO_OK && completion.length==4 && !memcmp(completion.bytes,"cdef",4));
    NEO_CHECK(neo_services_release(services,id)==NEO_OK);
    NEO_CHECK(neo_services_submit(services,stream,NEO_IO_READ,NULL,1,0,&id)==NEO_OK);
    NEO_CHECK(neo_services_cancel(services,id)==NEO_OK);
    NEO_CHECK(neo_services_cancel(services,id)==NEO_CLOSED);
    neo_services_poll(services);
    NEO_CHECK(neo_services_next(services,&completion)==NEO_OK && completion.status==NEO_CANCELLED);
    NEO_CHECK(neo_services_release(services,id)==NEO_OK);
    NEO_CHECK(neo_services_next(services,&completion)==NEO_PENDING);
    NEO_CHECK(neo_services_submit(services,stream,NEO_IO_READ,NULL,1,10,&id)==NEO_OK);
    NEO_CHECK(neo_services_advance(services,10)==NEO_OK);
    neo_services_poll(services);
    NEO_CHECK(neo_services_next(services,&completion)==NEO_OK && completion.status==NEO_TIMEOUT);
    NEO_CHECK(neo_services_release(services,id)==NEO_OK);
    NEO_CHECK(neo_services_submit(services,stream,NEO_IO_READ,NULL,1,0,&id)==NEO_OK);
    NEO_CHECK(neo_services_revoke(services,stream)==NEO_OK);
    NEO_CHECK(neo_services_next(services,&completion)==NEO_OK && completion.status==NEO_DENIED);
    NEO_CHECK(neo_services_release(services,id)==NEO_OK);
    NEO_CHECK(neo_services_memory_stream(services,NEO_RIGHT_READ,"new",3,&replacement)==NEO_OK);
    NEO_CHECK(replacement.slot==stream.slot && replacement.generation!=stream.generation);
    NEO_CHECK(neo_services_submit(services,stream,NEO_IO_READ,NULL,1,0,&id)==NEO_CLOSED && id==0);
    NEO_CHECK(neo_services_submit(services,replacement,NEO_IO_WRITE,"x",1,0,&id)==NEO_DENIED);
    NEO_CHECK(neo_services_memory_stream(other,NEO_RIGHT_READ,"foreign",7,&foreign)==NEO_OK);
    NEO_CHECK(neo_services_submit(services,foreign,NEO_IO_READ,NULL,1,0,&id)==NEO_CLOSED);

    NEO_CHECK(neo_services_root(services,"memory",NEO_RIGHT_READ|NEO_RIGHT_WRITE,&root)==NEO_OK);
    NEO_CHECK(neo_services_root(services,"memory",NEO_RIGHT_READ,&readonly)==NEO_OK);
    NEO_CHECK(neo_services_store_replace(services,root,"sample",0,"hello",5,&revision)==NEO_OK && revision==1);
    NEO_CHECK(neo_services_store_replace(services,root,"sample",0,"wrong",5,&revision)==NEO_CONFLICT);
    NEO_CHECK(neo_services_store_replace(services,readonly,"sample",1,"wrong",5,&revision)==NEO_DENIED);
    NEO_CHECK(neo_services_store_replace(services,root,"../outside",0,"wrong",5,&revision)==NEO_INVALID);
    char bytes[256]={0};
    NEO_CHECK(neo_services_store_read(services,root,"sample",bytes,sizeof(bytes),&length,&revision)==NEO_OK &&
              revision==1 && length==5 && !memcmp(bytes,"hello",5));
    NEO_CHECK(neo_services_store_list(services,root,bytes,sizeof(bytes))==NEO_OK && !strcmp(bytes,"sample\n"));
    NEO_CHECK(neo_services_store_replace(services,root,"removed",0,"old",3,&revision)==NEO_OK && revision==1);
    NEO_CHECK(neo_services_store_delete(services,root,"removed",1,&revision)==NEO_OK && revision==2);
    NEO_CHECK(neo_services_store_read(services,root,"removed",bytes,sizeof(bytes),&length,&revision)==NEO_EOF && revision==2);
    NEO_CHECK(neo_services_store_replace(services,root,"removed",0,"wrong",5,&revision)==NEO_CONFLICT);
    NEO_CHECK(neo_services_store_replace(services,root,"removed",2,"new",3,&revision)==NEO_OK && revision==3);
    NEO_CHECK(neo_services_store_submit(services,root,"sample",NEO_IO_STORE_READ,0,NULL,2,0,&id)==NEO_OK);
    neo_services_poll(services);
    NEO_CHECK(neo_services_next(services,&completion)==NEO_OK && completion.status==NEO_TRUNCATED &&
              completion.length==0 && completion.required_length==5 && completion.revision==1);
    NEO_CHECK(neo_services_release(services,id)==NEO_OK);
    NEO_CHECK(neo_services_surface(services,4,3,&surface)==NEO_OK);
    neo_frame frame, stale;
    NEO_CHECK(neo_services_acquire(services,surface,&frame)==NEO_OK);
    NEO_CHECK(neo_services_resize(services,surface,3,2)==NEO_PENDING);
    memset(frame.pixels,0x5a,frame.stride*frame.height); stale=frame;
    NEO_CHECK(neo_services_present(services,&frame)==NEO_OK);
    NEO_CHECK(neo_services_capture(services,surface,bytes,sizeof(bytes),&length)==NEO_OK && length==48);
    for (size_t i=0; i<length; ++i) { NEO_CHECK((unsigned char)bytes[i]==0x5a); }
    NEO_CHECK(neo_services_present(services,&stale)==NEO_INVALID);
    NEO_CHECK(neo_services_acquire(services,surface,&frame)==NEO_OK);
    NEO_CHECK(neo_services_present(services,&stale)==NEO_INVALID);
    NEO_CHECK(neo_services_abandon(services,&frame)==NEO_OK);
    NEO_CHECK(neo_services_resize(services,surface,3,2)==NEO_OK);
    NEO_CHECK(neo_services_present(services,&stale)==NEO_INVALID);
    neo_input input={.kind=NEO_INPUT_WHEEL,.surface=surface,.time_ns=10,.dy=0.125};
    neo_input invalid=input; invalid.kind=NEO_INPUT_TEXT; invalid.text[0]=(char)0xc0; invalid.text[1]=(char)0xaf;
    NEO_CHECK(neo_services_push_input(services,&invalid)==NEO_INVALID);
    NEO_CHECK(neo_services_push_input(services,&input)==NEO_OK);
    NEO_CHECK(neo_services_input(services,&input)==NEO_OK && input.dy==0.125 && input.time_ns==10);
    for (size_t i=0; i<NEO_SERVICE_EVENTS; ++i) { NEO_CHECK(neo_services_push_input(services,&input)==NEO_OK); }
    NEO_CHECK(neo_services_push_input(services,&input)==NEO_EXHAUSTED);
    neo_script_step script[]={
        {.ready_ns=1000,.operation=NEO_IO_READ,.status=NEO_OK,.length=4,.bytes={'w','i','r','e'}},
        {.ready_ns=1500,.operation=NEO_IO_READ,.status=NEO_IO_ERROR},
        {.ready_ns=2000,.operation=NEO_IO_READ,.status=NEO_EOF}};
    neo_handle scripted;
    NEO_CHECK(neo_services_script_stream(services,script,3,NEO_RIGHT_READ,&scripted)==NEO_OK);
    NEO_CHECK(neo_services_submit(services,scripted,NEO_IO_READ,NULL,2,0,&id)==NEO_OK);
    neo_services_poll(services);
    NEO_CHECK(neo_services_next(services,&completion)==NEO_PENDING);
    NEO_CHECK(neo_services_advance(services,1000)==NEO_OK);
    neo_services_poll(services);
    NEO_CHECK(neo_services_next(services,&completion)==NEO_OK && completion.length==2 && !memcmp(completion.bytes,"wi",2));
    NEO_CHECK(neo_services_release(services,id)==NEO_OK);
    NEO_CHECK(neo_services_submit(services,scripted,NEO_IO_READ,NULL,8,0,&id)==NEO_OK);
    neo_services_poll(services);
    NEO_CHECK(neo_services_next(services,&completion)==NEO_OK && completion.length==2 && !memcmp(completion.bytes,"re",2));
    NEO_CHECK(neo_services_release(services,id)==NEO_OK);
    NEO_CHECK(neo_services_submit(services,scripted,NEO_IO_READ,NULL,8,0,&id)==NEO_OK);
    NEO_CHECK(neo_services_advance(services,1500)==NEO_OK);
    neo_services_poll(services);
    NEO_CHECK(neo_services_next(services,&completion)==NEO_OK && completion.status==NEO_IO_ERROR);
    NEO_CHECK(neo_services_release(services,id)==NEO_OK);
    NEO_CHECK(neo_services_submit(services,scripted,NEO_IO_READ,NULL,8,1900,&id)==NEO_OK);
    NEO_CHECK(neo_services_advance(services,1900)==NEO_OK);
    neo_services_poll(services);
    NEO_CHECK(neo_services_next(services,&completion)==NEO_OK && completion.status==NEO_TIMEOUT);
    NEO_CHECK(neo_services_release(services,id)==NEO_OK);
    NEO_CHECK(neo_services_submit(services,scripted,NEO_IO_READ,NULL,8,0,&id)==NEO_OK);
    NEO_CHECK(neo_services_advance(services,2000)==NEO_OK);
    neo_services_poll(services);
    NEO_CHECK(neo_services_next(services,&completion)==NEO_OK && completion.status==NEO_EOF);
    NEO_CHECK(neo_services_release(services,id)==NEO_OK);
    for (size_t i=0; i<NEO_SERVICE_REQUESTS; ++i) {
        neo_handle queued;
        NEO_CHECK(neo_services_memory_stream(services,NEO_RIGHT_READ,"x",1,&queued)==NEO_OK);
        NEO_CHECK(neo_services_submit(services,queued,NEO_IO_READ,NULL,1,0,&id)==NEO_OK);
    }
    NEO_CHECK(neo_services_submit(services,replacement,NEO_IO_READ,NULL,1,0,&id)==NEO_EXHAUSTED && !id);
    size_t cancelled=0;
    neo_services_shutdown(services,neo_shutdown_completion,&cancelled);
    NEO_CHECK(cancelled==NEO_SERVICE_REQUESTS);
    NEO_CHECK(neo_services_next(services,&completion)==NEO_PENDING);
    NEO_CHECK(neo_services_root(services,"memory",NEO_RIGHT_READ,&root)==NEO_CLOSED);
    passed=true;
done:
    neo_services_close(other); neo_services_close(services);
    if (passed) { puts("PASS platform contracts: ownership, cancellation, deadlines, revocation, storage conflicts, frames, input, and shutdown"); }
    return passed;
}
static bool neo_test_await(neo_services *services, uint64_t id, neo_completion *completion) {
    uint64_t deadline=neo_services_clock(services)+UINT64_C(3000000000);
    while (neo_services_clock(services)<deadline) {
        neo_services_wait(services,1);
        if (neo_services_next(services,completion)==NEO_OK) { return completion->id==id; }
    }
    return false;
}
bool neo_network_image_selftest(const neo_code_platform *platform) {
    neo_services *left=NULL, *right=NULL;
    neo_session *sender=NULL, *receiver=NULL;
    bool passed=false;
    const char *source="(warp 1 (entry sender ready) (object sender (handler ready 0)"
        " (handler payload (return-text \"image payload\"))))";
    const char *target="(warp 1 (entry receiver reading) (object receiver (slot text \"\")"
        " (handler accept text (do (set-text self text text) (text-length (get self text))))"
        " (handler reading (text-length (get self text)))))";
    NEO_CHECK(!neo_session_open(source,&sender) && !neo_session_open(target,&receiver));
    NEO_CHECK(neo_platform_services(true,&left)==NEO_OK && neo_platform_services(true,&right)==NEO_OK);
    neo_handle listener, client, peer;
    uint16_t port=0; uint64_t id=0;
    NEO_CHECK(neo_services_endpoint(right,NEO_RESOURCE_LISTENER,true,&port,NEO_RIGHT_ACCEPT,&listener)==NEO_OK);
    NEO_CHECK(neo_services_endpoint(left,NEO_RESOURCE_CONNECTION,false,&port,NEO_RIGHT_WRITE,&client)==NEO_OK);
    NEO_CHECK(neo_services_submit(right,listener,NEO_IO_ACCEPT,NULL,0,0,&id)==NEO_OK);
    neo_completion completion;
    NEO_CHECK(neo_test_await(right,id,&completion) && completion.status==NEO_OK);
    peer=completion.accepted; NEO_CHECK(neo_services_release(right,id)==NEO_OK);
    char literal[NEO_TEXT_RESULT_CAPACITY];
    NEO_CHECK(!neo_session_receive_sequence(sender,"sender","payload",NULL,0,platform,'"',literal));
    size_t length=strlen(literal);
    NEO_CHECK(neo_services_submit(left,client,NEO_IO_WRITE,literal,length,0,&id)==NEO_OK);
    NEO_CHECK(neo_test_await(left,id,&completion) && completion.status==NEO_OK && completion.length==length);
    NEO_CHECK(neo_services_release(left,id)==NEO_OK);
    char received[NEO_TEXT_RESULT_CAPACITY]={0}; size_t used=0;
    while (used<length) {
        NEO_CHECK(neo_services_submit(right,peer,NEO_IO_READ,NULL,2,0,&id)==NEO_OK);
        NEO_CHECK(neo_test_await(right,id,&completion) && completion.status==NEO_OK && completion.length);
        NEO_CHECK(completion.length<=length-used);
        memcpy(received+used,completion.bytes,completion.length); used+=completion.length;
        NEO_CHECK(neo_services_release(right,id)==NEO_OK);
    }
    int32_t result=0;
    NEO_CHECK(!strcmp(literal,received));
    NEO_CHECK(!neo_session_send_text(receiver,"receiver","accept",received,platform,&result) && result==13);
    NEO_CHECK(!neo_session_send(receiver,"receiver","reading",platform,&result) && result==13);
    NEO_CHECK(neo_services_submit(left,client,NEO_IO_READ,NULL,1,0,&id)==NEO_DENIED && !id);
    passed=true;
done:
    neo_session_close(sender); neo_session_close(receiver);
    neo_services_close(left); neo_services_close(right);
    if (passed) { puts("PASS independently granted image sessions exchange a graph value through partial TCP transfers"); }
    return passed;
}
bool neo_optional_services_selftest(void) {
    neo_services *services=NULL; bool passed=false;
    NEO_CHECK(neo_platform_services(false,&services)==NEO_OK);
    neo_handle clipboard;
    neo_status clipboard_status=neo_services_clipboard(services,NEO_RIGHT_READ,&clipboard);
    if (clipboard_status==NEO_OK) {
        uint64_t request;
        NEO_CHECK(neo_services_submit(services,clipboard,NEO_IO_WRITE,"private",7,0,&request)==NEO_DENIED);
        NEO_CHECK(neo_services_submit(services,clipboard,NEO_IO_READ,NULL,32,0,&request)==NEO_OK);
        NEO_CHECK(neo_services_cancel(services,request)==NEO_OK);
        neo_completion cancelled;
        NEO_CHECK(neo_services_next(services,&cancelled)==NEO_OK && cancelled.status==NEO_CANCELLED);
        NEO_CHECK(neo_services_release(services,request)==NEO_OK);
        NEO_CHECK(neo_services_revoke(services,clipboard)==NEO_OK);
        puts("PASS optional clipboard grant and cancellation without reading or changing host clipboard");
    } else { NEO_CHECK(clipboard_status==NEO_UNSUPPORTED); }
    neo_handle audio;
    NEO_CHECK(neo_services_audio(services,(neo_audio_format){0,2,false},&audio)==NEO_INVALID);
    neo_status status=neo_services_audio(services,(neo_audio_format){48000,2,false},&audio);
    if (status==NEO_UNSUPPORTED) {
        puts("SKIP native audio device unavailable; unsupported reported explicitly"); passed=true; goto done;
    }
    NEO_CHECK(status==NEO_OK);
    uint64_t first,second; int16_t silence[512]={0};
    NEO_CHECK(neo_services_submit(services,audio,NEO_IO_READ,NULL,sizeof(silence),0,&first)==NEO_DENIED);
    NEO_CHECK(neo_services_submit(services,audio,NEO_IO_WRITE,silence,sizeof(silence)-1,0,&first)==NEO_INVALID);
    NEO_CHECK(neo_services_submit(services,audio,NEO_IO_WRITE,silence,sizeof(silence),0,&first)==NEO_OK);
    NEO_CHECK(neo_services_submit(services,audio,NEO_IO_WRITE,silence,sizeof(silence),0,&second)==NEO_OK);
    neo_completion completion;
    NEO_CHECK(neo_test_await(services,first,&completion) && completion.status==NEO_OK && completion.length==sizeof(silence));
    NEO_CHECK(neo_services_release(services,first)==NEO_OK);
    NEO_CHECK(neo_test_await(services,second,&completion) && completion.status==NEO_OK && completion.length==sizeof(silence));
    NEO_CHECK(neo_services_release(services,second)==NEO_OK);
    neo_audio_stats stats;
    NEO_CHECK(neo_services_audio_stats(services,audio,&stats)==NEO_OK && stats.frames==512);
    NEO_CHECK(neo_services_submit(services,audio,NEO_IO_WRITE,silence,sizeof(silence),0,&first)==NEO_OK);
    NEO_CHECK(neo_services_cancel(services,first)==NEO_OK);
    NEO_CHECK(neo_services_next(services,&completion)==NEO_OK && completion.status==NEO_CANCELLED);
    NEO_CHECK(neo_services_release(services,first)==NEO_OK);
    NEO_CHECK(neo_services_revoke(services,audio)==NEO_OK);
    NEO_CHECK(neo_services_audio_stats(services,audio,&stats)==NEO_CLOSED);
    puts("PASS optional audio: silent queued buffers, frame accounting, grants, alignment, cancellation, and cleanup");
    passed=true;
done:
    neo_services_close(services); return passed;
}
