#define _WIN32_WINNT 0x0a00
#include "../platform.h"

#include <windows.h>
#include <userenv.h>
#include <wchar.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <malloc.h>

void *neo_platform_allocate(size_t bytes, size_t alignment) {
    if (alignment<sizeof(void *) || (alignment & (alignment-1))) { return NULL; }
    return _aligned_malloc(bytes ? bytes : 1,alignment);
}
void *neo_platform_malloc(size_t bytes) { return neo_platform_allocate(bytes,16); }
void *neo_platform_calloc(size_t count, size_t bytes) {
    if (count && bytes>SIZE_MAX/count) { return NULL; }
    size_t length=count*bytes;
    void *memory=neo_platform_malloc(length);
    if (memory) { memset(memory,0,length); }
    return memory;
}
void *neo_platform_realloc(void *memory, size_t bytes) { return _aligned_realloc(memory,bytes ? bytes : 1,16); }
void neo_platform_free(void *memory) { _aligned_free(memory); }

typedef struct {
    size_t length;
    unsigned char code[NEO_NATIVE_CODE_LIMIT];
    int32_t slots[NEO_NATIVE_SLOTS];
    int64_t result;
    bool complete;
} neo_native_packet;

typedef struct {
    HANDLE mapping, request, response, job, process;
    neo_native_packet *packet;
    PSID sid;
    wchar_t name[96];
} neo_native_channel;

static neo_native_channel neo_channel;
static bool neo_keep_worker;

uint32_t neo_native_worker_id(void) {
    return neo_channel.process ? (uint32_t)GetProcessId(neo_channel.process) : 0;
}

void neo_native_end(void) {
    if (neo_channel.process) {
        TerminateProcess(neo_channel.process,0);
        WaitForSingleObject(neo_channel.process,2000);
        CloseHandle(neo_channel.process);
    }
    if (neo_channel.job) { CloseHandle(neo_channel.job); }
    if (neo_channel.packet) { UnmapViewOfFile(neo_channel.packet); }
    if (neo_channel.mapping) { CloseHandle(neo_channel.mapping); }
    if (neo_channel.request) { CloseHandle(neo_channel.request); }
    if (neo_channel.response) { CloseHandle(neo_channel.response); }
    if (neo_channel.sid) {
        FreeSid(neo_channel.sid);
        DeleteAppContainerProfile(neo_channel.name);
    }
    neo_channel=(neo_native_channel){0};
    neo_keep_worker=false;
}

void neo_native_begin(void) {
    neo_native_end();
    neo_keep_worker=true;
}

static const char *neo_exchange(neo_native_channel *channel, const unsigned char *code,
                                size_t length, int32_t *slots, int64_t *result) {
    neo_native_packet *packet=channel->packet;
    packet->length=length;
    packet->complete=false;
    memcpy(packet->code,code,length);
    memcpy(packet->slots,slots,sizeof(packet->slots));
    if (!SetEvent(channel->request)) { return "native worker signal failed"; }
    HANDLE handles[]={channel->response,channel->process};
    DWORD wait=WaitForMultipleObjects(2,handles,FALSE,2000);
    if (wait==WAIT_TIMEOUT) { return "native worker timed out"; }
    if (wait!=WAIT_OBJECT_0 || !packet->complete ||
        WaitForSingleObject(channel->process,0)==WAIT_OBJECT_0) {
        return "native worker failed";
    }
    memcpy(slots,packet->slots,sizeof(packet->slots));
    *result=packet->result;
    return NULL;
}
int neo_native_worker(const char *handle_text) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    HANDLE token;
    DWORD confined, bytes;
    if (!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)) { return 1; }
    BOOL queried=GetTokenInformation(token,TokenIsAppContainer,&confined,sizeof(confined),&bytes);
    CloseHandle(token);
    if (!queried || !confined) { return 1; }

    char *end;
    unsigned long long raw=strtoull(handle_text,&end,10);
    if (!raw || *end!=',') { return 1; }
    HANDLE mapping=(HANDLE)(uintptr_t)raw;
    raw=strtoull(end+1,&end,10);
    if (!raw || *end!=',') { return 1; }
    HANDLE request=(HANDLE)(uintptr_t)raw;
    raw=strtoull(end+1,&end,10);
    if (!raw || *end) { return 1; }
    HANDLE response=(HANDLE)(uintptr_t)raw;
    neo_native_packet *packet=MapViewOfFile(mapping,FILE_MAP_WRITE,0,0,sizeof(*packet));
    if (!packet) { return 1; }
    for (;;) {
        if (WaitForSingleObject(request,INFINITE)!=WAIT_OBJECT_0) { return 1; }
        size_t length=packet->length;
        if (!length || length>sizeof(packet->code)) { return 1; }
        void *memory=VirtualAlloc(NULL,length,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
        if (!memory) { return 1; }
        memcpy(memory,packet->code,length);
        DWORD previous;
        if (!VirtualProtect(memory,length,PAGE_EXECUTE_READ,&previous) ||
            !FlushInstructionCache(GetCurrentProcess(),memory,length)) { return 1; }
        int64_t (*entry)(int32_t *);
        _Static_assert(sizeof(entry)==sizeof(memory),"x86-64 native entry pointer");
        memcpy(&entry,&memory,sizeof(entry));
        packet->result=entry(packet->slots);
        VirtualFree(memory,0,MEM_RELEASE);
        packet->complete=true;
        if (!SetEvent(response)) { return 1; }
    }
}
const char *neo_native_execute(const unsigned char *code, size_t length,
                               int32_t *slots, int64_t *result) {
    if (!length || length>NEO_NATIVE_CODE_LIMIT) { return "invalid native code length"; }
    if (neo_channel.process) {
        const char *failure=neo_exchange(&neo_channel,code,length,slots,result);
        if (failure) {
            bool keep=neo_keep_worker;
            neo_native_end();
            neo_keep_worker=keep;
        }
        return failure;
    }
    const char *failure="could not configure native worker";
    SECURITY_ATTRIBUTES inheritance={sizeof(inheritance),NULL,TRUE};
    HANDLE mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,&inheritance,PAGE_READWRITE,0,
                                     sizeof(neo_native_packet),NULL);
    if (!mapping) { return failure; }
    neo_native_packet *packet=MapViewOfFile(mapping,FILE_MAP_WRITE,0,0,sizeof(*packet));
    HANDLE job=NULL;
    HANDLE request=CreateEventW(&inheritance,FALSE,FALSE,NULL);
    HANDLE response=CreateEventW(&inheritance,FALSE,FALSE,NULL);
    PSID sid=NULL;
    wchar_t name[96];
    LPPROC_THREAD_ATTRIBUTE_LIST attributes=NULL;
    bool initialized=false;
    PROCESS_INFORMATION process={0};
    if (!packet || !request || !response) { goto done; }
    packet->length=length;
    memcpy(packet->code,code,length);
    memcpy(packet->slots,slots,sizeof(packet->slots));

    job=CreateJobObjectW(NULL,NULL);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits={0};
    limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
        JOB_OBJECT_LIMIT_ACTIVE_PROCESS | JOB_OBJECT_LIMIT_PROCESS_MEMORY;
    limits.BasicLimitInformation.ActiveProcessLimit=1;
    limits.ProcessMemoryLimit=64u*1024u*1024u;
    if (!job || !SetInformationJobObject(job,JobObjectExtendedLimitInformation,
                                         &limits,sizeof(limits))) { goto done; }

    wchar_t command[160], executable[32768];
    static volatile LONG sequence;
    if (swprintf(name,96,L"neo.native.%lu.%llu.%ld",(unsigned long)GetCurrentProcessId(),
                 (unsigned long long)GetTickCount64(),
                 (long)InterlockedIncrement(&sequence))<0) { goto done; }
    HRESULT profile=CreateAppContainerProfile(name,name,L"Neo native worker",NULL,0,&sid);
    if (FAILED(profile)) { fprintf(stderr,"profile %lx\n",(unsigned long)profile); goto done; }
    SECURITY_CAPABILITIES security={0};
    security.AppContainerSid=sid;
    SIZE_T size=0;
    InitializeProcThreadAttributeList(NULL,2,0,&size);
    attributes=malloc(size);
    if (!attributes || !InitializeProcThreadAttributeList(attributes,2,0,&size)) { goto done; }
    initialized=true;
    HANDLE inherited[]={mapping,request,response};
    if (!UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherited,
                                   sizeof(inherited),NULL,NULL) ||
        !UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES,
                                   &security,sizeof(security),NULL,NULL)) { goto done; }
    DWORD path_length=GetModuleFileNameW(NULL,executable,32768);
    if (!path_length || path_length>=32768 ||
        swprintf(command,160,L"neo --native-worker %llu,%llu,%llu",
                 (unsigned long long)(uintptr_t)mapping,
                 (unsigned long long)(uintptr_t)request,
                 (unsigned long long)(uintptr_t)response)<0) { goto done; }
    STARTUPINFOEXW startup={0};
    startup.StartupInfo.cb=sizeof(startup);
    startup.lpAttributeList=attributes;
    wchar_t directory[MAX_PATH];
    wchar_t environment[3*(32767+32)+1];
    wchar_t *item=environment;
    const wchar_t *const keys[]={L"LOCALAPPDATA",L"SystemRoot",L"USERPROFILE"};
    for (size_t i=0; i<sizeof(keys)/sizeof(keys[0]); ++i) {
        size_t key_length=wcslen(keys[i]);
        memcpy(item,keys[i],key_length*sizeof(*item));
        item[key_length]=L'=';
        DWORD value_length=GetEnvironmentVariableW(keys[i],item+key_length+1,32767);
        if (!value_length || value_length>=32767) { goto done; }
        item+=key_length+1+value_length+1;
    }
    *item=0;
    if (!GetSystemDirectoryW(directory,MAX_PATH)) { goto done; }
    failure="could not start isolated native worker";
    if (!CreateProcessW(executable,command,NULL,NULL,TRUE,
                        EXTENDED_STARTUPINFO_PRESENT|CREATE_NO_WINDOW|CREATE_SUSPENDED|
                        CREATE_UNICODE_ENVIRONMENT,environment,directory,&startup.StartupInfo,
                        &process)) { fprintf(stderr,"CreateProcessW error %lu\n",(unsigned long)GetLastError()); goto done; }
    if (!AssignProcessToJobObject(job,process.hProcess) ||
        ResumeThread(process.hThread)==(DWORD)-1) { fprintf(stderr,"Worker job error %lu\n",(unsigned long)GetLastError()); goto done; }
    neo_native_channel channel={mapping,request,response,job,process.hProcess,packet,sid,{0}};
    wcscpy(channel.name,name);
    failure=neo_exchange(&channel,code,length,slots,result);
    if (!failure && neo_keep_worker) {
        neo_channel=channel;
        mapping=NULL;
        request=NULL;
        response=NULL;
        job=NULL;
        packet=NULL;
        sid=NULL;
        CloseHandle(process.hThread);
        process=(PROCESS_INFORMATION){0};
    }
done:
    if (process.hProcess) {
        if (TerminateProcess(process.hProcess,1)) {
            WaitForSingleObject(process.hProcess,2000);
        }
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
    if (initialized) { DeleteProcThreadAttributeList(attributes); }
    free(attributes);
    if (sid) { FreeSid(sid); DeleteAppContainerProfile(name); }
    if (job) { CloseHandle(job); }
    if (packet) { UnmapViewOfFile(packet); }
    if (mapping) { CloseHandle(mapping); }
    if (request) { CloseHandle(request); }
    if (response) { CloseHandle(response); }
    return failure;
}

typedef struct {
    HANDLE file;
    OVERLAPPED operation;
    unsigned char bytes[64];
    DWORD length;
    bool complete;
} neo_native_read;

static uint64_t neo_native_clock(void) {
    return (uint64_t)GetTickCount64();
}

static void neo_native_wait(unsigned milliseconds) {
    Sleep(milliseconds);
}

static void neo_native_read_close(void *request) {
    neo_native_read *read=request;
    if (!read) { return; }
    if (!read->complete) {
        CancelIoEx(read->file,&read->operation);
        DWORD transferred;
        GetOverlappedResult(read->file,&read->operation,&transferred,TRUE);
    }
    CloseHandle(read->file);
    CloseHandle(read->operation.hEvent);
    free(read);
}

static const char *neo_native_read_start(const char *path, void **request) {
    int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,NULL,0);
    if (!length) { return "invalid UTF-8 file path"; }
    wchar_t *wide=malloc((size_t)length*sizeof(*wide));
    neo_native_read *read=calloc(1,sizeof(*read));
    if (!wide || !read) { free(wide); free(read); return "out of memory"; }
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,wide,length);
    read->file=CreateFileW(wide,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,
                           FILE_FLAG_OVERLAPPED|FILE_FLAG_SEQUENTIAL_SCAN,NULL);
    free(wide);
    if (read->file==INVALID_HANDLE_VALUE) { free(read); return "could not open text file"; }
    read->operation.hEvent=CreateEventW(NULL,TRUE,FALSE,NULL);
    if (!read->operation.hEvent) { CloseHandle(read->file); free(read); return "could not create I/O event"; }
    if (ReadFile(read->file,read->bytes,sizeof(read->bytes),NULL,&read->operation)) {
        read->complete=GetOverlappedResult(read->file,&read->operation,&read->length,FALSE)!=FALSE;
        if (!read->complete) { neo_native_read_close(read); return "text file read failed"; }
    } else {
        DWORD error=GetLastError();
        if (error==ERROR_HANDLE_EOF) { read->complete=true; }
        else if (error!=ERROR_IO_PENDING) {
            read->complete=true;
            neo_native_read_close(read);
            return "text file read failed";
        }
    }
    *request=read;
    return NULL;
}

static const char *neo_native_read_poll(void *request, bool *ready, char *literal, size_t capacity) {
    neo_native_read *read=request;
    *ready=false;
    if (!read->complete) {
        if (!GetOverlappedResult(read->file,&read->operation,&read->length,FALSE)) {
            DWORD error=GetLastError();
            if (error==ERROR_IO_INCOMPLETE) { return NULL; }
            read->complete=true;
            if (error!=ERROR_HANDLE_EOF) { return "text file read failed"; }
            read->length=0;
        }
        read->complete=true;
    }
    *ready=true;
    size_t length=0;
    if (capacity<3) { return "text result buffer too small"; }
    literal[length++]='"';
    for (DWORD i=0; i<read->length; ++i) {
        unsigned char byte=read->bytes[i];
        char escape=0;
        if (byte=='\n') { escape='n'; }
        else if (byte=='\r') { escape='r'; }
        else if (byte=='\t') { escape='t'; }
        else if (byte=='"' || byte=='\\') { escape=(char)byte; }
        else if (byte<32 || byte==127) { return "text file contains unsupported control bytes"; }
        size_t count=escape ? 2u : 1u;
        if (length+count+2>capacity) { return "text file exceeds current text value limit"; }
        if (escape) {
            literal[length++]='\\';
            literal[length++]=escape;
        } else {
            literal[length++]=(char)byte;
        }
    }
    literal[length++]='"';
    literal[length]=0;
    return NULL;
}

neo_event_platform neo_native_event_platform(void) {
    neo_event_platform platform={neo_native_clock,neo_native_wait,neo_native_read_start,
                                  neo_native_read_poll,neo_native_read_close};
    return platform;
}
