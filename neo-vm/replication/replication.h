#ifndef NEO_REPLICATION_H
#define NEO_REPLICATION_H
#include "objects/objects.h"

#define NEO_REPLICA_MAX_BYTES (1024u * 1024u)
/** A live, noncopyable channel retaining only the supplied stream authority.
 * ACT controls channel operations; underlying READ/WRITE are checked per transfer.
 * No behavior runs on receipt or import. One queued frame per direction.
 * All calls are sequential. Destroy channel to abandon a partial/failed frame;
 * discard the underlying connection too before attempting resynchronization. */
neo_status neo_replica_channel_create(neo_vm *vm, const neo_capability *stream,
    const neo_capability *parent, const char *name, const neo_capability **out);
/** Freeze a closed ordinary region (READ|COPY) into a version-1 snapshot frame. */
neo_status neo_replica_prepare(neo_vm *vm, const neo_capability *channel,
                              const neo_capability *source);
/** One bounded nonblocking attempt. Complete=true only after the whole frame.
 * A finished send is consumed; another send requires prepare. Receive stays
 * complete until explicit import succeeds. Boundary EOF returns OK/incomplete;
 * ended distinguishes it from pending. EOF inside a frame, malformed framing
 * and transport errors fail the receive direction permanently. */
neo_status neo_replica_send(neo_vm *vm, const neo_capability *channel, bool *complete);
neo_status neo_replica_receive(neo_vm *vm, const neo_capability *channel, bool *complete);
/** ACT plus retained stream READ required. Cached observation only, no I/O.
 * True after receive observed EOF between frames, not inside an unfinished one.
 * Does not imply receipt/acceptance, authentication, or closure of our writes. */
neo_status neo_replica_ended(neo_vm *vm, const neo_capability *channel, bool *out);
/** Explicit inert validation/publication; INSERT on destination. Atomic graph
 * import, no overwrite. Failed parse/import retains frame for inspection/retry
 * or channel deletion. Source object IDs are local and remapped. No peer identity
 * or remote authority is inferred from the frame or its serialized local edges. */
neo_status neo_replica_import(neo_vm *vm, const neo_capability *channel,
    const neo_capability *destination, const char *name, const neo_capability **out);
#endif
