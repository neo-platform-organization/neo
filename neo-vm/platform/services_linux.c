#define _GNU_SOURCE
#include "../platform.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include <sys/socket.h>
#include <sys/eventfd.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <locale.h>
#include <alsa/asoundlib.h>
#include <limits.h>
#include <signal.h>

typedef struct neo_linux_services neo_linux_services;
static XErrorHandler neo_previous_xerror;
static pthread_once_t neo_x11_once=PTHREAD_ONCE_INIT;
static int neo_x11_error(Display *display, XErrorEvent *error) {
    // Selection requestors can disappear between request and response.
    if (error->error_code==BadWindow) { return 0; }
    return neo_previous_xerror ? neo_previous_xerror(display,error) : 0;
}
static void neo_x11_initialize(void) { signal(SIGPIPE,SIG_IGN); XInitThreads(); neo_previous_xerror=XSetErrorHandler(neo_x11_error); }
typedef struct {
    neo_resource_kind kind; neo_linux_services *owner; neo_handle identity;
    int fd, original_flags; bool restore_flags, connecting;
    Window window; Colormap colormap; XVisualInfo *visual; XIC input;
    GLXContext gl; GLuint texture;
    snd_pcm_t *audio;
    neo_audio_format audio_format;
    uint64_t audio_frames, audio_starvations;
    unsigned char *clipboard_data;
    size_t clipboard_length;
    void *clipboard_request;
} neo_linux_resource;
struct neo_linux_services {
    int wake; bool visible; Display *display; Atom close; XIM input;
    neo_renderer renderer;
    neo_linux_resource *surfaces[NEO_SERVICE_RESOURCES];
    neo_input queued[NEO_SERVICE_EVENTS]; size_t count;
    neo_linux_resource *clipboard;
    Atom clipboard_atom, utf8_atom, targets_atom, transfer_atom;
};
typedef struct {
    unsigned kind;
    neo_linux_resource *resource;
    Window window;
    bool done;
    neo_status status;
    unsigned char *data;
    size_t length;
} neo_linux_clipboard_job;
static void neo_linux_selection(neo_linux_services *linux) {
    if (!linux->display || !linux->clipboard) { return; }
    XEvent event;
    while (XCheckTypedEvent(linux->display,SelectionRequest,&event)) {
        XSelectionRequestEvent *request=&event.xselectionrequest;
        if (request->owner!=linux->clipboard->window) { continue; }
        XEvent response={0}; response.xselection.type=SelectionNotify; response.xselection.display=linux->display;
        response.xselection.requestor=request->requestor; response.xselection.selection=request->selection;
        response.xselection.target=request->target; response.xselection.time=request->time;
        response.xselection.property=None; Atom property=request->property ? request->property : request->target;
        if (request->target==linux->targets_atom) {
            Atom targets[]={linux->targets_atom,linux->utf8_atom};
            XChangeProperty(linux->display,request->requestor,property,XA_ATOM,32,PropModeReplace,(const unsigned char *)targets,2);
            response.xselection.property=property;
        } else if (request->target==linux->utf8_atom) {
            XChangeProperty(linux->display,request->requestor,property,linux->utf8_atom,8,PropModeReplace,
                linux->clipboard->clipboard_data,(int)linux->clipboard->clipboard_length);
            response.xselection.property=property;
        }
        XSendEvent(linux->display,request->requestor,False,0,&response); XFlush(linux->display);
    }
    while (XCheckTypedEvent(linux->display,SelectionNotify,&event)) {
        neo_linux_clipboard_job *job=linux->clipboard->clipboard_request;
        if (!job || job->window!=event.xselection.requestor) { continue; }
        job->done=true; job->status=NEO_EOF;
        if (event.xselection.property!=None) {
            Atom type; int format; unsigned long count, remaining; unsigned char *data=NULL;
            if (XGetWindowProperty(linux->display,job->window,linux->transfer_atom,0,(NEO_SERVICE_BYTES+3)/4,True,
                linux->utf8_atom,&type,&format,&count,&remaining,&data)==Success && type==linux->utf8_atom && format==8) {
                job->data=data; job->length=(size_t)count+(size_t)remaining; job->status=remaining ? NEO_TRUNCATED : NEO_OK;
            } else { if (data) { XFree(data); } job->status=NEO_IO_ERROR; }
        }
    }
}
static uint64_t neo_linux_clock(void *context) {
    (void)context; struct timespec now; clock_gettime(CLOCK_MONOTONIC,&now);
    return (uint64_t)now.tv_sec*UINT64_C(1000000000)+(uint64_t)now.tv_nsec;
}
static void neo_linux_wait(void *context, uint32_t milliseconds) {
    neo_linux_services *linux=context;
    neo_linux_selection(linux);
    struct pollfd fds[]={{linux->wake,POLLIN,0},{linux->display ? ConnectionNumber(linux->display) : -1,POLLIN,0}};
    poll(fds,2,milliseconds>10 ? 10 : (int)milliseconds);
    if (fds[0].revents & POLLIN) { uint64_t signal; (void)read(linux->wake,&signal,sizeof(signal)); }
}
static void neo_linux_wake(void *context) { uint64_t signal=1; (void)write(((neo_linux_services *)context)->wake,&signal,sizeof(signal)); }
static neo_status neo_linux_machine(void *context, neo_machine *machine) {
    (void)context; struct timespec resolution; clock_getres(CLOCK_MONOTONIC,&resolution);
    *machine=(neo_machine){(uint32_t)sysconf(_SC_PAGESIZE),(uint32_t)sysconf(_SC_NPROCESSORS_ONLN),64,
        (uint64_t)resolution.tv_sec*UINT64_C(1000000000)+(uint64_t)resolution.tv_nsec}; return NEO_OK;
}
static neo_status neo_linux_entropy(void *context, void *bytes, size_t length) {
    (void)context; size_t used=0;
    while (used<length) {
        ssize_t count=getrandom((unsigned char *)bytes+used,length-used,0);
        if (count<0) { if (errno==EINTR) { continue; } return NEO_IO_ERROR; }
        used+=(size_t)count;
    }
    return NEO_OK;
}
static neo_status neo_linux_surface(void *context, uint32_t width, uint32_t height, void **output) {
    neo_linux_services *linux=context;
    if (!linux->display) { return NEO_UNSUPPORTED; }
    neo_linux_resource *resource=calloc(1,sizeof(*resource));
    if (!resource) { return NEO_EXHAUSTED; }
    resource->owner=linux; resource->kind=NEO_RESOURCE_SURFACE; resource->fd=-1;
    int attributes[]={GLX_RGBA,GLX_DOUBLEBUFFER,GLX_RED_SIZE,8,GLX_GREEN_SIZE,8,GLX_BLUE_SIZE,8,GLX_ALPHA_SIZE,8,None};
    resource->visual=linux->renderer==NEO_RENDER_SOFTWARE ? NULL : glXChooseVisual(linux->display,DefaultScreen(linux->display),attributes);
    if (resource->visual) { resource->gl=glXCreateContext(linux->display,resource->visual,NULL,True); }
    if (!resource->gl && linux->renderer==NEO_RENDER_OPENGL) { if (resource->visual) { XFree(resource->visual); } free(resource); return NEO_UNSUPPORTED; }
    Visual *visual=resource->visual ? resource->visual->visual : DefaultVisual(linux->display,DefaultScreen(linux->display));
    int depth=resource->visual ? resource->visual->depth : DefaultDepth(linux->display,DefaultScreen(linux->display));
    resource->colormap=XCreateColormap(linux->display,DefaultRootWindow(linux->display),visual,AllocNone);
    XSetWindowAttributes options={0}; options.colormap=resource->colormap;
    options.event_mask=ExposureMask|StructureNotifyMask|KeyPressMask|KeyReleaseMask|PointerMotionMask|ButtonPressMask|ButtonReleaseMask|FocusChangeMask;
    resource->window=XCreateWindow(linux->display,DefaultRootWindow(linux->display),0,0,width,height,0,depth,InputOutput,
        visual,CWColormap|CWEventMask,&options);
    XStoreName(linux->display,resource->window,"neo"); XSetWMProtocols(linux->display,resource->window,&linux->close,1);
    if (linux->input) {
        resource->input=XCreateIC(linux->input,XNInputStyle,XIMPreeditNothing|XIMStatusNothing,
            XNClientWindow,resource->window,XNFocusWindow,resource->window,NULL);
    }
    if (resource->gl && glXMakeCurrent(linux->display,resource->window,resource->gl)) { glGenTextures(1,&resource->texture); }
    if (linux->visible) { XMapWindow(linux->display,resource->window); }
    XFlush(linux->display); *output=resource; return NEO_OK;
}
static void neo_linux_bind(void *context, void *native, neo_handle identity) {
    neo_linux_resource *resource=native; resource->identity=identity;
    if (resource->kind==NEO_RESOURCE_SURFACE) { ((neo_linux_services *)context)->surfaces[identity.slot]=resource; }
}
static neo_status neo_linux_present(void *context, void *native, const neo_frame *frame) {
    neo_linux_services *linux=context; neo_linux_resource *resource=native;
    if (resource->gl) {
        if (!glXMakeCurrent(linux->display,resource->window,resource->gl)) { return NEO_DEVICE_LOST; }
        glViewport(0,0,(GLsizei)frame->width,(GLsizei)frame->height);
        glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D,resource->texture); glPixelStorei(GL_UNPACK_ALIGNMENT,1);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,(GLsizei)frame->width,(GLsizei)frame->height,0,GL_RGBA,GL_UNSIGNED_BYTE,frame->pixels);
        glMatrixMode(GL_PROJECTION); glLoadIdentity(); glMatrixMode(GL_MODELVIEW); glLoadIdentity();
        glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
        glBegin(GL_QUADS);
        glTexCoord2f(0,1); glVertex2f(-1,-1); glTexCoord2f(1,1); glVertex2f(1,-1);
        glTexCoord2f(1,0); glVertex2f(1,1); glTexCoord2f(0,0); glVertex2f(-1,1); glEnd();
        if (getenv("NEO_TEST_RENDER_READBACK")) {
            unsigned char sample[4]; glReadPixels(0,(GLint)frame->height-1,1,1,GL_RGBA,GL_UNSIGNED_BYTE,sample);
            if (memcmp(sample,frame->pixels,3)) { return NEO_DEVICE_LOST; }
        }
        glFlush(); glXSwapBuffers(linux->display,resource->window);
        return glGetError()==GL_NO_ERROR ? NEO_OK : NEO_DEVICE_LOST;
    }
    int screen=DefaultScreen(linux->display);
    XImage *image=XCreateImage(linux->display,DefaultVisual(linux->display,screen),(unsigned)DefaultDepth(linux->display,screen),
        ZPixmap,0,NULL,frame->width,frame->height,32,0);
    if (!image) { return NEO_EXHAUSTED; }
    image->data=calloc((size_t)image->bytes_per_line,frame->height);
    if (!image->data) { XDestroyImage(image); return NEO_EXHAUSTED; }
    for (uint32_t y=0; y<frame->height; ++y) {
        for (uint32_t x=0; x<frame->width; ++x) {
            const unsigned char *pixel=frame->pixels+(size_t)y*frame->stride+x*4u;
            XPutPixel(image,(int)x,(int)y,((unsigned long)pixel[0]<<16)|((unsigned long)pixel[1]<<8)|pixel[2]);
        }
    }
    GC gc=XCreateGC(linux->display,resource->window,0,NULL);
    XPutImage(linux->display,resource->window,gc,image,0,0,0,0,frame->width,frame->height);
    XFreeGC(linux->display,gc); XDestroyImage(image); XFlush(linux->display); return NEO_OK;
}
static neo_status neo_linux_input(void *context, neo_input *input) {
    neo_linux_services *linux=context;
    neo_linux_selection(linux);
    if (linux->count) { *input=linux->queued[0]; --linux->count;
        memmove(linux->queued,linux->queued+1,linux->count*sizeof(*input)); return NEO_OK; }
    if (!linux->display || !XPending(linux->display)) { return NEO_PENDING; }
    XEvent event; XNextEvent(linux->display,&event);
    neo_linux_resource *surface=NULL;
    for (size_t i=0; i<NEO_SERVICE_RESOURCES; ++i) {
        if (linux->surfaces[i] && linux->surfaces[i]->window==event.xany.window) { surface=linux->surfaces[i]; break; }
    }
    if (!surface) { return NEO_PENDING; }
    *input=(neo_input){.surface=surface->identity,.time_ns=neo_linux_clock(linux)};
    switch (event.type) {
    case DestroyNotify: input->kind=NEO_INPUT_CLOSE; break;
    case ClientMessage: if ((Atom)event.xclient.data.l[0]!=linux->close) { return NEO_PENDING; } input->kind=NEO_INPUT_CLOSE; break;
    case ConfigureNotify:
        input->kind=NEO_INPUT_RESIZE; input->width=(uint32_t)event.xconfigure.width;
        input->height=(uint32_t)event.xconfigure.height; input->scale=1; break;
    case MotionNotify: input->kind=NEO_INPUT_POINTER; input->x=event.xmotion.x; input->y=event.xmotion.y; break;
    case ButtonPress: case ButtonRelease:
        input->x=event.xbutton.x; input->y=event.xbutton.y;
        if (event.xbutton.button>=4 && event.xbutton.button<=7) {
            if (event.type==ButtonRelease) { return NEO_PENDING; }
            input->kind=NEO_INPUT_WHEEL;
            if (event.xbutton.button<=5) { input->dy=event.xbutton.button==4 ? 1 : -1; }
            else { input->dx=event.xbutton.button==7 ? 1 : -1; }
        } else { input->kind=NEO_INPUT_BUTTON; input->code=(int32_t)event.xbutton.button; input->pressed=event.type==ButtonPress; }
        break;
    case FocusIn: case FocusOut:
        input->kind=NEO_INPUT_FOCUS; input->code=event.type==FocusIn;
        if (surface->input) { if (input->code) { XSetICFocus(surface->input); } else { XUnsetICFocus(surface->input); } } break;
    case KeyPress: case KeyRelease: {
        input->kind=NEO_INPUT_KEY; input->code=(int32_t)event.xkey.keycode-8; input->pressed=event.type==KeyPress;
        const int32_t extended[][2]={{96,0x11c},{97,0x11d},{98,0x135},{100,0x138},{102,0x147},{103,0x148},
            {104,0x149},{105,0x14b},{106,0x14d},{107,0x14f},{108,0x150},{109,0x151},{110,0x152},{111,0x153},{125,0x15b},{126,0x15c}};
        for (size_t i=0; i<sizeof(extended)/sizeof(extended[0]); ++i) { if (input->code==extended[i][0]) { input->code=extended[i][1]; break; } }
        input->modifiers=((event.xkey.state & ShiftMask) ? 1 : 0)|((event.xkey.state & ControlMask) ? 2 : 0)|
            ((event.xkey.state & Mod1Mask) ? 4 : 0)|((event.xkey.state & Mod4Mask) ? 8 : 0);
        if (event.type==KeyPress && surface->input) {
            KeySym key; Status status; int count=Xutf8LookupString(surface->input,&event.xkey,input->text,31,&key,&status);
            if (count>0 && count<=31 && status!=XBufferOverflow) {
                neo_input text=*input; text.kind=NEO_INPUT_TEXT; text.text[count]=0;
                if (linux->count<NEO_SERVICE_EVENTS) { linux->queued[linux->count++]=text; }
                memset(input->text,0,sizeof(input->text));
            }
        }
        break;
    }
    default: return NEO_PENDING;
    }
    return NEO_OK;
}
static void neo_linux_close(void *context, void *native, neo_resource_kind kind) {
    neo_linux_services *linux=context; neo_linux_resource *resource=native;
    if (kind==NEO_RESOURCE_SURFACE) {
        linux->surfaces[resource->identity.slot]=NULL;
        if (resource->input) { XDestroyIC(resource->input); }
        if (resource->gl) { glXMakeCurrent(linux->display,None,NULL); glXDestroyContext(linux->display,resource->gl); }
        XDestroyWindow(linux->display,resource->window); XFreeColormap(linux->display,resource->colormap);
        if (resource->visual) { XFree(resource->visual); }
    } else if (kind==NEO_RESOURCE_AUDIO) { snd_pcm_drop(resource->audio); snd_pcm_close(resource->audio); }
    else if (kind==NEO_RESOURCE_CLIPBOARD) {
        linux->clipboard=NULL; XDestroyWindow(linux->display,resource->window); free(resource->clipboard_data);
    }
    else {
        if (resource->restore_flags) { fcntl(resource->fd,F_SETFL,resource->original_flags); }
        close(resource->fd);
    }
    free(resource);
}
static neo_status neo_linux_step(void *context, void *native, neo_io_kind operation, void **ticket,
                                    unsigned char *bytes, size_t capacity, size_t *length, void **accepted) {
    (void)context; (void)ticket; neo_linux_resource *resource=native;
    if (resource->kind==NEO_RESOURCE_CLIPBOARD) {
        neo_linux_services *linux=resource->owner; neo_linux_selection(linux);
        if (operation==NEO_IO_WRITE) {
            if (memchr(bytes,0,capacity)) { return NEO_INVALID; }
            unsigned char *copy=malloc(capacity ? capacity : 1); if (!copy) { return NEO_EXHAUSTED; }
            if (capacity) { memcpy(copy,bytes,capacity); } free(resource->clipboard_data); resource->clipboard_data=copy;
            resource->clipboard_length=capacity; XSetSelectionOwner(linux->display,linux->clipboard_atom,resource->window,CurrentTime);
            XFlush(linux->display); *length=capacity; return NEO_OK;
        }
        if (XGetSelectionOwner(linux->display,linux->clipboard_atom)==resource->window) {
            *length=resource->clipboard_length;
            if (*length>capacity) { return NEO_TRUNCATED; }
            if (*length) { memcpy(bytes,resource->clipboard_data,*length); } return NEO_OK;
        }
        neo_linux_clipboard_job *job=*ticket;
        if (!job) {
            if (XGetSelectionOwner(linux->display,linux->clipboard_atom)==None) { return NEO_EOF; }
            job=calloc(1,sizeof(*job)); if (!job) { return NEO_EXHAUSTED; }
            job->kind=1; job->resource=resource; job->window=XCreateSimpleWindow(linux->display,DefaultRootWindow(linux->display),0,0,1,1,0,0,0);
            resource->clipboard_request=job; *ticket=job;
            XConvertSelection(linux->display,linux->clipboard_atom,linux->utf8_atom,linux->transfer_atom,job->window,CurrentTime);
            XFlush(linux->display); return NEO_PENDING;
        }
        if (!job->done) { return NEO_PENDING; }
        *length=job->length;
        if (*length>capacity || job->status==NEO_TRUNCATED) { return NEO_TRUNCATED; }
        if (*length) { memcpy(bytes,job->data,*length); } return job->status;
    }
    if (resource->kind==NEO_RESOURCE_AUDIO) {
        snd_pcm_sframes_t frames=operation==NEO_IO_READ ? snd_pcm_readi(resource->audio,bytes,capacity/(resource->audio_format.channels*2u)) :
            snd_pcm_writei(resource->audio,bytes,capacity/(resource->audio_format.channels*2u));
        if (frames==-EAGAIN) { return NEO_PENDING; }
        if (frames==-EPIPE) { ++resource->audio_starvations; return snd_pcm_prepare(resource->audio)<0 ? NEO_DEVICE_LOST : NEO_PENDING; }
        if (frames<0) { return NEO_DEVICE_LOST; }
        *length=(size_t)frames*resource->audio_format.channels*2u; resource->audio_frames+=(uint64_t)frames; return NEO_OK;
    }
    if (resource->connecting) {
        struct pollfd ready={resource->fd,POLLOUT,0}; int result=poll(&ready,1,0);
        if (!result) { return NEO_PENDING; } if (result<0) { return NEO_IO_ERROR; }
        int error=0; socklen_t size=sizeof(error);
        if (getsockopt(resource->fd,SOL_SOCKET,SO_ERROR,&error,&size) || error) { return NEO_IO_ERROR; }
        resource->connecting=false;
    }
    if (operation==NEO_IO_ACCEPT) {
        int fd=accept4(resource->fd,NULL,NULL,SOCK_NONBLOCK|SOCK_CLOEXEC);
        if (fd<0) { return errno==EAGAIN || errno==EINTR ? NEO_PENDING : NEO_IO_ERROR; }
        neo_linux_resource *child=calloc(1,sizeof(*child));
        if (!child) { close(fd); return NEO_EXHAUSTED; }
        child->fd=fd; child->kind=NEO_RESOURCE_CONNECTION; child->owner=resource->owner; *accepted=child; return NEO_OK;
    }
    ssize_t count;
    if (resource->kind==NEO_RESOURCE_STREAM) {
        count=operation==NEO_IO_READ ? read(resource->fd,bytes,capacity) : write(resource->fd,bytes,capacity);
    } else if (operation==NEO_IO_READ && resource->kind==NEO_RESOURCE_DATAGRAM) {
        count=recv(resource->fd,bytes,capacity,MSG_TRUNC);
        if (count>=0 && (size_t)count>capacity) { return NEO_TRUNCATED; }
    } else { count=operation==NEO_IO_READ ? recv(resource->fd,bytes,capacity,0) : send(resource->fd,bytes,capacity,MSG_NOSIGNAL); }
    if (count<0) { return errno==EAGAIN || errno==EINTR ? NEO_PENDING : NEO_IO_ERROR; }
    *length=(size_t)count;
    return operation==NEO_IO_READ && !count && capacity && resource->kind!=NEO_RESOURCE_DATAGRAM ? NEO_EOF : NEO_OK;
}
static neo_status neo_linux_endpoint(void *context, neo_resource_kind kind, bool listening, uint16_t *port, void **output) {
    int fd=socket(AF_INET,(kind==NEO_RESOURCE_DATAGRAM ? SOCK_DGRAM : SOCK_STREAM)|SOCK_NONBLOCK|SOCK_CLOEXEC,0);
    if (fd<0) { return NEO_IO_ERROR; }
    struct sockaddr_in address={.sin_family=AF_INET,.sin_port=htons(*port),.sin_addr={.s_addr=htonl(INADDR_LOOPBACK)}};
    bool connecting=false;
    if (listening) {
        if (bind(fd,(struct sockaddr *)&address,sizeof(address)) || (kind==NEO_RESOURCE_LISTENER && listen(fd,4))) { close(fd); return NEO_IO_ERROR; }
        socklen_t size=sizeof(address); if (getsockname(fd,(struct sockaddr *)&address,&size)) { close(fd); return NEO_IO_ERROR; }
        *port=ntohs(address.sin_port);
    } else if (connect(fd,(struct sockaddr *)&address,sizeof(address))) {
        if (errno!=EINPROGRESS) { close(fd); return NEO_IO_ERROR; } connecting=true;
    }
    neo_linux_resource *resource=calloc(1,sizeof(*resource)); if (!resource) { close(fd); return NEO_EXHAUSTED; }
    resource->owner=context; resource->kind=kind; resource->fd=fd; resource->connecting=connecting; *output=resource; return NEO_OK;
}
static neo_status neo_linux_standard(void *context, unsigned number, void **output, bool *terminal, uint32_t *columns, uint32_t *rows) {
    int fd=fcntl((int)number,F_DUPFD_CLOEXEC,3); if (fd<0) { return NEO_UNSUPPORTED; }
    neo_linux_resource *resource=calloc(1,sizeof(*resource)); if (!resource) { close(fd); return NEO_EXHAUSTED; }
    resource->owner=context; resource->kind=NEO_RESOURCE_STREAM; resource->fd=fd; resource->original_flags=fcntl(fd,F_GETFL);
    resource->restore_flags=true; fcntl(fd,F_SETFL,resource->original_flags|O_NONBLOCK);
    *terminal=isatty(fd)!=0; *columns=0; *rows=0; struct winsize size;
    if (!ioctl(fd,TIOCGWINSZ,&size)) { *columns=size.ws_col; *rows=size.ws_row; }
    *output=resource; return NEO_OK;
}
static neo_status neo_linux_root(void *context, const char *path, void **output) {
    int fd=open(path,O_DIRECTORY|O_RDONLY|O_NOFOLLOW|O_CLOEXEC); if (fd<0) { return NEO_IO_ERROR; }
    neo_linux_resource *resource=calloc(1,sizeof(*resource)); if (!resource) { close(fd); return NEO_EXHAUSTED; }
    resource->owner=context; resource->kind=NEO_RESOURCE_ROOT; resource->fd=fd; *output=resource; return NEO_OK;
}
static neo_status neo_linux_lock(neo_linux_resource *root, const char *key, int *lock) {
    char name[80]; snprintf(name,sizeof(name),"%s.lock",key);
    *lock=openat(root->fd,name,O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);
    struct stat info;
    if (*lock<0) { return NEO_IO_ERROR; }
    if (fstat(*lock,&info) || !S_ISREG(info.st_mode) || info.st_nlink!=1) { close(*lock); return NEO_DENIED; }
    if (flock(*lock,LOCK_EX|LOCK_NB)) { close(*lock); return errno==EWOULDBLOCK ? NEO_CONFLICT : NEO_IO_ERROR; }
    return NEO_OK;
}
static neo_status neo_linux_record(neo_linux_resource *root, const char *key, void *bytes, size_t capacity, size_t *length, uint64_t *revision) {
    char name[80]; snprintf(name,sizeof(name),"%s.data",key); int fd=openat(root->fd,name,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    if (fd<0) { return errno==ENOENT ? NEO_EOF : NEO_IO_ERROR; }
    struct stat info; uint64_t header[3]; neo_status status=NEO_IO_ERROR;
    if (fstat(fd,&info) || !S_ISREG(info.st_mode) || info.st_nlink!=1) { status=NEO_DENIED; }
    else if (read(fd,header,sizeof(header))==(ssize_t)sizeof(header) && header[0]==UINT64_C(0x3145524f54534f4e) &&
        (header[2]<=NEO_SERVICE_BYTES || header[2]==UINT64_MAX) &&
        (uint64_t)info.st_size==sizeof(header)+(header[2]==UINT64_MAX ? 0 : header[2])) {
        *revision=header[1]; *length=header[2]==UINT64_MAX ? 0 : (size_t)header[2];
        if (header[2]==UINT64_MAX) { status=NEO_EOF; }
        else if (capacity<*length) { status=NEO_TRUNCATED; }
        else {
            size_t used=0; status=NEO_OK;
            while (used<*length) { ssize_t count=read(fd,(unsigned char *)bytes+used,*length-used);
                if (count<0 && errno==EINTR) { continue; } if (count<=0) { status=NEO_IO_ERROR; break; } used+=(size_t)count; }
        }
    }
    close(fd); return status;
}
static neo_status neo_linux_store_read(void *context, void *native, const char *key, void *bytes, size_t capacity, size_t *length, uint64_t *revision) {
    (void)context; int lock; neo_status status=neo_linux_lock(native,key,&lock); if (status!=NEO_OK) { return status; }
    status=neo_linux_record(native,key,bytes,capacity,length,revision); close(lock); return status;
}
static neo_status neo_linux_commit(void *context, void *native, const char *key, uint64_t expected, const void *bytes,
                                     size_t length, bool deleted, uint64_t *revision) {
    (void)context; neo_linux_resource *root=native; int lock;
    neo_status status=neo_linux_lock(root,key,&lock); if (status!=NEO_OK) { return status; }
    size_t previous_length=0; uint64_t previous=0;
    status=neo_linux_record(root,key,NULL,0,&previous_length,&previous);
    if (status==NEO_EOF || status==NEO_TRUNCATED || status==NEO_OK) {
        if (previous!=expected) { status=NEO_CONFLICT; }
        else if (expected==UINT64_MAX) { status=NEO_EXHAUSTED; }
        else {
            uint64_t random=0; neo_linux_entropy(NULL,&random,sizeof(random)); char name[80], temporary[100];
            snprintf(name,sizeof(name),"%s.data",key); snprintf(temporary,sizeof(temporary),"%s.%llu.tmp",key,(unsigned long long)random);
            int fd=openat(root->fd,temporary,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600);
            status=NEO_IO_ERROR;
            if (fd>=0) {
                uint64_t header[]={UINT64_C(0x3145524f54534f4e),expected+1,deleted ? UINT64_MAX : (uint64_t)length};
                bool written=write(fd,header,sizeof(header))==(ssize_t)sizeof(header); size_t used=0;
                while (written && used<length) { ssize_t count=write(fd,(const unsigned char *)bytes+used,length-used);
                    if (count<0 && errno==EINTR) { continue; } if (count<=0) { written=false; break; } used+=(size_t)count; }
                written=written && fsync(fd)==0; close(fd);
                if (written && !renameat(root->fd,temporary,root->fd,name)) {
                    if (!fsync(root->fd)) { *revision=expected+1; status=NEO_OK; } else { status=NEO_UNCERTAIN; }
                }
                unlinkat(root->fd,temporary,0);
            }
        }
    }
    close(lock); return status;
}
static neo_status neo_linux_replace(void *context, void *root, const char *key, uint64_t expected, const void *bytes, size_t length, uint64_t *revision) {
    return neo_linux_commit(context,root,key,expected,bytes,length,false,revision);
}
static neo_status neo_linux_delete(void *context, void *root, const char *key, uint64_t expected, uint64_t *revision) {
    return neo_linux_commit(context,root,key,expected,NULL,0,true,revision);
}
static neo_status neo_linux_list(void *context, void *native, char *text, size_t capacity) {
    (void)context; neo_linux_resource *root=native;
    int fd=openat(root->fd,".",O_DIRECTORY|O_RDONLY|O_CLOEXEC); if (fd<0) { return NEO_IO_ERROR; }
    DIR *directory=fdopendir(fd); if (!directory) { close(fd); return NEO_IO_ERROR; }
    struct dirent *entry; size_t used=0; neo_status status=NEO_OK;
    while ((entry=readdir(directory))) {
        size_t length=strlen(entry->d_name); if (length<=5 || strcmp(entry->d_name+length-5,".data")) { continue; }
        char key[64]; if (length-5>=sizeof(key)) { continue; }
        memcpy(key,entry->d_name,length-5); key[length-5]=0; size_t bytes=0; uint64_t revision=0;
        neo_status state=neo_linux_store_read(context,root,key,NULL,0,&bytes,&revision);
        if (state==NEO_EOF) { continue; } if (state!=NEO_OK && state!=NEO_TRUNCATED) { status=state; break; }
        if (used+length-5+2>capacity) { status=NEO_TRUNCATED; break; }
        memcpy(text+used,key,length-5); used+=length-5; text[used++]='\n';
    }
    text[used]=0; closedir(directory); return status;
}
typedef struct {
    unsigned kind;
    pthread_t thread; atomic_bool done; neo_io_kind operation; neo_linux_resource *root;
    char key[64]; uint64_t expected, revision; unsigned char *bytes; size_t capacity, length; neo_status status;
} neo_linux_store_job;
static void *neo_linux_store_thread(void *context) {
    neo_linux_store_job *job=context;
    if (job->operation==NEO_IO_STORE_READ) { job->status=neo_linux_store_read(NULL,job->root,job->key,job->bytes,job->capacity,&job->length,&job->revision); }
    else if (job->operation==NEO_IO_STORE_DELETE) { job->status=neo_linux_delete(NULL,job->root,job->key,job->expected,&job->revision); }
    else { job->status=neo_linux_replace(NULL,job->root,job->key,job->expected,job->bytes,job->capacity,&job->revision); job->length=job->status==NEO_OK ? job->capacity : 0; }
    atomic_store(&job->done,true); neo_linux_wake(job->root->owner); return NULL;
}
static neo_status neo_linux_store_step(void *context, void *native, neo_io_kind operation, const char *key, uint64_t expected,
                                         void **pending, unsigned char *bytes, size_t capacity, size_t *length, uint64_t *revision) {
    (void)context; neo_linux_store_job *job=*pending;
    if (!job) {
        job=calloc(1,sizeof(*job)); if (!job) { return NEO_EXHAUSTED; }
        job->root=native; job->operation=operation; strcpy(job->key,key); job->expected=expected; job->bytes=bytes; job->capacity=capacity;
        atomic_init(&job->done,false);
        if (pthread_create(&job->thread,NULL,neo_linux_store_thread,job)) { free(job); return NEO_IO_ERROR; }
        *pending=job; return NEO_PENDING;
    }
    if (!atomic_load(&job->done)) { return NEO_PENDING; }
    *length=job->length; *revision=job->revision; return job->status;
}
static void neo_linux_cancel(void *context, void *native) {
    if (*(unsigned *)native==1) {
        neo_linux_clipboard_job *job=native; neo_linux_services *linux=context;
        job->resource->clipboard_request=NULL; XDestroyWindow(linux->display,job->window);
        if (job->data) { XFree(job->data); } free(job); return;
    }
    neo_linux_store_job *job=native; pthread_join(job->thread,NULL); free(job);
}
static void neo_linux_destroy(void *context) {
    neo_linux_services *linux=context;
    if (linux->input) { XCloseIM(linux->input); } if (linux->display) { XCloseDisplay(linux->display); }
    close(linux->wake); free(linux);
}
static neo_status neo_linux_renderer(void *context, neo_renderer renderer) { ((neo_linux_services *)context)->renderer=renderer; return NEO_OK; }
static neo_status neo_linux_audio(void *context, const neo_audio_format *format, void **output) {
    neo_linux_resource *resource=calloc(1,sizeof(*resource)); if (!resource) { return NEO_EXHAUSTED; }
    const char *device=getenv("NEO_AUDIO_DEVICE"); if (!device) { device="default"; }
    if (snd_pcm_open(&resource->audio,device,format->capture ? SND_PCM_STREAM_CAPTURE : SND_PCM_STREAM_PLAYBACK,SND_PCM_NONBLOCK)<0) {
        free(resource); return NEO_UNSUPPORTED;
    }
    if (snd_pcm_set_params(resource->audio,SND_PCM_FORMAT_S16_LE,SND_PCM_ACCESS_RW_INTERLEAVED,
        format->channels,format->rate,0,50000)<0) { snd_pcm_close(resource->audio); free(resource); return NEO_UNSUPPORTED; }
    resource->owner=context; resource->kind=NEO_RESOURCE_AUDIO; resource->fd=-1; resource->audio_format=*format;
    *output=resource; return NEO_OK;
}
static neo_status neo_linux_audio_stats(void *context, void *native, neo_audio_stats *stats) {
    (void)context; neo_linux_resource *resource=native;
    *stats=(neo_audio_stats){resource->audio_frames,resource->audio_starvations}; return NEO_OK;
}
static neo_status neo_linux_position(void *context, void *native, int32_t x, int32_t y) {
    neo_linux_services *linux=context; neo_linux_resource *resource=native;
    XMoveWindow(linux->display,resource->window,x,y); XFlush(linux->display); return NEO_OK;
}
static neo_status neo_linux_clipboard(void *context, void **output) {
    neo_linux_services *linux=context; if (!linux->display) { return NEO_UNSUPPORTED; }
    if (linux->clipboard) { return NEO_EXHAUSTED; }
    neo_linux_resource *resource=calloc(1,sizeof(*resource)); if (!resource) { return NEO_EXHAUSTED; }
    resource->owner=linux; resource->kind=NEO_RESOURCE_CLIPBOARD; resource->fd=-1;
    resource->window=XCreateSimpleWindow(linux->display,DefaultRootWindow(linux->display),0,0,1,1,0,0,0);
    char selection[64]="CLIPBOARD";
    if (getenv("NEO_TEST_PRIVATE_SELECTION")) { snprintf(selection,sizeof(selection),"NEO_TEST_%ld",(long)getpid()); }
    linux->clipboard_atom=XInternAtom(linux->display,selection,False); linux->utf8_atom=XInternAtom(linux->display,"UTF8_STRING",False);
    linux->targets_atom=XInternAtom(linux->display,"TARGETS",False); linux->transfer_atom=XInternAtom(linux->display,"NEO_TRANSFER",False);
    linux->clipboard=resource; *output=resource; return NEO_OK;
}
static neo_status neo_linux_renderer_name(void *context, void *native, char *text, size_t capacity) {
    neo_linux_services *linux=context; neo_linux_resource *resource=native;
    const char *name="software";
    if (resource->gl) { glXMakeCurrent(linux->display,resource->window,resource->gl);
        const unsigned char *driver=glGetString(GL_RENDERER); if (!driver) { return NEO_DEVICE_LOST; }
        int count=snprintf(text,capacity,"opengl: %s",(const char *)driver); return count>=0 && (size_t)count<capacity ? NEO_OK : NEO_TRUNCATED; }
    if (strlen(name)+1>capacity) { return NEO_TRUNCATED; } strcpy(text,name); return NEO_OK;
}
neo_status neo_platform_services(bool visible, neo_services **output) {
    *output=NULL; neo_linux_services *linux=calloc(1,sizeof(*linux)); if (!linux) { return NEO_EXHAUSTED; }
    linux->wake=eventfd(0,EFD_NONBLOCK|EFD_CLOEXEC); if (linux->wake<0) { free(linux); return NEO_IO_ERROR; }
    linux->visible=visible; pthread_once(&neo_x11_once,neo_x11_initialize); setlocale(LC_CTYPE,""); linux->display=XOpenDisplay(NULL);
    if (linux->display) { linux->close=XInternAtom(linux->display,"WM_DELETE_WINDOW",False); XSetLocaleModifiers(""); linux->input=XOpenIM(linux->display,NULL,NULL,NULL); }
    neo_service_backend backend={.clock_ns=neo_linux_clock,.wait=neo_linux_wait,.wake=neo_linux_wake,.machine=neo_linux_machine,
        .entropy=neo_linux_entropy,.close=neo_linux_close,.bind=neo_linux_bind,.step=neo_linux_step,.cancel=neo_linux_cancel,
        .surface_open=neo_linux_surface,.present=neo_linux_present,.input=neo_linux_input,.root_open=neo_linux_root,
        .store_read=neo_linux_store_read,.store_replace=neo_linux_replace,.store_delete=neo_linux_delete,.store_list=neo_linux_list,
        .store_step=neo_linux_store_step,.endpoint=neo_linux_endpoint,.standard=neo_linux_standard,
        .renderer=neo_linux_renderer,.renderer_name=neo_linux_renderer_name,.audio=neo_linux_audio,
        .audio_stats=neo_linux_audio_stats,.clipboard=neo_linux_clipboard,.position=neo_linux_position,.destroy=neo_linux_destroy};
    neo_capabilities capabilities={.host_windows=linux->display!=NULL,.input=linux->display!=NULL,.surfaces=linux->display!=NULL,
        .storage=true,.network=true,.entropy=true,.native_isolation=true};
    neo_status status=neo_services_open(&backend,linux,capabilities,output); if (status!=NEO_OK) { neo_linux_destroy(linux); } return status;
}
bool neo_platform_services_selftest(void) {
    neo_services *services=NULL; bool passed=false; char directory[]="/tmp/neo-platform-XXXXXX";
    if (!mkdtemp(directory) || neo_platform_services(false,&services)!=NEO_OK) { return false; }
    neo_handle root, readonly; uint64_t revision=0; char bytes[16]; size_t length=0;
    passed=neo_services_root(services,directory,NEO_RIGHT_READ|NEO_RIGHT_WRITE,&root)==NEO_OK &&
        neo_services_root(services,directory,NEO_RIGHT_READ,&readonly)==NEO_OK &&
        neo_services_store_replace(services,root,"sample",0,"linux",5,&revision)==NEO_OK && revision==1 &&
        neo_services_store_replace(services,root,"sample",0,"wrong",5,&revision)==NEO_CONFLICT &&
        neo_services_store_replace(services,readonly,"sample",1,"wrong",5,&revision)==NEO_DENIED &&
        neo_services_store_read(services,root,"sample",bytes,sizeof(bytes),&length,&revision)==NEO_OK && length==5 && !memcmp(bytes,"linux",5) &&
        neo_services_store_delete(services,root,"sample",1,&revision)==NEO_OK && revision==2 &&
        neo_services_store_replace(services,root,"sample",0,"wrong",5,&revision)==NEO_CONFLICT;
    if (passed && getenv("NEO_TEST_PRIVATE_SELECTION") && neo_services_capabilities(services).host_windows) {
        neo_services *peer=NULL; neo_handle owner_clip, peer_clip; uint64_t id=0; neo_completion completion;
        const char payload[]="neo \xf0\x9f\x98\x80";
        passed=neo_platform_services(false,&peer)==NEO_OK &&
            neo_services_clipboard(services,NEO_RIGHT_READ|NEO_RIGHT_WRITE,&owner_clip)==NEO_OK &&
            neo_services_clipboard(peer,NEO_RIGHT_READ,&peer_clip)==NEO_OK &&
            neo_services_submit(services,owner_clip,NEO_IO_WRITE,payload,sizeof(payload)-1,0,&id)==NEO_OK;
        if (passed) {
            neo_services_poll(services);
            passed=neo_services_next(services,&completion)==NEO_OK && completion.status==NEO_OK;
            if (passed) { neo_services_release(services,completion.id); }
        }
        if (passed) {
            passed=neo_services_submit(peer,peer_clip,NEO_IO_READ,NULL,64,0,&id)==NEO_OK;
            uint64_t deadline=neo_services_clock(peer)+UINT64_C(2000000000); bool received=false;
            while (passed && neo_services_clock(peer)<deadline) {
                neo_services_wait(services,1); neo_services_wait(peer,1);
                if (neo_services_next(peer,&completion)==NEO_OK) {
                    received=true; passed=completion.status==NEO_OK && completion.length==sizeof(payload)-1 &&
                        !memcmp(completion.bytes,payload,sizeof(payload)-1);
                    neo_services_release(peer,completion.id); break;
                }
            }
            passed=passed && received;
        }
        neo_services_close(peer);
        if (passed) { puts("PASS Linux UTF-8 clipboard exchange over a private test selection; user clipboard untouched"); }
    }
    neo_services_close(services);
    char file[128]; snprintf(file,sizeof(file),"%s/sample.data",directory); unlink(file); snprintf(file,sizeof(file),"%s/sample.lock",directory); unlink(file); rmdir(directory);
    if (passed) {
        const unsigned char socket_attempt[]={0xb8,41,0,0,0,0xbf,2,0,0,0,0xbe,1,0,0,0,0x31,0xd2,0x0f,0x05,0xc3};
        int32_t slots[NEO_NATIVE_SLOTS]={7}; int64_t result=13;
        const char *failure=neo_native_execute(socket_attempt,sizeof(socket_attempt),slots,&result);
        passed=failure && !strcmp(failure,"native worker failed") && slots[0]==7 && result==13;
    }
    if (passed) { puts("PASS Linux services: scoped storage, conditional writes, tombstones, and seccomp syscall denial"); }
    return passed;
}
