#define _POSIX_C_SOURCE 200809L
#include "neo_window_x11.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(EXIT_FAILURE); } } while (0)
#define OK(x) CHECK((x) == NEO_OK)

static Window neo_find_window(Display *display, Window parent, const char *title, unsigned depth) {
    if (depth > 8) { return None; }
    char *name = NULL;
    if (XFetchName(display, parent, &name) != 0 && name != NULL) {
        bool match = strcmp(name, title) == 0;
        XFree(name);
        if (match) { return parent; }
    }
    Window root, owner, *children = NULL;
    unsigned count = 0;
    Window found = None;
    if (XQueryTree(display, parent, &root, &owner, &children, &count) != 0) {
        for (unsigned i = 0; i < count && found == None; ++i) {
            found = neo_find_window(display, children[i], title, depth + 1);
        }
    }
    if (children != NULL) { XFree(children); }
    return found;
}

int main(void) {
    Display *display = XOpenDisplay(NULL);
    CHECK(display != NULL);
    neo_vm *vm;
    const neo_capability *root, *buffer, *window;
    OK(neo_vm_create(NULL, &vm));
    OK(neo_image_create(vm, "test", &root));
    OK(neo_buffer_create(vm, root, "pixels", 2, 1, &buffer));
    const uint8_t pixels[] = {255, 0, 0, 255, 0, 255, 0, 255};
    OK(neo_buffer_write(vm, buffer, 0, pixels, sizeof(pixels)));
    char title[80];
    (void)snprintf(title, sizeof(title), "neo X11 interface test %ld", (long)getpid());
    OK(neo_window_x11_create(vm, root, "window", title, 64, 64, &window));
    Window native = None;
    const struct timespec delay = {.tv_nsec = 20000000};
    for (unsigned i = 0; i < 100; ++i) {
        native = neo_find_window(display, DefaultRootWindow(display), title, 0);
        XWindowAttributes attributes;
        if (native != None && XGetWindowAttributes(display, native, &attributes) != 0 &&
            attributes.map_state == IsViewable) { break; }
        (void)nanosleep(&delay, NULL);
    }
    CHECK(native != None);
    neo_window_state state;
    OK(neo_window_poll(vm, window, &state));
    CHECK(!state.closed && state.width != 0 && state.height != 0);
    OK(neo_window_present(vm, window, buffer));
    bool colors_match = false;
    for (unsigned i = 0; i < 100 && !colors_match; ++i) {
        OK(neo_window_poll(vm, window, &state));
        OK(neo_window_present(vm, window, buffer));
        (void)nanosleep(&delay, NULL);
        XImage *image = XGetImage(display, native, 0, 0, 2, 1, AllPlanes, ZPixmap);
        CHECK(image != NULL);
        unsigned long red = XGetPixel(image, 0, 0), green = XGetPixel(image, 1, 0);
        colors_match = (red & image->red_mask) == image->red_mask &&
            (red & (image->green_mask | image->blue_mask)) == 0 &&
            (green & image->green_mask) == image->green_mask &&
            (green & (image->red_mask | image->blue_mask)) == 0;
        if (i == 99 && !colors_match) {
            fprintf(stderr, "pixels=%lx,%lx masks=%lx,%lx,%lx\n", red, green,
                    image->red_mask, image->green_mask, image->blue_mask);
        }
        XDestroyImage(image);
    }
    CHECK(colors_match);
    XResizeWindow(display, native, 96, 80);
    XFlush(display);
    for (unsigned i = 0; i < 100; ++i) {
        (void)nanosleep(&delay, NULL);
        OK(neo_window_poll(vm, window, &state));
        if (state.width == 96 && state.height == 80) { break; }
    }
    CHECK(state.width == 96 && state.height == 80);
    OK(neo_window_present(vm, window, buffer));
    XEvent event = {0};
    event.xclient.type = ClientMessage;
    event.xclient.window = native;
    event.xclient.message_type = XInternAtom(display, "WM_PROTOCOLS", False);
    event.xclient.format = 32;
    event.xclient.data.l[0] = (long)XInternAtom(display, "WM_DELETE_WINDOW", False);
    CHECK(XSendEvent(display, native, False, NoEventMask, &event) != 0);
    XFlush(display);
    for (unsigned i = 0; i < 100 && !state.closed; ++i) {
        (void)nanosleep(&delay, NULL);
        OK(neo_window_poll(vm, window, &state));
    }
    CHECK(state.closed);
    CHECK(neo_window_present(vm, window, buffer) == NEO_UNAVAILABLE);
    neo_vm_destroy(vm);
    XCloseDisplay(display);
    puts("PASS: X11 pixel transfer/readback, resize, close event, resource cleanup");
    return EXIT_SUCCESS;
}
