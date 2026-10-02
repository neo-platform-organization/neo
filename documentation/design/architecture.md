# neo implementation architecture

[Documentation index](../README.md)

Quotes always denote text payloads, never identifiers or primitive bindings. Use `(word "if")` for text and `(if ...)` for the primitive. Comments start with `//`; `>` is not a comment marker. The current reader requires bare object names and rejects quoted names. This supersedes earlier quoted-name and quoted-binding syntax.

## Accepted typing and executable syntax update

neo is dynamically typed. Fields have current payload kinds, not fixed declared types; writes can change those kinds. The reader accepts `(n 7)`, `(flag true)`, and `(name "Alice")`, with runtime checks for operation compatibility. `(if ...)` selects a primitive by its name. Known primitive names bind implicitly when no payload is supplied. Explicit literal payloads remain data; `:object` can suppress implicit binding for a data-only object with a reserved name. Named wrappers such as `(body (if ...))` hold nested operations. Legacy tagged input remains supported. See the [runtime reference](../reference/runtime-format.md) for exact behavior; older syntax examples below are historical proposals where they differ.


## Purpose and authority

An initial interpreter now implements a subset of this architecture. Read [build-and-test.md](../development/build-and-test.md) for actual support, [runtime-format.md](../reference/runtime-format.md) for executable syntax, and [code-tour.md](../learning/code-tour.md) for the reading path. Proposed features below must not be mistaken for implemented behavior.

This is the implementation guide for agents working on neo. It records the design agreed in discussion, explains why it has that shape, and identifies provisional engineering choices. It does not claim that neo is already a complete language.

Read this file and AGENTS.md before implementing, then [c-conventions.md](../development/c-conventions.md), [language-design.md](language-design.md), and implementation-plan.md. Later explicit user instructions take precedence. Superseded design notes have been removed: use this guide for duplicate-then-delete move, ETHER message acceptance, and user-controlled failure handling.

Use these labels throughout:

- **Agreed:** a requirement established by the user.
- **Proposed:** an implementation choice agents may use for an initial prototype, but must identify as provisional.
- **Open:** a semantic decision that must not be silently settled by incidental implementation behavior.

Paths in this guide are relative to the repository root. C code belongs in neo-vm/source/, headers in neo-vm/include/, examples in neo-vm/examples/, tests in neo-vm/tests/, and the image in neo/image.neo. Build output stays in build/. Project guides belong in documentation/; README.md, LICENSE, and applicable agent entry points stay at their roots. Keep the whitepaper unchanged unless the user explicitly requests an edit.

## 1. The world and its objects

**Agreed.** The object graph is the world itself. It is not a separate map, a controller, or a class hierarchy. Each object owns its internal behavior and is responsible for its activity or inactivity. Behavior is copied from examples, not inherited through a shared delegation chain.

The main image container, described as the plate, is an ordinary object. Its intended role is to contain the live image. It may have behavior and may be directly mutated when authority permits. Its foundational role does not grant immunity from mutation; modifying its dependencies may break the system.

An object has one immediate container. Other objects may communicate with it without owning it. Contained behavior is itself made of objects. Sharing the host implementation of primitive arithmetic is permissible; sharing mutable language-level behavior between purportedly independent copies is not.

**Reason.** The model is built from concrete individuals, like cells with their own internal machinery. Prototype reuse creates another individual rather than making it depend on the original's future behavior.

**Proposed representation.** Use stable opaque object identifiers, a runtime object table, named contained objects, and separate authorized connections to external targets. Primitive values are language objects even if the host stores their payload as an integer or string. Host implementation types are bookkeeping, not neo classes.

Containment is proposed to be acyclic, with no parent for the image root. Ordinary connections may form cycles. Names are local to containers. Reading an object never automatically evaluates it.

**Open.** Exact slot layout, executable-role tagging, and whether every kind of contained value has observable identity remain language-design choices.

## 2. Copy, containment, and move

**Agreed.** Objects acquire internal contents by duplication. copy() duplicates; move() duplicates into the destination and deletes the original, as its final step or through another object immediately afterward. Move is not an identity-preserving reparent operation.

Copy must reproduce internal objects and behavior, preserving their organization. Connections among copied internal objects must point to their new counterparts. Connections to outside objects are not ownership and cannot gain authority through copying.

**Proposed algorithm.**

1. Validate source access and destination insertion authority.
2. Identify the contained region to copy and validate its structure.
3. Allocate fresh identities for all copied objects and build an old-to-new mapping.
4. Duplicate payloads and behavior; remap internal connections using the mapping.
5. Preserve external target connections with no increased authority.
6. Publish the completed duplicate in the destination's contents and listing.
7. For move, update the source environment's listing and delete the original region.

Prepare a copy before publication so other objects cannot observe a half-constructed region. If duplication fails, do not delete the original. A sequential runtime can perform structural publication and deletion without interleaving another turn; later parallel execution needs synchronization.

**Reason.** A duplicate must have its own machinery, including internal relationships. The explicit duplicate/delete model matches the user's meaning of movement. Safe publication prevents implementation artifacts from appearing as language states.

**Open.** Copying active objects, queued messages, pending deliveries, and activations needs an explicit policy. Proposed v0 restricts copying to quiescent ordinary objects; do not silently drop internal runtime state or duplicate an accepted message as if it were unsent. Failure between destination publication and source deletion must be observable rather than silently hidden.

## 3. Environment knowledge and disappearance

**Agreed.** Objects discover state through interaction. Keeping a target connection does not provide automatic awareness of the target's current state. Try a known target first; if it is unavailable, ask the containing environment or a peer for current information, or perform an explicit recursive search.

Environments maintain listings of their objects. When D consumes food, D or the food reports its disappearance to the plate before the food is deleted. A, B, and C can subsequently query the plate for an updated listing. Listings are environment-relative; the whole image may have a main listing, but ordinary objects do not automatically gain global access.

**Reason.** This preserves local knowledge while providing a place to discover changes. It does not require broadcasting every change to every object.

**Proposed mechanism.** Supply an authorized environment operation that updates the listing before final deletion. Ordinary code can express the report, but the final runtime deletion path must prevent references into freed memory even if that code fails. Distinguish reported availability from target liveness.

Connections to deleted identities report unavailable when used; they do not silently redirect to a move's new copy. The latter is a provisional choice consistent with discovery, not a completed universal naming specification. Use non-reused IDs or generation-checked handles. A tombstone or failed table lookup can implement invalidation.

Recursive searches should carry a request identity, visited targets, and a finite budget. Search results are observations, not reservations; the final operation must handle a target that changed after discovery.

**Open.** Required completeness of listings, forced deletion when reporting fails, and recovery from partial move/report operations require explicit policy.

## 4. Messages and the ETHER

**Agreed.** A creates message X to communicate with B. X contains information such as open. B reads that information and acts through B's own internal behavior. Message contents are not an implementation of B's behavior.

Messages exist in the ETHER. Other objects may attempt to interact with them; the maker specializes which objects may perform which inspections, reads, writes, and acceptance operations. Being reachable does not grant access. Specializing a message cannot create authority its maker does not have.

Before acceptance, permitted writers, including the sender, may modify its contents. Acceptance by the designated receiver is the final delivery boundary and closes the sender's writing window. Authorized receiver operations may follow acceptance. Acceptance is not proof that the requested action succeeded.

**Proposed lifecycle.**

    created -> pending -> accepted -> processed

Cancellation, rejection, failure, and retention should be separate recorded outcomes, not undocumented reuse of these states. Their exact protocols remain provisional.

Acceptance atomically checks recipient identity and authority, fixes the delivered contents against further sender edits, and enqueues the message exactly once. An edit racing acceptance either completes first or fails afterward. Enforce this on nested payload writes and existing aliases, not only on a top-level setter.

**Proposed storage.** Represent ETHER as an explicit image object containing message objects. Receiver queues hold authorized connections to accepted messages; acceptance does not imply moving the message into receiver containment. Use a runtime-managed delivery record for atomic transitions. Keep language-visible state and the delivery record consistent through one mutation path.

The sender's editable payload is separate from routing metadata and permissions. For v0, keep the recipient and delegated authority fixed once delivery is requested. This is a provisional constraint, not a rule inferred from the word message.

**Reason.** An information object must remain editable before acceptance while giving the receiver a well-defined accepted request. ETHER access control prevents public reachability from becoming unrestricted mutation.

**Open.** ETHER scope and discovery authority, policy-editing authority, other writers after acceptance, cancellation, and whether messages may be accepted by multiple recipients. Proposed v0 uses one designated receiver per message and freezes accepted payloads during processing; broader receiver mutation can be added under explicit policy.

## 5. Identity and authority

**Agreed.** Authority is limited. Passing or duplicating authority may preserve or reduce permissions, never increase them. Ordinary messages and their text do not grant the receiver additional powers merely by requesting an action.

**Proposed enforcement.** Use opaque capabilities with target identity, allowed operations, and optional selector restrictions. Separate reading, writing, inspection, copying, moving, sending, acceptance, and delegation. Authenticate the acting object from the execution context, never from a caller-supplied sender-name field. Enforce permissions at the runtime boundary of each protected operation.

Message policy answers who may interact with the message. Target authority answers what the receiver may do to other objects while processing it. These are distinct checks. Revocable policy must be checked on use so old aliases cannot bypass acceptance.

**Reason.** Otherwise object code could forge an identity, rewrite a permission field, or retain a writable alias after acceptance, defeating the agreed model.

The runtime is a trusted enforcement substrate, not a language-visible unrestricted root reference. Host code may implement primitives, but ordinary neo objects must not escape to arbitrary host evaluation or manufacture raw pointers.

**Open.** Exact capability format, revocation mechanisms, and how maker-defined policy is represented as objects. Declarative permission records are a reasonable initial implementation; arbitrary executable access policies add termination and reentrancy concerns.

## 6. Queue ordering and execution

**Agreed.** Use a queue per receiver. Preserve message order from a sender to a receiver. Concurrent arrivals acquire a definite queue order. Identical messages remain separate requests. A receiver processes one message at a time and completes that handling before processing the next.

Objects register independently for scheduled activity; containment does not itself schedule children. Internal ticks advance execution as fast as the work and hardware permit, without artificial pacing. Ticks are not necessarily operator-facing controls and are not literally individual CPU cycles. Changes are visible to subsequent operations immediately, not committed as a frozen next-frame snapshot.

**Proposed v0 scheduler.** Start sequentially. Serialize delivery acceptance through one event loop. At a tick boundary, accept eligible pending deliveries in submission order, then snapshot runnable receivers and explicitly registered objects in stable registration order. Give each runnable receiver at most one message turn, and each registered object its scheduled turn. Coalesce runnable entries so an object is not scheduled twice accidentally. Deliveries submitted during the tick become eligible at the next delivery phase.

An inbox can make a receiver runnable even without continuous tick registration. Each turn runs to completion. An empty runtime may wait for external input; no pacing requirement means no artificial delay during available work, not mandatory busy spinning.

**Reason.** A sequential event loop gives a precise reference behavior and avoids premature data races. Stable ordering makes debugging and tests reproducible. Tick phases are provisional engineering choices, not independently approved language guarantees.

Parallelism is deferred. A later implementation needs synchronized queue insertion and acceptance, object access coordination, and explicit ordering for conflicting writes. A concurrent queue alone does not make mutable object internals safe. Idempotence, such as opening an already-open door, belongs to receiver behavior rather than message deduplication.

## 7. Behavior and activations

**Agreed.** Objects carry their own behavior as internal objects. A receiver interprets message information through that behavior. A behavior definition and an invocation of it are distinct: concurrent or recursive invocations need separate execution state.

**Agreed primitive model.** Primitives are objects, including control-flow, arithmetic, messaging, and lifecycle primitives. Native implementation code supplies their execution mechanism; it is not a separate category of language-level entity. Host functions and opcodes are implementation details. Primitive objects remain subject to the applicable containment, copying, inspection, and authority rules. Copying a primitive object must not widen its authority.

**Proposed evaluator.** Represent bodies as contained operation objects with explicit roles. Start with literals, local bindings, slot access, sequence, conditionals, repetition, arithmetic/comparison, allocation, copy, move, message construction/editing/delivery, and scheduler registration. Protected operations always check authority.

An activation records receiver, body, current operation, local values, input message, result destination, and failure state. Activations should be inspectable language-level objects when suitable authority exists. Host frames may initially implement execution but must not be confused with a finished persistent activation model.

A method body is inert until explicitly evaluated. Parsing or inspecting it never runs it. Do not treat every incoming message as unrestricted source code. A receiver may be an evaluator, but only through its own defined behavior and authority.

**Reason.** This gives code-as-data an executable meaning without treating arbitrary data as automatically active. Separating invocation state preserves recursion and makes later introspection possible.

**Open.** The exact minimal instruction vocabulary, handler selection rules, primitive value semantics, and effects of editing a running body. Proposed v0 permits editing behavior between turns only.

## 8. Failures belong to user policy

**Agreed.** neo does not automatically repair erroneous behavior. Faulty behavior can continue to fail until the programmer addresses it. Handling policy is the programmer's choice, rather than an imposed retry, rollback, continuation, or deletion rule.

**Proposed mechanism.** Expose a failure object carrying the failing operation, activation, receiver, message, and diagnostic information. Provide a configurable authorized handler. Separate detection from the handler's decision to report, retry, stop, repair, or propagate. Replies, including failure replies, can be ordinary message objects with correlation to the request and explicit reply authority.

**Open and critical.** The fallback when no handler exists is not yet agreed. A prototype may pause and return a structured unhandled failure to its host runner, but must document that as a host fallback. It must not silently acknowledge successful processing, delete the message, or repeatedly retry it. Earlier visible writes are not automatically reversible; rollback would require explicit transactional or compensating machinery. Retry may repeat effects.

Nontermination is distinct from a detected failure. Run-to-completion code can stall a sequential runtime. Supply a host/operator interrupt and bounded stepping for tests; do not pretend termination can always be detected or silently impose a language-level time slice.

**Reason.** User choice requires a controllable mechanism and observable state. It cannot be implemented by hiding errors or letting host exceptions tear down the runtime unpredictably.

## 9. Bootstrap and implementation boundaries

**Agreed runtime boundary.** image.neo describes the live image as a plain object graph. A separate VM loads and executes that graph. One VM can load multiple images, duplicate them, and delete them. The on-disk file is the textual representation; the loaded graph is its live instance. Loading data must not implicitly execute its behaviors.

**Proposed image lifecycle.** Expose host operations load, start, pause, duplicate, unload, and save. User-requested deletion of a loaded image means unloading its runtime instance; deletion of its source file is a separate explicit operation. Loading the same file twice creates independent image instances. Each image has its own root, ETHER, queues, and object-identity namespace. Cross-image access requires explicit host-granted connections, never accidental identifier collisions.

Load in two passes: allocate nodes first, then resolve edges and validate containment, roles, and authority. Publish only a fully validated image. Textual links reconstruct graph relationships, not ambient host permissions. The loader may reconstruct image-local policy under a restricted image principal; external resources require explicit host grants.

Whole-image duplication occurs at a safe pause boundary, remaps all internal identities and connections, and preserves message lifecycle state and queue order. Do not replay accepted messages. A first implementation may reject images with live activations or external resources it cannot duplicate safely; never silently discard state. Unlike ordinary object copying, a whole-image clone includes the ETHER and queues. External-resource duplication and cross-image authority remain open.

**Reason.** Separating substrate from image allows multiple independent worlds, reproducible loading, and image duplication without confusing the graph with its execution engine. Saving/loading is now an initial requirement for quiescent graphs; resuming arbitrary running computations remains later work.

**Agreed implementation language: C.** Use C for the initial VM so development focuses on neo's design and runtime rather than a younger host toolchain. Odin or Zig remain possible future migration targets. A later self-hosted implementation using a specialized compilable subset of neo is also a possible direction, not a first-milestone requirement.

**Proposed C baseline.** Use portable C17, the standard library, and a conventional build with strict compiler warnings. Use address/undefined-behavior sanitizers where supported during validation. Avoid compiler-specific extensions unless isolated behind a platform interface. The exact compiler and build system remain engineering choices.

Keep memory allocation, object storage, evaluation, scheduling, image serialization, and platform services behind explicit C interfaces. Define ownership and failure results at interface boundaries. Use opaque, validated object handles instead of exposing host pointers to neo. Do not serialize C struct layouts, function pointers, or native addresses into image.neo. Represent native primitive bindings with versioned semantic identifiers validated by the loader. A replacement VM should be able to implement the same image format and behavior without reproducing C's layouts.

**Reason.** C is the initial execution substrate, not neo's language specification. Behavioral tests and documented image semantics provide the migration contract. This separation supports another host or eventual self-hosting without designing a compiler before the interpreter works.

**Future self-hosting boundary.** A compilable neo subset would need explicit low-level representations, memory and calling conventions, primitive bindings, and a bootstrap path. First establish these from a working C runtime; do not prematurely restrict all neo programs to the needs of that future subset.

Module responsibilities within the repository (C modules under neo-vm/source/):

| Component | Responsibility and reason |
| --- | --- |
| vm | Image instance lifecycle and isolation; distinguish runtime management from object behavior. |
| objects | Identity table, containment, duplication, deletion, and listings. |
| authority | Capability and policy checks at every protected boundary. |
| messages | ETHER, edit/accept lifecycle, delivery records, and queues. |
| evaluator | Operation objects and activations. |
| scheduler | Internal tick phases and runnable work. |
| reader / writer | Inert graph parsing and serialization, identity labels, references, and source diagnostics. |
| neo/image.neo | The initial image graph, including receiver-owned behavior. |
| cli | Host operations and diagnostics without implicit file deletion. |
| tests / examples | Semantic checks and runnable image examples. |

See [language-design.md](language-design.md) for proposed syntax, control flow, primitives, and host-language tradeoffs. All syntax there is provisional. Keep [whitepaper.md](whitepaper.md) unchanged.

## 10. Delivery plan and evidence

Implement in small runnable stages:

1. Object store, containment, deep copy with internal remapping, duplicate/delete move, listings, and safe stale-target detection.
2. Identity-based authority checks, ETHER messages, editable pending payloads, atomic acceptance, and receiver queues.
3. Minimal behavior evaluator and sequential scheduler with configurable failure handling.
4. Reader/writer, multi-image VM lifecycle, image.neo, and an end-to-end example in which objects exchange messages and acquire/consume a resource.

Each stage should be runnable and tested before extending it. The first three stages alone are a substrate; do not claim a usable language until source can express and execute behavior through the reader and evaluator.

Required meaningful checks:

- Editing copied behavior cannot alter its source; internal connections point into the copy.
- Move produces new identities, updates listings, deletes originals, and leaves safe unavailable old targets.
- Copy failure never deletes the source; unauthorized structure changes are rejected.
- Makers can specialize message access; unauthorized inspections and impersonation fail.
- Permitted edits work before acceptance; sender writes through all aliases fail afterward.
- Acceptance queues one complete message once; two equal messages remain two requests.
- Sender ordering is preserved and immediate updates are visible to later turns.
- Failed recipient actions are distinguished from successful delivery; chosen failure handlers control policy.
- Discovery handles missing targets and terminates under its configured search policy.
- A .neo example exercises receiver-owned behavior rather than hardcoded host callbacks alone.

## 11. Decisions to preserve for explicit resolution

The four highest-priority remaining decisions are ETHER reachability, enforceable message-policy representation, the full acceptance boundary, and the minimal executable behavior vocabulary. Containment edge cases, active-object copying, conflicting parallel writes, and unhandled-failure fallback also remain open as identified above.

Implement provisional mechanisms behind narrow interfaces and label their defaults. Do not infer that the user's approval of the general architecture settles every entry marked proposed or open.

Resumption of arbitrary running images, spatial protocols, rendering, native code generation, and kernel self-reproduction are later stages. Keep them possible without claiming they are implemented by the initial object store.

## Window and rendering boundary

**Agreed.** Windowing and access to a render buffer are host interfaces. Rendering algorithms, maths, and higher-level libraries belong in neo. The user selected plain X11 windowing instead of OpenGL.

**Implemented first interface.** A backend-independent RGBA byte buffer and window callback interface are attached to capability-protected objects by the trusted host. An optional X11 adapter transfers completed pixels and reports close/resize/expose state. The evaluator accesses resources through granted connections only. No scene renderer is added. Private resource storage is not a new scalar payload kind and never exposes native pointers to neo.

**Provisional scope.** Fixed host-created buffer dimensions, byte-at-a-time language writes, 1:1 opaque presentation, and unsupported resource copy/move/persistence keep this initial boundary small. Bulk writes, input-event objects, buffer resizing, and external-resource persistence remain future work. See the [interface reference](../reference/window-interface.md).
