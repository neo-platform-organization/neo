#include "cli/cli.h"

#ifdef NEO_PLATFORM_WITH_WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdlib.h>

int main(void) {
    int argc = 0;
    wchar_t **wide = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (wide == NULL) { return EXIT_FAILURE; }
    char **argv = calloc((size_t)argc + 1, sizeof(*argv));
    if (argv == NULL) { (void)LocalFree(wide); return EXIT_FAILURE; }
    bool okay = true;
    for (int i = 0; i < argc; ++i) {
        int length = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, NULL, 0, NULL, NULL);
        if (length == 0) { okay = false; break; }
        argv[i] = malloc((size_t)length);
        if (argv[i] == NULL) { okay = false; break; }
        if (WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, argv[i], length, NULL, NULL) == 0) {
            okay = false; break;
        }
    }
    int result = okay ? neo_cli_run(argc, argv) : EXIT_FAILURE;
    for (int i = 0; i < argc; ++i) { free(argv[i]); }
    free(argv);
    (void)LocalFree(wide);
    return result;
}
#else
int main(int argc, char **argv) {
    return neo_cli_run(argc, argv);
}
#endif
