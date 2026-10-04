#ifndef NEO_IO_POSIX_H
#define NEO_IO_POSIX_H
#include "io/io.h"

/** Duplicates a host-granted descriptor; never changes its flags or terminal
 * mode. Closing the stream closes only the duplicate. Nonblocking descriptors
 * give nonblocking transfers. For blocking descriptors readiness is checked
 * first, but a subsequent system call can block: use only in a dedicated host
 * runner, or supply nonblocking descriptors for scheduling-sensitive use.
 * Single-threaded adapter; requires exclusive I/O access to the descriptor. */
neo_status neo_stream_posix_create(neo_vm *vm, const neo_capability *parent, const char *name,
                                   int descriptor, bool readable, bool writable,
                                   const neo_capability **out);
#endif
