#define _GNU_SOURCE
#include "../platform.h"
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <seccomp.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <time.h>

void *neo_platform_allocate(size_t bytes, size_t alignment) {
    void *memory=NULL;
    if (alignment<sizeof(void *) || (alignment & (alignment-1))) { return NULL; }
    return posix_memalign(&memory,alignment,bytes ? bytes : 1)==0 ? memory : NULL;
}
void *neo_platform_malloc(size_t bytes) { return malloc(bytes ? bytes : 1); }
void *neo_platform_calloc(size_t count, size_t bytes) { return count && bytes>SIZE_MAX/count ? NULL : calloc(count,bytes); }
void *neo_platform_realloc(void *memory, size_t bytes) { return realloc(memory,bytes ? bytes : 1); }
void neo_platform_free(void *memory) { free(memory); }
typedef struct {
    size_t length; unsigned char code[NEO_NATIVE_CODE_LIMIT];
    int32_t slots[NEO_NATIVE_SLOTS]; int64_t result; bool complete;
} neo_linux_packet;
static struct { pid_t pid; int socket; neo_linux_packet *packet; } neo_linux_native={0,-1,NULL};
static bool neo_linux_keep;
uint32_t neo_native_worker_id(void) { return (uint32_t)neo_linux_native.pid; }
void neo_native_end(void) {
    if (neo_linux_native.pid) { kill(neo_linux_native.pid,SIGKILL); while (waitpid(neo_linux_native.pid,NULL,0)<0 && errno==EINTR) {} }
    if (neo_linux_native.socket>=0) { close(neo_linux_native.socket); }
    if (neo_linux_native.packet) { munmap(neo_linux_native.packet,sizeof(*neo_linux_native.packet)); }
    neo_linux_native.pid=0; neo_linux_native.socket=-1; neo_linux_native.packet=NULL; neo_linux_keep=false;
}
void neo_native_begin(void) { neo_native_end(); neo_linux_keep=true; }
int neo_native_worker(const char *text) {
    char *end; long mapping=strtol(text,&end,10);
    if (*end!=',' || mapping<0 || mapping>INT_MAX) { return 1; }
    long channel=strtol(end+1,&end,10);
    if (*end || channel<0 || channel>INT_MAX || channel==mapping) { return 1; }
    neo_linux_packet *packet=mmap(NULL,sizeof(*packet),PROT_READ|PROT_WRITE,MAP_SHARED,(int)mapping,0);
    if (packet==MAP_FAILED) { return 1; }
    close((int)mapping);
    if (channel) { close_range(0,(unsigned)channel-1u,0); }
    close_range((unsigned)channel+1u,UINT_MAX,0);
    struct rlimit memory={64u*1024u*1024u,64u*1024u*1024u}, core={0,0};
    if (setrlimit(RLIMIT_AS,&memory) || setrlimit(RLIMIT_CORE,&core) || prctl(PR_SET_DUMPABLE,0) ||
        prctl(PR_SET_PDEATHSIG,SIGKILL) || getppid()==1 || prctl(PR_SET_NO_NEW_PRIVS,1,0,0,0)) { return 1; }
    scmp_filter_ctx filter=seccomp_init(SCMP_ACT_KILL_PROCESS);
    if (!filter) { return 1; }
    int failed=seccomp_rule_add(filter,SCMP_ACT_ALLOW,SCMP_SYS(read),1,SCMP_A0(SCMP_CMP_EQ,(scmp_datum_t)channel));
    failed|=seccomp_rule_add(filter,SCMP_ACT_ALLOW,SCMP_SYS(write),1,SCMP_A0(SCMP_CMP_EQ,(scmp_datum_t)channel));
    failed|=seccomp_rule_add(filter,SCMP_ACT_ALLOW,SCMP_SYS(mmap),2,
        SCMP_A2(SCMP_CMP_EQ,PROT_READ|PROT_WRITE),SCMP_A3(SCMP_CMP_EQ,MAP_PRIVATE|MAP_ANONYMOUS));
    failed|=seccomp_rule_add(filter,SCMP_ACT_ALLOW,SCMP_SYS(mprotect),1,SCMP_A2(SCMP_CMP_EQ,PROT_READ|PROT_EXEC));
    failed|=seccomp_rule_add(filter,SCMP_ACT_ALLOW,SCMP_SYS(munmap),0);
    failed|=seccomp_rule_add(filter,SCMP_ACT_ALLOW,SCMP_SYS(exit),0);
    failed|=seccomp_rule_add(filter,SCMP_ACT_ALLOW,SCMP_SYS(exit_group),0);
    if (failed || seccomp_load(filter)) { seccomp_release(filter); return 1; }
    // The filter stays allocated until process exit; no allocator calls after confinement.
    for (;;) {
        char signal;
        if (read((int)channel,&signal,1)!=1) { _exit(1); }
        size_t length=packet->length;
        if (!length || length>NEO_NATIVE_CODE_LIMIT) { _exit(1); }
        void *code=mmap(NULL,length,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        if (code==MAP_FAILED) { _exit(1); }
        memcpy(code,packet->code,length);
        if (mprotect(code,length,PROT_READ|PROT_EXEC)) { _exit(1); }
        int64_t (__attribute__((ms_abi)) *entry)(int32_t *);
        memcpy(&entry,&code,sizeof(entry));
        packet->result=entry(packet->slots); munmap(code,length); packet->complete=true;
        if (write((int)channel,"x",1)!=1) { _exit(1); }
    }
}
const char *neo_native_execute(const unsigned char *code, size_t length, int32_t *slots, int64_t *result) {
    if (!length || length>NEO_NATIVE_CODE_LIMIT) { return "invalid native code length"; }
    if (!neo_linux_native.pid) {
        int mapping=memfd_create("neo.native",0), channels[2];
        if (mapping<0) { return "could not configure native worker"; }
        if (ftruncate(mapping,(off_t)sizeof(neo_linux_packet)) || socketpair(AF_UNIX,SOCK_STREAM,0,channels)) {
            close(mapping); return "could not configure native worker";
        }
        neo_linux_packet *packet=mmap(NULL,sizeof(*packet),PROT_READ|PROT_WRITE,MAP_SHARED,mapping,0);
        if (packet==MAP_FAILED) { close(mapping); close(channels[0]); close(channels[1]); return "could not configure native worker"; }
        char handles[64]; snprintf(handles,sizeof(handles),"%d,%d",mapping,channels[1]);
        pid_t pid=fork();
        if (!pid) {
            close(channels[0]);
            execl("/proc/self/exe","neo","--native-worker",handles,(char *)NULL); _exit(1);
        }
        close(mapping); close(channels[1]);
        if (pid<0) { close(channels[0]); munmap(packet,sizeof(*packet)); return "could not configure native worker"; }
        neo_linux_native.pid=pid; neo_linux_native.socket=channels[0]; neo_linux_native.packet=packet;
    }
    neo_linux_packet *packet=neo_linux_native.packet;
    packet->length=length; packet->complete=false; memcpy(packet->code,code,length); memcpy(packet->slots,slots,sizeof(packet->slots));
    const char *failure=NULL;
    if (send(neo_linux_native.socket,"x",1,MSG_NOSIGNAL)!=1) { failure="native worker failed"; }
    struct pollfd ready={neo_linux_native.socket,POLLIN,0};
    int status=failure ? -1 : poll(&ready,1,2000);
    if (!failure && status==0) { failure="native worker timed out"; }
    char response;
    if (!failure && (status<0 || recv(neo_linux_native.socket,&response,1,0)!=1 || !packet->complete)) { failure="native worker failed"; }
    if (!failure) { memcpy(slots,packet->slots,sizeof(packet->slots)); *result=packet->result; }
    if (failure || !neo_linux_keep) { bool keep=neo_linux_keep; neo_native_end(); neo_linux_keep=keep; }
    return failure;
}
typedef struct { int fd; } neo_linux_read;
static uint64_t neo_linux_clock_ms(void) {
    struct timespec now; clock_gettime(CLOCK_MONOTONIC,&now);
    return (uint64_t)now.tv_sec*1000u+(uint64_t)now.tv_nsec/1000000u;
}
static void neo_linux_wait_ms(unsigned milliseconds) { poll(NULL,0,(int)milliseconds); }
static const char *neo_linux_read_start(const char *path, void **output) {
    int fd=open(path,O_RDONLY|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC);
    if (fd<0) { return "could not open text file"; }
    neo_linux_read *request=malloc(sizeof(*request));
    if (!request) { close(fd); return "out of memory"; }
    request->fd=fd; *output=request; return NULL;
}
static void neo_linux_read_close(void *context) { neo_linux_read *request=context; close(request->fd); free(request); }
static const char *neo_linux_read_poll(void *context, bool *ready, char *literal, size_t capacity) {
    neo_linux_read *request=context; unsigned char bytes[64]; *ready=false;
    ssize_t count=read(request->fd,bytes,sizeof(bytes));
    if (count<0) { return errno==EAGAIN || errno==EINTR ? NULL : "text file read failed"; }
    *ready=true; size_t length=0;
    if (capacity<3) { return "text result buffer too small"; }
    literal[length++]='"';
    for (ssize_t i=0; i<count; ++i) {
        unsigned char byte=bytes[i]; char escape=0;
        if (byte=='\n') { escape='n'; } else if (byte=='\r') { escape='r'; } else if (byte=='\t') { escape='t'; }
        else if (byte=='"' || byte=='\\') { escape=(char)byte; }
        else if (byte<32 || byte==127) { return "text file contains unsupported control bytes"; }
        if (length+(escape ? 2u : 1u)+2>capacity) { return "text file exceeds current text value limit"; }
        if (escape) { literal[length++]='\\'; literal[length++]=escape; } else { literal[length++]=(char)byte; }
    }
    literal[length++]='"'; literal[length]=0; return NULL;
}
neo_event_platform neo_native_event_platform(void) {
    return (neo_event_platform){neo_linux_clock_ms,neo_linux_wait_ms,neo_linux_read_start,neo_linux_read_poll,neo_linux_read_close};
}
