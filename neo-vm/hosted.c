#include "platform.h"
#include "services.h"
#include "exercise.h"
#include "presentation.h"
#include "weft.h"
#include "lattice.h"

#include <inttypes.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#if !defined(__x86_64__)
#error The native backend requires x86-64.
#endif

static bool neo_read_source(const char *path, char *source, size_t capacity) {
    FILE *file=fopen(path,"rb");
    if (!file) { return false; }
    size_t length=fread(source,1,capacity-1,file);
    bool complete=fgetc(file)==EOF && !ferror(file) && !memchr(source,0,length);
    source[length]=0;
    return fclose(file)==0 && complete;
}

static void neo_print_trace(const neo_trace_event *event, void *context) {
    FILE *output=context;
    if (event->kind==NEO_TRACE_LIMIT_REACHED) {
        fputs("trace event limit reached\n",output);
    } else if (event->kind==NEO_TRACE_WRITE) {
        fprintf(output,"trace %s set %s.%s = %" PRId32 " [%s]\n",
                event->sender,event->receiver,event->name,event->value,
                event->committed ? "committed" : "rolled back");
    } else {
        fprintf(output,"trace %s %s %s.%s via %s\n",event->sender ? event->sender : event->kind==NEO_TRACE_KNIGHT ? "knight" : "host",
                event->kind==NEO_TRACE_KNIGHT ? "call" : "send",
                event->receiver,event->name,event->owner);
    }
}
static const char *neo_inspect_command(const neo_session *session, char *command,
                                       char *output, size_t capacity) {
    const char *object=strtok(command," \t");
    const char *member=strtok(NULL," \t");
    const char *as=member;
    if (member && strcmp(member,"as")) { as=strtok(NULL," \t"); }
    else { member=NULL; }
    const char *view=strtok(NULL," \t");
    if (!object || !as || strcmp(as,"as") || !view || strtok(NULL," \t")) {
        return "usage: inspect OBJECT [MEMBER] as warp|object|knight";
    }
    neo_inspect_view mode;
    if (!strcmp(view,"warp")) { mode=NEO_INSPECT_WARP; }
    else if (!strcmp(view,"object") || !strcmp(view,"weft")) { mode=NEO_INSPECT_OBJECT; }
    else if (!strcmp(view,"knight")) { mode=NEO_INSPECT_KNIGHT; }
    else { return "unknown inspection view"; }
    return neo_session_inspect(session,object,member,mode,output,capacity);
}
typedef enum { NEO_EVALUATE, NEO_PROBE, NEO_PUBLISH } neo_execution_command;

static const char *neo_evaluate_command(neo_session **active, char *command,
                                        const neo_code_platform *platform, neo_execution_command mode,
                                        char *source, size_t capacity) {
    neo_session *session=*active;
    bool file_mode=mode!=NEO_EVALUATE;
    const char *revision_text=strtok(command," \t");
    const char *object=strtok(NULL," \t");
    const char *handler=strtok(NULL," \t");
    const char *as=strtok(NULL," \t");
    const char *model=strtok(NULL," \t");
    char *input=strtok(NULL,file_mode ? "\r\n" : " \t");
    if (!revision_text || !object || !handler || !as || strcmp(as,"as") ||
        !model || strcmp(model,"knight") || !input) {
        return file_mode ? "usage: probe REVISION OBJECT FUNCTION as knight FILE|- [INTEGER ...], or publish REVISION OBJECT FUNCTION as knight FILE" :
            "usage: evaluate REVISION OBJECT HANDLER as knight STATE [INTEGER ...]";
    }
    errno=0;
    char *end;
    uintmax_t revision=strtoumax(revision_text,&end,10);
    if (errno || *end || revision_text[0]<'0' || revision_text[0]>'9' || revision>UINT64_MAX) {
        return "expected a graph revision";
    }

    const char *candidate=NULL;
    if (file_mode) {
        if ((uint64_t)revision!=neo_session_revision(session)) { return "stale graph revision"; }
        input+=strspn(input," \t");
        char *path=input;
        if (*path=='"') {
            ++path;
            input=strchr(path,'"');
            if (!input || (input[1] && input[1]!=' ' && input[1]!='\t')) {
                return "invalid quoted candidate path";
            }
        } else {
            input=path+strcspn(path," \t");
        }
        if (*input) { *input++=0; }
        if (!*path) { return "expected a candidate path or -"; }
        if (strcmp(path,"-")) {
            if (!neo_read_source(path,source,capacity)) { return "could not read Warp expression"; }
            candidate=source;
        }
        input=strtok(input," \t");
    }
    if (mode==NEO_PUBLISH) {
        if (!candidate || input) { return "publish expects one candidate file"; }
        const char *failure=neo_session_publish_knight(active,(uint64_t)revision,object,handler,candidate);
        if (!failure) { printf("revision %" PRIu64 "\n",neo_session_revision(*active)); }
        return failure;
    }
    int32_t values[NEO_MAX_ARGUMENTS+1], next, result;
    size_t count=0;
    for (; input; input=strtok(NULL," \t")) {
        if (count==NEO_MAX_ARGUMENTS+(file_mode ? 0u : 1u)) { return "too many arguments"; }
        errno=0;
        intmax_t value=strtoimax(input,&end,10);
        if (errno || *end || value<INT32_MIN || value>INT32_MAX) {
            return "expected a signed 32-bit integer";
        }
        values[count++]=(int32_t)value;
    }
    if (file_mode) {
        const char *failure=neo_session_probe_knight(session,(uint64_t)revision,object,handler,
            candidate,values,count,platform,&result);
        if (!failure) { printf("result %" PRId32 "\n",result); }
        return failure;
    }
    const char *failure=neo_session_evaluate_knight(session,(uint64_t)revision,object,handler,
        values[0],values+1,count-1,platform,&next,&result);
    if (!failure) { printf("next state %" PRId32 " result %" PRId32 "\n",next,result); }
    return failure;
}
static const char *neo_transform_command(const neo_session *session, char *command,
                                         char *output, size_t capacity) {
    const char *revision_text=strtok(command," \t");
    const char *object=strtok(NULL," \t");
    const char *function=strtok(NULL," \t");
    const char *from=strtok(NULL," \t");
    const char *to=strtok(NULL," \t");
    if (!revision_text || !object || !function || !from || !to || strtok(NULL," \t")) {
        return "usage: transform REVISION OBJECT KNIGHT OLD_ATOM NEW_ATOM";
    }
    errno=0;
    char *end;
    uintmax_t revision=strtoumax(revision_text,&end,10);
    if (errno || *end || revision_text[0]<'0' || revision_text[0]>'9' || revision>UINT64_MAX) {
        return "expected a graph revision";
    }
    return neo_session_transform_knight(session,(uint64_t)revision,object,function,from,to,
                                        output,capacity);
}
static void neo_console_completion(void *context, const char *id, const char *failure, int32_t result) {
    FILE *output=context;
    if (failure) { fprintf(output,"event %s error %s\n",id,failure); }
    else { fprintf(output,"event %s result %" PRId32 "\n",id,result); }
}

typedef struct {
    neo_events *events;
    neo_session *session;
    const neo_code_platform *platform;
} neo_console_pump;

static void neo_console_idle(void *context) {
    neo_console_pump *pump=context;
    neo_events_poll(pump->events,pump->session,pump->platform);
    fflush(stdout);
}

static bool neo_next_console_line(neo_console *console, char *line, size_t capacity, neo_events *events,
                                  neo_session *session, const neo_code_platform *platform) {
    neo_console_pump pump={events,session,platform};
    bool received;
    for (;;) {
        const char *failure=neo_platform_read_line(console,line,capacity,&received,neo_console_idle,&pump);
        if (failure && !strcmp(failure,"command exceeds limit")) { puts("error command exceeds limit"); fflush(stdout); continue; }
        if (failure) { fprintf(stderr,"neo: %s\n",failure); return false; }
        return received;
    }
}
static int neo_live(const char *source, const neo_code_platform *platform) {
    neo_session *session;
    const char *failure=neo_session_open(source,&session);
    if (failure) { fprintf(stderr,"neo: %s\n",failure); return 1; }
    neo_console *console=NULL;
    neo_status console_status=neo_console_open(&console);
    if (console_status!=NEO_OK) { neo_session_close(session); fprintf(stderr,"neo: %s\n",neo_status_name(console_status)); return 1; }
    neo_native_begin();
    neo_lattice layout={0};
    neo_events events={0};
    events.platform=neo_native_event_platform();
    events.complete=neo_console_completion;
    events.completion_context=stdout;
    neo_session_schedule(session,neo_events_schedule,&events);
    static char line[65539], text[65537], warp[65537];
    while (neo_next_console_line(console,line,sizeof(line),&events,session,platform)) {
        line[strcspn(line,"\r\n")]=0;
        failure=NULL;
        if (!strcmp(line,"quit")) { break; }
        if (!strcmp(line,"revision")) {
            printf("revision %" PRIu64 "\n",neo_session_revision(session));
        } else if (!strncmp(line,"post ",5) || !strncmp(line,"after ",6) ||
                   !strncmp(line,"read-text ",10) || !strncmp(line,"cancel ",7) ||
                   !strcmp(line,"poll") || !strcmp(line,"await")) {
            failure=neo_events_command(&events,session,platform,line);
        } else if (!strncmp(line,"allow-import ",13) || !strncmp(line,"revoke-import ",14)) {
            bool allow=line[0]=='a';
            const char *object=strtok(line+(allow ? 13 : 14)," \t");
            const char *alias=strtok(NULL," \t");
            if (!object || !alias || strtok(NULL," \t")) {
                failure="usage: allow-import OBJECT ALIAS or revoke-import OBJECT ALIAS";
            } else {
                failure=neo_session_authorize_import(session,object,alias,allow);
                if (!failure) { puts(allow ? "import allowed" : "import revoked"); }
            }
        } else if (!strncmp(line,"delete ",7)) {
            const char *revision_text=strtok(line+7," \t");
            const char *name=strtok(NULL," \t");
            if (!revision_text || !name || strtok(NULL," \t")) {
                failure="usage: delete REVISION OBJECT";
            } else {
                errno=0;
                char *end;
                uintmax_t revision=strtoumax(revision_text,&end,10);
                if (errno || *end || *revision_text<'0' || *revision_text>'9' || revision>UINT64_MAX) {
                    failure="expected a graph revision";
                } else {
                    failure=neo_session_delete(&session,(uint64_t)revision,name);
                    if (!failure) { printf("revision %" PRIu64 "\n",neo_session_revision(session)); }
                }
            }
        } else if (!strncmp(line,"clone ",6)) {
            const char *revision_text=strtok(line+6," \t");
            const char *prototype=strtok(NULL," \t");
            const char *name=strtok(NULL," \t");
            if (!revision_text || !prototype || !name || strtok(NULL," \t")) {
                failure="usage: clone REVISION PROTOTYPE NAME";
            } else {
                errno=0;
                char *end;
                uintmax_t revision=strtoumax(revision_text,&end,10);
                if (errno || *end || *revision_text<'0' || *revision_text>'9' || revision>UINT64_MAX) {
                    failure="expected a graph revision";
                } else {
                    failure=neo_session_clone(&session,(uint64_t)revision,prototype,name);
                    if (!failure) { printf("revision %" PRIu64 "\n",neo_session_revision(session)); }
                }
            }
        } else if (!strncmp(line,"inspect ",8)) {
            failure=neo_inspect_command(session,line+8,text,sizeof(text));
            if (!failure) { fputs(text,stdout); }
        } else if (!strncmp(line,"evaluate ",9)) {
            failure=neo_evaluate_command(&session,line+9,platform,NEO_EVALUATE,text,sizeof(text));
        } else if (!strncmp(line,"probe ",6)) {
            failure=neo_evaluate_command(&session,line+6,platform,NEO_PROBE,text,sizeof(text));
        } else if (!strncmp(line,"publish ",8)) {
            failure=neo_evaluate_command(&session,line+8,platform,NEO_PUBLISH,text,sizeof(text));
        } else if (!strncmp(line,"transform ",10)) {
            failure=neo_transform_command(session,line+10,text,sizeof(text));
            if (!failure) { puts(text); }
        } else if (!strncmp(line,"lattice ",8)) {
            failure=neo_lattice_command(&layout,session,line+8,text,sizeof(text));
        } else if (!strcmp(line,"source")) {
            failure=neo_session_weft(session,text,sizeof(text));
            if (!failure) { fputs(text,stdout); }
        } else if (!strcmp(line,"snapshot")) {
            failure=neo_session_snapshot(session,text,sizeof(text));
            if (!failure) { puts(text); }
        } else if (!strncmp(line,"send-text ",10)) {
            const char *object=strtok(line+10," \t");
            const char *selector=strtok(NULL," \t");
            char *literal=strtok(NULL,"\r\n");
            if (!object || !selector || !literal) {
                failure="usage: send-text OBJECT SELECTOR \"TEXT\"";
            } else {
                literal+=strspn(literal," \t");
                size_t end=strlen(literal);
                while (end && strchr(" \t",literal[end-1])) { --end; }
                literal[end]=0;
                int32_t result;
                failure=neo_session_send_text(session,object,selector,literal,platform,&result);
                if (!failure) { printf("result %" PRId32 "\n",result); }
            }
        } else if (!strncmp(line,"send ",5) || !strncmp(line,"trace ",6) ||
                   !strncmp(line,"receive-text ",13) || !strncmp(line,"receive-bytes ",14) ||
                   !strncmp(line,"receive-vector ",15) || !strncmp(line,"receive-ref ",12) ||
                   !strncmp(line,"receive-f64 ",12) || !strncmp(line,"receive-f64-vector ",19)) {
            bool tracing=line[0]=='t';
            bool receive_text=line[0]=='r';
            char kind=!strncmp(line,"receive-f64-vector ",19) ? 'd' : line[8]=='f' ? 'f' : line[8]=='r' ? '@' : line[8]=='v' ? '[' : line[8]=='b' ? '#' : '"';
            const char *object=strtok(line+(receive_text ? (kind=='d' ? 19 : (kind=='@' || kind=='f') ? 12 : kind=='[' ? 15 : kind=='#' ? 14 : 13) : tracing ? 6 : 5)," \t");
            const char *selector=strtok(NULL," \t");
            int32_t arguments[NEO_MAX_ARGUMENTS], result;
            size_t count=0;
            if (!object || !selector) {
                failure="usage: send, trace, or receive-text OBJECT SELECTOR [INTEGER ...]";
            }
            for (const char *input=strtok(NULL," \t"); input && !failure; input=strtok(NULL," \t")) {
                if (count==NEO_MAX_ARGUMENTS) { failure="too many arguments"; break; }
                char *end;
                errno=0;
                intmax_t value=strtoimax(input,&end,10);
                if (errno || *end || value<INT32_MIN || value>INT32_MAX) {
                    failure="expected a signed 32-bit integer";
                } else {
                    arguments[count++]=(int32_t)value;
                }
            }
            if (!failure && tracing) {
                printf("trace revision %" PRIu64 "\n",neo_session_revision(session));
                failure=neo_session_trace(session,object,selector,arguments,count,
                                          platform,&result,neo_print_trace,stdout);
            } else if (!failure && receive_text) {
                char literal[NEO_TEXT_RESULT_CAPACITY];
                failure=neo_session_receive_sequence(session,object,selector,arguments,count,
                                                  platform,kind,literal);
                if (!failure) { printf("result %s\n",literal); }
            } else if (!failure) {
                failure=neo_session_send_args(session,object,selector,arguments,count,platform,&result);
            }
            if (!failure && !receive_text) { printf("result %" PRId32 "\n",result); }
        } else if (!strncmp(line,"reload ",7) || !strncmp(line,"reload-weft ",12)) {
            bool weft=line[6]=='-';
            char *digits=line+(weft ? 12 : 7);
            uint64_t revision=0;
            bool valid=*digits>='0' && *digits<='9';
            while (*digits>='0' && *digits<='9') {
                unsigned digit=(unsigned)(*digits++-'0');
                if (revision>(UINT64_MAX-digit)/10u) { valid=false; break; }
                revision=revision*10u+digit;
            }
            if (!valid || *digits!=' ' || !digits[1]) {
                failure="usage: reload REVISION IMAGE or reload-weft REVISION SOURCE";
            } else if (!neo_read_source(digits+1,text,sizeof(text))) {
                failure="could not read Warp source";
            } else {
                if (weft) { failure=neo_weft_lower(text,warp,sizeof(warp)); }
                if (!failure) { failure=neo_session_reload(&session,revision,weft ? warp : text); }
                if (!failure) { printf("revision %" PRIu64 "\n",neo_session_revision(session)); }
            }
        } else {
            failure="commands: post, after, read-text, cancel, poll, await, send, trace, receive-text|bytes|vector|ref|f64|f64-vector, send-text, revision, clone, delete, allow-import OBJECT ALIAS, revoke-import OBJECT ALIAS, reload, reload-weft, inspect, evaluate, probe, publish, transform, lattice, source, snapshot, quit";
        }
        if (failure) { printf("error %s\n",failure); }
        if (fflush(stdout)==EOF) { break; }
    }
    neo_session_close(session);
    neo_events_close(&events);
    neo_console_close(console);
    neo_lattice_clear(&layout);
    neo_native_end();
    return ferror(stdin) || ferror(stdout);
}

#ifndef NEO_FREESTANDING
static bool neo_expect_send(neo_session *session, const neo_code_platform *platform,
                            const char *selector, int32_t expected, const char *error) {
    int32_t result;
    const char *failure=neo_session_send(session,"editor",selector,platform,&result);
    if (error ? failure && !strcmp(failure,error) : !failure && result==expected) { return true; }
    fprintf(stderr,"selftest: unexpected %s response (%s)\n",selector,failure ? failure : "value");
    return false;
}

static bool neo_worker_selftest(void) {
    /* Both payloads modify staging slot zero before faulting or looping. */
    const unsigned char crash[]={0xc7,0x01,99,0,0,0,0x0f,0x0b};
    const unsigned char hang[]={0xc7,0x01,99,0,0,0,0xeb,0xfe};
    const unsigned char value[]={0xb8,42,0,0,0,0xc3};
    int32_t slots[NEO_NATIVE_SLOTS]={7};
    int64_t result=13;
    neo_native_begin();
    const char *failure=neo_native_execute(value,sizeof(value),slots,&result);
    uint32_t first_worker=neo_native_worker_id();
    if (failure || !first_worker || result!=42 ||
        neo_native_execute(value,sizeof(value),slots,&result) ||
        neo_native_worker_id()!=first_worker) {
        neo_native_end();
        fputs("selftest: native worker reuse failed\n",stderr);
        return false;
    }
    result=13;
    failure=neo_native_execute(crash,sizeof(crash),slots,&result);
    if (!failure || strcmp(failure,"native worker failed") || slots[0]!=7 || result!=13) {
        neo_native_end();
        fputs("selftest: worker crash containment failed\n",stderr);
        return false;
    }
    failure=neo_native_execute(hang,sizeof(hang),slots,&result);
    if (!failure || strcmp(failure,"native worker timed out") || slots[0]!=7 || result!=13) {
        neo_native_end();
        fputs("selftest: worker timeout containment failed\n",stderr);
        return false;
    }
    failure=neo_native_execute(value,sizeof(value),slots,&result);
    bool recovered=!failure && result==42 && slots[0]==7 && neo_native_worker_id()!=0;
    neo_native_end();
    if (!recovered) {
        fputs("selftest: native worker recovery failed\n",stderr);
        return false;
    }
    puts("PASS persistent native worker reuse, crash containment, timeout, and recovery");
    return true;
}
static bool neo_package_selftest(const neo_code_platform *platform) {
    const char *source=
        "(warp 1 (entry client start)"
        " (object client (package application) (slot value 0)"
        "  (import add @service add) (import fail @service fail)"
        "  (import read @service read)"
        "  (handler start (send-import add 3))"
        "  (handler fail (do (set self value 99) (send-import fail)))"
        "  (handler private (get @service value))"
        "  (handler direct (send @service add 3))"
        "  (handler stolen (set-ref self selected @service))"
        "  (slot selected @client)"
        "  (handler document (read-document))"
        "  (handler read (send-import read))"
        "  (handler value (get self value)))"
        " (object service (package storage) (slot value 7)"
        "  (export add) (export fail) (export read)"
        "  (handler add amount (set self value (call self plus (get self value) amount)))"
        "  (handler fail (do (set self value 999) (call self plus 2147483647 1)))"
        "  (handler read (read-document))"
        "  (handler value (get self value))"
        "  (knight plus left right (+ left right))))";
    neo_session *session=NULL, *restored=NULL;
    const char *failure=neo_session_open(source,&session);
    int32_t result=0, document=42;
    char snapshot[8192];
    bool passed=false;
    if (failure) { goto done; }
    failure=neo_session_send(session,"client","start",platform,&result);
    if (!failure || strcmp(failure,"package import access denied")) { goto done; }
    if (neo_session_authorize_import(session,"client","add",true) ||
        neo_session_send(session,"client","start",platform,&result) || result!=10) { goto done; }
    const char *denied[]={"private","direct","stolen"};
    for (size_t i=0; i<sizeof(denied)/sizeof(denied[0]); ++i) {
        if (!neo_session_send(session,"client",denied[i],platform,&result)) { goto done; }
    }
    if (neo_session_authorize_import(session,"client","fail",true)) { goto done; }
    failure=neo_session_send(session,"client","fail",platform,&result);
    if (!failure || strcmp(failure,"integer overflow") ||
        neo_session_send(session,"client","value",platform,&result) || result!=0 ||
        neo_session_send(session,"service","value",platform,&result) || result!=10) { goto done; }
    if (neo_session_grant_package_document(session,"storage",&document,NEO_DOCUMENT_READ) ||
        neo_session_authorize_import(session,"client","read",true) ||
        neo_session_send(session,"client","read",platform,&result) || result!=42) { goto done; }
    failure=neo_session_send(session,"client","document",platform,&result);
    if (!failure || strcmp(failure,"document access denied")) { goto done; }
    if (neo_session_snapshot(session,snapshot,sizeof(snapshot)) ||
        neo_session_open(snapshot,&restored)) { goto done; }
    failure=neo_session_send(restored,"client","start",platform,&result);
    if (!failure || strcmp(failure,"package import access denied")) { goto done; }
    if (neo_session_reload(&session,1,source) ||
        neo_session_send(session,"client","start",platform,&result) || result!=13 ||
        neo_session_authorize_import(session,"client","add",false) ||
        neo_session_reload(&session,2,snapshot)) { goto done; }
    failure=neo_session_send(session,"client","start",platform,&result);
    if (!failure || strcmp(failure,"package import access denied")) { goto done; }
    passed=true;
done:
    neo_session_close(restored);
    neo_session_close(session);
    if (!passed) { fprintf(stderr,"selftest: package boundary failed (%s)\n",failure ? failure : "assertion"); }
    else { puts("PASS explicit package imports, scoped grants, revocation, and cross-package rollback"); }
    return passed;
}

static bool neo_scheduling_selftest(const neo_code_platform *platform) {
    const char *source="(warp 1 (entry counter read) (object counter (slot count 0)"
        " (handler read (get self count))"
        " (handler queue (do (set self count 9) (write-document 9) (post self read)))"
        " (knight impure (post @counter read)) (handler try-pure (call self impure))))";
    neo_session *session=NULL;
    const char *failure=neo_session_open(source,&session);
    if (failure) { return false; }
    int32_t document=7, result=0;
    neo_session_grant_document(session,&document,NEO_DOCUMENT_READ|NEO_DOCUMENT_WRITE);
    failure=neo_session_send(session,"counter","queue",platform,&result);
    bool passed=failure && !strcmp(failure,"scheduling service unavailable") && document==7;
    failure=neo_session_send(session,"counter","read",platform,&result);
    passed=passed && !failure && result==0;
    failure=neo_session_send(session,"counter","try-pure",platform,&result);
    passed=passed && failure && !strcmp(failure,"scheduling requires a handler and cannot nest inside an argument");
    neo_session_close(session);
    if (passed) { puts("PASS scheduling service denial rolls back state and document; Knight stays pure"); }
    return passed;
}

static uint64_t neo_test_time;
static uint64_t neo_test_clock(void) { return neo_test_time; }
typedef struct {
    size_t count;
    int32_t results[3];
    bool failed;
} neo_test_completions;
static void neo_test_completion(void *context, const char *id, const char *failure, int32_t result) {
    neo_test_completions *completed=context;
    char expected[32];
    snprintf(expected,sizeof(expected),"image-%zu",completed->count+1u);
    if (failure || strcmp(id,expected) || completed->count>=3) { completed->failed=true; return; }
    completed->results[completed->count++]=result;
}

static bool neo_headless_events_selftest(const neo_code_platform *platform) {
    const char *source="(warp 1 (entry counter read) (object counter (slot count 0)"
        " (handler read (get self count)) (knight add a b (+ a b))"
        " (handler start (post self advance 2))"
        " (handler advance remaining (if (> remaining 0)"
        "  (do (set self count (call self add (get self count) 1))"
        "      (after 5 self advance (call self add remaining -1)) (get self count))"
        "  (get self count)))))";
    neo_session *session=NULL;
    if (neo_session_open(source,&session)) { return false; }
    neo_events events={0};
    neo_test_completions completed={0};
    neo_test_time=100;
    events.platform.clock_ms=neo_test_clock;
    events.complete=neo_test_completion;
    events.completion_context=&completed;
    neo_session_schedule(session,neo_events_schedule,&events);
    int32_t result=0;
    const char *failure=neo_session_send(session,"counter","start",platform,&result);
    bool passed=!failure && result==1 && completed.count==0;
    neo_events_poll(&events,session,platform);
    passed=passed && completed.count==1 && completed.results[0]==1;
    neo_events_poll(&events,session,platform);
    passed=passed && completed.count==1;
    neo_test_time=105;
    neo_events_poll(&events,session,platform);
    passed=passed && completed.count==2 && completed.results[1]==2;
    neo_test_time=110;
    neo_events_poll(&events,session,platform);
    passed=passed && completed.count==3 && completed.results[2]==2 && !completed.failed;
    neo_events_poll(&events,session,platform);
    passed=passed && completed.count==3 && !completed.failed;
    failure=neo_session_send(session,"counter","read",platform,&result);
    passed=passed && !failure && result==2;
    neo_events_close(&events);
    neo_session_close(session);
    if (passed) { puts("PASS headless event replay with injected clock and completion output"); }
    return passed;
}

static int neo_security_selftest(const neo_code_platform *platform) {
    if (!neo_services_selftest() || !neo_platform_services_selftest()) { return 1; }
    if (!neo_optional_services_selftest()) { return 1; }
    if (!neo_network_image_selftest(platform)) { return 1; }
    static char exercise[65537];
    if (!neo_read_source("neo/neo-os/image.neo",exercise,sizeof(exercise)) ||
        !neo_presentation_selftest(exercise,platform)) { return 1; }
    if (!neo_worker_selftest()) { return 1; }
    if (!neo_package_selftest(platform)) { return 1; }
    if (!neo_scheduling_selftest(platform)) { return 1; }
    if (!neo_headless_events_selftest(platform)) { return 1; }
    const char *source=
        "(warp 1 (entry editor read)"
        " (object editor (handler read (read-document))"
        "  (handler save (write-document 42))"
        "  (handler skip (if 0 (write-document 99) 5))"
        "  (handler steal (send @picker read))"
        "  (handler fail (do (write-document 99) (call @editor twice 2147483647)))"
        "  (knight twice n (* n 2))))";
    const char *edit=
        "(warp 1 (entry editor read)"
        " (object editor (handler read (read-document)) (handler save (write-document 77))))";
    const char *forge=
        "(warp 1 (entry editor read)"
        " (object editor (delegate @picker) (handler read (read-document))))";
    const char *trusted=
        "(warp 1 (entry picker read) (object picker (handler read (read-document))))";
    neo_session *editor=NULL, *picker=NULL, *restored=NULL;
    int32_t document_a=7, document_b=13, result;
    char snapshot[2048];
    const char *failure=neo_session_open(source,&editor);
    if (!failure) { failure=neo_session_open(trusted,&picker); }
    bool passed=false;
    if (failure) { goto done; }
    neo_session_grant_document(picker,&document_b,NEO_DOCUMENT_READ);
    if (!neo_expect_send(editor,platform,"read",0,"document access denied")) { goto done; }
    neo_session_grant_document(editor,&document_a,NEO_DOCUMENT_READ);
    if (!neo_expect_send(editor,platform,"read",7,NULL) ||
        !neo_expect_send(editor,platform,"save",0,"document access denied")) { goto done; }
    neo_session_grant_document(editor,&document_a,NEO_DOCUMENT_WRITE);
    if (!neo_expect_send(editor,platform,"read",0,"document access denied") ||
        !neo_expect_send(editor,platform,"skip",5,NULL) || document_a!=7 ||
        !neo_expect_send(editor,platform,"save",42,NULL) || document_a!=42 ||
        !neo_expect_send(editor,platform,"fail",0,"integer overflow") || document_a!=42 ||
        !neo_expect_send(editor,platform,"steal",0,"invalid call or message")) { goto done; }
    failure=neo_session_reload(&editor,1,forge);
    if (!failure || neo_session_revision(editor)!=1) { goto done; }
    failure=neo_session_reload(&editor,1,edit);
    if (failure || !neo_expect_send(editor,platform,"save",77,NULL) || document_a!=77 ||
        neo_session_send(picker,"picker","read",platform,&result) ||
        result!=13 || document_b!=13) { goto done; }
    failure=neo_session_snapshot(editor,snapshot,sizeof(snapshot));
    if (!failure) { failure=neo_session_open(snapshot,&restored); }
    if (failure || !neo_expect_send(restored,platform,"save",0,"document access denied")) { goto done; }
    neo_session_grant_document(editor,NULL,0);
    failure=neo_session_reload(&editor,2,snapshot);
    if (failure || !neo_expect_send(editor,platform,"save",0,"document access denied") ||
        document_a!=77) { goto done; }
    passed=true;
done:
    neo_session_close(restored);
    neo_session_close(picker);
    neo_session_close(editor);
    if (!passed) {
        fprintf(stderr,"selftest: compartment boundary failed (%s)\n",failure ? failure : "assertion");
        return 1;
    }
    puts("PASS isolated graphs, scoped document grants, live edits, rollback, and revocation");
    return 0;
}
#endif
int main(int argc, char **argv) {
    const neo_code_platform platform={neo_native_execute};
#ifndef NEO_FREESTANDING
    if (argc==3 && !strcmp(argv[1],"exercise-snapshot")) { return neo_exercise_checkpoint(argv[2]); }
    if (argc==5 && (!strcmp(argv[1],"exercise") || !strcmp(argv[1],"exercise-test"))) {
        static char exercise_source[65537];
        if (!neo_read_source(argv[2],exercise_source,sizeof(exercise_source))) { return 1; }
        return neo_image_exercise(exercise_source,argv[3],argv[4],!strcmp(argv[1],"exercise-test"),&platform);
    }
#endif
    if (argc==2 && !strcmp(argv[1],"platform")) {
        neo_services *services=NULL;
        neo_status status=neo_platform_services(false,&services);
        if (status!=NEO_OK) { fprintf(stderr,"neo: %s\n",neo_status_name(status)); return 1; }
        neo_capabilities capabilities=neo_services_capabilities(services); neo_machine machine={0};
        status=neo_services_machine(services,&machine);
        if (status==NEO_OK) {
            printf("word-bits %u processors %u page-bytes %u clock-resolution-ns %" PRIu64 "\n",
                   machine.word_bits,machine.processors,machine.page_size,machine.clock_resolution_ns);
            printf("input %u surfaces %u storage %u network-loopback %u entropy %u native-isolation %u\n",
                   (unsigned)capabilities.input,(unsigned)capabilities.surfaces,(unsigned)capabilities.storage,
                   (unsigned)capabilities.network,(unsigned)capabilities.entropy,(unsigned)capabilities.native_isolation);
        }
        neo_services_close(services); return status==NEO_OK ? 0 : 1;
    }
    if ((argc==4 || argc==5) && !strcmp(argv[1],"surface")) {
        static char surface_source[65537];
        if (!neo_read_source(argv[2],surface_source,sizeof(surface_source))) { return 1; }
        neo_renderer renderer=NEO_RENDER_AUTO;
        if (argc==5) {
            if (!strcmp(argv[4],"software")) { renderer=NEO_RENDER_SOFTWARE; }
            else if (!strcmp(argv[4],"opengl")) { renderer=NEO_RENDER_OPENGL; }
            else if (strcmp(argv[4],"auto")) { fputs("neo: renderer must be auto, software, or opengl\n",stderr); return 1; }
        }
        return neo_surface_host(surface_source,argv[3],renderer,&platform);
    }
#ifndef NEO_FREESTANDING
    if (argc==3 && !strcmp(argv[1],"--native-worker")) { return neo_native_worker(argv[2]); }
    if (argc==2 && !strcmp(argv[1],"selftest")) { return neo_security_selftest(&platform); }
#endif
    if (argc!=3 || (strcmp(argv[1],"run") && strcmp(argv[1],"roundtrip") &&
                    strcmp(argv[1],"snapshot") && strcmp(argv[1],"session") &&
                    strcmp(argv[1],"weft") && strcmp(argv[1],"run-weft") && strcmp(argv[1],"source"))) {
        fputs("Usage: neo run|roundtrip|snapshot|session|source IMAGE, neo weft|run-weft SOURCE, neo surface IMAGE OBJECT, neo exercise|exercise-test IMAGE OBJECT STORE, neo exercise-snapshot STORE, neo platform, or neo selftest\n",stderr);
        return 1;
    }
    static char source[65537];
    if (!neo_read_source(argv[2],source,sizeof(source))) {
        fputs("neo: could not read Warp source\n",stderr);
        return 1;
    }
    if (!strcmp(argv[1],"source")) {
        static char weft[65537];
        neo_session *session;
        const char *failure=neo_session_open(source,&session);
        if (!failure) {
            failure=neo_session_weft(session,weft,sizeof(weft));
            neo_session_close(session);
        }
        if (!failure) { return fputs(weft,stdout)==EOF ? 1 : 0; }
        fprintf(stderr,"neo: %s\n",failure);
        return 1;
    }
    if (!strcmp(argv[1],"weft") || !strcmp(argv[1],"run-weft")) {
        static char warp[65537];
        const char *failure=neo_weft_lower(source,warp,sizeof(warp));
        if (!failure && !strcmp(argv[1],"weft")) {
            neo_session *session;
            failure=neo_session_open(warp,&session);
            if (!failure) {
                neo_session_close(session);
                return puts(warp)==EOF ? 1 : 0;
            }
        } else if (!failure) {
            int32_t result;
            failure=neo_run(warp,&platform,&result);
            if (!failure) { return printf("%" PRId32 "\n",result)<0 ? 1 : 0; }
        }
        fprintf(stderr,"neo: %s\n",failure);
        return 1;
    }
    if (!strcmp(argv[1],"session")) { return neo_live(source,&platform); }
    if (!strcmp(argv[1],"snapshot")) {
        static char snapshot[65537];
        const char *failure=neo_snapshot(source,&platform,snapshot,sizeof(snapshot));
        if (failure) { fprintf(stderr,"neo: %s\n",failure); return 1; }
        return puts(snapshot)==EOF ? 1 : 0;
    }
    int32_t result;
    const char *failure=!strcmp(argv[1],"roundtrip") ? neo_roundtrip(source,&platform,&result) :
        neo_run(source,&platform,&result);
    if (failure) { fprintf(stderr,"neo: %s\n",failure); return 1; }
    return printf("%" PRId32 "\n",result)<0 ? 1 : 0;
}
