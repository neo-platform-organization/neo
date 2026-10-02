#ifndef NEO_BOOTSTRAP_H
#define NEO_BOOTSTRAP_H
#include "neo.h"

/* Host-only composition before execution: copy template fields and merge its
 * handlers into an actor. No shared behavior, execution, or authority grants.
 * Templates must have no connections. Names must not conflict. On error the
 * actor may contain a partial installation: discard the bootstrap image.
 * Not a language import primitive or a live-update operation. */
neo_status neo_actor_install(neo_vm *vm, const neo_capability *actor,
                             const neo_capability *prototype);
#endif
