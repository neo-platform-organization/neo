#include "presentation.h"
#include "platform.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>

const char *neo_buffer_apply(void *context, const neo_buffer_command *commands, size_t count) {
    neo_buffer_grant *grant=context;
    neo_frame frame;
    neo_status status=neo_services_acquire(grant->services,grant->surface,&frame);
    if (status!=NEO_OK) { return neo_status_name(status); }
    size_t pixels=(size_t)frame.width*frame.height;
    bool present=false;
    for (size_t i=0; i<count; ++i) {
        if (commands[i].present) {
            if (present || i+1!=count) { neo_services_abandon(grant->services,&frame); return "presentation must end the buffer batch"; }
            present=true;
        } else if (commands[i].offset<0 || commands[i].count<0 ||
            (size_t)commands[i].offset>pixels || (size_t)commands[i].count>pixels-(size_t)commands[i].offset) {
            neo_services_abandon(grant->services,&frame); return "buffer range out of bounds";
        }
    }
    size_t bytes=pixels*4u;
    unsigned char *previous=neo_platform_malloc(bytes);
    if (!previous) { neo_services_abandon(grant->services,&frame); return "out of memory"; }
    memcpy(previous,frame.pixels,bytes);
    for (size_t i=0; i<count; ++i) {
        if (commands[i].present) { continue; }
        uint32_t rgba=(uint32_t)commands[i].rgba;
        for (size_t pixel=(size_t)commands[i].offset; pixel<(size_t)commands[i].offset+(size_t)commands[i].count; ++pixel) {
            unsigned char *destination=frame.pixels+pixel*4u;
            destination[0]=(unsigned char)(rgba>>24); destination[1]=(unsigned char)(rgba>>16);
            destination[2]=(unsigned char)(rgba>>8); destination[3]=(unsigned char)rgba;
        }
    }
    unsigned char *destination=frame.pixels;
    status=present ? neo_services_present(grant->services,&frame) : neo_services_abandon(grant->services,&frame);
    if (status!=NEO_OK) { memcpy(destination,previous,bytes); }
    neo_platform_free(previous);
    return status==NEO_OK ? NULL : neo_status_name(status);
}
const char *neo_surface_configure(neo_services *services, neo_handle surface, neo_session *session,
                                    const char *object, uint32_t width, uint32_t height, uint32_t scale,
                                    const neo_code_platform *platform) {
    neo_status status=neo_services_resize(services,surface,width,height);
    if (status!=NEO_OK) { return neo_status_name(status); }
    int32_t arguments[]={(int32_t)width,(int32_t)height,(int32_t)scale}, result;
    return neo_session_send_args(session,object,"configure",arguments,3,platform,&result);
}
const char *neo_surface_input(neo_session *session, const char *object,
                                        const neo_input *input, const neo_code_platform *platform) {
    int32_t arguments[4]={0}, result; size_t count=0; const char *selector;
    switch (input->kind) {
    case NEO_INPUT_POINTER:
        selector="pointer"; count=3; arguments[0]=(int32_t)input->x; arguments[1]=(int32_t)input->y; arguments[2]=input->modifiers; break;
    case NEO_INPUT_BUTTON:
        selector="button"; count=4; arguments[0]=input->code; arguments[1]=input->pressed ? 1 : 0;
        arguments[2]=(int32_t)input->x; arguments[3]=(int32_t)input->y; break;
    case NEO_INPUT_WHEEL:
        selector="wheel"; count=4; arguments[0]=(int32_t)(input->dx*120.0); arguments[1]=(int32_t)(input->dy*120.0);
        arguments[2]=(int32_t)input->x; arguments[3]=(int32_t)input->y; break;
    case NEO_INPUT_KEY:
        selector="key"; count=3; arguments[0]=input->code; arguments[1]=input->pressed ? 1 : 0; arguments[2]=input->modifiers; break;
    case NEO_INPUT_FOCUS: selector="focus"; count=1; arguments[0]=input->code; break;
    case NEO_INPUT_TEXT: {
        char literal[NEO_TEXT_RESULT_CAPACITY]; size_t length=1; literal[0]='"';
        for (const unsigned char *byte=(const unsigned char *)input->text; *byte; ++byte) {
            if (*byte<32 && *byte!='\n' && *byte!='\r' && *byte!='\t') { return NULL; }
            bool escaped=*byte=='"' || *byte=='\\' || *byte=='\n' || *byte=='\r' || *byte=='\t';
            if (length+(escaped ? 2u : 1u)+2u>sizeof(literal)) { return "input text exceeds current token limit"; }
            if (escaped) { literal[length++]='\\'; }
            literal[length++]=*byte=='\n' ? 'n' : *byte=='\r' ? 'r' : *byte=='\t' ? 't' : (char)*byte;
        }
        literal[length++]='"'; literal[length]=0;
        return neo_session_send_text(session,object,"text",literal,platform,&result);
    }
    default: return "unsupported input event";
    }
    return neo_session_send_args(session,object,selector,arguments,count,platform,&result);
}
int neo_surface_host(const char *source, const char *object, neo_renderer renderer, const neo_code_platform *platform) {
    neo_services *services=NULL; neo_session *session=NULL; neo_handle surface={0};
    const char *failure=neo_session_open(source,&session);
    if (failure) { fprintf(stderr,"neo: %s\n",failure); return 1; }
    neo_status status=neo_platform_services(true,&services);
    if (status==NEO_OK) { status=neo_services_renderer(services,renderer); }
    if (status==NEO_OK) { status=neo_services_surface(services,640,360,&surface); }
    if (status==NEO_OK) { char name[256];
        if (neo_services_renderer_name(services,surface,name,sizeof(name))==NEO_OK) {
            fprintf(stderr,"neo renderer %s%s\n",name,renderer==NEO_RENDER_AUTO && !strcmp(name,"software") ? " (OpenGL unavailable)" : ""); } }
    neo_buffer_grant grant={services,surface}; char package[64];
    neo_events events={0};
    events.platform=neo_native_event_platform();
    neo_session_schedule(session,neo_events_schedule,&events);
    if (status!=NEO_OK) { failure=neo_status_name(status); }
    if (!failure) { failure=neo_session_object_package(session,object,package); }
    if (!failure) { failure=neo_session_grant_buffer(session,package,neo_buffer_apply,&grant); }
    neo_native_begin();
    if (!failure) { failure=neo_surface_configure(services,surface,session,object,640,360,1000,platform); }
    bool running=!failure;
    while (running) {
        neo_events_poll(&events,session,platform);
        neo_input input;
        status=neo_services_input(services,&input);
        if (status==NEO_PENDING) { neo_services_wait(services,10); continue; }
        if (status!=NEO_OK) { failure=neo_status_name(status); break; }
        if (input.kind==NEO_INPUT_CLOSE) { break; }
        if (input.kind==NEO_INPUT_RESIZE) {
            if (!input.width || !input.height) { continue; }
            failure=neo_surface_configure(services,surface,session,object,input.width,input.height,
                                           (uint32_t)(input.scale*1000.0),platform);
        } else {
            failure=neo_surface_input(session,object,&input,platform);
        }
        if (failure) { running=false; }
    }
    if (failure) { fprintf(stderr,"neo: %s\n",failure); }
    neo_events_close(&events);
    neo_native_end(); neo_session_close(session); neo_services_close(services);
    return failure ? 1 : 0;
}
static bool neo_presentation_backend_test(const char *source, const neo_code_platform *platform, bool hosted, neo_renderer renderer) {
    neo_services *services=NULL; neo_session *session=NULL;
    neo_handle surface; bool passed=false; unsigned char pixels[48]; size_t length;
    if ((hosted ? neo_platform_services(false,&services) : neo_services_headless(&services))!=NEO_OK) { return false; }
    const char *failure=neo_session_open(source,&session);
    if (neo_services_renderer(services,renderer)!=NEO_OK) { failure="renderer unavailable"; }
    if (failure || neo_services_surface(services,4,3,&surface)!=NEO_OK) { goto done; }
    neo_buffer_grant grant={services,surface}; char package[64];
    failure=neo_session_object_package(session,"surface-exercise",package);
    if (!failure) { failure=neo_session_grant_buffer(session,package,neo_buffer_apply,&grant); }
    if (!failure) { failure=neo_surface_configure(services,surface,session,"surface-exercise",4,3,1000,platform); }
    if (failure || neo_services_capture(services,surface,pixels,sizeof(pixels),&length)!=NEO_OK || length!=48) { goto done; }
    for (size_t i=0; i<48; i+=4) {
        if (pixels[i]!=0x35 || pixels[i+1]!=0x35 || pixels[i+2]!=0x35 || pixels[i+3]!=0xff) { goto done; }
    }
    neo_input input={.kind=NEO_INPUT_BUTTON,.surface=surface,.code=1,.pressed=true};
    if (neo_services_push_input(services,&input)!=NEO_OK || neo_services_input(services,&input)!=NEO_OK) { goto done; }
    int32_t result;
    failure=neo_surface_input(session,"surface-exercise",&input,platform);
    if (failure || neo_services_capture(services,surface,pixels,sizeof(pixels),&length)!=NEO_OK) { goto done; }
    for (size_t i=0; i<48; i+=4) {
        if (pixels[i]!=0x91 || pixels[i+1]!=0x91 || pixels[i+2]!=0x91 || pixels[i+3]!=0xff) { goto done; }
    }
    failure=neo_session_send(session,"surface-exercise","bad-frame",platform,&result);
    if (!failure || strcmp(failure,"buffer range out of bounds")) { goto done; }
    if (neo_services_capture(services,surface,pixels,sizeof(pixels),&length)!=NEO_OK || pixels[0]!=0x91) { goto done; }
    failure=neo_session_send(session,"surface-exercise","reading",platform,&result);
    if (failure || result!=1) { goto done; }
    neo_session_grant_buffer(session,package,NULL,NULL);
    failure=neo_session_send(session,"surface-exercise","draw",platform,&result);
    if (!failure || strcmp(failure,"buffer access denied")) { goto done; }
    passed=true;
done:
    if (!passed) { fprintf(stderr,"presentation selftest: %s\n",failure ? failure : "frame mismatch"); }
    neo_session_close(session); neo_services_close(services);
    return passed;
}
bool neo_presentation_selftest(const char *source, const neo_code_platform *platform) {
    neo_services *probe=NULL;
    if (neo_platform_services(false,&probe)!=NEO_OK) { return false; }
    bool display=neo_services_capabilities(probe).surfaces, accelerated=false;
    if (display) {
        neo_handle surface;
        neo_status status=neo_services_renderer(probe,NEO_RENDER_OPENGL);
        if (status==NEO_OK) { status=neo_services_surface(probe,4,3,&surface); }
        accelerated=status==NEO_OK;
        if (status!=NEO_OK && status!=NEO_UNSUPPORTED) { neo_services_close(probe); return false; }
    }
    neo_services_close(probe);
    if (!display) {
        puts("SKIP hosted display unavailable; headless image rendering still tested");
        return neo_presentation_backend_test(source,platform,false,NEO_RENDER_SOFTWARE);
    }
    bool passed=neo_presentation_backend_test(source,platform,false,NEO_RENDER_SOFTWARE) &&
        neo_presentation_backend_test(source,platform,true,NEO_RENDER_SOFTWARE) &&
        (!accelerated || neo_presentation_backend_test(source,platform,true,NEO_RENDER_OPENGL));
    if (!accelerated) { puts("SKIP OpenGL unavailable; hosted software presentation still tested"); }
    if (passed) { puts(accelerated ? "PASS canonical image: identical headless/software/OpenGL frames, input, buffer rollback, and revocation" :
        "PASS canonical image: identical headless/software frames, input, buffer rollback, and revocation"); }
    return passed;
}
