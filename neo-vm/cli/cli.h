#ifndef NEO_CLI_H
#define NEO_CLI_H
#include "objects/objects.h"

/** Native CLI layer, not part of libneo.a. Borrows argc argument strings; does
 * not modify or retain them. Returns EXIT_SUCCESS/EXIT_FAILURE, never exits.
 * Uses process standard streams. Each invocation creates and destroys its VM.
 * Calls must be sequential. argv[0] is the executable name. */
int neo_cli_run(int argc, char **argv);

/** Launch parser uses the same argv convention as neo_cli_run. */
int neo_cli_launch(int argc, char **argv);

int neo_cli_selftest(void);
int neo_cli_gui_selftest(void);
int neo_cli_live(int argc, char **argv);
bool neo_live_selftest(neo_vm *vm);
bool neo_live_graph_selftest(neo_vm *vm);
bool neo_live_scheduler_selftest(neo_vm *vm);
bool neo_live_environment_selftest(neo_vm *vm);
bool neo_launch_selftest(neo_vm *vm);
bool neo_live_package_policy_selftest(neo_vm *vm);

#endif
