#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include "platform/win32/win32.h"
#include "display/display.h"
#include "display/text.h"
#include "image/image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct neo_win32_window {
    HWND handle;
    neo_window_state state;
    neo_input_event events[64];
    size_t first;
    size_t count;
    uint64_t lost;
    uint64_t pinch_distance;
    neo_text_decoder text_decoder;
    uint8_t *pixels;
    BITMAPINFO bitmap;
} neo_win32_window;

static void neo_win32_input_reset(neo_win32_window *window) {
    window->text_decoder.high = 0;
    window->pinch_distance = 0;
    bool owned = window->state.captured;
    neo_window_input_reset(&window->state);
    if (owned && GetCapture() == window->handle) { (void)ReleaseCapture(); }
}

static void neo_win32_enqueue(neo_win32_window *window, neo_input_event event) {
    event.control=(GetKeyState(VK_CONTROL)&0x8000) != 0;
    event.shift=(GetKeyState(VK_SHIFT)&0x8000) != 0;
    if (window->count == 64 || window->lost != 0) {
        if (window->lost == 0) { neo_win32_input_reset(window); }
        if (window->lost != UINT64_MAX) { ++window->lost; }
        return;
    }
    event.epoch = window->state.input_epoch;
    window->events[(window->first + window->count) % 64] = event;
    ++window->count;
}

static void neo_win32_text(neo_win32_window *window, uint32_t scalar, size_t repeats) {
    if (!window->state.focused || scalar < 32u || scalar == 127u) { return; }
    char text[5];
    if (neo_text_encode(scalar, text) != NEO_OK) { (void)neo_text_encode(65533u, text); }
    neo_input_event event = {.kind=NEO_INPUT_TEXT};
    memcpy(event.text, text, sizeof(text));
    size_t bounded = repeats > 65u ? 65u : repeats;
    for (size_t i = 0; i < bounded; ++i) { neo_win32_enqueue(window, event); }
    size_t remainder = repeats - bounded;
    if (remainder != 0) {
        uint64_t lost = (uint64_t)remainder;
        window->lost = lost > UINT64_MAX - window->lost ? UINT64_MAX : window->lost + lost;
    }
}

static void neo_win32_key(WPARAM code, char *out) {
    if ((code >= 'A' && code <= 'Z') || (code >= '0' && code <= '9')) {
        out[0] = (char)(code >= 'A' ? code + ('a' - 'A') : code); out[1] = '\0'; return;
    }
    const char *name = "unknown";
    switch (code) {
        case VK_RETURN: name = "enter"; break;
        case VK_ESCAPE: name = "escape"; break;
        case VK_TAB: name = "tab"; break;
        case VK_BACK: name = "backspace"; break;
        case VK_DELETE: name = "delete"; break;
        case VK_SPACE: name = "space"; break;
        case VK_F5: name = "f5"; break;
        case VK_F1: name = "f1"; break;
        case VK_F2: name = "f2"; break;
        case VK_F3: name = "f3"; break;
        case VK_LEFT: name = "left"; break;
        case VK_RIGHT: name = "right"; break;
        case VK_UP: name = "up"; break;
        case VK_DOWN: name = "down"; break;
        case VK_SHIFT: name = "shift"; break;
        case VK_CONTROL: name = "control"; break;
        case VK_MENU: name = "alt"; break;
    }
    strcpy(out, name);
}

static bool neo_win32_paint(neo_win32_window *window, HDC dc) {
    RECT bounds;
    if (dc == NULL || !GetClientRect(window->handle, &bounds)) { return false; }
    if (FillRect(dc, &bounds, (HBRUSH)GetStockObject(BLACK_BRUSH)) == 0) { return false; }
    if (window->pixels != NULL) {
        int width = window->bitmap.bmiHeader.biWidth;
        int height = -window->bitmap.bmiHeader.biHeight;
        if (SetDIBitsToDevice(dc, 0, 0, (DWORD)width, (DWORD)height, 0, 0, 0,
            (UINT)height, window->pixels, &window->bitmap, DIB_RGB_COLORS) == 0) { return false; }
    }
    return true;
}

static LRESULT CALLBACK neo_win32_window_proc(HWND handle, UINT message, WPARAM wparam, LPARAM lparam) {
    neo_win32_window *window = (neo_win32_window *)GetWindowLongPtrW(handle, GWLP_USERDATA);
    if (message == WM_NCCREATE) {
        const CREATESTRUCTW *creation = (const CREATESTRUCTW *)lparam;
        window = creation->lpCreateParams;
        window->handle = handle;
        (void)SetWindowLongPtrW(handle, GWLP_USERDATA, (LONG_PTR)window);
    }
    if (window == NULL) { return DefWindowProcW(handle, message, wparam, lparam); }
    neo_input_event input = {0};
    switch (message) {
        case WM_CLOSE:
            if (!window->state.closed) {
                neo_win32_input_reset(window);
                window->state.closed = true; window->state.focused = false;
                neo_win32_enqueue(window, (neo_input_event){.kind = NEO_INPUT_CLOSE});
            }
            (void)ShowWindow(handle, SW_HIDE);
            return 0;
        case WM_SIZE:
            window->state.width = (size_t)LOWORD(lparam);
            window->state.height = (size_t)HIWORD(lparam);
            window->state.redraw = true;
            input = (neo_input_event){.kind = NEO_INPUT_RESIZE,
                .width = (int64_t)window->state.width, .height = (int64_t)window->state.height};
            break;
        case WM_DPICHANGED: {
            UINT dpi = LOWORD(wparam);
            if (neo_window_surface_change(&window->state, dpi, 96, window->state.presentation_lost) == NEO_OK) {
                const RECT *bounds = (const RECT *)lparam;
                (void)SetWindowPos(handle, NULL, bounds->left, bounds->top,
                    bounds->right - bounds->left, bounds->bottom - bounds->top, SWP_NOZORDER | SWP_NOACTIVATE);
            }
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT paint;
            HDC dc = BeginPaint(handle, &paint);
            bool presented = neo_win32_paint(window, dc);
            (void)neo_window_surface_change(&window->state, window->state.scale_numerator,
                window->state.scale_denominator, !presented);
            (void)EndPaint(handle, &paint);
            window->state.redraw = true;
            input.kind = NEO_INPUT_EXPOSE;
            break;
        }
        case WM_CAPTURECHANGED:
            if (window->state.captured) {
                neo_win32_input_reset(window);
                neo_win32_enqueue(window, (neo_input_event){.kind=NEO_INPUT_CAPTURE,.pressed=false});
            }
            return 0;
        case WM_CANCELMODE:
            neo_win32_input_reset(window);
            return 0;
        case WM_SETFOCUS: case WM_KILLFOCUS:
            if (message == WM_KILLFOCUS && (window->state.focused || window->state.captured)) { neo_win32_input_reset(window); }
            window->state.focused = message == WM_SETFOCUS;
            input = (neo_input_event){.kind = NEO_INPUT_FOCUS, .pressed = message == WM_SETFOCUS};
            break;
        case WM_MOUSEMOVE:
            input = (neo_input_event){.kind = NEO_INPUT_POINTER, .x = GET_X_LPARAM(lparam), .y = GET_Y_LPARAM(lparam)};
            break;
        case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_MBUTTONDOWN: case WM_MBUTTONUP:
        case WM_RBUTTONDOWN: case WM_RBUTTONUP:
            input = (neo_input_event){.kind = NEO_INPUT_BUTTON,
                .x = GET_X_LPARAM(lparam), .y = GET_Y_LPARAM(lparam),
                .button = (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP) ? 1 :
                    (message == WM_MBUTTONDOWN || message == WM_MBUTTONUP) ? 2 : 3,
                .pressed = message == WM_LBUTTONDOWN || message == WM_MBUTTONDOWN || message == WM_RBUTTONDOWN};
            break;
        case WM_MOUSEWHEEL: {
            POINT point = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            (void)ScreenToClient(handle, &point);
            input = (neo_input_event){.kind = NEO_INPUT_BUTTON, .x = point.x, .y = point.y,
                .button = GET_WHEEL_DELTA_WPARAM(wparam) > 0 ? 4 : 5, .pressed = true};
            if ((GET_KEYSTATE_WPARAM(wparam) & MK_CONTROL) != 0) {
                input.kind=NEO_INPUT_PINCH;
                input.width=GET_WHEEL_DELTA_WPARAM(wparam) > 0 ? 1200 : 1000;
                input.height=GET_WHEEL_DELTA_WPARAM(wparam) > 0 ? 1000 : 1200;
            }
            break;
        }
        case WM_GESTURE: {
            GESTUREINFO info={.cbSize=sizeof(info)};
            if (!GetGestureInfo((HGESTUREINFO)lparam,&info) || info.dwID != GID_ZOOM) { return DefWindowProcW(handle,message,wparam,lparam); }
            uint64_t distance=info.ullArguments;
            if ((info.dwFlags & GF_BEGIN) == 0 && distance > 0 && distance <= INT64_MAX && window->pinch_distance > 0) {
                POINT point={info.ptsLocation.x,info.ptsLocation.y};
                (void)ScreenToClient(handle,&point);
                input=(neo_input_event){.kind=NEO_INPUT_PINCH,.x=point.x,.y=point.y,.width=(int64_t)distance,.height=(int64_t)window->pinch_distance};
            }
            window->pinch_distance=(info.dwFlags & GF_END) != 0 || distance > INT64_MAX ? 0 : distance;
            (void)CloseGestureInfoHandle((HGESTUREINFO)lparam);
            break;
        }
        case WM_CHAR: {
            if (!window->state.focused) { window->text_decoder.high = 0; return 0; }
            uint32_t scalars[2];
            size_t count = 0, repeats = (size_t)LOWORD(lparam);
            if (repeats == 0) { repeats = 1; }
            if (wparam > UINT16_MAX) { scalars[count++] = 65533u; window->text_decoder.high = 0; }
            else { count = neo_text_utf16(&window->text_decoder, (uint16_t)wparam, scalars); }
            for (size_t i = 0; i < count; ++i) { neo_win32_text(window, scalars[i], repeats); }
            return 0;
        }
        case WM_UNICHAR:
            if (wparam == UNICODE_NOCHAR) { return TRUE; }
            window->text_decoder.high = 0;
            neo_win32_text(window, wparam > UINT32_MAX ? 65533u : (uint32_t)wparam, 1);
            return 0;
        case WM_KEYDOWN: case WM_KEYUP: case WM_SYSKEYDOWN: case WM_SYSKEYUP:
            input = (neo_input_event){.kind = NEO_INPUT_KEY,
                .pressed = message == WM_KEYDOWN || message == WM_SYSKEYDOWN};
            neo_win32_key(wparam, input.key);
            neo_win32_enqueue(window, input);
            /* Preserve native Alt+F4 and other system key handling. */
            return DefWindowProcW(handle, message, wparam, lparam);
        case WM_NCDESTROY:
            if (!window->state.closed) { neo_win32_input_reset(window); }
            window->state.closed = true; window->state.focused = false; window->handle = NULL;
            (void)SetWindowLongPtrW(handle, GWLP_USERDATA, 0);
            return DefWindowProcW(handle, message, wparam, lparam);
        default: return DefWindowProcW(handle, message, wparam, lparam);
    }
    if (input.kind != NEO_INPUT_NONE) { neo_win32_enqueue(window, input); }
    return 0;
}

static neo_status neo_win32_poll(void *context, neo_window_state *out) {
    neo_win32_window *window = context;
    window->state.redraw = false;
    MSG message;
    for (size_t i = 0; i < 256 && PeekMessageW(&message, window->handle, 0, 0, PM_REMOVE); ++i) {
        (void)TranslateMessage(&message);
        (void)DispatchMessageW(&message);
    }
    *out = window->state;
    return NEO_OK;
}

static neo_status neo_win32_next_event(void *context, neo_input_event *out) {
    neo_win32_window *window = context;
    *out = (neo_input_event){0};
    if (window->count != 0) {
        *out = window->events[window->first];
        window->first = (window->first + 1) % 64; --window->count;
    } else if (window->lost != 0) {
        *out = (neo_input_event){.kind = NEO_INPUT_OVERFLOW, .lost = window->lost, .epoch = window->state.input_epoch};
        window->lost = 0;
    }
    return NEO_OK;
}

static neo_status neo_win32_present_pixels(void *context, const uint8_t *pixels, size_t width, size_t height) {
    neo_win32_window *window = context;
    if (window->state.closed || window->handle == NULL) { return NEO_UNAVAILABLE; }
    if (width > 32767 || height > 32767 || width == 0 || height == 0 ||
        width > SIZE_MAX / 4 / height) { return NEO_LIMIT; }
    if ((size_t)window->bitmap.bmiHeader.biWidth != width ||
        (size_t)(-window->bitmap.bmiHeader.biHeight) != height) {
        uint8_t *replacement = malloc(width * height * 4);
        if (replacement == NULL) { return NEO_OUT_OF_MEMORY; }
        free(window->pixels); window->pixels = replacement;
        window->bitmap = (BITMAPINFO){.bmiHeader = {.biSize = sizeof(BITMAPINFOHEADER),
            .biWidth = (LONG)width, .biHeight = -(LONG)height, .biPlanes = 1,
            .biBitCount = 32, .biCompression = BI_RGB}};
    }
    /* Windows DIBs use BGRX; the VM owns portable RGBA pixels. */
    for (size_t i = 0; i < width * height; ++i) {
        window->pixels[i * 4] = pixels[i * 4 + 2];
        window->pixels[i * 4 + 1] = pixels[i * 4 + 1];
        window->pixels[i * 4 + 2] = pixels[i * 4];
        window->pixels[i * 4 + 3] = 0;
    }
    HDC dc = GetDC(window->handle);
    if (dc == NULL) {
        (void)neo_window_surface_change(&window->state, window->state.scale_numerator, window->state.scale_denominator, true);
        return NEO_UNAVAILABLE;
    }
    bool presented = neo_win32_paint(window, dc);
    (void)ReleaseDC(window->handle, dc);
    neo_status changed = neo_window_surface_change(&window->state, window->state.scale_numerator, window->state.scale_denominator, !presented);
    if (changed != NEO_OK) { return changed; }
    return presented ? NEO_OK : NEO_UNAVAILABLE;
}

static neo_status neo_win32_present(void *context, const uint8_t *pixels, size_t width, size_t height) {
    neo_win32_window *window = context;
    DPI_AWARENESS_CONTEXT previous = window->handle == NULL ? NULL :
        SetThreadDpiAwarenessContext(GetWindowDpiAwarenessContext(window->handle));
    neo_status status = neo_win32_present_pixels(context,pixels,width,height);
    if (previous != NULL) { (void)SetThreadDpiAwarenessContext(previous); }
    return status;
}

static void neo_win32_window_destroy(void *context) {
    neo_win32_window *window = context;
    if (window->handle != NULL) { (void)DestroyWindow(window->handle); }
    free(window->pixels); free(window);
}

static neo_status neo_win32_capture(void *context, bool enabled, neo_window_state *out) {
    neo_win32_window *window = context;
    if (window->handle == NULL || window->state.closed || (enabled && !window->state.focused)) { return NEO_UNAVAILABLE; }
    if (enabled) {
        (void)SetCapture(window->handle);
        if (GetCapture() != window->handle) { return NEO_UNAVAILABLE; }
        window->state.captured = true;
    } else if (window->state.captured) { neo_win32_input_reset(window); }
    *out = window->state;
    return NEO_OK;
}

static neo_status neo_win32_create_window(void *context, neo_vm *vm, const neo_capability *parent,
                                const char *name, const char *title, size_t width,
                                size_t height, const neo_capability **out, bool visible, HWND *out_handle) {
    (void)context;
    if (out_handle != NULL) { *out_handle = NULL; }
    *out = NULL;
    if (width > 32767 || height > 32767) { return NEO_LIMIT; }
    HINSTANCE instance = GetModuleHandleW(NULL);
    const wchar_t *class_name = L"neo_pixel_window";
    WNDCLASSW definition = {.lpfnWndProc = neo_win32_window_proc, .hInstance = instance,
        .hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512)), .lpszClassName = class_name};
    if (!RegisterClassW(&definition) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) { return NEO_IO_ERROR; }
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, title, -1, NULL, 0);
    if (length == 0) { return NEO_INVALID; }
    wchar_t *wide_title = malloc((size_t)length * sizeof(*wide_title));
    neo_win32_window *window = calloc(1, sizeof(*window));
    if (wide_title == NULL || window == NULL) { free(wide_title); free(window); return NEO_OUT_OF_MEMORY; }
    (void)MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, title, -1, wide_title, length);
    window->state = (neo_window_state){.width = width, .height = height, .redraw = true,.scale_numerator=1,.scale_denominator=1};
    /* Scope awareness to this window creation; do not change process-wide policy. */
    DPI_AWARENESS_CONTEXT previous = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    RECT bounds = {0, 0, (LONG)width, (LONG)height};
    (void)AdjustWindowRect(&bounds, WS_OVERLAPPEDWINDOW, FALSE);
    HWND handle = CreateWindowExW(0, class_name, wide_title, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left, bounds.bottom - bounds.top,
        NULL, NULL, instance, window);
    if (previous != NULL) { (void)SetThreadDpiAwarenessContext(previous); }
    free(wide_title);
    if (handle == NULL) { neo_win32_window_destroy(window); return NEO_UNAVAILABLE; }
    UINT dpi = GetDpiForWindow(handle);
    if (dpi != 0) { (void)neo_window_surface_change(&window->state, dpi, 96, false); }
    const neo_window_backend backend = {neo_win32_present, neo_win32_poll,
        neo_win32_window_destroy, neo_win32_next_event, neo_win32_capture};
    neo_status status = neo_window_create(vm, parent, name, &backend, window, out);
    if (status != NEO_OK) { neo_win32_window_destroy(window); return status; }
    if (visible) { (void)ShowWindow(handle, SW_SHOW); }
    if (out_handle != NULL) { *out_handle = handle; }
    return NEO_OK;
}

neo_status neo_win32_open_window(void *context, neo_vm *vm, const neo_capability *parent,
                                const char *name, const char *title, size_t width,
                                size_t height, const neo_capability **out) {
    return neo_win32_create_window(context, vm, parent, name, title, width, height, out, true, NULL);
}

static bool neo_win32_test_event(neo_vm *vm, const neo_capability *window, neo_input_kind kind,
                                  const char *text) {
    neo_input_event event = {0};
    bool okay = neo_window_next_event(vm, window, &event) == NEO_OK && event.kind == kind &&
        (text == NULL || strcmp(event.text, text) == 0);
    if (!okay) { fprintf(stderr, "FAIL Win32 event: expected %s, got %s\n", neo_input_kind_name(kind), neo_input_kind_name(event.kind)); }
    return okay;
}

static bool neo_win32_test_drain(neo_vm *vm, const neo_capability *window) {
    for (size_t i = 0; i < 256; ++i) {
        neo_input_event event = {0};
        if (neo_window_next_event(vm, window, &event) != NEO_OK) { return false; }
        if (event.kind == NEO_INPUT_NONE) { return true; }
    }
    return false;
}

static bool neo_win32_test_pixels(neo_vm *vm, const neo_capability *root,
                                   const neo_capability *window, HWND handle) {
    const neo_capability *buffer = NULL;
    const uint8_t rgba[] = {255, 0, 0, 255, 0, 128, 255, 255};
    bool okay = neo_buffer_create(vm, root, "pixels", 2, 1, &buffer) == NEO_OK &&
        neo_buffer_write(vm, buffer, 0, rgba, sizeof(rgba)) == NEO_OK;
    if (okay) {
        neo_status status = neo_window_present(vm, window, buffer);
        /* Hidden native destinations can be clipped; verify the same paint path
         * against a real GDI memory surface without exposing a visible window. */
        okay = status == NEO_OK || status == NEO_UNAVAILABLE;
    }
    HDC dc = CreateCompatibleDC(NULL); void *pixels = NULL;
    BITMAPINFO bitmap = {.bmiHeader={.biSize=sizeof(BITMAPINFOHEADER),.biWidth=32,.biHeight=-24,
        .biPlanes=1,.biBitCount=32,.biCompression=BI_RGB}};
    HBITMAP image = dc == NULL ? NULL : CreateDIBSection(dc, &bitmap, DIB_RGB_COLORS, &pixels, NULL, 0);
    HGDIOBJ previous = image == NULL ? NULL : SelectObject(dc, image);
    if (okay) { okay = pixels != NULL && previous != NULL && previous != (HGDIOBJ)(LONG_PTR)-1; }
    if (okay) {
        neo_win32_window *native = (neo_win32_window *)GetWindowLongPtrW(handle, GWLP_USERDATA);
        okay = native != NULL && neo_win32_paint(native, dc) && GdiFlush();
        if (okay) {
            const uint8_t *bytes = pixels;
            okay = bytes[0] == 0 && bytes[1] == 0 && bytes[2] == 255 &&
                bytes[4] == 255 && bytes[5] == 128 && bytes[6] == 0 &&
                bytes[8] == 0 && bytes[9] == 0 && bytes[10] == 0;
        }
    }
    if (previous != NULL && previous != (HGDIOBJ)(LONG_PTR)-1) { (void)SelectObject(dc, previous); }
    if (image != NULL) { (void)DeleteObject(image); }
    if (dc != NULL) { (void)DeleteDC(dc); }
    if (!okay) { fprintf(stderr, "FAIL Win32 RGBA/GDI pixel presentation\n"); }
    return okay;
}

bool neo_win32_window_selftest(neo_vm *vm) {
    const neo_capability *root = NULL, *window = NULL;
    neo_diagnostic diagnostic = {0}; HWND handle = NULL;
    bool okay = neo_image_parse(vm, "(image)", &root, &diagnostic) == NEO_OK &&
        neo_win32_create_window(NULL, vm, root, "window", "neo hidden adapter test", 32, 24, &window, false, &handle) == NEO_OK;
    neo_window_state state = {0}; neo_input_event event = {0};
    if (okay) { okay = !IsWindowVisible(handle) && neo_window_poll(vm, window, &state) == NEO_OK && neo_win32_test_drain(vm, window); }
    if (okay) { okay = neo_win32_test_pixels(vm, root, window, handle) &&
        neo_window_poll(vm, window, &state) == NEO_OK && neo_win32_test_drain(vm, window); }
    if (okay) {
        (void)SendMessageW(handle,WM_MOUSEWHEEL,MAKEWPARAM(MK_CONTROL,WHEEL_DELTA),MAKELPARAM(0,0));
        okay=neo_window_poll(vm,window,&state) == NEO_OK && neo_window_next_event(vm,window,&event) == NEO_OK &&
            event.kind == NEO_INPUT_PINCH && event.width == 1200 && event.height == 1000 && neo_win32_test_drain(vm,window);
        if (!okay) { fputs("FAIL Win32 normalized pinch input\n",stderr); }
    }
    if (okay) {
        (void)SendMessageW(handle, WM_SETFOCUS, 0, 0);
        (void)SendMessageW(handle, WM_KEYDOWN, 'A', 1);
        okay = PostMessageW(handle, WM_CHAR, 0x131, 1) != 0 && PostMessageW(handle, WM_CHAR, 0xd83d, 1) != 0 &&
            PostMessageW(handle, WM_CHAR, 0xde42, 1) != 0 && neo_window_poll(vm, window, &state) == NEO_OK && state.focused &&
            neo_win32_test_event(vm, window, NEO_INPUT_FOCUS, NULL) &&
            neo_window_next_event(vm, window, &event) == NEO_OK && event.kind == NEO_INPUT_KEY && event.pressed && strcmp(event.key, "a") == 0 && event.text[0] == '\0' &&
            neo_win32_test_event(vm, window, NEO_INPUT_TEXT, "\xc4\xb1") &&
            neo_win32_test_event(vm, window, NEO_INPUT_TEXT, "\xf0\x9f\x99\x82") && neo_win32_test_event(vm, window, NEO_INPUT_NONE, NULL);
    }
    if (okay) {
        okay = SendMessageW(handle, WM_UNICHAR, UNICODE_NOCHAR, 0) == TRUE;
        (void)SendMessageW(handle, WM_UNICHAR, 0x1f642, 0);
        (void)SendMessageW(handle, WM_CHAR, '\r', 1);
        (void)SendMessageW(handle, WM_CHAR, 'x', 2);
        okay = okay && neo_window_poll(vm, window, &state) == NEO_OK &&
            neo_win32_test_event(vm, window, NEO_INPUT_TEXT, "\xf0\x9f\x99\x82") &&
            neo_win32_test_event(vm, window, NEO_INPUT_TEXT, "x") &&
            neo_win32_test_event(vm, window, NEO_INPUT_TEXT, "x") && neo_win32_test_event(vm, window, NEO_INPUT_NONE, NULL);
    }
    if (okay) {
        okay = neo_window_capture(vm, window, true) == NEO_OK && GetCapture() == handle &&
            neo_window_capture(vm, window, false) == NEO_OK && GetCapture() != handle &&
            neo_win32_test_event(vm, window, NEO_INPUT_RESET, NULL);
    }
    if (okay) {
        okay = neo_window_capture(vm, window, true) == NEO_OK;
        if (okay) { (void)ReleaseCapture(); }
        okay = okay && neo_window_poll(vm, window, &state) == NEO_OK && !state.captured && state.focused &&
            neo_win32_test_event(vm, window, NEO_INPUT_RESET, NULL) && neo_win32_test_event(vm, window, NEO_INPUT_CAPTURE, NULL);
    }
    if (okay) {
        (void)SendMessageW(handle, WM_CHAR, 0xd83d, 1);
        (void)SendMessageW(handle, WM_KEYDOWN, 'B', 1);
        (void)SendMessageW(handle, WM_KILLFOCUS, 0, 0);
        (void)SendMessageW(handle, WM_SETFOCUS, 0, 0);
        (void)SendMessageW(handle, WM_CHAR, 0xde42, 1);
        okay = neo_window_poll(vm, window, &state) == NEO_OK && state.focused && !state.captured &&
            neo_win32_test_event(vm, window, NEO_INPUT_RESET, NULL) &&
            neo_win32_test_event(vm, window, NEO_INPUT_FOCUS, NULL) &&
            neo_win32_test_event(vm, window, NEO_INPUT_FOCUS, NULL) &&
            neo_win32_test_event(vm, window, NEO_INPUT_TEXT, "\xef\xbf\xbd") && neo_win32_test_event(vm, window, NEO_INPUT_NONE, NULL);
    }
    if (okay) {
        (void)SendMessageW(handle, WM_MOUSEMOVE, 0, MAKELPARAM((WORD)-7, (WORD)-9));
        (void)SendMessageW(handle, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(3, 4));
        okay = neo_window_poll(vm, window, &state) == NEO_OK &&
            neo_window_next_event(vm, window, &event) == NEO_OK && event.kind == NEO_INPUT_POINTER && event.x == -7 && event.y == -9 &&
            neo_window_next_event(vm, window, &event) == NEO_OK && event.kind == NEO_INPUT_BUTTON && event.button == 1 && event.pressed && event.x == 3 && event.y == 4;
    }
    if (okay) {
        for (size_t i = 0; i < 65; ++i) { (void)SendMessageW(handle, WM_MOUSEMOVE, 0, 0); }
        okay = neo_window_poll(vm, window, &state) == NEO_OK &&
            neo_win32_test_event(vm, window, NEO_INPUT_RESET, NULL) &&
            neo_window_next_event(vm, window, &event) == NEO_OK && event.kind == NEO_INPUT_OVERFLOW && event.lost == 1 &&
            neo_win32_test_event(vm, window, NEO_INPUT_NONE, NULL);
    }
    if (okay) {
        uint32_t dpi = state.scale_numerator == 3 && state.scale_denominator == 2 ? 192u : 144u;
        RECT bounds = {0, 0, 80, 80};
        (void)SendMessageW(handle, WM_DPICHANGED, MAKEWPARAM((WORD)dpi, (WORD)dpi), (LPARAM)&bounds);
        okay = neo_window_poll(vm, window, &state) == NEO_OK &&
            (uint64_t)state.scale_numerator * 96u == (uint64_t)dpi * state.scale_denominator &&
            neo_win32_test_event(vm, window, NEO_INPUT_SURFACE, NULL) && neo_win32_test_drain(vm, window);
    }
    if (okay) {
        (void)SendMessageW(handle, WM_CLOSE, 0, 0);
        okay = neo_window_poll(vm, window, &state) == NEO_OK && state.closed && !state.focused && !state.captured &&
            neo_win32_test_event(vm, window, NEO_INPUT_RESET, NULL) && neo_win32_test_event(vm, window, NEO_INPUT_CLOSE, NULL);
    }
    if (root != NULL && neo_image_unload(vm, root) != NEO_OK) { okay = false; }
    if (handle != NULL && IsWindow(handle)) { okay = false; }
    if (!okay) { fprintf(stderr, "FAIL Win32 hidden-window adapter selftest\n"); }
    return okay;
}
