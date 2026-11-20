#include <efi.h>
#include <efilib.h>
#include "../../platform.h"
#include "../../presentation.h"
#include "x86_64.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "image.inc"

const char *neo_x86_64_image=neo_bundle_source;
static uint64_t *neo_page_entries;
static unsigned char *neo_code;
static uint64_t neo_frequency,neo_epoch;
static volatile uint32_t *neo_screen;
static uint32_t neo_width,neo_height,neo_stride;
static bool neo_bgr;
extern char _text[],_etext[];
extern int main(int,char **);
extern void neo_x86_64_enter(void *) __attribute__((noreturn));
void neo_x86_64_serial_byte(unsigned char byte) {
    for(unsigned i=0;i<100000 && !(neo_in(0x3fd)&0x20);++i) { __asm__ volatile("pause"); }
    neo_out(0x3f8,byte);
}
void neo_x86_64_serial(const char *text) { while(*text) { neo_x86_64_serial_byte((unsigned char)*text++); } }
void neo_x86_64_panic(const char *text) { __asm__ volatile("cli"); neo_x86_64_serial("NEO PANIC: "); neo_x86_64_serial(text); neo_x86_64_serial("\n"); for(;;) { __asm__ volatile("hlt"); } }
static void neo_protect(void *memory,size_t length,bool execute) {
    uintptr_t start=(uintptr_t)memory&~(uintptr_t)4095,end=((uintptr_t)memory+length+4095)&~(uintptr_t)4095;
    for(uintptr_t page=start;page<end;page+=4096) {
        neo_page_entries[page/4096]=page|1u|(execute ? 0:UINT64_C(0x8000000000000002));
        __asm__ volatile("invlpg (%0)"::"r"(page):"memory");
    }
}
void neo_native_begin(void) {}
void neo_native_end(void) {}
uint32_t neo_native_worker_id(void) { return 0; }
int neo_native_worker(const char *text) { (void)text; return 1; }
const char *neo_native_execute(const unsigned char *code,size_t length,int32_t *slots,int64_t *result) {
    if(!length || length>NEO_NATIVE_CODE_LIMIT) { return "invalid native code length"; }
    neo_protect(neo_code,NEO_NATIVE_CODE_LIMIT,false); memcpy(neo_code,code,length); neo_protect(neo_code,NEO_NATIVE_CODE_LIMIT,true);
    int64_t (__attribute__((ms_abi)) *entry)(int32_t *); void *address=neo_code; memcpy(&entry,&address,sizeof(entry));
    *result=entry(slots); neo_protect(neo_code,NEO_NATIVE_CODE_LIMIT,false); return NULL;
}
static uint64_t neo_x86_64_clock(void *context) { (void)context; uint64_t ticks=neo_tsc()-neo_epoch;
    return ticks/neo_frequency*UINT64_C(1000000000)+(ticks%neo_frequency)*UINT64_C(1000000000)/neo_frequency; }
static void neo_x86_64_wait(void *context,uint32_t milliseconds) { uint64_t end=neo_x86_64_clock(context)+(uint64_t)milliseconds*1000000u;
    while(neo_x86_64_clock(context)<end) { __asm__ volatile("pause"); } }
static neo_status neo_x86_64_machine(void *context,neo_machine *machine) { (void)context; *machine=(neo_machine){4096,1,64,1}; return NEO_OK; }
static uint64_t neo_x86_64_ms(void) { return neo_x86_64_clock(NULL)/1000000; }
static void neo_x86_64_delay(unsigned milliseconds) { neo_x86_64_wait(NULL,milliseconds); }
static const char *neo_no_read(const char *path,void **output) { (void)path; (void)output; return "file stream unavailable on x86_64; bundle is read-only"; }
static const char *neo_no_poll(void *request,bool *ready,char *text,size_t capacity) { (void)request;(void)text;(void)capacity;*ready=true;return "unsupported"; }
static void neo_no_close(void *request) { (void)request; }
neo_event_platform neo_native_event_platform(void) { return (neo_event_platform){neo_x86_64_ms,neo_x86_64_delay,neo_no_read,neo_no_poll,neo_no_close}; }
typedef struct { neo_input events[64];size_t count;neo_handle surface; bool claimed; } neo_x86_64_services;
static bool neo_display_claimed;
static int32_t neo_mouse_x,neo_mouse_y;
static unsigned char neo_packet[3],neo_packet_index,neo_buttons;
static bool neo_extended,neo_shift;
static unsigned char neo_console_bytes[256];static size_t neo_console_count;
static void neo_ps2_wait(void) { for(unsigned i=0;i<100000 && (neo_in(0x64)&2);++i) { __asm__ volatile("pause"); } }
static void neo_ps2_command(unsigned char command) { neo_ps2_wait();neo_out(0x64,command); }
static void neo_ps2_data(unsigned char data) { neo_ps2_wait();neo_out(0x60,data); }
static void neo_mouse_command(unsigned char command) { neo_ps2_command(0xd4);neo_ps2_data(command);
    for(unsigned i=0;i<100000;++i) { if(neo_in(0x64)&1) { (void)neo_in(0x60);break; } } }
static void neo_ps2_init(void) {
    neo_ps2_command(0xa8);neo_ps2_command(0xae);
    while(neo_in(0x64)&1) { (void)neo_in(0x60); }
    neo_ps2_command(0x20);unsigned char config=0x40;
    for(unsigned i=0;i<100000;++i) { if(neo_in(0x64)&1) { config=neo_in(0x60);break; } }
    neo_ps2_command(0x60);neo_ps2_data((unsigned char)((config|0x40)&~0x33));
    neo_mouse_command(0xf6);neo_mouse_command(0xf4);
}
static void neo_input_push(neo_x86_64_services *services,neo_input input) { if(services->count<64) {
    input.surface=services->surface;input.time_ns=neo_x86_64_clock(NULL);services->events[services->count++]=input; } }
static void neo_ps2_poll(neo_x86_64_services *services) {
    static const char keys[]="\0\x1b" "1234567890-=\b\tqwertyuiop[]\n\0asdfghjkl;'`\0\\zxcvbnm,./\0*\0 ";
    for(unsigned n=0;n<64;++n) {
        unsigned char status=neo_in(0x64); if(!(status&1)) { break; } unsigned char byte=neo_in(0x60);
        if(status&0x20) {
            if(!neo_packet_index && !(byte&8)) { continue; } neo_packet[neo_packet_index++]=byte;
            if(neo_packet_index<3) { continue; } neo_packet_index=0;
            if(neo_packet[0]&0xc0) { continue; }
            neo_mouse_x+=(int8_t)neo_packet[1];neo_mouse_y-=(int8_t)neo_packet[2];
            if(neo_mouse_x<0) { neo_mouse_x=0; }if(neo_mouse_y<0) { neo_mouse_y=0; }
            if((uint32_t)neo_mouse_x>=neo_width) { neo_mouse_x=(int32_t)neo_width-1; }
            if((uint32_t)neo_mouse_y>=neo_height) { neo_mouse_y=(int32_t)neo_height-1; }
            neo_input_push(services,(neo_input){.kind=NEO_INPUT_POINTER,.x=neo_mouse_x,.y=neo_mouse_y});
            unsigned char buttons=neo_packet[0]&7;
            for(unsigned b=0;b<3;++b) { if((buttons^neo_buttons)&(1u<<b)) {
                neo_input_push(services,(neo_input){.kind=NEO_INPUT_BUTTON,.code=(int32_t)b+1,
                    .pressed=(buttons&(1u<<b))!=0,.x=neo_mouse_x,.y=neo_mouse_y}); } }
            neo_buttons=buttons;
        } else {
            if(byte==0xe0) { neo_extended=true;continue; }
            bool pressed=(byte&0x80)==0;unsigned code=(byte&0x7f)|(neo_extended ? 0x100u:0);neo_extended=false;
            if(code==42 || code==54) { neo_shift=pressed; }
            neo_input_push(services,(neo_input){.kind=NEO_INPUT_KEY,.code=(int32_t)code,.pressed=pressed,.modifiers=neo_shift ? 1:0});
            if(pressed && code<sizeof(keys)-1 && keys[code]) {
                unsigned char text=(unsigned char)keys[code];if(neo_shift && text>='a' && text<='z') { text=(unsigned char)(text-'a'+'A'); }
                neo_input input={.kind=NEO_INPUT_TEXT};input.text[0]=(char)text;neo_input_push(services,input);
                if(neo_console_count<sizeof(neo_console_bytes)) { neo_console_bytes[neo_console_count++]=text; }
            }
        }
    }
}
static neo_status neo_x86_64_surface(void *context,uint32_t width,uint32_t height,void **native) {
    (void)width;(void)height;neo_x86_64_services *services=context;
    if(neo_display_claimed) { return NEO_EXHAUSTED; }neo_display_claimed=true;services->claimed=true;*native=services;return NEO_OK;
}
static void neo_x86_64_bind(void *context,void *native,neo_handle handle) { (void)native;
    if(handle.kind==NEO_RESOURCE_SURFACE) { neo_x86_64_services *services=context;services->surface=handle;
        neo_input_push(services,(neo_input){.kind=NEO_INPUT_RESIZE,.width=neo_width,.height=neo_height,.scale=1}); } }
static neo_status neo_x86_64_present(void *context,void *native,const neo_frame *frame) {
    (void)context;(void)native;
    if(frame->width>neo_width || frame->height>neo_height) { return NEO_INVALID; }
    for(uint32_t y=0;y<frame->height;++y) { for(uint32_t x=0;x<frame->width;++x) {
        const unsigned char *p=frame->pixels+(size_t)y*frame->stride+x*4u;
        neo_screen[(size_t)y*neo_stride+x]=neo_bgr ? ((uint32_t)p[0]<<16)|((uint32_t)p[1]<<8)|p[2] :
            ((uint32_t)p[2]<<16)|((uint32_t)p[1]<<8)|p[0]; } }
    printf("NEO frame %u %u %u ticks %lu\n",frame->pixels[0],frame->pixels[1],frame->pixels[2],neo_x86_64_ticks);
    return NEO_OK;
}
static neo_status neo_x86_64_input(void *context,neo_input *input) {
    neo_x86_64_services *services=context;neo_ps2_poll(services);
    if(!services->count) { return NEO_PENDING; }*input=services->events[0];--services->count;
    memmove(services->events,services->events+1,services->count*sizeof(*input));return NEO_OK;
}
static neo_status neo_x86_64_standard(void *context,unsigned number,void **native,bool *terminal,uint32_t *columns,uint32_t *rows) {
    (void)context;*native=(void *)(uintptr_t)(number+1);*terminal=true;*columns=80;*rows=25;return NEO_OK;
}
static neo_status neo_x86_64_step(void *context,void *native,neo_io_kind operation,void **ticket,
    unsigned char *bytes,size_t capacity,size_t *length,void **accepted) {
    (void)ticket;(void)accepted;
    if(operation==NEO_IO_WRITE && (uintptr_t)native!=1) { for(size_t i=0;i<capacity;++i) { neo_x86_64_serial_byte(bytes[i]); }*length=capacity;return NEO_OK; }
    if(operation!=NEO_IO_READ || (uintptr_t)native!=1) { return NEO_DENIED; }
    neo_ps2_poll(context);*length=0;
    while(*length<capacity && (neo_in(0x3fd)&1)) { bytes[(*length)++]=neo_in(0x3f8); }
    while(*length<capacity && neo_console_count) { bytes[(*length)++]=neo_console_bytes[0];--neo_console_count;
        memmove(neo_console_bytes,neo_console_bytes+1,neo_console_count); }
    return *length || !capacity ? NEO_OK:NEO_PENDING;
}
static void neo_x86_64_close(void *context,void *native,neo_resource_kind kind) { (void)native;
    if(kind==NEO_RESOURCE_SURFACE) { ((neo_x86_64_services *)context)->claimed=false;neo_display_claimed=false; } }
static void neo_x86_64_destroy(void *context) { neo_platform_free(context); }
neo_status neo_platform_services(bool visible,neo_services **output) {
    (void)visible;neo_x86_64_services *context=neo_platform_calloc(1,sizeof(*context));if(!context) { return NEO_EXHAUSTED; }
    neo_service_backend backend={.clock_ns=neo_x86_64_clock,.wait=neo_x86_64_wait,.machine=neo_x86_64_machine,.close=neo_x86_64_close,
        .bind=neo_x86_64_bind,.step=neo_x86_64_step,.surface_open=neo_x86_64_surface,.present=neo_x86_64_present,
        .input=neo_x86_64_input,.standard=neo_x86_64_standard,.destroy=neo_x86_64_destroy};
    neo_capabilities capabilities={.input=true,.surfaces=true};neo_status status=neo_services_open(&backend,context,capabilities,output);
    if(status!=NEO_OK) { neo_platform_free(context); }return status;
}
bool neo_platform_services_selftest(void) { return false; }
void neo_x86_64_run(void) {
    neo_x86_64_interrupts();neo_ps2_init();neo_mouse_x=(int32_t)neo_width/2;neo_mouse_y=(int32_t)neo_height/2;
    printf("NEO x86_64 ready: firmware exited, trusted code only, framebuffer %u x %u\n",neo_width,neo_height);
    char *arguments[]={"neo","surface","neo/neo-os/image.neo",(char *)neo_bundle_object,"software",NULL};
    int count=5;if(!strcmp(neo_bundle_mode,"cli")) { arguments[1]="session";count=3; }
    int result=main(count,arguments);printf("NEO image stopped %d\n",result);for(;;) { __asm__ volatile("hlt"); }
}
EFI_STATUS efi_main(EFI_HANDLE image,EFI_SYSTEM_TABLE *system) {
    InitializeLib(image,system);
    neo_out(0x3f9,0);neo_out(0x3fb,0x80);neo_out(0x3f8,1);neo_out(0x3f9,0);neo_out(0x3fb,3);neo_out(0x3fa,0xc7);neo_out(0x3fc,0x0b);
    neo_x86_64_serial("NEO UEFI bootstrap: bundle images 1\n");
    EFI_GUID guid=EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;EFI_GRAPHICS_OUTPUT_PROTOCOL *graphics=NULL;
    EFI_STATUS status=uefi_call_wrapper(system->BootServices->LocateProtocol,3,&guid,NULL,(void **)&graphics);
    if(EFI_ERROR(status) || !graphics) { neo_x86_64_panic("framebuffer unavailable"); }
    for(UINT32 mode=0;mode<graphics->Mode->MaxMode;++mode) { UINTN size=0;EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info=NULL;
        status=uefi_call_wrapper(graphics->QueryMode,4,graphics,mode,&size,&info);
        if(!EFI_ERROR(status) && info && info->HorizontalResolution==640 && info->VerticalResolution==480) {
            uefi_call_wrapper(system->BootServices->FreePool,1,info);uefi_call_wrapper(graphics->SetMode,2,graphics,mode);break; }
        if(info) { uefi_call_wrapper(system->BootServices->FreePool,1,info); }
    }
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info=graphics->Mode->Info;
    bool rgb=info->PixelFormat==PixelRedGreenBlueReserved8BitPerColor ||
        (info->PixelFormat==PixelBitMask && info->PixelInformation.RedMask==0xff && info->PixelInformation.GreenMask==0xff00 && info->PixelInformation.BlueMask==0xff0000);
    bool bgr=info->PixelFormat==PixelBlueGreenRedReserved8BitPerColor ||
        (info->PixelFormat==PixelBitMask && info->PixelInformation.RedMask==0xff0000 && info->PixelInformation.GreenMask==0xff00 && info->PixelInformation.BlueMask==0xff);
    if(!rgb && !bgr) { neo_x86_64_panic("unsupported framebuffer layout"); }
    neo_width=info->HorizontalResolution;neo_height=info->VerticalResolution;neo_stride=info->PixelsPerScanLine;
    neo_screen=(volatile uint32_t *)(uintptr_t)graphics->Mode->FrameBufferBase;neo_bgr=bgr;
    if((uint64_t)(uintptr_t)neo_screen+graphics->Mode->FrameBufferSize>UINT64_C(0x100000000)) { neo_x86_64_panic("framebuffer outside initial identity map"); }
    EFI_PHYSICAL_ADDRESS memory=0xffffffff;UINTN pages=32768;
    status=uefi_call_wrapper(system->BootServices->AllocatePages,4,AllocateMaxAddress,EfiLoaderData,pages,&memory);
    if(EFI_ERROR(status)) { neo_x86_64_panic("boot memory allocation failed"); }
    unsigned char *pool=(unsigned char *)(uintptr_t)memory;memset(pool,0,128u*1024u*1024u);
    uint64_t *pml4=(uint64_t *)pool,*pdpt=pml4+512,*directories=pdpt+512;
    neo_page_entries=directories+2048;
    pml4[0]=(uintptr_t)pdpt|3u;
    for(unsigned i=0;i<4;++i) { pdpt[i]=(uintptr_t)(directories+i*512)|3u; }
    for(unsigned i=0;i<2048;++i) { directories[i]=(uintptr_t)(neo_page_entries+(size_t)i*512)|3u; }
    for(size_t i=0;i<1048576;++i) { uint64_t address=(uint64_t)i*4096u;
        neo_page_entries[i]=address|UINT64_C(0x8000000000000003)|(address>=0xb0000000 ? 0x18u:0u); }
    for(uintptr_t address=(uintptr_t)_text&~(uintptr_t)4095;address<(uintptr_t)_etext;address+=4096) { neo_page_entries[address/4096]=address|1u; }
    size_t reserved=10u*1024u*1024u;neo_code=pool+reserved;reserved+=NEO_NATIVE_CODE_LIMIT;
    unsigned char *stack=pool+reserved;reserved+=4u*1024u*1024u;void *stack_top=stack+4u*1024u*1024u;
    neo_x86_64_heap(pool+reserved,128u*1024u*1024u-reserved);
    uint64_t first=neo_tsc();uefi_call_wrapper(system->BootServices->Stall,1,20000);neo_frequency=(neo_tsc()-first)*50u;neo_epoch=neo_tsc();
    UINTN map_bytes=65536,key=0,descriptor_size=0;UINT32 version=0;void *map=pool+9u*1024u*1024u;
    for(unsigned attempt=0;attempt<4;++attempt) { map_bytes=65536;
        status=uefi_call_wrapper(system->BootServices->GetMemoryMap,5,&map_bytes,map,&key,&descriptor_size,&version);
        if(EFI_ERROR(status)) { neo_x86_64_panic("memory map failed"); }
        status=uefi_call_wrapper(system->BootServices->ExitBootServices,2,image,key);if(!EFI_ERROR(status)) { break; }
    }
    if(EFI_ERROR(status)) { neo_x86_64_panic("ExitBootServices failed"); }
    __asm__ volatile("cli");uint32_t lo,hi;__asm__ volatile("rdmsr":"=a"(lo),"=d"(hi):"c"(0xc0000080));lo|=0x800;
    __asm__ volatile("wrmsr"::"a"(lo),"d"(hi),"c"(0xc0000080));
    __asm__ volatile("mov %0,%%cr3"::"r"(pml4):"memory");uintptr_t cr0;__asm__ volatile("mov %%cr0,%0":"=r"(cr0));
    cr0=(cr0|0x10002u)&~(uintptr_t)12;__asm__ volatile("mov %0,%%cr0"::"r"(cr0):"memory");
    neo_x86_64_enter(stack_top);
}
