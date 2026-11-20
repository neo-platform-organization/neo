#include "platform.h"
#include "services.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct {
    bool active, memory, leased;
    uint64_t generation, frame_generation;
    neo_resource_kind kind;
    unsigned rights;
    void *native;
    unsigned char *bytes, *captured;
    size_t length, position;
    uint32_t width, height;
    uint32_t audio_alignment;
} neo_resource;
typedef struct {
    bool active, finished, reported;
    uint64_t id, deadline;
    neo_handle handle;
    neo_io_kind operation;
    unsigned char *buffer;
    size_t capacity;
    void *ticket;
    neo_completion result;
    char key[64];
    uint64_t expected;
} neo_request;
struct neo_services {
    neo_service_backend backend;
    void *context;
    neo_capabilities capabilities;
    uint64_t owner, next;
    bool stopping, headless;
    neo_resource resources[NEO_SERVICE_RESOURCES];
    neo_request requests[NEO_SERVICE_REQUESTS];
    neo_input inputs[NEO_SERVICE_EVENTS];
    size_t input_count;
};
static atomic_uint_fast64_t neo_service_owners=1;
static bool neo_utf8_bytes(const unsigned char *bytes, size_t length) {
    for (size_t i=0; i<length;) {
        unsigned first=bytes[i++], count;
        if (!first) { return false; } if (first<0x80) { continue; }
        if (first>=0xc2 && first<=0xdf) { count=1; }
        else if (first>=0xe0 && first<=0xef) { count=2; }
        else if (first>=0xf0 && first<=0xf4) { count=3; }
        else { return false; }
        if (count>length-i || (first==0xe0 && bytes[i]<0xa0) || (first==0xed && bytes[i]>=0xa0) ||
            (first==0xf0 && bytes[i]<0x90) || (first==0xf4 && bytes[i]>=0x90)) { return false; }
        for (unsigned j=0; j<count; ++j) { if (bytes[i]<0x80 || bytes[i]>0xbf) { return false; } ++i; }
    }
    return true;
}
static bool neo_service_budget(const neo_services *services, size_t extra) {
    size_t used=0;
    for (size_t i=0; i<NEO_SERVICE_RESOURCES; ++i) {
        const neo_resource *resource=&services->resources[i];
        if (resource->active) { used+=resource->memory ? NEO_SERVICE_BYTES : resource->length*2u; }
    }
    for (size_t i=0; i<NEO_SERVICE_REQUESTS; ++i) {
        if (services->requests[i].active) { used+=services->requests[i].capacity; }
    }
    return extra<=64u*1024u*1024u && used<=64u*1024u*1024u-extra;
}
const char *neo_status_name(neo_status status) {
    static const char *const names[]={"ok","pending","unsupported","denied","closed","exhausted",
        "cancelled","timeout","eof","conflict","invalid","io-error","device-lost","truncated","uncertain"};
    return (unsigned)status<sizeof(names)/sizeof(names[0]) ? names[status] : "invalid-status";
}
static neo_resource *neo_resource_get(neo_services *services, neo_handle handle) {
    if (handle.owner!=services->owner || handle.slot>=NEO_SERVICE_RESOURCES) { return NULL; }
    neo_resource *resource=&services->resources[handle.slot];
    return resource->active && resource->generation==handle.generation &&
        (uint32_t)resource->kind==handle.kind ? resource : NULL;
}
static neo_status neo_resource_right(neo_services *services, neo_handle handle, unsigned right,
                                      neo_resource **resource) {
    if (services->stopping) { *resource=NULL; return NEO_CLOSED; }
    *resource=neo_resource_get(services,handle);
    if (!*resource) { return NEO_CLOSED; }
    return ((*resource)->rights & right)==right ? NEO_OK : NEO_DENIED;
}
neo_status neo_services_open(const neo_service_backend *backend, void *context,
                              neo_capabilities capabilities, neo_services **output) {
    *output=NULL;
    if (!backend || !backend->clock_ns) { return NEO_INVALID; }
    neo_services *services=neo_platform_calloc(1,sizeof(*services));
    if (!services) { return NEO_EXHAUSTED; }
    uint_fast64_t owner=atomic_load(&neo_service_owners);
    for (;;) {
        if (!owner || owner>=UINT64_MAX) { neo_platform_free(services); return NEO_EXHAUSTED; }
        if (atomic_compare_exchange_weak(&neo_service_owners,&owner,owner+1)) { break; }
    }
    services->owner=(uint64_t)owner;
    services->backend=*backend;
    services->context=context;
    services->capabilities=capabilities;
    *output=services;
    return NEO_OK;
}
neo_capabilities neo_services_capabilities(const neo_services *services) { return services->capabilities; }
uint64_t neo_services_clock(neo_services *services) { return services->backend.clock_ns(services->context); }
neo_status neo_services_entropy(neo_services *services, void *bytes, size_t length) {
    if (services->stopping) { return NEO_CLOSED; }
    if ((!bytes && length) || length>NEO_SERVICE_BYTES) { return NEO_INVALID; }
    return services->backend.entropy ? services->backend.entropy(services->context,bytes,length) : NEO_UNSUPPORTED;
}
neo_status neo_services_adopt(neo_services *services, neo_resource_kind kind, unsigned rights,
                               void *native, neo_handle *handle) {
    *handle=(neo_handle){0};
    if (services->stopping || kind<NEO_RESOURCE_STREAM || kind>NEO_RESOURCE_CLIPBOARD) { return NEO_INVALID; }
    for (uint32_t i=0; i<NEO_SERVICE_RESOURCES; ++i) {
        neo_resource *resource=&services->resources[i];
        if (!resource->active && resource->generation!=UINT64_MAX) {
            uint64_t generation=resource->generation+1;
            *resource=(neo_resource){.active=true,.generation=generation,.kind=kind,.rights=rights,.native=native};
            *handle=(neo_handle){services->owner,generation,i,(uint32_t)kind};
            if (native && services->backend.bind) { services->backend.bind(services->context,native,*handle); }
            return NEO_OK;
        }
    }
    return NEO_EXHAUSTED;
}
neo_status neo_services_memory_stream(neo_services *services, unsigned rights, const void *bytes,
                                        size_t length, neo_handle *handle) {
    *handle=(neo_handle){0};
    if (length>NEO_SERVICE_BYTES || (!bytes && length)) { return NEO_INVALID; }
    if (!neo_service_budget(services,NEO_SERVICE_BYTES)) { return NEO_EXHAUSTED; }
    unsigned char *copy=neo_platform_malloc(NEO_SERVICE_BYTES);
    if (!copy) { return NEO_EXHAUSTED; }
    if (length) { memcpy(copy,bytes,length); }
    neo_status status=neo_services_adopt(services,NEO_RESOURCE_STREAM,rights,NULL,handle);
    if (status!=NEO_OK) { neo_platform_free(copy); return status; }
    neo_resource *resource=neo_resource_get(services,*handle);
    resource->memory=true;
    resource->bytes=copy;
    resource->length=length;
    return NEO_OK;
}
static void neo_request_finish(neo_services *services, neo_request *request, neo_status status) {
    if (request->finished) { return; }
    if (request->ticket && services->backend.cancel) {
        if ((request->operation==NEO_IO_WRITE || request->operation==NEO_IO_STORE_REPLACE || request->operation==NEO_IO_STORE_DELETE) &&
            (status==NEO_CANCELLED || status==NEO_TIMEOUT || status==NEO_DENIED)) { status=NEO_UNCERTAIN; }
        services->backend.cancel(services->context,request->ticket);
        request->ticket=NULL;
    }
    request->finished=true;
    request->result.id=request->id;
    request->result.status=status;
    request->result.bytes=request->buffer;
}
neo_status neo_services_revoke(neo_services *services, neo_handle handle) {
    neo_resource *resource=neo_resource_get(services,handle);
    if (!resource) { return NEO_CLOSED; }
    for (size_t i=0; i<NEO_SERVICE_REQUESTS; ++i) {
        neo_request *request=&services->requests[i];
        if (request->active && !request->finished && request->handle.slot==handle.slot) {
            neo_request_finish(services,request,NEO_DENIED);
        }
    }
    if (resource->native && services->backend.close) {
        services->backend.close(services->context,resource->native,resource->kind);
    }
    neo_platform_free(resource->bytes); neo_platform_free(resource->captured);
    uint64_t generation=resource->generation;
    *resource=(neo_resource){.generation=generation};
    return NEO_OK;
}
static neo_status neo_service_submit(neo_services *services, neo_handle handle, neo_io_kind operation,
                                       const void *bytes, size_t length, uint64_t deadline, uint64_t *id,
                                       const char *key, uint64_t expected) {
    *id=0;
    bool store=operation==NEO_IO_STORE_READ || operation==NEO_IO_STORE_REPLACE || operation==NEO_IO_STORE_DELETE;
    bool write=operation==NEO_IO_WRITE || operation==NEO_IO_STORE_REPLACE || operation==NEO_IO_STORE_DELETE;
    if (services->stopping || (unsigned)operation>NEO_IO_STORE_DELETE || length>NEO_SERVICE_BYTES ||
        (write && !bytes && length)) { return NEO_INVALID; }
    neo_resource *resource;
    unsigned right=write ? NEO_RIGHT_WRITE : operation==NEO_IO_ACCEPT ? NEO_RIGHT_ACCEPT : NEO_RIGHT_READ;
    neo_status status=neo_resource_right(services,handle,right,&resource);
    if (status!=NEO_OK) { return status; }
    if ((store && resource->kind!=NEO_RESOURCE_ROOT) ||
        (operation==NEO_IO_ACCEPT && resource->kind!=NEO_RESOURCE_LISTENER) ||
        (!store && operation!=NEO_IO_ACCEPT && resource->kind!=NEO_RESOURCE_STREAM &&
         resource->kind!=NEO_RESOURCE_CONNECTION && resource->kind!=NEO_RESOURCE_DATAGRAM &&
         resource->kind!=NEO_RESOURCE_AUDIO && resource->kind!=NEO_RESOURCE_CLIPBOARD)) { return NEO_INVALID; }
    if (resource->kind==NEO_RESOURCE_AUDIO && (!resource->audio_alignment || length%resource->audio_alignment)) { return NEO_INVALID; }
    if (resource->kind==NEO_RESOURCE_CLIPBOARD && write && !neo_utf8_bytes(bytes,length)) { return NEO_INVALID; }
    for (size_t i=0; i<NEO_SERVICE_REQUESTS; ++i) {
        neo_request *request=&services->requests[i];
        if (resource->kind!=NEO_RESOURCE_AUDIO && request->active && !request->finished && request->handle.slot==handle.slot && request->operation==operation) { return NEO_EXHAUSTED; }
    }
    if (services->next==UINT64_MAX || !neo_service_budget(services,length)) { return NEO_EXHAUSTED; }
    for (size_t i=0; i<NEO_SERVICE_REQUESTS; ++i) {
        neo_request *request=&services->requests[i];
        if (request->active) { continue; }
        unsigned char *buffer=neo_platform_calloc(length ? length : 1,1);
        if (!buffer) { return NEO_EXHAUSTED; }
        if (write && length) { memcpy(buffer,bytes,length); }
        *request=(neo_request){.active=true,.id=++services->next,.deadline=deadline,
            .handle=handle,.operation=operation,.buffer=buffer,.capacity=length};
        if (key) { strcpy(request->key,key); request->expected=expected; }
        *id=request->id;
        return NEO_OK;
    }
    return NEO_EXHAUSTED;
}
neo_status neo_services_submit(neo_services *services, neo_handle handle, neo_io_kind operation,
                                 const void *bytes, size_t length, uint64_t deadline, uint64_t *id) {
    if ((unsigned)operation>NEO_IO_ACCEPT) { *id=0; return NEO_INVALID; }
    return neo_service_submit(services,handle,operation,bytes,length,deadline,id,NULL,0);
}
neo_status neo_services_cancel(neo_services *services, uint64_t id) {
    for (size_t i=0; i<NEO_SERVICE_REQUESTS; ++i) {
        neo_request *request=&services->requests[i];
        if (request->active && request->id==id && !request->finished) {
            neo_request_finish(services,request,NEO_CANCELLED); return NEO_OK;
        }
    }
    return NEO_CLOSED;
}
void neo_services_poll(neo_services *services) {
    uint64_t previous=0;
    for (size_t visited=0; visited<NEO_SERVICE_REQUESTS; ++visited) {
        neo_request *request=NULL;
        for (size_t i=0; i<NEO_SERVICE_REQUESTS; ++i) {
            neo_request *candidate=&services->requests[i];
            if (candidate->active && !candidate->finished && candidate->id>previous &&
                (!request || candidate->id<request->id)) { request=candidate; }
        }
        if (!request) { break; } previous=request->id;
        if (request->deadline && neo_services_clock(services)>=request->deadline) {
            neo_request_finish(services,request,NEO_TIMEOUT); continue;
        }
        neo_resource *resource=neo_resource_get(services,request->handle);
        neo_status status=NEO_CLOSED;
        size_t length=0;
        void *accepted=NULL;
        if (resource && resource->memory) {
            if (request->operation==NEO_IO_READ) {
                length=resource->length-resource->position;
                if (length>request->capacity) { length=request->capacity; }
                memcpy(request->buffer,resource->bytes+resource->position,length);
                resource->position+=length;
                status=length || !request->capacity ? NEO_OK : NEO_EOF;
            } else {
                length=NEO_SERVICE_BYTES-resource->length;
                if (length>request->capacity) { length=request->capacity; }
                memcpy(resource->bytes+resource->length,request->buffer,length);
                resource->length+=length;
                status=length || !request->capacity ? NEO_OK : NEO_EXHAUSTED;
            }
        } else if (resource && resource->kind==NEO_RESOURCE_ROOT && services->backend.store_step) {
            status=services->backend.store_step(services->context,resource->native,request->operation,
                request->key,request->expected,&request->ticket,request->buffer,request->capacity,
                &length,&request->result.revision);
        } else if (resource && services->backend.step) {
            status=services->backend.step(services->context,resource->native,request->operation,
                &request->ticket,request->buffer,request->capacity,&length,&accepted);
        }
        if ((unsigned)status>NEO_UNCERTAIN) { status=NEO_IO_ERROR; }
        if (accepted && status!=NEO_OK) {
            if (services->backend.close) { services->backend.close(services->context,accepted,NEO_RESOURCE_CONNECTION); }
            accepted=NULL;
            if (status==NEO_PENDING) { status=NEO_IO_ERROR; }
        }
        if (status==NEO_PENDING) { continue; }
        if (length>request->capacity) {
            if (status==NEO_TRUNCATED) { request->result.required_length=length; }
            else { status=NEO_IO_ERROR; }
            length=0;
        }
        if (resource && resource->kind==NEO_RESOURCE_CLIPBOARD && request->operation==NEO_IO_READ &&
            status==NEO_OK && !neo_utf8_bytes(request->buffer,length)) { status=NEO_IO_ERROR; length=0; }
        if (accepted) {
            status=neo_services_adopt(services,NEO_RESOURCE_CONNECTION,NEO_RIGHT_READ|NEO_RIGHT_WRITE,accepted,
                                      &request->result.accepted);
            if (status!=NEO_OK && services->backend.close) {
                services->backend.close(services->context,accepted,NEO_RESOURCE_CONNECTION);
            }
        }
        request->result.length=length;
        neo_request_finish(services,request,status);
    }
}
void neo_services_wait(neo_services *services, uint32_t milliseconds) {
    neo_services_poll(services);
    for (size_t i=0; i<NEO_SERVICE_REQUESTS; ++i) {
        if (services->requests[i].active && services->requests[i].finished && !services->requests[i].reported) { return; }
    }
    if (services->backend.wait) { services->backend.wait(services->context,milliseconds); }
    neo_services_poll(services);
}
void neo_services_wake(neo_services *services) {
    if (services->backend.wake) { services->backend.wake(services->context); }
}
neo_status neo_services_machine(neo_services *services, neo_machine *machine) {
    return services->backend.machine ? services->backend.machine(services->context,machine) : NEO_UNSUPPORTED;
}
neo_status neo_services_renderer(neo_services *services, neo_renderer renderer) {
    if ((unsigned)renderer>NEO_RENDER_OPENGL) { return NEO_INVALID; }
    for (size_t i=0; i<NEO_SERVICE_RESOURCES; ++i) {
        if (services->resources[i].active && services->resources[i].kind==NEO_RESOURCE_SURFACE) { return NEO_CONFLICT; }
    }
    return services->backend.renderer ? services->backend.renderer(services->context,renderer) :
        renderer==NEO_RENDER_OPENGL ? NEO_UNSUPPORTED : NEO_OK;
}
neo_status neo_services_renderer_name(neo_services *services, neo_handle handle, char *text, size_t capacity) {
    neo_resource *resource=neo_resource_get(services,handle);
    if (!resource) { return NEO_CLOSED; }
    if (resource->kind!=NEO_RESOURCE_SURFACE || capacity<9) { return NEO_INVALID; }
    if (services->backend.renderer_name) { return services->backend.renderer_name(services->context,resource->native,text,capacity); }
    memcpy(text,"software",9); return NEO_OK;
}
neo_status neo_services_audio(neo_services *services, neo_audio_format format, neo_handle *handle) {
    *handle=(neo_handle){0}; if (services->stopping) { return NEO_CLOSED; }
    if (format.rate<8000 || format.rate>192000 || !format.channels || format.channels>8) { return NEO_INVALID; }
    if (!services->backend.audio) { return NEO_UNSUPPORTED; }
    void *native=NULL; neo_status status=services->backend.audio(services->context,&format,&native);
    if (status!=NEO_OK) { return status; }
    status=neo_services_adopt(services,NEO_RESOURCE_AUDIO,format.capture ? NEO_RIGHT_READ : NEO_RIGHT_WRITE,native,handle);
    if (status==NEO_OK) { neo_resource_get(services,*handle)->audio_alignment=format.channels*2u; }
    if (status!=NEO_OK && services->backend.close) { services->backend.close(services->context,native,NEO_RESOURCE_AUDIO); }
    return status;
}
neo_status neo_services_audio_stats(neo_services *services, neo_handle handle, neo_audio_stats *stats) {
    neo_resource *resource=neo_resource_get(services,handle); if (!resource) { return NEO_CLOSED; }
    if (resource->kind!=NEO_RESOURCE_AUDIO) { return NEO_INVALID; }
    return services->backend.audio_stats ? services->backend.audio_stats(services->context,resource->native,stats) : NEO_UNSUPPORTED;
}
neo_status neo_services_clipboard(neo_services *services, unsigned rights, neo_handle *handle) {
    *handle=(neo_handle){0}; if (services->stopping) { return NEO_CLOSED; }
    if (!services->backend.clipboard) { return NEO_UNSUPPORTED; }
    void *native=NULL; neo_status status=services->backend.clipboard(services->context,&native);
    if (status!=NEO_OK) { return status; }
    status=neo_services_adopt(services,NEO_RESOURCE_CLIPBOARD,rights&(NEO_RIGHT_READ|NEO_RIGHT_WRITE),native,handle);
    if (status!=NEO_OK && services->backend.close) { services->backend.close(services->context,native,NEO_RESOURCE_CLIPBOARD); }
    return status;
}
neo_status neo_services_position(neo_services *services, neo_handle handle, int32_t x, int32_t y) {
    neo_resource *resource; neo_status status=neo_resource_right(services,handle,NEO_RIGHT_PRESENT,&resource);
    if (status!=NEO_OK) { return status; } if (resource->kind!=NEO_RESOURCE_SURFACE) { return NEO_INVALID; }
    return services->backend.position ? services->backend.position(services->context,resource->native,x,y) : NEO_UNSUPPORTED;
}
neo_status neo_services_next(neo_services *services, neo_completion *completion) {
    neo_request *first=NULL;
    for (size_t i=0; i<NEO_SERVICE_REQUESTS; ++i) {
        neo_request *request=&services->requests[i];
        if (request->active && request->finished && !request->reported && (!first || request->id<first->id)) { first=request; }
    }
    if (!first) { return NEO_PENDING; }
    first->reported=true; *completion=first->result; return NEO_OK;
}
neo_status neo_services_release(neo_services *services, uint64_t id) {
    for (size_t i=0; i<NEO_SERVICE_REQUESTS; ++i) {
        neo_request *request=&services->requests[i];
        if (request->active && request->id==id && request->finished && request->reported) {
            neo_platform_free(request->buffer); *request=(neo_request){0}; return NEO_OK;
        }
    }
    return NEO_CLOSED;
}
void neo_services_shutdown(neo_services *services, void (*sink)(void *, const neo_completion *), void *context) {
    services->stopping=true;
    for (size_t i=0; i<NEO_SERVICE_REQUESTS; ++i) {
        neo_request *request=&services->requests[i];
        if (!request->active) { continue; }
        neo_request_finish(services,request,NEO_CANCELLED);
        if (!request->reported && sink) { sink(context,&request->result); }
        neo_platform_free(request->buffer); *request=(neo_request){0};
    }
}
void neo_services_close(neo_services *services) {
    if (!services) { return; }
    neo_services_shutdown(services,NULL,NULL);
    for (uint32_t i=0; i<NEO_SERVICE_RESOURCES; ++i) {
        neo_resource *resource=&services->resources[i];
        if (resource->active) {
            neo_handle handle={services->owner,resource->generation,i,(uint32_t)resource->kind};
            neo_services_revoke(services,handle);
        }
    }
    if (services->backend.destroy) { services->backend.destroy(services->context); }
    neo_platform_free(services);
}
static bool neo_dimensions(uint32_t width, uint32_t height) {
    return width && height && width<=4096 && height<=4096;
}
neo_status neo_services_surface(neo_services *services, uint32_t width, uint32_t height, neo_handle *handle) {
    *handle=(neo_handle){0};
    if (services->stopping) { return NEO_CLOSED; }
    if (!neo_dimensions(width,height)) { return NEO_INVALID; }
    if (!services->capabilities.surfaces) { return NEO_UNSUPPORTED; }
    void *native=NULL;
    neo_status status=services->backend.surface_open ?
        services->backend.surface_open(services->context,width,height,&native) : NEO_OK;
    if (status!=NEO_OK) { return status; }
    status=neo_services_adopt(services,NEO_RESOURCE_SURFACE,NEO_RIGHT_PRESENT|NEO_RIGHT_INPUT,native,handle);
    if (status!=NEO_OK) {
        if (native && services->backend.close) { services->backend.close(services->context,native,NEO_RESOURCE_SURFACE); }
        return status;
    }
    status=neo_services_resize(services,*handle,width,height);
    if (status!=NEO_OK) { neo_services_revoke(services,*handle); *handle=(neo_handle){0}; }
    return status;
}
neo_status neo_services_resize(neo_services *services, neo_handle handle, uint32_t width, uint32_t height) {
    neo_resource *resource;
    neo_status status=neo_resource_right(services,handle,NEO_RIGHT_PRESENT,&resource);
    if (status!=NEO_OK) { return status; }
    if (resource->kind!=NEO_RESOURCE_SURFACE || !neo_dimensions(width,height)) { return NEO_INVALID; }
    if (resource->leased) { return NEO_PENDING; }
    if (resource->frame_generation==UINT64_MAX) { return NEO_EXHAUSTED; }
    size_t length=(size_t)width*height*4u;
    if (!neo_service_budget(services,length*2u)) { return NEO_EXHAUSTED; }
    unsigned char *pixels=neo_platform_calloc(length,1), *captured=neo_platform_calloc(length,1);
    if (!pixels || !captured) { neo_platform_free(pixels); neo_platform_free(captured); return NEO_EXHAUSTED; }
    neo_platform_free(resource->bytes); neo_platform_free(resource->captured);
    resource->bytes=pixels; resource->captured=captured; resource->length=length;
    resource->width=width; resource->height=height; ++resource->frame_generation;
    return NEO_OK;
}
neo_status neo_services_acquire(neo_services *services, neo_handle handle, neo_frame *frame) {
    neo_resource *resource;
    neo_status status=neo_resource_right(services,handle,NEO_RIGHT_PRESENT,&resource);
    if (status!=NEO_OK) { return status; }
    if (resource->kind!=NEO_RESOURCE_SURFACE) { return NEO_INVALID; }
    if (resource->leased) { return NEO_PENDING; }
    if (resource->frame_generation==UINT64_MAX) { return NEO_EXHAUSTED; }
    ++resource->frame_generation;
    resource->leased=true;
    *frame=(neo_frame){handle,resource->frame_generation,resource->width,resource->height,
                        (size_t)resource->width*4u,resource->bytes};
    return NEO_OK;
}
neo_status neo_services_present(neo_services *services, neo_frame *frame) {
    neo_resource *resource=neo_resource_get(services,frame->surface);
    if (!resource) { return NEO_CLOSED; }
    if (!resource->leased || resource->frame_generation!=frame->generation || frame->pixels!=resource->bytes ||
        frame->width!=resource->width || frame->height!=resource->height || frame->stride!=(size_t)resource->width*4u) { return NEO_INVALID; }
    neo_status status=services->backend.present ? services->backend.present(services->context,resource->native,frame) : NEO_OK;
    if (status==NEO_OK) { memcpy(resource->captured,resource->bytes,resource->length); }
    resource->leased=false; frame->pixels=NULL;
    return status;
}
neo_status neo_services_abandon(neo_services *services, neo_frame *frame) {
    neo_resource *resource=neo_resource_get(services,frame->surface);
    if (!resource || !resource->leased || frame->generation!=resource->frame_generation || frame->pixels!=resource->bytes) { return NEO_CLOSED; }
    resource->leased=false; frame->pixels=NULL; return NEO_OK;
}
neo_status neo_services_capture(neo_services *services, neo_handle handle, void *bytes, size_t capacity, size_t *length) {
    neo_resource *resource=neo_resource_get(services,handle);
    *length=0;
    if (!resource) { return NEO_CLOSED; }
    if (resource->kind!=NEO_RESOURCE_SURFACE || (!bytes && capacity)) { return NEO_INVALID; }
    *length=resource->length;
    if (capacity<*length) { return NEO_TRUNCATED; }
    memcpy(bytes,resource->captured,*length); return NEO_OK;
}
static bool neo_input_valid(const neo_input *input) {
    if ((unsigned)input->kind>NEO_INPUT_CLOSE || !isfinite(input->x) || !isfinite(input->y) ||
        !isfinite(input->dx) || !isfinite(input->dy) || fabs(input->x)>INT32_MAX || fabs(input->y)>INT32_MAX ||
        fabs(input->dx)>(double)INT32_MAX/120.0 || fabs(input->dy)>(double)INT32_MAX/120.0 ||
        !memchr(input->text,0,sizeof(input->text))) { return false; }
    if (input->kind==NEO_INPUT_RESIZE && (!isfinite(input->scale) || input->scale<=0 || input->scale>64)) { return false; }
    const unsigned char *text=(const unsigned char *)input->text;
    while (*text) {
        unsigned char first=*text++; size_t count;
        if (first<0x80) { continue; }
        if (first>=0xc2 && first<=0xdf) { count=1; }
        else if (first>=0xe0 && first<=0xef) { count=2; }
        else if (first>=0xf0 && first<=0xf4) { count=3; }
        else { return false; }
        if ((first==0xe0 && *text<0xa0) || (first==0xed && *text>=0xa0) ||
            (first==0xf0 && *text<0x90) || (first==0xf4 && *text>=0x90)) { return false; }
        for (size_t i=0; i<count; ++i) { if (*text<0x80 || *text>0xbf) { return false; } ++text; }
    }
    return true;
}
neo_status neo_services_push_input(neo_services *services, const neo_input *input) {
    neo_resource *resource;
    neo_status status=neo_resource_right(services,input->surface,NEO_RIGHT_INPUT,&resource);
    if (status!=NEO_OK) { return status; }
    if (!neo_input_valid(input)) { return NEO_INVALID; }
    if (services->input_count==NEO_SERVICE_EVENTS) { return NEO_EXHAUSTED; }
    services->inputs[services->input_count++]=*input; return NEO_OK;
}
neo_status neo_services_input(neo_services *services, neo_input *input) {
    if (services->input_count) {
        *input=services->inputs[0];
        --services->input_count;
        memmove(services->inputs,services->inputs+1,services->input_count*sizeof(*input));
        return neo_resource_get(services,input->surface) ? NEO_OK : NEO_CLOSED;
    }
    if (!services->backend.input) { return NEO_PENDING; }
    neo_status status=services->backend.input(services->context,input);
    if (status!=NEO_OK) { return status; }
    if (!neo_input_valid(input)) { return NEO_INVALID; }
    return neo_resource_get(services,input->surface) ? NEO_OK : NEO_CLOSED;
}
static bool neo_store_key(const char *key) {
    if (!key) { return false; }
    size_t length=strlen(key);
    if (!length || length>63 || !strcmp(key,".") || !strcmp(key,"..")) { return false; }
    for (const unsigned char *p=(const unsigned char *)key; *p; ++p) {
        if (!((*p>='a' && *p<='z') || (*p>='0' && *p<='9') || *p=='-' || *p=='_' || *p=='.')) { return false; }
    }
    return true;
}
neo_status neo_services_root(neo_services *services, const char *path, unsigned rights, neo_handle *handle) {
    *handle=(neo_handle){0};
    if (services->stopping) { return NEO_CLOSED; }
    if (!services->backend.root_open) { return NEO_UNSUPPORTED; }
    void *native=NULL;
    neo_status status=services->backend.root_open(services->context,path,&native);
    if (status!=NEO_OK) { return status; }
    status=neo_services_adopt(services,NEO_RESOURCE_ROOT,rights,native,handle);
    if (status!=NEO_OK && services->backend.close) { services->backend.close(services->context,native,NEO_RESOURCE_ROOT); }
    return status;
}
neo_status neo_services_store_read(neo_services *services, neo_handle root, const char *key,
                                     void *bytes, size_t capacity, size_t *length, uint64_t *revision) {
    *length=0; *revision=0;
    neo_resource *resource;
    neo_status status=neo_resource_right(services,root,NEO_RIGHT_READ,&resource);
    if (status!=NEO_OK) { return status; }
    if (resource->kind!=NEO_RESOURCE_ROOT || !neo_store_key(key) || capacity>NEO_SERVICE_BYTES || (!bytes && capacity)) { return NEO_INVALID; }
    return services->backend.store_read ? services->backend.store_read(services->context,resource->native,key,bytes,capacity,length,revision) : NEO_UNSUPPORTED;
}
neo_status neo_services_store_replace(neo_services *services, neo_handle root, const char *key,
                                        uint64_t expected, const void *bytes, size_t length, uint64_t *revision) {
    *revision=0;
    neo_resource *resource;
    neo_status status=neo_resource_right(services,root,NEO_RIGHT_WRITE,&resource);
    if (status!=NEO_OK) { return status; }
    if (resource->kind!=NEO_RESOURCE_ROOT || !neo_store_key(key) || length>NEO_SERVICE_BYTES || (!bytes && length)) { return NEO_INVALID; }
    return services->backend.store_replace ? services->backend.store_replace(services->context,resource->native,key,expected,bytes,length,revision) : NEO_UNSUPPORTED;
}
neo_status neo_services_store_list(neo_services *services, neo_handle root, char *text, size_t capacity) {
    neo_resource *resource;
    neo_status status=neo_resource_right(services,root,NEO_RIGHT_READ,&resource);
    if (status!=NEO_OK) { return status; }
    if (resource->kind!=NEO_RESOURCE_ROOT || !capacity) { return NEO_INVALID; }
    return services->backend.store_list ? services->backend.store_list(services->context,resource->native,text,capacity) : NEO_UNSUPPORTED;
}
neo_status neo_services_store_delete(neo_services *services, neo_handle root, const char *key,
                                        uint64_t expected, uint64_t *revision) {
    *revision=0; neo_resource *resource;
    neo_status status=neo_resource_right(services,root,NEO_RIGHT_WRITE,&resource);
    if (status!=NEO_OK) { return status; }
    if (resource->kind!=NEO_RESOURCE_ROOT || !neo_store_key(key)) { return NEO_INVALID; }
    return services->backend.store_delete ? services->backend.store_delete(services->context,resource->native,key,expected,revision) : NEO_UNSUPPORTED;
}
neo_status neo_services_store_submit(neo_services *services, neo_handle root, const char *key,
                                       neo_io_kind operation, uint64_t expected, const void *bytes, size_t length,
                                       uint64_t deadline, uint64_t *id) {
    *id=0;
    if (!neo_store_key(key) || (operation!=NEO_IO_STORE_READ && operation!=NEO_IO_STORE_REPLACE && operation!=NEO_IO_STORE_DELETE) ||
        (operation==NEO_IO_STORE_DELETE && length)) { return NEO_INVALID; }
    return neo_service_submit(services,root,operation,bytes,length,deadline,id,key,expected);
}
neo_status neo_services_endpoint(neo_services *services, neo_resource_kind kind, bool listen,
                                   uint16_t *port, unsigned rights, neo_handle *handle) {
    *handle=(neo_handle){0};
    if (services->stopping) { return NEO_CLOSED; }
    if (kind!=NEO_RESOURCE_CONNECTION && kind!=NEO_RESOURCE_LISTENER && kind!=NEO_RESOURCE_DATAGRAM) { return NEO_INVALID; }
    if ((kind==NEO_RESOURCE_LISTENER && !listen) || (kind==NEO_RESOURCE_CONNECTION && listen)) { return NEO_INVALID; }
    if (!services->backend.endpoint) { return NEO_UNSUPPORTED; }
    void *native=NULL;
    neo_status status=services->backend.endpoint(services->context,kind,listen,port,&native);
    if (status!=NEO_OK) { return status; }
    status=neo_services_adopt(services,kind,rights,native,handle);
    if (status!=NEO_OK && services->backend.close) { services->backend.close(services->context,native,kind); }
    return status;
}
neo_status neo_services_standard(neo_services *services, unsigned number, neo_handle *handle,
                                   bool *terminal, uint32_t *columns, uint32_t *rows) {
    *handle=(neo_handle){0}; *terminal=false; *columns=0; *rows=0;
    if (services->stopping) { return NEO_CLOSED; }
    if (number>2) { return NEO_INVALID; }
    if (!services->backend.standard) { return NEO_UNSUPPORTED; }
    void *native=NULL;
    neo_status status=services->backend.standard(services->context,number,&native,terminal,columns,rows);
    if (status!=NEO_OK) { return status; }
    status=neo_services_adopt(services,NEO_RESOURCE_STREAM,number ? NEO_RIGHT_WRITE : NEO_RIGHT_READ,native,handle);
    if (status!=NEO_OK && services->backend.close) { services->backend.close(services->context,native,NEO_RESOURCE_STREAM); }
    return status;
}

typedef struct { char key[64]; unsigned char *bytes; size_t length; uint64_t revision; bool deleted; } neo_memory_key;
typedef struct { uint64_t time; atomic_bool woken; neo_memory_key keys[32]; } neo_headless;
static void neo_headless_wake(void *context) { atomic_store(&((neo_headless *)context)->woken,true); }
static neo_status neo_headless_machine(void *context, neo_machine *machine) {
    (void)context; *machine=(neo_machine){4096,1,64,1}; return NEO_OK;
}
typedef struct { neo_script_step steps[16]; size_t count, position, offset; } neo_script_stream;
static neo_status neo_headless_step(void *context, void *native, neo_io_kind operation, void **ticket,
                                      unsigned char *bytes, size_t capacity, size_t *length, void **accepted) {
    (void)ticket; (void)accepted;
    neo_headless *headless=context; neo_script_stream *script=native;
    if (script->position==script->count) { return NEO_PENDING; }
    neo_script_step *step=&script->steps[script->position];
    if (headless->time<step->ready_ns) { return NEO_PENDING; }
    if (operation!=step->operation) { return NEO_IO_ERROR; }
    if (step->status!=NEO_OK) { ++script->position; script->offset=0; return step->status; }
    size_t count=step->length-script->offset;
    if (count>capacity) { count=capacity; }
    if (operation==NEO_IO_READ) { memcpy(bytes,step->bytes+script->offset,count); }
    else if (memcmp(bytes,step->bytes+script->offset,count)) { return NEO_IO_ERROR; }
    *length=count; script->offset+=count;
    if (script->offset==step->length) { ++script->position; script->offset=0; }
    return NEO_OK;
}
static void neo_headless_close(void *context, void *native, neo_resource_kind kind) {
    (void)context;
    if (kind!=NEO_RESOURCE_ROOT) { neo_platform_free(native); }
}
static uint64_t neo_headless_clock(void *context) { return ((neo_headless *)context)->time; }
static void neo_headless_wait(void *context, uint32_t milliseconds) {
    neo_headless *headless=context;
    if (atomic_exchange(&headless->woken,false)) { return; }
    uint64_t delta=(uint64_t)milliseconds*1000000u;
    if (delta<=UINT64_MAX-headless->time) { headless->time+=delta; }
}
static neo_status neo_headless_root(void *context, const char *path, void **root) {
    if (strcmp(path,"memory")) { return NEO_DENIED; }
    *root=context; return NEO_OK;
}
static neo_memory_key *neo_headless_key(neo_headless *headless, const char *key, bool create) {
    neo_memory_key *empty=NULL;
    for (size_t i=0; i<32; ++i) {
        if (!strcmp(headless->keys[i].key,key)) { return &headless->keys[i]; }
        if (!headless->keys[i].key[0] && !empty) { empty=&headless->keys[i]; }
    }
    if (create && empty) { return empty; }
    return NULL;
}
static neo_status neo_headless_read(void *context, void *root, const char *name, void *bytes,
                                      size_t capacity, size_t *length, uint64_t *revision) {
    (void)context;
    neo_memory_key *key=neo_headless_key(root,name,false);
    if (!key) { return NEO_EOF; }
    *revision=key->revision; *length=key->length;
    if (key->deleted) { return NEO_EOF; }
    if (capacity<key->length) { return NEO_TRUNCATED; }
    if (key->length) { memcpy(bytes,key->bytes,key->length); }
    return NEO_OK;
}
static neo_status neo_headless_replace(void *context, void *root, const char *name, uint64_t expected,
                                         const void *bytes, size_t length, uint64_t *revision) {
    (void)context;
    neo_memory_key *key=neo_headless_key(root,name,true);
    if (!key) { return NEO_EXHAUSTED; }
    if (key->revision!=expected) { return NEO_CONFLICT; }
    if (expected==UINT64_MAX) { return NEO_EXHAUSTED; }
    unsigned char *copy=neo_platform_malloc(length ? length : 1);
    if (!copy) { return NEO_EXHAUSTED; }
    if (length) { memcpy(copy,bytes,length); }
    neo_platform_free(key->bytes); key->bytes=copy; key->length=length;
    strcpy(key->key,name); key->revision=expected+1; *revision=key->revision;
    key->deleted=false;
    return NEO_OK;
}
static neo_status neo_headless_list(void *context, void *root, char *text, size_t capacity) {
    (void)context;
    neo_headless *headless=root; size_t length=0;
    for (size_t i=0; i<32; ++i) {
        if (!headless->keys[i].key[0] || headless->keys[i].deleted) { continue; }
        size_t bytes=strlen(headless->keys[i].key);
        if (length+bytes+2>capacity) { return NEO_TRUNCATED; }
        memcpy(text+length,headless->keys[i].key,bytes); length+=bytes; text[length++]='\n';
    }
    text[length]=0; return NEO_OK;
}
static neo_status neo_headless_delete(void *context, void *root, const char *name, uint64_t expected, uint64_t *revision) {
    neo_status status=neo_headless_replace(context,root,name,expected,NULL,0,revision);
    if (status==NEO_OK) { neo_headless_key(root,name,false)->deleted=true; }
    return status;
}
static neo_status neo_headless_store_step(void *context, void *root, neo_io_kind operation, const char *key,
                                             uint64_t expected, void **ticket, unsigned char *bytes, size_t capacity,
                                             size_t *length, uint64_t *revision) {
    (void)ticket;
    if (operation==NEO_IO_STORE_READ) { return neo_headless_read(context,root,key,bytes,capacity,length,revision); }
    if (operation==NEO_IO_STORE_DELETE) { return neo_headless_delete(context,root,key,expected,revision); }
    neo_status status=neo_headless_replace(context,root,key,expected,bytes,capacity,revision);
    *length=status==NEO_OK ? capacity : 0; return status;
}
static void neo_headless_destroy(void *context) {
    neo_headless *headless=context;
    for (size_t i=0; i<32; ++i) { neo_platform_free(headless->keys[i].bytes); }
    neo_platform_free(headless);
}
neo_status neo_services_headless(neo_services **output) {
    *output=NULL;
    neo_headless *headless=neo_platform_calloc(1,sizeof(*headless));
    if (!headless) { return NEO_EXHAUSTED; }
    atomic_init(&headless->woken,false);
    neo_service_backend backend={.clock_ns=neo_headless_clock,.wait=neo_headless_wait,.wake=neo_headless_wake,
        .machine=neo_headless_machine,.step=neo_headless_step,.close=neo_headless_close,
        .root_open=neo_headless_root,.store_read=neo_headless_read,.store_replace=neo_headless_replace,
        .store_list=neo_headless_list,.store_delete=neo_headless_delete,.store_step=neo_headless_store_step,.destroy=neo_headless_destroy};
    neo_capabilities capabilities={.input=true,.surfaces=true,.storage=true};
    neo_status status=neo_services_open(&backend,headless,capabilities,output);
    if (status!=NEO_OK) { neo_platform_free(headless); } else { (*output)->headless=true; }
    return status;
}
neo_status neo_services_advance(neo_services *services, uint64_t nanoseconds) {
    if (!services->headless) { return NEO_UNSUPPORTED; }
    neo_headless *headless=services->context;
    if (nanoseconds<headless->time) { return NEO_INVALID; }
    headless->time=nanoseconds; return NEO_OK;
}
neo_status neo_services_script_stream(neo_services *services, const neo_script_step *steps, size_t count,
                                        unsigned rights, neo_handle *handle) {
    *handle=(neo_handle){0};
    if (!services->headless) { return NEO_UNSUPPORTED; }
    if (!steps || !count || count>16) { return NEO_INVALID; }
    for (size_t i=0; i<count; ++i) {
        if (steps[i].length>sizeof(steps[i].bytes) || (unsigned)steps[i].operation>NEO_IO_WRITE ||
            (unsigned)steps[i].status>NEO_UNCERTAIN || steps[i].status==NEO_PENDING ||
            (i && steps[i].ready_ns<steps[i-1].ready_ns)) { return NEO_INVALID; }
    }
    neo_script_stream *script=neo_platform_calloc(1,sizeof(*script));
    if (!script) { return NEO_EXHAUSTED; }
    script->count=count; memcpy(script->steps,steps,count*sizeof(*steps));
    neo_status status=neo_services_adopt(services,NEO_RESOURCE_CONNECTION,rights,script,handle);
    if (status!=NEO_OK) { neo_platform_free(script); }
    return status;
}
