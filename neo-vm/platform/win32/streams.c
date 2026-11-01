#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "platform/win32/win32.h"
#include "io/io.h"
#include <stdlib.h>
#include <string.h>

typedef struct neo_win32_stream {
    HANDLE handle;
    bool console;
    char input[4096];
    size_t used;
    size_t at;
    bool line_ready;
} neo_win32_stream;

static neo_status neo_win32_read(void *context, uint8_t *bytes, size_t count, neo_io_result *out) {
    neo_win32_stream *stream = context;
    *out = (neo_io_result){NEO_IO_WOULD_BLOCK, 0};
    if (stream->console) {
        /* Consume key records without blocking the VM. Keep cooked line editing
         * in this adapter rather than changing shared console input modes. */
        for (size_t i = 0; !stream->line_ready && i < 256; ++i) {
            DWORD available = 0, read_count = 0;
            INPUT_RECORD record;
            if (!GetNumberOfConsoleInputEvents(stream->handle, &available)) { return NEO_IO_ERROR; }
            if (available == 0) { break; }
            if (!ReadConsoleInputW(stream->handle, &record, 1, &read_count)) { return NEO_IO_ERROR; }
            if (read_count == 0 || record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown) { continue; }
            WCHAR character = record.Event.KeyEvent.uChar.UnicodeChar;
            if (character == 0) { continue; }
            DWORD ignored = 0, mode = 0;
            bool echo = GetConsoleMode(stream->handle, &mode) && (mode & ENABLE_ECHO_INPUT) != 0;
            HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
            if (character == L'\b') {
                if (stream->used != 0) {
                    do { --stream->used; } while (stream->used != 0 &&
                        ((unsigned char)stream->input[stream->used] & 0xc0u) == 0x80u);
                    if (echo) { (void)WriteConsoleW(output, L"\b \b", 3, &ignored, NULL); }
                }
                continue;
            }
            if (character == L'\r') {
                stream->input[stream->used++] = '\n'; stream->line_ready = true;
                if (echo) { (void)WriteConsoleW(output, L"\r\n", 2, &ignored, NULL); }
            } else {
                char encoded[4];
                int length = WideCharToMultiByte(CP_UTF8, 0, &character, 1, encoded, 4, NULL, NULL);
                if (length > 0 && stream->used + (size_t)length < sizeof(stream->input)) {
                    memcpy(stream->input + stream->used, encoded, (size_t)length);
                    stream->used += (size_t)length;
                    if (echo) { (void)WriteConsoleW(output, &character, 1, &ignored, NULL); }
                }
            }
        }
        if (!stream->line_ready) { return NEO_OK; }
        size_t available = stream->used - stream->at;
        if (count > available) { count = available; }
        memcpy(bytes, stream->input + stream->at, count);
        stream->at += count;
        if (stream->at == stream->used) { stream->at = 0; stream->used = 0; stream->line_ready = false; }
        *out = (neo_io_result){NEO_IO_TRANSFERRED, count};
        return NEO_OK;
    }
    if (GetFileType(stream->handle) == FILE_TYPE_PIPE) {
        DWORD available = 0;
        if (!PeekNamedPipe(stream->handle, NULL, 0, NULL, &available, NULL)) {
            if (GetLastError() == ERROR_BROKEN_PIPE) { out->state = NEO_IO_EOF; return NEO_OK; }
            return NEO_IO_ERROR;
        }
        if (available == 0) { return NEO_OK; }
        if (count > available) { count = available; }
    }
    if (count > 4096) { count = 4096; }
    DWORD transferred = 0;
    if (!ReadFile(stream->handle, bytes, (DWORD)count, &transferred, NULL)) {
        DWORD error = GetLastError();
        if (error == ERROR_BROKEN_PIPE || error == ERROR_HANDLE_EOF) { out->state = NEO_IO_EOF; return NEO_OK; }
        if (error == ERROR_NO_DATA) { return NEO_OK; }
        return NEO_IO_ERROR;
    }
    *out = (neo_io_result){transferred == 0 ? NEO_IO_EOF : NEO_IO_TRANSFERRED, (size_t)transferred};
    return NEO_OK;
}

static neo_status neo_win32_write(void *context, const uint8_t *bytes, size_t count, neo_io_result *out) {
    neo_win32_stream *stream = context;
    *out = (neo_io_result){NEO_IO_WOULD_BLOCK, 0};
    if (count > 4096) { count = 4096; }
    DWORD transferred = 0;
    if (!WriteFile(stream->handle, bytes, (DWORD)count, &transferred, NULL)) {
        if (GetLastError() == ERROR_NO_DATA) { return NEO_OK; }
        return NEO_IO_ERROR;
    }
    if (transferred != 0) { *out = (neo_io_result){NEO_IO_TRANSFERRED, (size_t)transferred}; }
    return NEO_OK;
}

static void neo_win32_stream_destroy(void *context) {
    neo_win32_stream *stream = context;
    (void)CloseHandle(stream->handle);
    free(stream);
}

neo_status neo_win32_open_stream(void *context, neo_vm *vm, const neo_capability *parent,
                                const char *name, neo_standard_stream which,
                                const neo_capability **out) {
    (void)context;
    *out = NULL;
    DWORD identifier = which == NEO_STANDARD_INPUT ? STD_INPUT_HANDLE :
        which == NEO_STANDARD_OUTPUT ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE;
    HANDLE original = GetStdHandle(identifier);
    if (original == NULL || original == INVALID_HANDLE_VALUE) { return NEO_UNAVAILABLE; }
    neo_win32_stream *stream = calloc(1, sizeof(*stream));
    if (stream == NULL) { return NEO_OUT_OF_MEMORY; }
    if (!DuplicateHandle(GetCurrentProcess(), original, GetCurrentProcess(), &stream->handle,
                         0, FALSE, DUPLICATE_SAME_ACCESS)) { free(stream); return NEO_IO_ERROR; }
    DWORD mode = 0;
    stream->console = GetConsoleMode(stream->handle, &mode) != 0;
    neo_stream_backend backend = {.read = which == NEO_STANDARD_INPUT ? neo_win32_read : NULL,
        .write = which == NEO_STANDARD_INPUT ? NULL : neo_win32_write, .destroy = neo_win32_stream_destroy};
    neo_status status = neo_stream_create(vm, parent, name, &backend, stream, out);
    if (status != NEO_OK) { neo_win32_stream_destroy(stream); }
    return status;
}

static void neo_win32_clipboard_destroy(void *context) { (void)DestroyWindow((HWND)context); }

static neo_status neo_win32_clipboard_read(void *context, uint8_t *bytes, size_t capacity, neo_io_result *out) {
    *out=(neo_io_result){0};
    if (!OpenClipboard((HWND)context)) { return NEO_BUSY; }
    HANDLE handle=GetClipboardData(CF_UNICODETEXT);
    if (handle == NULL) { (void)CloseClipboard(); out->state=NEO_IO_EOF; return NEO_OK; }
    const wchar_t *wide=GlobalLock(handle);
    if (wide == NULL) { (void)CloseClipboard(); return NEO_IO_ERROR; }
    size_t capacity_wide=GlobalSize(handle)/sizeof(wchar_t),length=0;
    while (length<capacity_wide && wide[length] != 0) { ++length; }
    neo_status status=length == capacity_wide || length>INT_MAX ? NEO_INVALID : NEO_OK;
    int needed=0;
    if (status == NEO_OK && length != 0) {
        needed=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide,(int)length,NULL,0,NULL,NULL);
        if (needed == 0) { status=NEO_INVALID; }
    }
    if (status == NEO_OK && (size_t)needed>capacity) { status=NEO_LIMIT; }
    if (status == NEO_OK && needed != 0 && WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide,(int)length,(char *)bytes,needed,NULL,NULL) != needed) { status=NEO_IO_ERROR; }
    (void)GlobalUnlock(handle); (void)CloseClipboard();
    if (status == NEO_OK) { out->state=NEO_IO_TRANSFERRED; out->count=(size_t)needed; }
    return status;
}

static neo_status neo_win32_clipboard_write(void *context, const uint8_t *bytes, size_t count, neo_io_result *out) {
    *out=(neo_io_result){0};
    if (count>NEO_IMAGE_MAX_SOURCE_BYTES || memchr(bytes,0,count) != NULL) { return NEO_LIMIT; }
    int needed=count == 0 ? 0 : MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,(const char *)bytes,(int)count,NULL,0);
    if (count != 0 && needed == 0) { return NEO_INVALID; }
    HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,((size_t)needed+1u)*sizeof(wchar_t));
    if (memory == NULL) { return NEO_OUT_OF_MEMORY; }
    wchar_t *wide=GlobalLock(memory);
    if (wide == NULL) { (void)GlobalFree(memory); return NEO_OUT_OF_MEMORY; }
    bool valid=count == 0 || MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,(const char *)bytes,(int)count,wide,needed) == needed;
    wide[needed]=0; (void)GlobalUnlock(memory);
    if (!valid) { (void)GlobalFree(memory); return NEO_INVALID; }
    if (!OpenClipboard((HWND)context)) { (void)GlobalFree(memory); return NEO_BUSY; }
    bool published=EmptyClipboard() && SetClipboardData(CF_UNICODETEXT,memory) != NULL;
    (void)CloseClipboard();
    if (!published) { (void)GlobalFree(memory); return NEO_IO_ERROR; }
    out->state=NEO_IO_TRANSFERRED; out->count=count; return NEO_OK;
}

neo_status neo_win32_open_clipboard(void *context, neo_vm *vm, const neo_capability *parent,
                                    const char *name, const neo_capability **out) {
    (void)context;
    /* EmptyClipboard must assign a real owner before SetClipboardData. A
     * message-only owner also works when the app window is temporarily inactive. */
    HWND owner=CreateWindowExW(0,L"STATIC",L"neo clipboard",0,0,0,0,0,HWND_MESSAGE,NULL,GetModuleHandleW(NULL),NULL);
    if (owner == NULL) { return NEO_IO_ERROR; }
    const neo_stream_backend backend={neo_win32_clipboard_read,neo_win32_clipboard_write,neo_win32_clipboard_destroy};
    neo_status status=neo_stream_create(vm,parent,name,&backend,(void *)owner,out);
    if (status != NEO_OK) { (void)DestroyWindow(owner); }
    return status;
}
