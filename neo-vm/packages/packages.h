#ifndef NEO_PACKAGES_H
#define NEO_PACKAGES_H
#include "objects/objects.h"

/** Schema-1 structural validation is inert; dependency compatibility is image policy.
 * Bundles must be closed ordinary graphs.
 * Calls are sequential and host calls occur between behavior turns. */
neo_status neo_package_validate(neo_vm *vm, const neo_capability *bundle);
/** Catalogue records contain an installed bundle and a closed baseline. Staging
 * failures publish no record. Dependency authority derives only from catalogue.
 * Replacement/removal refuse incoming links and retained runtime work.
 * These mechanisms do not select dependency versions, detect metadata dependency
 * cycles or enforce version ordering. Callers must run their image policy first. */
neo_status neo_package_install(neo_vm *vm, const neo_capability *catalogue,
                               const neo_capability *bundle, bool replacing,
                               const neo_capability **out);
neo_status neo_package_remove(neo_vm *vm, const neo_capability *catalogue, const char *id);
neo_status neo_package_resolve(neo_vm *vm, const neo_capability *catalogue,
                               const char *id, const char *export_name,
                               unsigned rights, const neo_capability **out);
/** Export creates an independent inert image, owned by caller until unload. */
neo_status neo_package_export(neo_vm *vm, const neo_capability *catalogue,
                              const char *id, const neo_capability **out);
#endif
