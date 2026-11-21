#include "depth.h"
#include <stdio.h>

int main(int argc, char **argv) {
    const neo_depth_plugin *plugin=neo_depth_provider();
    const char *failure=neo_depth_install(plugin);
    if (failure) { fprintf(stderr,"neo: %s\n",failure); return 1; }
    return plugin->run(argc,argv);
}
