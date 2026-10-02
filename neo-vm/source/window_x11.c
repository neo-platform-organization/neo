#include "neo_window_x11.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdlib.h>
#include <string.h>
#include <X11/keysym.h>

/* All Xlib details stay behind the host backend. This copies completed pixels;
 * it knows nothing about shapes, cameras, scene objects, or drawing algorithms. */
typedef struct neo_x11_window {
    Display *display;
    Window window;
    GC gc;
    Atom protocols;
    Atom delete_window;
    XImage *image;
    neo_window_state state;
    neo_input_event events[64];
    size_t first;
    size_t count;
    uint64_t lost;
} neo_x11_window;

static void neo_x11_destroy(void *context) {
    neo_x11_window *window = context;
    if (window->image != NULL) { XDestroyImage(window->image); }
    if (window->gc != NULL) { XFreeGC(window->display, window->gc); }
    if (window->window != None) { XDestroyWindow(window->display, window->window); }
    if (window->display != NULL) { XCloseDisplay(window->display); }
    free(window);
}

static void neo_x11_enqueue(neo_x11_window *window, neo_input_event event) {
    if (window->count == 64 || window->lost != 0) {
        if (window->lost != UINT64_MAX) { ++window->lost; }
        return;
    }
    window->events[(window->first + window->count) % 64] = event;
    ++window->count;
}

static neo_status neo_x11_next_event(void *context, neo_input_event *out) {
    neo_x11_window *window = context;
    *out = (neo_input_event){0};
    if (window->count != 0) {
        *out = window->events[window->first];
        window->first = (window->first + 1) % 64;
        --window->count;
    } else if (window->lost != 0) {
        *out = (neo_input_event){.kind = NEO_INPUT_OVERFLOW, .lost = window->lost};
        window->lost = 0;
    }
    return NEO_OK;
}

static void neo_x11_key(XKeyEvent *event, char *out) {
    KeySym key = XLookupKeysym(event, 0);
    if ((key >= XK_a && key <= XK_z) || (key >= XK_0 && key <= XK_9)) {
        out[0] = (char)key; out[1] = '\0'; return;
    }
    const char *name = "unknown";
    switch (key) {
        case XK_Return: name = "enter"; break;
        case XK_Escape: name = "escape"; break;
        case XK_Tab: name = "tab"; break;
        case XK_BackSpace: name = "backspace"; break;
        case XK_Delete: name = "delete"; break;
        case XK_space: name = "space"; break;
        case XK_Left: name = "left"; break;
        case XK_Right: name = "right"; break;
        case XK_Up: name = "up"; break;
        case XK_Down: name = "down"; break;
        case XK_Shift_L: case XK_Shift_R: name = "shift"; break;
        case XK_Control_L: case XK_Control_R: name = "control"; break;
        case XK_Alt_L: case XK_Alt_R: name = "alt"; break;
    }
    strcpy(out, name);
}

static neo_status neo_x11_poll(void *context, neo_window_state *out) {
    neo_x11_window *window = context;
    window->state.redraw = false;
    /* Bound work even if another client continuously supplies events. */
    for (size_t i = 0; i < 256 && XPending(window->display) != 0; ++i) {
        XEvent event;
        XNextEvent(window->display, &event);
        if (event.xany.window != window->window) { continue; }
        neo_input_event input = {0};
        if (event.type == ClientMessage && event.xclient.message_type == window->protocols &&
            event.xclient.format == 32 && (Atom)event.xclient.data.l[0] == window->delete_window) {
            window->state.closed = true;
            input.kind = NEO_INPUT_CLOSE;
        } else if (event.type == DestroyNotify) {
            window->window = None;
            window->state.closed = true;
            input.kind = NEO_INPUT_CLOSE;
        } else if (event.type == ConfigureNotify) {
            window->state.width = (size_t)event.xconfigure.width;
            window->state.height = (size_t)event.xconfigure.height;
            window->state.redraw = true;
            input = (neo_input_event){.kind = NEO_INPUT_RESIZE,
                .width = event.xconfigure.width, .height = event.xconfigure.height};
        } else if (event.type == Expose) {
            window->state.redraw = true;
            input.kind = NEO_INPUT_EXPOSE;
        } else if (event.type == FocusIn || event.type == FocusOut) {
            input = (neo_input_event){.kind = NEO_INPUT_FOCUS, .pressed = event.type == FocusIn};
        } else if (event.type == MotionNotify) {
            input = (neo_input_event){.kind = NEO_INPUT_POINTER, .x = event.xmotion.x, .y = event.xmotion.y};
        } else if (event.type == ButtonPress || event.type == ButtonRelease) {
            input = (neo_input_event){.kind = NEO_INPUT_BUTTON, .x = event.xbutton.x,
                .y = event.xbutton.y, .button = event.xbutton.button, .pressed = event.type == ButtonPress};
        } else if (event.type == KeyPress || event.type == KeyRelease) {
            input = (neo_input_event){.kind = NEO_INPUT_KEY, .pressed = event.type == KeyPress};
            neo_x11_key(&event.xkey, input.key);
        }
        if (input.kind != NEO_INPUT_NONE) { neo_x11_enqueue(window, input); }
    }
    *out = window->state;
    return NEO_OK;
}

static unsigned long neo_x11_channel(uint8_t value, unsigned long mask) {
    unsigned shift = 0;
    if (mask == 0) { return 0; }
    while ((mask & 1ul) == 0) { mask >>= 1; ++shift; }
    /* TrueColor component masks are contiguous. Round from 8-bit to mask range. */
    return (unsigned long)(((uint64_t)value * mask + 127u) / 255u) << shift;
}

static neo_status neo_x11_present(void *context, const uint8_t *pixels, size_t width, size_t height) {
    neo_x11_window *window = context;
    if (window->state.closed || window->window == None) { return NEO_UNAVAILABLE; }
    /* X11 coordinates/dimensions have 16-bit protocol limits. */
    if (width > 32767u || height > 32767u) { return NEO_LIMIT; }
    if (window->image == NULL || (size_t)window->image->width != width ||
        (size_t)window->image->height != height) {
        int screen = DefaultScreen(window->display);
        XImage *image = XCreateImage(window->display, DefaultVisual(window->display, screen),
            (unsigned)DefaultDepth(window->display, screen), ZPixmap, 0, NULL,
            (unsigned)width, (unsigned)height, 32, 0);
        if (image == NULL) { return NEO_OUT_OF_MEMORY; }
        if (image->bytes_per_line <= 0 || height > SIZE_MAX / (size_t)image->bytes_per_line) {
            XDestroyImage(image);
            return NEO_LIMIT;
        }
        image->data = calloc(height, (size_t)image->bytes_per_line);
        if (image->data == NULL) { XDestroyImage(image); return NEO_OUT_OF_MEMORY; }
        if (window->image != NULL) { XDestroyImage(window->image); }
        window->image = image;
    }
    XImage *image = window->image;
    for (size_t y = 0; y < height; ++y) {
        for (size_t x = 0; x < width; ++x) {
            const uint8_t *pixel = pixels + (y * width + x) * 4u;
            unsigned long native = neo_x11_channel(pixel[0], image->red_mask) |
                neo_x11_channel(pixel[1], image->green_mask) |
                neo_x11_channel(pixel[2], image->blue_mask);
            XPutPixel(image, (int)x, (int)y, native);
        }
    }
    /* Clear exposed margins when the window is larger than the fixed buffer. */
    XClearWindow(window->display, window->window);
    XPutImage(window->display, window->window, window->gc, image, 0, 0, 0, 0,
              (unsigned)width, (unsigned)height);
    XFlush(window->display);
    return NEO_OK;
}

neo_status neo_window_x11_create(neo_vm *vm, const neo_capability *parent, const char *name,
                                 const char *title, size_t width, size_t height,
                                 const neo_capability **out) {
    if (out == NULL) { return NEO_INVALID; }
    *out = NULL;
    if (vm == NULL || parent == NULL || name == NULL || title == NULL || width == 0 || height == 0) {
        return NEO_INVALID;
    }
    if (width > 32767u || height > 32767u) { return NEO_LIMIT; }
    neo_x11_window *window = calloc(1, sizeof(*window));
    if (window == NULL) { return NEO_OUT_OF_MEMORY; }
    window->display = XOpenDisplay(NULL);
    if (window->display == NULL) { free(window); return NEO_UNAVAILABLE; }
    int screen = DefaultScreen(window->display);
    if (DefaultVisual(window->display, screen)->class != TrueColor) {
        neo_x11_destroy(window);
        return NEO_UNSUPPORTED;
    }
    window->state = (neo_window_state){.width = width, .height = height, .redraw = true};
    window->window = XCreateSimpleWindow(window->display, RootWindow(window->display, screen),
        0, 0, (unsigned)width, (unsigned)height, 0, BlackPixel(window->display, screen),
        BlackPixel(window->display, screen));
    window->gc = XCreateGC(window->display, window->window, 0, NULL);
    if (window->gc == NULL) { neo_x11_destroy(window); return NEO_OUT_OF_MEMORY; }
    window->protocols = XInternAtom(window->display, "WM_PROTOCOLS", False);
    window->delete_window = XInternAtom(window->display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(window->display, window->window, &window->delete_window, 1);
    XStoreName(window->display, window->window, title);
    XSelectInput(window->display, window->window, ExposureMask | StructureNotifyMask | FocusChangeMask |
        PointerMotionMask | ButtonPressMask | ButtonReleaseMask | KeyPressMask | KeyReleaseMask);
    const neo_window_backend backend = {neo_x11_present, neo_x11_poll, neo_x11_destroy, neo_x11_next_event};
    neo_status status = neo_window_create(vm, parent, name, &backend, window, out);
    if (status != NEO_OK) { neo_x11_destroy(window); return status; }
    XMapWindow(window->display, window->window);
    XFlush(window->display);
    neo_window_state initial;
    return neo_window_poll(vm, *out, &initial);
}
