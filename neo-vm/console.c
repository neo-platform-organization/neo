#include "platform.h"
#include <string.h>

struct neo_console {
    neo_services *services;
    neo_handle input;
    unsigned char bytes[4096];
    size_t position, length;
    uint64_t request;
    bool eof;
};
neo_status neo_console_open(neo_console **output) {
    *output=NULL;
    neo_console *console=neo_platform_calloc(1,sizeof(*console));
    if (!console) { return NEO_EXHAUSTED; }
    neo_status status=neo_platform_services(false,&console->services);
    bool terminal; uint32_t columns, rows;
    if (status==NEO_OK) { status=neo_services_standard(console->services,0,&console->input,&terminal,&columns,&rows); }
    if (status!=NEO_OK) { neo_console_close(console); return status; }
    *output=console; return NEO_OK;
}
void neo_console_close(neo_console *console) {
    if (!console) { return; }
    neo_services_close(console->services); neo_platform_free(console);
}
const char *neo_platform_read_line(neo_console *console, char *line, size_t capacity, bool *received,
                                     neo_platform_idle *idle, void *context) {
    *received=false;
    if (capacity<2) { return "invalid console buffer capacity"; }
    size_t length=0; bool truncated=false;
    for (;;) {
        while (console->position<console->length) {
            unsigned char byte=console->bytes[console->position++];
            if (length+1<capacity) { line[length++]=(char)byte; } else { truncated=true; }
            if (byte=='\n') {
                line[length]=0; *received=true; return truncated ? "command exceeds limit" : NULL;
            }
        }
        if (console->eof) {
            line[length]=0; *received=length!=0 || truncated;
            return truncated ? "command exceeds limit" : NULL;
        }
        if (!console->request) {
            neo_status status=neo_services_submit(console->services,console->input,NEO_IO_READ,NULL,
                                                   sizeof(console->bytes),0,&console->request);
            if (status!=NEO_OK) { return neo_status_name(status); }
        }
        neo_services_poll(console->services);
        neo_completion completion;
        if (neo_services_next(console->services,&completion)==NEO_PENDING) {
            if (idle) { idle(context); }
            neo_services_wait(console->services,10); continue;
        }
        console->position=0; console->length=completion.length;
        if (completion.length) { memcpy(console->bytes,completion.bytes,completion.length); }
        neo_status status=completion.status;
        neo_services_release(console->services,completion.id); console->request=0;
        if (status==NEO_EOF) { console->eof=true; }
        else if (status!=NEO_OK) { return neo_status_name(status); }
    }
}
