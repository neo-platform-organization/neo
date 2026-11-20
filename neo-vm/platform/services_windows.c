#define _WIN32_WINNT 0x0a00
#include "../platform.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <windowsx.h>
#include <bcrypt.h>
#include <GL/gl.h>
#include <mmsystem.h>
#include "../services.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

typedef struct {
    neo_input inputs[NEO_SERVICE_EVENTS];
    size_t count;
    bool visible;
    bool input_lost;
    HANDLE wakeup;
    neo_renderer renderer;
    HWND clipboard;
} neo_windows_services;
typedef struct {
    neo_resource_kind kind;
    neo_windows_services *owner;
    neo_handle identity;
    HANDLE file;
    SOCKET socket;
    HWND window;
    wchar_t path[1024];
    unsigned char *pixels;
    uint32_t width, height;
    wchar_t surrogate;
    bool connecting;
    HDC dc;
    HGLRC gl;
    GLuint texture;
    GLuint framebuffer, rendered_texture;
    uint32_t texture_width, texture_height;
    void (APIENTRY *gen_framebuffers)(GLsizei,GLuint *);
    void (APIENTRY *bind_framebuffer)(GLenum,GLuint);
    void (APIENTRY *attach_texture)(GLenum,GLenum,GLenum,GLuint,GLint);
    GLenum (APIENTRY *framebuffer_status)(GLenum);
    HWAVEOUT audio_output;
    HWAVEIN audio_input;
    neo_audio_format audio_format;
    uint64_t audio_epoch, audio_frames, audio_starvations;
    unsigned audio_queued;
} neo_windows_resource;
typedef struct {
    HANDLE thread;
    HANDLE file;
    neo_io_kind operation;
    unsigned char *buffer;
    DWORD capacity, transferred, error;
    neo_windows_resource *root;
    char key[64];
    uint64_t expected, revision;
    size_t length;
    neo_status status;
    bool audio;
    neo_windows_resource *audio_resource;
    uint64_t audio_epoch;
    WAVEHDR header;
} neo_windows_io;
static neo_status neo_windows_store_read(void *, void *, const char *, void *, size_t, size_t *, uint64_t *);
static neo_status neo_windows_replace(void *, void *, const char *, uint64_t, const void *, size_t, uint64_t *);
static neo_status neo_windows_delete(void *, void *, const char *, uint64_t, uint64_t *);

static uint64_t neo_windows_clock(void *context) {
    (void)context;
    LARGE_INTEGER counter, frequency;
    QueryPerformanceCounter(&counter); QueryPerformanceFrequency(&frequency);
    uint64_t ticks=(uint64_t)counter.QuadPart, rate=(uint64_t)frequency.QuadPart;
    return ticks/rate*1000000000u+(ticks%rate)*1000000000u/rate;
}
static void neo_windows_wait(void *context, uint32_t milliseconds) {
    neo_windows_services *windows=context;
    if (milliseconds>10) { milliseconds=10; }
    MsgWaitForMultipleObjects(1,&windows->wakeup,FALSE,milliseconds,QS_ALLINPUT);
}
static void neo_windows_wake(void *context) { SetEvent(((neo_windows_services *)context)->wakeup); }
static neo_status neo_windows_machine(void *context, neo_machine *machine) {
    (void)context; SYSTEM_INFO info; GetNativeSystemInfo(&info);
    LARGE_INTEGER frequency; QueryPerformanceFrequency(&frequency);
    *machine=(neo_machine){info.dwPageSize,info.dwNumberOfProcessors,64,
        (UINT64_C(1000000000)+(uint64_t)frequency.QuadPart-1u)/(uint64_t)frequency.QuadPart};
    return NEO_OK;
}
static neo_status neo_windows_entropy(void *context, void *bytes, size_t length) {
    (void)context;
    if (length>ULONG_MAX) { return NEO_INVALID; }
    return BCryptGenRandom(NULL,bytes,(ULONG)length,BCRYPT_USE_SYSTEM_PREFERRED_RNG)==0 ? NEO_OK : NEO_IO_ERROR;
}
static bool neo_windows_gl_function(const char *name, void *output) {
    PROC function=wglGetProcAddress(name); uintptr_t address=0; memcpy(&address,&function,sizeof(address));
    if (address<=3 || address==UINTPTR_MAX) { return false; }
    memcpy(output,&function,sizeof(function)); return true;
}
static void neo_windows_queue(neo_windows_resource *resource, neo_input input) {
    neo_windows_services *context=resource->owner;
    if (!resource->identity.owner) { return; }
    if (context->count==NEO_SERVICE_EVENTS) { context->input_lost=true; return; }
    input.surface=resource->identity; input.time_ns=neo_windows_clock(context);
    context->inputs[context->count++]=input;
}
static neo_status neo_windows_gl_draw(neo_windows_resource *resource) {
    if (!wglMakeCurrent(resource->dc,resource->gl)) { return NEO_DEVICE_LOST; }
    glViewport(0,0,(GLsizei)resource->width,(GLsizei)resource->height);
    glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D,resource->texture); glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    uint32_t width=1,height=1; while (width<resource->width) { width*=2; } while (height<resource->height) { height*=2; }
    if (width!=resource->texture_width || height!=resource->texture_height) {
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,(GLsizei)width,(GLsizei)height,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        glBindTexture(GL_TEXTURE_2D,resource->rendered_texture);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,(GLsizei)width,(GLsizei)height,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        resource->texture_width=width; resource->texture_height=height;
        glBindTexture(GL_TEXTURE_2D,resource->texture);
    }
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,(GLsizei)resource->width,(GLsizei)resource->height,GL_RGBA,GL_UNSIGNED_BYTE,resource->pixels);
    resource->bind_framebuffer(0x8d40,resource->framebuffer);
    resource->attach_texture(0x8d40,0x8ce0,GL_TEXTURE_2D,resource->rendered_texture,0);
    if (resource->framebuffer_status(0x8d40)!=0x8cd5) { return NEO_DEVICE_LOST; }
    glDrawBuffer(0x8ce0); glReadBuffer(0x8ce0);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    glBegin(GL_QUADS);
    GLfloat u=(GLfloat)resource->width/(GLfloat)width,v=(GLfloat)resource->height/(GLfloat)height;
    glTexCoord2f(0,v); glVertex2f(-1,-1); glTexCoord2f(u,v); glVertex2f(1,-1);
    glTexCoord2f(u,0); glVertex2f(1,1); glTexCoord2f(0,0); glVertex2f(-1,1); glEnd();
    if (getenv("NEO_TEST_RENDER_READBACK")) {
        unsigned char sample[4]; glReadPixels(0,(GLint)resource->height-1,1,1,GL_RGBA,GL_UNSIGNED_BYTE,sample);
        if (memcmp(sample,resource->pixels,3)) { return NEO_DEVICE_LOST; }
    }
    resource->bind_framebuffer(0x8d40,0); glDrawBuffer(GL_BACK); glReadBuffer(GL_BACK);
    glBindTexture(GL_TEXTURE_2D,resource->rendered_texture);
    glBegin(GL_QUADS);
    glTexCoord2f(0,0); glVertex2f(-1,-1); glTexCoord2f(u,0); glVertex2f(1,-1);
    glTexCoord2f(u,v); glVertex2f(1,1); glTexCoord2f(0,v); glVertex2f(-1,1); glEnd();
    if (!SwapBuffers(resource->dc) || glGetError()!=GL_NO_ERROR) { return NEO_DEVICE_LOST; }
    return NEO_OK;
}
static LRESULT CALLBACK neo_surface_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    neo_windows_resource *resource=(neo_windows_resource *)GetWindowLongPtrW(window,GWLP_USERDATA);
    if (message==WM_NCCREATE) {
        const CREATESTRUCTW *creation=(const CREATESTRUCTW *)lparam;
        resource=creation->lpCreateParams;
        SetWindowLongPtrW(window,GWLP_USERDATA,(LONG_PTR)resource);
    }
    if (!resource) { return DefWindowProcW(window,message,wparam,lparam); }
    neo_input input={0};
    input.modifiers=(GetKeyState(VK_SHIFT)<0 ? 1 : 0) | (GetKeyState(VK_CONTROL)<0 ? 2 : 0) |
        (GetKeyState(VK_MENU)<0 ? 4 : 0) | ((GetKeyState(VK_LWIN)<0 || GetKeyState(VK_RWIN)<0) ? 8 : 0);
    switch (message) {
    case WM_CLOSE: input.kind=NEO_INPUT_CLOSE; neo_windows_queue(resource,input); return 0;
    case WM_SETFOCUS: case WM_KILLFOCUS:
        if (message==WM_KILLFOCUS) { resource->surrogate=0; }
        input.kind=NEO_INPUT_FOCUS; input.code=message==WM_SETFOCUS;
        neo_windows_queue(resource,input); break;
    case WM_SIZE:
        input.kind=NEO_INPUT_RESIZE; input.width=LOWORD(lparam); input.height=HIWORD(lparam);
        input.scale=(double)GetDpiForWindow(window)/96.0;
        neo_windows_queue(resource,input); break;
    case WM_DPICHANGED: {
        const RECT *rectangle=(const RECT *)lparam;
        SetWindowPos(window,NULL,rectangle->left,rectangle->top,rectangle->right-rectangle->left,
            rectangle->bottom-rectangle->top,SWP_NOZORDER|SWP_NOACTIVATE);
        return 0;
    }
    case WM_KEYDOWN: case WM_KEYUP: case WM_SYSKEYDOWN: case WM_SYSKEYUP:
        input.kind=NEO_INPUT_KEY; input.code=(int32_t)((lparam>>16)&0x1ff);
        input.pressed=message==WM_KEYDOWN || message==WM_SYSKEYDOWN;
        neo_windows_queue(resource,input); break;
    case WM_CHAR: {
        wchar_t characters[2]={(wchar_t)wparam,0}; int count=1;
        if (wparam>=0xd800 && wparam<=0xdbff) { resource->surrogate=(wchar_t)wparam; return 0; }
        if (wparam>=0xdc00 && wparam<=0xdfff && resource->surrogate) {
            characters[0]=resource->surrogate; characters[1]=(wchar_t)wparam; count=2;
        }
        resource->surrogate=0; input.kind=NEO_INPUT_TEXT;
        int length=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,characters,count,input.text,31,NULL,NULL);
        if (length>0) { input.text[length]=0; neo_windows_queue(resource,input); }
        return 0;
    }
    case WM_MOUSEMOVE:
        input.kind=NEO_INPUT_POINTER; input.x=GET_X_LPARAM(lparam); input.y=GET_Y_LPARAM(lparam);
        neo_windows_queue(resource,input); break;
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_RBUTTONDOWN: case WM_RBUTTONUP:
        input.kind=NEO_INPUT_BUTTON; input.code=(message==WM_LBUTTONDOWN || message==WM_LBUTTONUP) ? 1 : 2;
        input.pressed=message==WM_LBUTTONDOWN || message==WM_RBUTTONDOWN;
        input.x=GET_X_LPARAM(lparam); input.y=GET_Y_LPARAM(lparam);
        if (input.pressed) { SetCapture(window); } else { ReleaseCapture(); }
        neo_windows_queue(resource,input); break;
    case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL: {
        POINT point={GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)};
        ScreenToClient(window,&point); input.kind=NEO_INPUT_WHEEL; input.x=point.x; input.y=point.y;
        if (message==WM_MOUSEWHEEL) { input.dy=(double)GET_WHEEL_DELTA_WPARAM(wparam)/WHEEL_DELTA; }
        else { input.dx=(double)GET_WHEEL_DELTA_WPARAM(wparam)/WHEEL_DELTA; }
        neo_windows_queue(resource,input); break;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint; HDC dc=BeginPaint(window,&paint);
        if (resource->pixels && !resource->gl) {
            BITMAPV4HEADER info={0}; info.bV4Size=sizeof(info); info.bV4Width=(LONG)resource->width;
            info.bV4Height=-(LONG)resource->height; info.bV4Planes=1; info.bV4BitCount=32; info.bV4V4Compression=BI_BITFIELDS;
            info.bV4RedMask=0xff; info.bV4GreenMask=0xff00; info.bV4BlueMask=0xff0000; info.bV4AlphaMask=0xff000000;
            info.bV4CSType=0x73524742u;
            SetDIBitsToDevice(dc,0,0,resource->width,resource->height,0,0,0,resource->height,resource->pixels,(const BITMAPINFO *)&info,DIB_RGB_COLORS);
        }
        EndPaint(window,&paint);
        if (resource->pixels && resource->gl) { (void)neo_windows_gl_draw(resource); }
        return 0;
    }
    default: break;
    }
    return DefWindowProcW(window,message,wparam,lparam);
}
static neo_status neo_windows_surface(void *context, uint32_t width, uint32_t height, void **output) {
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW klass={0}; klass.lpfnWndProc=neo_surface_procedure;
    klass.hInstance=GetModuleHandleW(NULL); klass.lpszClassName=L"neo.platform.surface";
    klass.style=CS_OWNDC;
    klass.hCursor=LoadCursorW(NULL,MAKEINTRESOURCEW(32512));
    if (!RegisterClassW(&klass) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) { return NEO_IO_ERROR; }
    neo_windows_resource *resource=calloc(1,sizeof(*resource));
    if (!resource) { return NEO_EXHAUSTED; }
    resource->owner=context; resource->kind=NEO_RESOURCE_SURFACE;
    RECT rectangle={0,0,(LONG)width,(LONG)height};
    AdjustWindowRectExForDpi(&rectangle,WS_OVERLAPPEDWINDOW,FALSE,0,GetDpiForSystem());
    resource->window=CreateWindowExW(0,klass.lpszClassName,L"neo",WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,CW_USEDEFAULT,rectangle.right-rectangle.left,rectangle.bottom-rectangle.top,
        NULL,NULL,klass.hInstance,resource);
    if (!resource->window) { free(resource); return NEO_IO_ERROR; }
    if (resource->owner->renderer!=NEO_RENDER_SOFTWARE) {
        resource->dc=GetDC(resource->window);
        PIXELFORMATDESCRIPTOR format={0}; format.nSize=sizeof(format); format.nVersion=1;
        format.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;
        format.iPixelType=PFD_TYPE_RGBA; format.cColorBits=32; format.cAlphaBits=8;
        int chosen=ChoosePixelFormat(resource->dc,&format);
        if (chosen && SetPixelFormat(resource->dc,chosen,&format)) { resource->gl=wglCreateContext(resource->dc); }
        if (resource->gl && wglMakeCurrent(resource->dc,resource->gl)) {
            if (neo_windows_gl_function("glGenFramebuffers",&resource->gen_framebuffers) &&
                neo_windows_gl_function("glBindFramebuffer",&resource->bind_framebuffer) &&
                neo_windows_gl_function("glFramebufferTexture2D",&resource->attach_texture) &&
                neo_windows_gl_function("glCheckFramebufferStatus",&resource->framebuffer_status)) {
                glGenTextures(1,&resource->texture); glGenTextures(1,&resource->rendered_texture);
                resource->gen_framebuffers(1,&resource->framebuffer);
            } else { wglMakeCurrent(NULL,NULL); wglDeleteContext(resource->gl); resource->gl=NULL; }
        }
        if (!resource->gl && resource->owner->renderer==NEO_RENDER_OPENGL) {
            ReleaseDC(resource->window,resource->dc); DestroyWindow(resource->window); free(resource); return NEO_UNSUPPORTED;
        }
        if (!resource->gl) {
            ReleaseDC(resource->window,resource->dc); resource->dc=NULL; DestroyWindow(resource->window);
            resource->window=CreateWindowExW(0,klass.lpszClassName,L"neo",WS_OVERLAPPEDWINDOW,
                CW_USEDEFAULT,CW_USEDEFAULT,rectangle.right-rectangle.left,rectangle.bottom-rectangle.top,
                NULL,NULL,klass.hInstance,resource);
            if (!resource->window) { free(resource); return NEO_IO_ERROR; }
        }
    }
    *output=resource; return NEO_OK;
}
static void neo_windows_bind(void *context, void *native, neo_handle handle) {
    (void)context;
    neo_windows_resource *resource=native; resource->identity=handle;
    if (resource->kind==NEO_RESOURCE_SURFACE) {
        RECT rectangle; GetClientRect(resource->window,&rectangle);
        neo_input input={.kind=NEO_INPUT_RESIZE,.width=(uint32_t)(rectangle.right-rectangle.left),
            .height=(uint32_t)(rectangle.bottom-rectangle.top),.scale=(double)GetDpiForWindow(resource->window)/96.0};
        neo_windows_queue(resource,input);
    }
    if (resource->kind==NEO_RESOURCE_SURFACE && resource->owner->visible) { ShowWindow(resource->window,SW_SHOW); }
}
static neo_status neo_windows_present(void *context, void *native, const neo_frame *frame) {
    (void)context;
    neo_windows_resource *resource=native;
    if (!IsWindow(resource->window)) { return NEO_DEVICE_LOST; }
    size_t length=(size_t)frame->width*frame->height*4u;
    if (resource->width!=frame->width || resource->height!=frame->height) {
        unsigned char *copy=malloc(length);
        if (!copy) { return NEO_EXHAUSTED; }
        free(resource->pixels); resource->pixels=copy;
        resource->width=frame->width; resource->height=frame->height;
    }
    memcpy(resource->pixels,frame->pixels,length);
    if (resource->gl) { return neo_windows_gl_draw(resource); }
    InvalidateRect(resource->window,NULL,FALSE); UpdateWindow(resource->window);
    return NEO_OK;
}
static neo_status neo_windows_input(void *context, neo_input *input) {
    neo_windows_services *windows=context; MSG message;
    for (size_t i=0; i<NEO_SERVICE_EVENTS && PeekMessageW(&message,NULL,0,0,PM_REMOVE); ++i) {
        TranslateMessage(&message); DispatchMessageW(&message);
    }
    if (windows->input_lost) { windows->input_lost=false; return NEO_EXHAUSTED; }
    if (!windows->count) { return NEO_PENDING; }
    *input=windows->inputs[0]; --windows->count;
    memmove(windows->inputs,windows->inputs+1,windows->count*sizeof(*input)); return NEO_OK;
}
static void neo_windows_close(void *context, void *native, neo_resource_kind kind) {
    (void)context;
    neo_windows_resource *resource=native;
    if (kind==NEO_RESOURCE_SURFACE) {
        if (resource->gl) { wglMakeCurrent(NULL,NULL); wglDeleteContext(resource->gl); }
        if (resource->dc) { ReleaseDC(resource->window,resource->dc); }
        DestroyWindow(resource->window); free(resource->pixels);
    }
    else if (kind==NEO_RESOURCE_AUDIO) {
        if (resource->audio_input) { waveInReset(resource->audio_input); waveInClose(resource->audio_input); }
        if (resource->audio_output) { waveOutReset(resource->audio_output); waveOutClose(resource->audio_output); }
    }
    else if (kind==NEO_RESOURCE_CLIPBOARD) {}
    else if (kind==NEO_RESOURCE_ROOT || kind==NEO_RESOURCE_STREAM) { CloseHandle(resource->file); }
    else { closesocket(resource->socket); }
    free(resource);
}
static DWORD WINAPI neo_windows_transfer(void *argument) {
    neo_windows_io *ticket=argument;
    if (ticket->operation==NEO_IO_STORE_READ) {
        ticket->status=neo_windows_store_read(NULL,ticket->root,ticket->key,ticket->buffer,ticket->capacity,
                                              &ticket->length,&ticket->revision);
        return 0;
    }
    if (ticket->operation==NEO_IO_STORE_REPLACE) {
        ticket->status=neo_windows_replace(NULL,ticket->root,ticket->key,ticket->expected,
                                           ticket->buffer,ticket->capacity,&ticket->revision);
        ticket->length=ticket->status==NEO_OK ? ticket->capacity : 0; return 0;
    }
    if (ticket->operation==NEO_IO_STORE_DELETE) {
        ticket->status=neo_windows_delete(NULL,ticket->root,ticket->key,ticket->expected,&ticket->revision); return 0;
    }
    BOOL success=ticket->operation==NEO_IO_READ ?
        ReadFile(ticket->file,ticket->buffer,ticket->capacity,&ticket->transferred,NULL) :
        WriteFile(ticket->file,ticket->buffer,ticket->capacity,&ticket->transferred,NULL);
    ticket->error=success ? 0 : GetLastError(); return 0;
}
static void neo_windows_cancel(void *context, void *native) {
    (void)context;
    neo_windows_io *ticket=native;
    if (ticket->audio) {
        neo_windows_resource *resource=ticket->audio_resource;
        if ((ticket->header.dwFlags & WHDR_PREPARED) && !(ticket->header.dwFlags & WHDR_DONE) && ticket->audio_epoch==resource->audio_epoch) {
            if (resource->audio_format.capture) { waveInReset(resource->audio_input); } else { waveOutReset(resource->audio_output); }
            ++resource->audio_epoch;
        }
        if (ticket->header.dwFlags & WHDR_PREPARED) {
            if (resource->audio_format.capture) { waveInUnprepareHeader(resource->audio_input,&ticket->header,sizeof(ticket->header)); }
            else { waveOutUnprepareHeader(resource->audio_output,&ticket->header,sizeof(ticket->header)); }
        }
        if (resource->audio_queued) { --resource->audio_queued; }
        free(ticket); return;
    }
    if (WaitForSingleObject(ticket->thread,0)==WAIT_TIMEOUT) {
        CancelSynchronousIo(ticket->thread);
        WaitForSingleObject(ticket->thread,INFINITE);
    }
    CloseHandle(ticket->thread); free(ticket);
}
static neo_status neo_windows_step(void *context, void *native, neo_io_kind operation, void **pending,
                                     unsigned char *bytes, size_t capacity, size_t *length, void **accepted) {
    (void)context;
    neo_windows_resource *resource=native;
    if (resource->kind==NEO_RESOURCE_AUDIO) {
        neo_windows_io *ticket=*pending;
        if (!ticket) {
            ticket=calloc(1,sizeof(*ticket)); if (!ticket) { return NEO_EXHAUSTED; }
            ticket->audio=true; ticket->audio_resource=resource; ticket->audio_epoch=resource->audio_epoch;
            ticket->header.lpData=(char *)bytes; ticket->header.dwBufferLength=(DWORD)capacity;
            MMRESULT result;
            if (resource->audio_format.capture) {
                result=waveInPrepareHeader(resource->audio_input,&ticket->header,sizeof(ticket->header));
                if (!result) { result=waveInAddBuffer(resource->audio_input,&ticket->header,sizeof(ticket->header)); }
                if (!result) { result=waveInStart(resource->audio_input); }
            } else {
                result=waveOutPrepareHeader(resource->audio_output,&ticket->header,sizeof(ticket->header));
                if (!result) { result=waveOutWrite(resource->audio_output,&ticket->header,sizeof(ticket->header)); }
            }
            if (!resource->audio_queued && resource->audio_frames) { ++resource->audio_starvations; }
            ++resource->audio_queued; *pending=ticket;
            return result ? NEO_DEVICE_LOST : NEO_PENDING;
        }
        if (ticket->audio_epoch!=resource->audio_epoch) { return resource->audio_format.capture ? NEO_CANCELLED : NEO_UNCERTAIN; }
        if (!(ticket->header.dwFlags & WHDR_DONE)) { return NEO_PENDING; }
        *length=resource->audio_format.capture ? ticket->header.dwBytesRecorded : capacity;
        resource->audio_frames+=*length/(resource->audio_format.channels*2u); return NEO_OK;
    }
    if (resource->kind==NEO_RESOURCE_CLIPBOARD) {
        neo_windows_services *windows=context;
        if (!OpenClipboard(windows->clipboard)) { return NEO_PENDING; }
        neo_status status=NEO_OK;
        if (operation==NEO_IO_READ) {
            HANDLE data=GetClipboardData(CF_UNICODETEXT);
            if (!data) { status=NEO_EOF; }
            else {
                const wchar_t *text=GlobalLock(data);
                if (!text) { status=NEO_IO_ERROR; }
                else {
                    size_t maximum=GlobalSize(data)/sizeof(wchar_t), characters=0;
                    while (characters<maximum && characters<NEO_SERVICE_BYTES && text[characters]) { ++characters; }
                    if (characters==maximum || characters==NEO_SERVICE_BYTES) {
                        GlobalUnlock(data); CloseClipboard(); return NEO_INVALID;
                    }
                    int count=characters>INT_MAX ? 0 : WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text,(int)characters,NULL,0,NULL,NULL);
                    if (!characters) { *length=0; }
                    else if (!count) { status=NEO_IO_ERROR; }
                    else if ((size_t)count>capacity) { *length=(size_t)count; status=NEO_TRUNCATED; }
                    else { *length=(size_t)WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text,(int)characters,(char *)bytes,(int)capacity,NULL,NULL); }
                    GlobalUnlock(data);
                }
            }
        } else {
            int count=capacity ? MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,(const char *)bytes,(int)capacity,NULL,0) : 0;
            if ((capacity && !count) || memchr(bytes,0,capacity)) { status=NEO_INVALID; }
            else {
                HGLOBAL data=GlobalAlloc(GMEM_MOVEABLE,((size_t)count+1)*sizeof(wchar_t));
                wchar_t *text=data ? GlobalLock(data) : NULL;
                if (!text) { if (data) { GlobalFree(data); } status=NEO_EXHAUSTED; }
                else {
                    if (count) { MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,(const char *)bytes,(int)capacity,text,count); }
                    text[count]=0; GlobalUnlock(data);
                    if (!EmptyClipboard()) { GlobalFree(data); status=NEO_IO_ERROR; }
                    else if (!SetClipboardData(CF_UNICODETEXT,data)) { GlobalFree(data); status=NEO_UNCERTAIN; }
                    else { *length=capacity; }
                }
            }
        }
        CloseClipboard(); return status;
    }
    if (resource->kind==NEO_RESOURCE_STREAM) {
        neo_windows_io *ticket=*pending;
        if (!ticket) {
            ticket=calloc(1,sizeof(*ticket)); if (!ticket) { return NEO_EXHAUSTED; }
            ticket->file=resource->file; ticket->operation=operation; ticket->buffer=bytes; ticket->capacity=(DWORD)capacity;
            ticket->thread=CreateThread(NULL,0,neo_windows_transfer,ticket,0,NULL);
            if (!ticket->thread) { free(ticket); return NEO_IO_ERROR; }
            *pending=ticket; return NEO_PENDING;
        }
        if (WaitForSingleObject(ticket->thread,0)==WAIT_TIMEOUT) { return NEO_PENDING; }
        *length=ticket->transferred;
        if (ticket->error==ERROR_BROKEN_PIPE || ticket->error==ERROR_HANDLE_EOF) { return NEO_EOF; }
        if (ticket->error) { return NEO_IO_ERROR; }
        return operation==NEO_IO_READ && !*length && capacity ? NEO_EOF : NEO_OK;
    }
    if (resource->connecting) {
        fd_set write_set, error_set; FD_ZERO(&write_set); FD_ZERO(&error_set);
        FD_SET(resource->socket,&write_set); FD_SET(resource->socket,&error_set);
        struct timeval timeout={0,0};
        int selected=select(0,NULL,&write_set,&error_set,&timeout);
        if (selected<0) { return NEO_IO_ERROR; }
        if (!selected) { return NEO_PENDING; }
        int error=0, size=sizeof(error);
        if (getsockopt(resource->socket,SOL_SOCKET,SO_ERROR,(char *)&error,&size) || error) { return NEO_IO_ERROR; }
        resource->connecting=false;
    }
    if (operation==NEO_IO_ACCEPT) {
        SOCKET client=accept(resource->socket,NULL,NULL);
        if (client==INVALID_SOCKET) { return WSAGetLastError()==WSAEWOULDBLOCK ? NEO_PENDING : NEO_IO_ERROR; }
        neo_windows_resource *copy=calloc(1,sizeof(*copy));
        if (!copy) { closesocket(client); return NEO_EXHAUSTED; }
        u_long nonblocking=1;
        if (ioctlsocket(client,(long)FIONBIO,&nonblocking)) { closesocket(client); free(copy); return NEO_IO_ERROR; }
        copy->socket=client; copy->owner=resource->owner; copy->kind=NEO_RESOURCE_CONNECTION;
        *accepted=copy; return NEO_OK;
    }
    int count=operation==NEO_IO_READ ? recv(resource->socket,(char *)bytes,(int)capacity,0) :
        send(resource->socket,(const char *)bytes,(int)capacity,0);
    if (count==SOCKET_ERROR) {
        int error=WSAGetLastError();
        if (error==WSAEWOULDBLOCK) { return NEO_PENDING; }
        return error==WSAEMSGSIZE ? NEO_TRUNCATED : NEO_IO_ERROR;
    }
    *length=(size_t)count;
    return operation==NEO_IO_READ && !count && capacity && resource->kind!=NEO_RESOURCE_DATAGRAM ? NEO_EOF : NEO_OK;
}
static neo_status neo_windows_endpoint(void *context, neo_resource_kind kind, bool listening,
                                         uint16_t *port, void **output) {
    int type=kind==NEO_RESOURCE_DATAGRAM ? SOCK_DGRAM : SOCK_STREAM;
    SOCKET socket_value=socket(AF_INET,type,0);
    if (socket_value==INVALID_SOCKET) { return NEO_IO_ERROR; }
    u_long nonblocking=1;
    if (ioctlsocket(socket_value,(long)FIONBIO,&nonblocking)) { closesocket(socket_value); return NEO_IO_ERROR; }
    struct sockaddr_in address={0}; address.sin_family=AF_INET;
    address.sin_addr.s_addr=htonl(INADDR_LOOPBACK); address.sin_port=htons(*port);
    bool connecting=false;
    if (kind==NEO_RESOURCE_LISTENER || listening) {
        if (bind(socket_value,(struct sockaddr *)&address,sizeof(address)) ||
            (kind==NEO_RESOURCE_LISTENER && listen(socket_value,4))) { closesocket(socket_value); return NEO_IO_ERROR; }
        int length=sizeof(address);
        if (getsockname(socket_value,(struct sockaddr *)&address,&length)) { closesocket(socket_value); return NEO_IO_ERROR; }
        *port=ntohs(address.sin_port);
        // A bound datagram endpoint receives. A separately granted connected endpoint sends.
    } else if (connect(socket_value,(struct sockaddr *)&address,sizeof(address))) {
        if (WSAGetLastError()!=WSAEWOULDBLOCK) { closesocket(socket_value); return NEO_IO_ERROR; }
        connecting=true;
    }
    neo_windows_resource *resource=calloc(1,sizeof(*resource));
    if (!resource) { closesocket(socket_value); return NEO_EXHAUSTED; }
    resource->kind=kind; resource->socket=socket_value; resource->owner=context; resource->connecting=connecting;
    *output=resource; return NEO_OK;
}
static neo_status neo_windows_standard(void *context, unsigned number, void **output,
                                         bool *terminal, uint32_t *columns, uint32_t *rows) {
    const DWORD streams[]={STD_INPUT_HANDLE,STD_OUTPUT_HANDLE,STD_ERROR_HANDLE};
    HANDLE original=GetStdHandle(streams[number]), file=NULL; DWORD mode;
    if (!original || original==INVALID_HANDLE_VALUE ||
        !DuplicateHandle(GetCurrentProcess(),original,GetCurrentProcess(),&file,0,FALSE,DUPLICATE_SAME_ACCESS)) { return NEO_UNSUPPORTED; }
    *terminal=GetConsoleMode(file,&mode)!=FALSE; *columns=0; *rows=0;
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(file,&info)) {
        *columns=(uint32_t)(info.srWindow.Right-info.srWindow.Left+1);
        *rows=(uint32_t)(info.srWindow.Bottom-info.srWindow.Top+1);
    }
    neo_windows_resource *resource=calloc(1,sizeof(*resource));
    if (!resource) { CloseHandle(file); return NEO_EXHAUSTED; }
    resource->owner=context; resource->kind=NEO_RESOURCE_STREAM; resource->file=file;
    *output=resource; return NEO_OK;
}

/* A root is an explicitly granted, managed opaque byte store. Its version
   metadata is adapter-private. External writers must honor this protocol. */
static bool neo_windows_name(neo_windows_resource *root, const char *key, const wchar_t *suffix,
                                 wchar_t path[1200]) {
    wchar_t name[64];
    if (!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,key,-1,name,64)) { return false; }
    int count=swprintf(path,1200,L"%ls\\%ls%ls",root->path,name,suffix);
    return count>0 && count<1200;
}
static bool neo_windows_plain(HANDLE file, bool directory) {
    BY_HANDLE_FILE_INFORMATION info;
    return GetFileInformationByHandle(file,&info) && !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
        ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)!=0)==directory;
}
static neo_status neo_windows_root(void *context, const char *path, void **output) {
    wchar_t wide[1024];
    if (!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,wide,1024)) { return NEO_INVALID; }
    HANDLE directory=CreateFileW(wide,FILE_LIST_DIRECTORY,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,
        OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    if (directory==INVALID_HANDLE_VALUE) { return NEO_IO_ERROR; }
    neo_windows_resource *resource=calloc(1,sizeof(*resource));
    if (!resource) { CloseHandle(directory); return NEO_EXHAUSTED; }
    DWORD length=GetFinalPathNameByHandleW(directory,resource->path,1024,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    if (!neo_windows_plain(directory,true) || !length || length>=1024) {
        CloseHandle(directory); free(resource); return NEO_DENIED;
    }
    resource->owner=context; resource->kind=NEO_RESOURCE_ROOT; resource->file=directory;
    *output=resource; return NEO_OK;
}
static neo_status neo_windows_lock(neo_windows_resource *root, const char *key, HANDLE *lock) {
    wchar_t path[1200];
    if (!neo_windows_name(root,key,L".lock",path)) { return NEO_INVALID; }
    *lock=CreateFileW(path,GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_ALWAYS,FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    if (*lock==INVALID_HANDLE_VALUE) { return GetLastError()==ERROR_SHARING_VIOLATION ? NEO_CONFLICT : NEO_IO_ERROR; }
    if (!neo_windows_plain(*lock,false)) { CloseHandle(*lock); return NEO_DENIED; }
    return NEO_OK;
}
static neo_status neo_windows_read_record(neo_windows_resource *root, const char *key, void *bytes,
                                             size_t capacity, size_t *length, uint64_t *revision) {
    wchar_t path[1200];
    if (!neo_windows_name(root,key,L".data",path)) { return NEO_INVALID; }
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    if (file==INVALID_HANDLE_VALUE) { return GetLastError()==ERROR_FILE_NOT_FOUND ? NEO_EOF : NEO_IO_ERROR; }
    uint64_t header[3]={0}; DWORD count=0; LARGE_INTEGER size;
    neo_status status=NEO_IO_ERROR;
    if (!neo_windows_plain(file,false)) { status=NEO_DENIED; }
    else if (ReadFile(file,header,sizeof(header),&count,NULL) && count==sizeof(header) &&
        header[0]==UINT64_C(0x3145524f54534f4e) && (header[2]<=NEO_SERVICE_BYTES || header[2]==UINT64_MAX) &&
        GetFileSizeEx(file,&size) && (uint64_t)size.QuadPart==sizeof(header)+(header[2]==UINT64_MAX ? 0 : header[2])) {
        *revision=header[1]; *length=header[2]==UINT64_MAX ? 0 : (size_t)header[2];
        if (header[2]==UINT64_MAX) { status=NEO_EOF; }
        else if (*length>capacity) { status=NEO_TRUNCATED; }
        else if (ReadFile(file,bytes,(DWORD)*length,&count,NULL) && count==*length) { status=NEO_OK; }
    }
    CloseHandle(file); return status;
}
static neo_status neo_windows_store_read(void *context, void *native, const char *key, void *bytes,
                                           size_t capacity, size_t *length, uint64_t *revision) {
    (void)context;
    HANDLE lock;
    neo_status status=neo_windows_lock(native,key,&lock);
    if (status!=NEO_OK) { return status; }
    status=neo_windows_read_record(native,key,bytes,capacity,length,revision);
    CloseHandle(lock); return status;
}
static neo_status neo_windows_commit(void *context, void *native, const char *key, uint64_t expected,
                                        const void *bytes, size_t length, bool deleted, uint64_t *revision) {
    (void)context;
    neo_windows_resource *root=native; HANDLE lock;
    neo_status status=neo_windows_lock(root,key,&lock);
    if (status!=NEO_OK) { return status; }
    size_t previous_length=0; uint64_t previous=0;
    status=neo_windows_read_record(root,key,NULL,0,&previous_length,&previous);
    if (status==NEO_EOF || status==NEO_TRUNCATED || status==NEO_OK) {
        if (previous!=expected) { status=NEO_CONFLICT; }
        else if (expected==UINT64_MAX) { status=NEO_EXHAUSTED; }
        else {
            wchar_t path[1200], temporary[1200], suffix[80]; uint64_t random=0;
            if (neo_windows_entropy(NULL,&random,sizeof(random))!=NEO_OK) { CloseHandle(lock); return NEO_IO_ERROR; }
            swprintf(suffix,80,L".%llu.tmp",(unsigned long long)random);
            if (!neo_windows_name(root,key,L".data",path) || !neo_windows_name(root,key,suffix,temporary)) {
                CloseHandle(lock); return NEO_INVALID;
            }
            HANDLE file=CreateFileW(temporary,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
            if (file==INVALID_HANDLE_VALUE) { CloseHandle(lock); return NEO_IO_ERROR; }
            uint64_t header[]={UINT64_C(0x3145524f54534f4e),expected+1,deleted ? UINT64_MAX : (uint64_t)length}; DWORD count;
            bool written=WriteFile(file,header,sizeof(header),&count,NULL) && count==sizeof(header) &&
                WriteFile(file,bytes,(DWORD)length,&count,NULL) && count==length && FlushFileBuffers(file);
            CloseHandle(file);
            status=NEO_IO_ERROR;
            if (written) {
                if (MoveFileExW(temporary,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
                    *revision=expected+1; status=NEO_OK;
                } else { status=NEO_UNCERTAIN; }
            }
            DeleteFileW(temporary);
        }
    }
    CloseHandle(lock); return status;
}
static neo_status neo_windows_replace(void *context, void *root, const char *key, uint64_t expected,
                                         const void *bytes, size_t length, uint64_t *revision) {
    return neo_windows_commit(context,root,key,expected,bytes,length,false,revision);
}
static neo_status neo_windows_delete(void *context, void *root, const char *key, uint64_t expected, uint64_t *revision) {
    return neo_windows_commit(context,root,key,expected,NULL,0,true,revision);
}
static neo_status neo_windows_list(void *context, void *native, char *text, size_t capacity) {
    (void)context;
    neo_windows_resource *root=native; wchar_t pattern[1200];
    swprintf(pattern,1200,L"%ls\\*.data",root->path);
    WIN32_FIND_DATAW found; HANDLE search=FindFirstFileW(pattern,&found); size_t length=0;
    if (search==INVALID_HANDLE_VALUE) {
        if (GetLastError()!=ERROR_FILE_NOT_FOUND) { return NEO_IO_ERROR; }
        text[0]=0; return NEO_OK;
    }
    neo_status status=NEO_OK;
    do {
        if (found.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)) { continue; }
        size_t characters=wcslen(found.cFileName);
        if (characters<5) { continue; }
        found.cFileName[characters-5]=0; char key[256];
        int bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,found.cFileName,-1,key,256,NULL,NULL);
        if (!bytes || length+(size_t)bytes+1>capacity) { status=NEO_TRUNCATED; break; }
        size_t payload=0; uint64_t revision=0;
        neo_status state=neo_windows_store_read(context,root,key,NULL,0,&payload,&revision);
        if (state==NEO_EOF) { continue; }
        if (state!=NEO_OK && state!=NEO_TRUNCATED) { status=state; break; }
        memcpy(text+length,key,(size_t)bytes-1); length+=(size_t)bytes-1; text[length++]='\n';
    } while (FindNextFileW(search,&found));
    FindClose(search); text[length]=0; return status;
}
static neo_status neo_windows_store_step(void *context, void *native, neo_io_kind operation,
                                            const char *key, uint64_t expected, void **pending,
                                            unsigned char *bytes, size_t capacity, size_t *length, uint64_t *revision) {
    (void)context;
    neo_windows_io *ticket=*pending;
    if (!ticket) {
        ticket=calloc(1,sizeof(*ticket)); if (!ticket) { return NEO_EXHAUSTED; }
        ticket->root=native; ticket->operation=operation; ticket->buffer=bytes;
        ticket->capacity=(DWORD)capacity; ticket->expected=expected; strcpy(ticket->key,key);
        ticket->thread=CreateThread(NULL,0,neo_windows_transfer,ticket,0,NULL);
        if (!ticket->thread) { free(ticket); return NEO_IO_ERROR; }
        *pending=ticket; return NEO_PENDING;
    }
    if (WaitForSingleObject(ticket->thread,0)==WAIT_TIMEOUT) { return NEO_PENDING; }
    *length=ticket->length; *revision=ticket->revision; return ticket->status;
}
static void neo_windows_destroy(void *context) {
    neo_windows_services *windows=context;
    if (windows->wakeup) { CloseHandle(windows->wakeup); }
    if (windows->clipboard) { DestroyWindow(windows->clipboard); }
    WSACleanup(); free(context);
}
static neo_status neo_windows_audio(void *context, const neo_audio_format *format, void **output) {
    if (format->capture ? !waveInGetNumDevs() : !waveOutGetNumDevs()) { return NEO_UNSUPPORTED; }
    neo_windows_resource *resource=calloc(1,sizeof(*resource)); if (!resource) { return NEO_EXHAUSTED; }
    resource->owner=context; resource->kind=NEO_RESOURCE_AUDIO; resource->audio_format=*format;
    WAVEFORMATEX wave={0}; wave.wFormatTag=WAVE_FORMAT_PCM; wave.nChannels=(WORD)format->channels;
    wave.nSamplesPerSec=format->rate; wave.wBitsPerSample=16; wave.nBlockAlign=(WORD)(format->channels*2u);
    wave.nAvgBytesPerSec=wave.nSamplesPerSec*wave.nBlockAlign;
    MMRESULT result=format->capture ? waveInOpen(&resource->audio_input,WAVE_MAPPER,&wave,0,0,CALLBACK_NULL) :
        waveOutOpen(&resource->audio_output,WAVE_MAPPER,&wave,0,0,CALLBACK_NULL);
    if (result) { free(resource); return NEO_UNSUPPORTED; } *output=resource; return NEO_OK;
}
static neo_status neo_windows_audio_stats(void *context, void *native, neo_audio_stats *stats) {
    (void)context; neo_windows_resource *resource=native;
    *stats=(neo_audio_stats){resource->audio_frames,resource->audio_starvations}; return NEO_OK;
}
static neo_status neo_windows_clipboard(void *context, void **output) {
    neo_windows_services *windows=context;
    if (!windows->clipboard) {
        WNDCLASSW klass={0}; klass.lpfnWndProc=DefWindowProcW; klass.hInstance=GetModuleHandleW(NULL); klass.lpszClassName=L"neo.platform.clipboard";
        if (!RegisterClassW(&klass) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) { return NEO_IO_ERROR; }
        windows->clipboard=CreateWindowExW(0,klass.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,NULL,klass.hInstance,NULL);
        if (!windows->clipboard) { return NEO_IO_ERROR; }
    }
    neo_windows_resource *resource=calloc(1,sizeof(*resource)); if (!resource) { return NEO_EXHAUSTED; }
    resource->owner=context; resource->kind=NEO_RESOURCE_CLIPBOARD; *output=resource; return NEO_OK;
}
static neo_status neo_windows_position(void *context, void *native, int32_t x, int32_t y) {
    (void)context; neo_windows_resource *resource=native;
    return SetWindowPos(resource->window,NULL,x,y,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE) ? NEO_OK : NEO_DEVICE_LOST;
}
static neo_status neo_windows_renderer(void *context, neo_renderer renderer) { ((neo_windows_services *)context)->renderer=renderer; return NEO_OK; }
static neo_status neo_windows_renderer_name(void *context, void *native, char *text, size_t capacity) {
    (void)context; neo_windows_resource *resource=native;
    if (!resource->gl) { if (capacity<9) { return NEO_TRUNCATED; } memcpy(text,"software",9); return NEO_OK; }
    if (!wglMakeCurrent(resource->dc,resource->gl)) { return NEO_DEVICE_LOST; }
    const GLubyte *driver=glGetString(GL_RENDERER); if (!driver) { return NEO_DEVICE_LOST; }
    int count=snprintf(text,capacity,"opengl: %s",(const char *)driver);
    return count>=0 && (size_t)count<capacity ? NEO_OK : NEO_TRUNCATED;
}
neo_status neo_platform_services(bool visible, neo_services **output) {
    *output=NULL;
    WSADATA data;
    if (WSAStartup(MAKEWORD(2,2),&data)) { return NEO_IO_ERROR; }
    neo_windows_services *context=calloc(1,sizeof(*context));
    if (!context) { WSACleanup(); return NEO_EXHAUSTED; }
    context->visible=visible;
    context->wakeup=CreateEventW(NULL,FALSE,FALSE,NULL);
    if (!context->wakeup) { neo_windows_destroy(context); return NEO_EXHAUSTED; }
    neo_service_backend backend={.clock_ns=neo_windows_clock,.wait=neo_windows_wait,.wake=neo_windows_wake,
        .machine=neo_windows_machine,.entropy=neo_windows_entropy,
        .close=neo_windows_close,.bind=neo_windows_bind,.step=neo_windows_step,.cancel=neo_windows_cancel,
        .surface_open=neo_windows_surface,.present=neo_windows_present,.input=neo_windows_input,
        .root_open=neo_windows_root,.store_read=neo_windows_store_read,.store_replace=neo_windows_replace,
        .store_list=neo_windows_list,.store_delete=neo_windows_delete,.store_step=neo_windows_store_step,.endpoint=neo_windows_endpoint,
        .standard=neo_windows_standard,.renderer=neo_windows_renderer,.renderer_name=neo_windows_renderer_name,
        .audio=neo_windows_audio,.audio_stats=neo_windows_audio_stats,.clipboard=neo_windows_clipboard,
        .position=neo_windows_position,.destroy=neo_windows_destroy};
    neo_capabilities capabilities={true,true,true,true,true,true,true};
    neo_status status=neo_services_open(&backend,context,capabilities,output);
    if (status!=NEO_OK) { neo_windows_destroy(context); }
    return status;
}


static bool neo_windows_completion(neo_services *services, uint64_t id, neo_completion *completion) {
    uint64_t deadline=neo_services_clock(services)+UINT64_C(3000000000);
    while (neo_services_clock(services)<deadline) {
        neo_services_wait(services,1);
        if (neo_services_next(services,completion)==NEO_OK) { return completion->id==id; }
    }
    return false;
}
#define NEO_WIN_CHECK(expression) do { if (!(expression)) { fprintf(stderr,"Windows platform selftest line %d: %s\n",__LINE__,#expression); goto done; } } while (0)
bool neo_platform_services_selftest(void) {
    neo_services *first=NULL, *second=NULL; bool passed=false;
    bool directory_created=false;
    wchar_t directory[1024]={0}, temporary[1024]={0};
    char path[4096]; uint64_t random=0;
    neo_handle listener, client, server, receiver, sender, root, other_root, readonly, surface;
    uint64_t id=0, revision=0; uint16_t port=0; neo_completion completion;
    NEO_WIN_CHECK(neo_platform_services(false,&first)==NEO_OK);
    NEO_WIN_CHECK(neo_platform_services(false,&second)==NEO_OK);
    NEO_WIN_CHECK(neo_services_entropy(first,&random,sizeof(random))==NEO_OK);
    neo_machine machine;
    NEO_WIN_CHECK(neo_services_machine(first,&machine)==NEO_OK && machine.word_bits==64 && machine.processors && machine.page_size && machine.clock_resolution_ns);
    DWORD characters=GetTempPathW(900,temporary);
    NEO_WIN_CHECK(characters>0 && characters<900);
    swprintf(directory,1024,L"%lsneo-services-%llu",temporary,(unsigned long long)random);
    NEO_WIN_CHECK(CreateDirectoryW(directory,NULL));
    directory_created=true;
    NEO_WIN_CHECK(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,directory,-1,path,sizeof(path),NULL,NULL)>0);
    NEO_WIN_CHECK(neo_services_root(first,path,NEO_RIGHT_READ|NEO_RIGHT_WRITE,&root)==NEO_OK);
    NEO_WIN_CHECK(neo_services_root(second,path,NEO_RIGHT_READ|NEO_RIGHT_WRITE,&other_root)==NEO_OK);
    NEO_WIN_CHECK(neo_services_root(first,path,NEO_RIGHT_READ,&readonly)==NEO_OK);
    NEO_WIN_CHECK(neo_services_store_replace(first,root,"sample",0,"alpha",5,&revision)==NEO_OK && revision==1);
    NEO_WIN_CHECK(neo_services_store_replace(second,other_root,"sample",0,"wrong",5,&revision)==NEO_CONFLICT);
    NEO_WIN_CHECK(neo_services_store_replace(first,readonly,"sample",1,"wrong",5,&revision)==NEO_DENIED);
    NEO_WIN_CHECK(neo_services_store_replace(first,root,"../escape",0,"wrong",5,&revision)==NEO_INVALID);
    unsigned char bytes[256]={0}; size_t length;
    NEO_WIN_CHECK(neo_services_store_read(second,other_root,"sample",bytes,sizeof(bytes),&length,&revision)==NEO_OK &&
        length==5 && revision==1 && !memcmp(bytes,"alpha",5));
    NEO_WIN_CHECK(neo_services_store_replace(second,other_root,"sample",1,"beta",4,&revision)==NEO_OK && revision==2);
    NEO_WIN_CHECK(neo_services_store_list(first,root,(char *)bytes,sizeof(bytes))==NEO_OK && !strcmp((char *)bytes,"sample\n"));
    uint64_t competing=0;
    NEO_WIN_CHECK(neo_services_store_submit(first,root,"sample",NEO_IO_STORE_REPLACE,2,"winner-a",8,0,&id)==NEO_OK);
    NEO_WIN_CHECK(neo_services_store_submit(second,other_root,"sample",NEO_IO_STORE_REPLACE,2,"winner-b",8,0,&competing)==NEO_OK);
    neo_services_poll(first); neo_services_poll(second);
    NEO_WIN_CHECK(neo_windows_completion(first,id,&completion));
    neo_status first_status=completion.status;
    NEO_WIN_CHECK(neo_services_release(first,id)==NEO_OK);
    NEO_WIN_CHECK(neo_windows_completion(second,competing,&completion));
    NEO_WIN_CHECK((first_status==NEO_OK && completion.status==NEO_CONFLICT) ||
                 (first_status==NEO_CONFLICT && completion.status==NEO_OK));
    NEO_WIN_CHECK(neo_services_release(second,competing)==NEO_OK);
    NEO_WIN_CHECK(neo_services_store_submit(first,root,"sample",NEO_IO_STORE_READ,0,NULL,sizeof(bytes),0,&id)==NEO_OK);
    NEO_WIN_CHECK(neo_windows_completion(first,id,&completion) && completion.status==NEO_OK && completion.revision==3 &&
                 completion.length==8 && !memcmp(completion.bytes,"winner-",7));
    NEO_WIN_CHECK(neo_services_release(first,id)==NEO_OK);
    NEO_WIN_CHECK(neo_services_store_submit(first,root,"unstarted",NEO_IO_STORE_REPLACE,0,"no",2,0,&id)==NEO_OK);
    NEO_WIN_CHECK(neo_services_cancel(first,id)==NEO_OK);
    NEO_WIN_CHECK(neo_services_next(first,&completion)==NEO_OK && completion.status==NEO_CANCELLED);
    NEO_WIN_CHECK(neo_services_release(first,id)==NEO_OK);
    NEO_WIN_CHECK(neo_services_store_replace(first,root,"removed",0,"old",3,&revision)==NEO_OK);
    NEO_WIN_CHECK(neo_services_store_submit(first,root,"removed",NEO_IO_STORE_DELETE,1,NULL,0,0,&id)==NEO_OK);
    NEO_WIN_CHECK(neo_windows_completion(first,id,&completion) && completion.status==NEO_OK && completion.revision==2);
    NEO_WIN_CHECK(neo_services_release(first,id)==NEO_OK);
    NEO_WIN_CHECK(neo_services_store_read(second,other_root,"removed",bytes,sizeof(bytes),&length,&revision)==NEO_EOF && revision==2);
    NEO_WIN_CHECK(neo_services_store_replace(second,other_root,"removed",0,"wrong",5,&revision)==NEO_CONFLICT);
    unsigned char large[4096]; memset(large,0xa5,sizeof(large));
    NEO_WIN_CHECK(neo_services_store_replace(first,root,"interrupt",0,"old",3,&revision)==NEO_OK);
    NEO_WIN_CHECK(neo_services_store_submit(first,root,"interrupt",NEO_IO_STORE_REPLACE,1,large,sizeof(large),0,&id)==NEO_OK);
    neo_services_poll(first);
    NEO_WIN_CHECK(neo_services_cancel(first,id)==NEO_OK);
    NEO_WIN_CHECK(neo_services_next(first,&completion)==NEO_OK && completion.status==NEO_UNCERTAIN);
    NEO_WIN_CHECK(neo_services_release(first,id)==NEO_OK);
    NEO_WIN_CHECK(neo_services_store_read(second,other_root,"interrupt",large,sizeof(large),&length,&revision)==NEO_OK);
    NEO_WIN_CHECK((revision==1 && length==3 && !memcmp(large,"old",3)) ||
                 (revision==2 && length==sizeof(large) && large[0]==0xa5 && large[4095]==0xa5));
    NEO_WIN_CHECK(neo_services_surface(first,4,3,&surface)==NEO_OK);
    neo_frame frame;
    NEO_WIN_CHECK(neo_services_acquire(first,surface,&frame)==NEO_OK);
    for (size_t i=0; i<frame.stride*frame.height; i+=4) {
        frame.pixels[i]=0x12; frame.pixels[i+1]=0x34; frame.pixels[i+2]=0x56; frame.pixels[i+3]=0xff;
    }
    NEO_WIN_CHECK(neo_services_present(first,&frame)==NEO_OK);
    NEO_WIN_CHECK(neo_services_capture(first,surface,bytes,sizeof(bytes),&length)==NEO_OK && length==48 && bytes[0]==0x12 && bytes[2]==0x56);
    NEO_WIN_CHECK(neo_services_endpoint(first,NEO_RESOURCE_LISTENER,true,&port,NEO_RIGHT_ACCEPT,&listener)==NEO_OK && port);
    NEO_WIN_CHECK(neo_services_endpoint(second,NEO_RESOURCE_CONNECTION,false,&port,NEO_RIGHT_READ|NEO_RIGHT_WRITE,&client)==NEO_OK);
    NEO_WIN_CHECK(neo_services_submit(first,listener,NEO_IO_ACCEPT,NULL,0,0,&id)==NEO_OK);
    NEO_WIN_CHECK(neo_windows_completion(first,id,&completion) && completion.status==NEO_OK);
    server=completion.accepted; NEO_WIN_CHECK(neo_services_release(first,id)==NEO_OK);
    NEO_WIN_CHECK(neo_services_submit(first,client,NEO_IO_READ,NULL,4,0,&id)==NEO_CLOSED);
    NEO_WIN_CHECK(neo_services_submit(second,client,NEO_IO_WRITE,"hello",5,0,&id)==NEO_OK);
    NEO_WIN_CHECK(neo_windows_completion(second,id,&completion) && completion.status==NEO_OK && completion.length==5);
    NEO_WIN_CHECK(neo_services_release(second,id)==NEO_OK);
    NEO_WIN_CHECK(neo_services_submit(first,server,NEO_IO_READ,NULL,2,0,&id)==NEO_OK);
    NEO_WIN_CHECK(neo_windows_completion(first,id,&completion) && completion.status==NEO_OK && completion.length==2 && !memcmp(completion.bytes,"he",2));
    NEO_WIN_CHECK(neo_services_release(first,id)==NEO_OK);
    NEO_WIN_CHECK(neo_services_submit(first,server,NEO_IO_READ,NULL,8,0,&id)==NEO_OK);
    NEO_WIN_CHECK(neo_windows_completion(first,id,&completion) && completion.status==NEO_OK && completion.length==3 && !memcmp(completion.bytes,"llo",3));
    NEO_WIN_CHECK(neo_services_release(first,id)==NEO_OK);
    NEO_WIN_CHECK(neo_services_submit(first,server,NEO_IO_READ,NULL,8,0,&id)==NEO_OK);
    neo_services_poll(first);
    NEO_WIN_CHECK(neo_services_next(first,&completion)==NEO_PENDING);
    NEO_WIN_CHECK(neo_services_cancel(first,id)==NEO_OK);
    NEO_WIN_CHECK(neo_services_next(first,&completion)==NEO_OK && completion.status==NEO_CANCELLED);
    NEO_WIN_CHECK(neo_services_release(first,id)==NEO_OK);
    NEO_WIN_CHECK(neo_services_revoke(second,client)==NEO_OK);
    NEO_WIN_CHECK(neo_services_submit(first,server,NEO_IO_READ,NULL,8,0,&id)==NEO_OK);
    NEO_WIN_CHECK(neo_windows_completion(first,id,&completion) && completion.status==NEO_EOF);
    NEO_WIN_CHECK(neo_services_release(first,id)==NEO_OK);
    port=0;
    NEO_WIN_CHECK(neo_services_endpoint(first,NEO_RESOURCE_DATAGRAM,true,&port,NEO_RIGHT_READ,&receiver)==NEO_OK && port);
    NEO_WIN_CHECK(neo_services_endpoint(second,NEO_RESOURCE_DATAGRAM,false,&port,NEO_RIGHT_WRITE,&sender)==NEO_OK);
    NEO_WIN_CHECK(neo_services_submit(second,sender,NEO_IO_WRITE,"packet",6,0,&id)==NEO_OK);
    NEO_WIN_CHECK(neo_windows_completion(second,id,&completion) && completion.status==NEO_OK);
    NEO_WIN_CHECK(neo_services_release(second,id)==NEO_OK);
    NEO_WIN_CHECK(neo_services_submit(first,receiver,NEO_IO_READ,NULL,2,0,&id)==NEO_OK);
    NEO_WIN_CHECK(neo_windows_completion(first,id,&completion) && completion.status==NEO_TRUNCATED);
    NEO_WIN_CHECK(neo_services_release(first,id)==NEO_OK);
    {
        neo_windows_services test_context={0}; void *native=NULL;
        NEO_WIN_CHECK(neo_windows_surface(&test_context,4,3,&native)==NEO_OK);
        neo_windows_bind(&test_context,native,(neo_handle){1,1,0,NEO_RESOURCE_SURFACE});
        neo_windows_resource *test_surface=native;
        neo_input input;
        while (neo_windows_input(&test_context,&input)==NEO_OK) {}
        SendMessageW(test_surface->window,WM_MOUSEWHEEL,MAKEWPARAM(0,15),0);
        bool wheel=neo_windows_input(&test_context,&input)==NEO_OK && input.kind==NEO_INPUT_WHEEL && input.dy==0.125;
        SendMessageW(test_surface->window,WM_CHAR,0xd83d,0); SendMessageW(test_surface->window,WM_CHAR,0xde00,0);
        bool unicode=neo_windows_input(&test_context,&input)==NEO_OK && input.kind==NEO_INPUT_TEXT &&
            !memcmp(input.text,"\xf0\x9f\x98\x80",5);
        unsigned char rgba[]={0x12,0x34,0x56,0xff};
        neo_frame sample={.width=1,.height=1,.stride=4,.pixels=rgba};
        bool channels=neo_windows_present(&test_context,native,&sample)==NEO_OK &&
            test_surface->pixels[0]==0x12 && test_surface->pixels[1]==0x34 && test_surface->pixels[2]==0x56;
        neo_windows_close(&test_context,native,NEO_RESOURCE_SURFACE);
        NEO_WIN_CHECK(wheel && unicode && channels);
    }
    passed=true;
done:
    neo_services_close(second); neo_services_close(first);
    // Delete only known files in the unique directory we created under GetTempPath.
    if (directory_created && directory[0] && temporary[0] && !wcsncmp(directory,temporary,wcslen(temporary))) {
        wchar_t file[1200];
        swprintf(file,1200,L"%ls\\sample.data",directory); DeleteFileW(file);
        swprintf(file,1200,L"%ls\\sample.lock",directory); DeleteFileW(file);
        swprintf(file,1200,L"%ls\\removed.data",directory); DeleteFileW(file);
        swprintf(file,1200,L"%ls\\removed.lock",directory); DeleteFileW(file);
        swprintf(file,1200,L"%ls\\interrupt.data",directory); DeleteFileW(file);
        swprintf(file,1200,L"%ls\\interrupt.lock",directory); DeleteFileW(file);
        RemoveDirectoryW(directory);
    }
    if (passed) { puts("PASS Windows platform: scoped persistent replacement, hidden framebuffer, TCP peers, partial reads, cancellation, EOF, and UDP truncation"); }
    return passed;
}
