#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "platform/win32/win32.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

neo_status neo_platform_native(unsigned services, neo_platform_info *out) {
    if (out == NULL) { return NEO_INVALID; }
    *out = (neo_platform_info){0};
    if ((services & ~((unsigned)NEO_PLATFORM_SERVICES_ALL)) != 0) { return NEO_INVALID; }
#if defined(_WIN32) && (defined(__x86_64__) || defined(_M_X64))
    *out = (neo_platform_info){.environment = NEO_ENVIRONMENT_HOSTED,
        .virtualization = NEO_VIRTUALIZATION_UNKNOWN, .backend = "windows-x86_64",
        .architecture = "x86_64", .host_os = "windows", .services = services};
    return NEO_OK;
#else
    return NEO_UNSUPPORTED;
#endif
}

/* Paths use UTF-8 at the host boundary; native filenames use UTF-16. */
static wchar_t *neo_windows_path(const char *text) {
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    if (count == 0) { return NULL; }
    wchar_t *wide = malloc((size_t)count * sizeof(*wide));
    if (wide != NULL && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, wide, count) == 0) {
        free(wide); return NULL;
    }
    return wide;
}

static neo_status neo_windows_read_source(void *context, const char *location, char *bytes,
                                          size_t capacity, size_t *out_count) {
    (void)context;
    *out_count = 0;
    wchar_t *path = neo_windows_path(location);
    if (path == NULL) { return NEO_IO_ERROR; }
    FILE *file = _wfopen(path, L"rb");
    free(path);
    if (file == NULL) { return NEO_IO_ERROR; }
    size_t count = fread(bytes, 1, capacity, file);
    bool excess = fgetc(file) != EOF;
    bool failed = ferror(file) != 0;
    if (fclose(file) != 0) { failed = true; }
    if (failed) { return NEO_IO_ERROR; }
    if (excess) { return NEO_LIMIT; }
    *out_count = count;
    return NEO_OK;
}

static neo_status neo_windows_write_source(void *context, const char *location, const char *text) {
    (void)context;
    wchar_t *path = neo_windows_path(location);
    if (path == NULL) { return NEO_IO_ERROR; }
    size_t length = wcslen(path);
    if (length > SIZE_MAX / sizeof(wchar_t) - 64) { free(path); return NEO_LIMIT; }
    wchar_t *temporary = malloc((length + 64) * sizeof(*temporary));
    if (temporary == NULL) { free(path); return NEO_OUT_OF_MEMORY; }
    HANDLE file = INVALID_HANDLE_VALUE;
    /* CREATE_NEW prevents collisions and leaves any preexisting file untouched. */
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        (void)swprintf(temporary, length + 64, L"%ls.tmp.%lu.%lu.%u", path,
            (unsigned long)GetCurrentProcessId(), (unsigned long)GetTickCount(), attempt);
        file = CreateFileW(temporary, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file != INVALID_HANDLE_VALUE || GetLastError() != ERROR_FILE_EXISTS) { break; }
    }
    bool okay = file != INVALID_HANDLE_VALUE;
    if (okay) {
        size_t remaining = strlen(text);
        while (okay && remaining != 0) {
            DWORD amount = remaining > MAXDWORD ? MAXDWORD : (DWORD)remaining;
            DWORD written = 0;
            okay = WriteFile(file, text, amount, &written, NULL) != 0 && written != 0;
            text += written; remaining -= written;
        }
        if (!FlushFileBuffers(file)) { okay = false; }
        if (!CloseHandle(file)) { okay = false; }
        if (okay) { okay = MoveFileExW(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0; }
        if (!okay) { (void)DeleteFileW(temporary); }
    }
    free(temporary); free(path);
    return okay ? NEO_OK : NEO_IO_ERROR;
}

static neo_status neo_windows_wait(void *context, uint32_t milliseconds) {
    (void)context;
    /* UINT32_MAX is a finite duration here, whereas Sleep(INFINITE) never returns. */
    if (milliseconds == UINT32_MAX) { Sleep(MAXDWORD - 1u); Sleep(1); }
    else { Sleep(milliseconds); }
    return NEO_OK;
}

neo_status neo_platform_native_services(unsigned requested, neo_platform_services *out) {
    if (out == NULL) { return NEO_INVALID; }
    *out = (neo_platform_services){0};
    neo_platform_services selected = {0};
    neo_status status = neo_platform_native(requested, &selected.info);
    if (status != NEO_OK) { return status; }
    if ((requested & NEO_PLATFORM_INPUT_EVENTS) != 0 && (requested & NEO_PLATFORM_PIXEL_WINDOWS) == 0) {
        return NEO_INVALID;
    }
    if ((requested & NEO_PLATFORM_HOST_FILES) != 0) {
        selected.backend.read_source = neo_windows_read_source;
        selected.backend.write_source = neo_windows_write_source;
    }
    if ((requested & NEO_PLATFORM_BYTE_STREAMS) != 0) { selected.backend.open_stream = neo_win32_open_stream; }
    if ((requested & NEO_PLATFORM_PIXEL_WINDOWS) != 0) {
        selected.backend.open_window = neo_win32_open_window;
        selected.backend.open_clipboard = neo_win32_open_clipboard;
    }
    if ((requested & NEO_PLATFORM_WAIT) != 0) { selected.backend.wait = neo_windows_wait; }
    if ((requested & NEO_PLATFORM_NETWORK) != 0) { selected.backend.network = neo_win32_network_backend(); }
    *out = selected;
    return NEO_OK;
}
