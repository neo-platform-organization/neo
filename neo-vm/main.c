#include "selection.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    const neo_vm_selection *selection=neo_build_selection();
    if (argc==2 && !strcmp(argv[1],"--build-info")) {
        printf("target %s\nincluded %s\n",selection->target,selection->ids); return 0;
    }
    const char *failure=selection->platform ? neo_depth_install(selection->platform) : NULL;
    if (!failure && selection->language) { failure=neo_language_install(selection->language); }
    if (failure) { fprintf(stderr,"neo: %s\n",failure); return 1; }
    if (selection->entry) { return selection->entry(argc,argv); }
    puts("neo bootstrap VM: no execution frontend selected; run make configure to include plugins");
    return argc>1 ? 1 : 0;
}
