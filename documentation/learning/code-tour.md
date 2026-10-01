# Reading neo from the outside in

[Documentation index](../README.md)

This guide is for someone comfortable with small C programs but new to interpreters and runtimes. Follow one complete path before trying to understand every file. C is the implementation language; diagrams below are plain text, not another programming language to learn.

## 1. Run something before reading its internals

From the repository root (the directory containing Makefile):

```sh
make test
./build/neo check neo/image.neo
./build/neo run neo/image.neo counter increment
./build/neo tick neo/image.neo 4
```

The direct invocation prints result 1 and counter.count = 1. The tick run prints counter.count = 3 and sender.sent = true. Each command starts a new VM and loads a new image; the previous invocation does not silently persist its changes to disk.

Open [the image](../../neo/image.neo). Find counter, count, handlers, increment, and body. The body contains objects describing a write and a calculation. This is actual data read from a file, not a behavior hardcoded into the C demo.

The intended path is:

```text
file bytes
  -> reader builds objects and connections
  -> VM retains the live image
  -> caller invokes a handler or scheduler delivers a message
  -> evaluator follows the receiver's behavior objects
  -> primitive implementation changes a receiver field
```

Loading does not execute the last three steps. That separation lets you inspect, duplicate, or validate an image before starting it.

## 2. Find the front door: main.c

Read [neo-vm/source/main.c](../../neo-vm/source/main.c), starting with main. Ignore the details of parsing and evaluation on your first pass. Notice its job is ordinary C orchestration:

```c
neo_vm *vm = NULL;
neo_status status = neo_vm_create(NULL, &vm);
```

The function returns a status and writes the VM pointer through the output argument. The NULL allocator selects the default malloc/free implementation. The VM is freed before the process exits.

The CLI reads a file, asks neo_image_parse to build a graph, and then selects a command. It performs filesystem I/O; the evaluator has no ambient filesystem access. This is one concrete authority boundary.

## 3. Read the public promises before private structures

Read [neo-vm/include/neo.h](../../neo-vm/include/neo.h). You do not need to memorize every function. Find:

- neo_status: the ways an operation can complete or fail.
- neo_value: the host representation of an object's payload.
- neo_vm and neo_capability: opaque types whose fields callers do not access.
- copy, move, and image lifecycle operations.

An output parameter is not a second return mechanism with hidden behavior. It is a pointer to caller-owned storage the function fills in. A status tells you whether that output is usable.

## 4. What is stored? internal.h and object.c

Read [neo-vm/source/internal.h](../../neo-vm/source/internal.h). Each neo_object has identity, image membership, a parent identity, a name, a payload, connections, and runtime bookkeeping. None of those field names causes behavior by itself.

The VM's object collection currently is a linked chain. Each object records its container ID; it does not allocate a separate child list. Looking for children scans the collection. This makes the code understandable but will not scale well to huge images. The next performance step should be measurement and indexing, not an undocumented change in semantics.

Read these functions in [neo-vm/source/object.c](../../neo-vm/source/object.c), in order:

1. neo_node_new: allocate and initialize one unpublished object.
2. neo_object_create: validate authority, allocate everything needed, then publish.
3. neo_resolve: validate the capability before accessing its target.
4. neo_duplicate: allocate all copies, remap internal connections, then publish.
5. neo_transfer: implement copy or duplicate-then-delete move.
6. neo_delete_region: remove a region from listings before freeing its storage.

The copy mapping is temporary bookkeeping. The published world is the object graph. These are different structures with different lifetimes.

## 5. How text becomes a graph: image.c

Read [neo-vm/source/image.c](../../neo-vm/source/image.c). The reader advances through a string, tracks line/column, and recognizes parentheses, names, tags, labels, and edges.

The central function is neo_read_node. It builds a node and recursively reads its contained children. All nodes remain private to the loading operation until validation succeeds.

Connections need a second pass because a target label may appear later in the file. Once every node exists, neo_image_parse resolves pending edges. An error releases the temporary graph instead of publishing half an image.

The writer performs the inverse traversal. It serializes meaning, not C memory addresses. A loaded copy gets different numeric IDs while preserving which objects connect to which.

The implemented grammar and exact limits are in [runtime-format.md](../reference/runtime-format.md). The older [language-design.md](../design/language-design.md) contains broader proposals, not a description of everything this reader accepts.

## 6. How behavior runs: evaluator.c

Read [neo-vm/source/evaluator.c](../../neo-vm/source/evaluator.c), starting at neo_behavior_run. It finds:

```text
receiver -> handlers -> selected handler -> body
```

An activation holds information about this invocation: receiver, authority, input message, remaining budget, and return state. It is currently a private C structure, not yet a fully inspectable neo activation object.

neo_evaluate asks whether the current object is data or a primitive operation. Data supplies a value. A primitive dispatches to the corresponding implementation rule. A primitive is still represented by an object in the image; the C branch is how this first VM executes it.

Trace increment:

```text
do
  write
    obtain slot name "count"
    add
      read current count
      obtain integer 1
    replace count's payload with the result
  return
    read updated count
```

Notice why if cannot evaluate all children before choosing a branch: the unselected branch may change state or fail. Evaluation order is a language rule, not a side effect of how the C loop happened to be written.

Temporary result payloads are owned by the evaluator and freed explicitly. They are not yet exposed as independently inspectable intermediate graph objects. This is one remaining gap between the prototype and the full everything-is-an-object vision.

## 7. Why messages need a separate module

Read [neo-vm/include/neo_message.h](../../neo-vm/include/neo_message.h), then the access check and acceptance functions in [neo-vm/source/message.c](../../neo-vm/source/message.c).

Messages are objects in ETHER with protected metadata. Ordinary object access cannot bypass their policy. A context identifies the acting object; a name written in a message cannot impersonate that actor.

Acceptance closes the editing window and enqueues the message without allocating or calling user code in between. This is atomic in our sequential runtime. It is not a claim that this C code is safe to call concurrently.

## 8. Who runs next? scheduler.c

Read [neo-vm/source/scheduler.c](../../neo-vm/source/scheduler.c). The scheduler is a loop that chooses work; it does not supply the receiver's behavior.

```text
capture the current submission boundary
accept eligible messages
for each registered receiver in order:
  process at most one message
  run its optional tick behavior
```

In image.neo, sender creates a message during its first tick. Acceptance for that tick has already happened, so counter processes the message at the next boundary. This is an explicit provisional scheduling policy.

If behavior fails, the receiver pauses. The host can inspect the report, repair the body, then explicitly retry or discard. Retry starts over and can repeat earlier side effects. There is no automatic rollback.

## 9. Read tests as executable examples

Start with neo_test_reader_and_roundtrip in [neo-vm/tests/test_runtime.c](../../neo-vm/tests/test_runtime.c). Then read neo_test_scheduler, which deliberately causes a failure, edits the divisor, and retries the retained message.

Next read [neo-vm/tests/test_objects.c](../../neo-vm/tests/test_objects.c) and [neo-vm/tests/test_messages.c](../../neo-vm/tests/test_messages.c). CHECK/OK are test assertions, not language syntax. Fault allocators deliberately make the nth allocation fail to prove partial work is cleaned up.

[neo-vm/tests/test_cli.sh](../../neo-vm/tests/test_cli.sh) only drives the real executable and checks its output and exit status. The runtime itself remains C.

## 10. Small experiments

Make a separate copy of image.neo before editing it. Change the increment amount from 1 to 2 and invoke increment. Change the tick comparison threshold and observe the final count. Then put fail in an unselected if branch: it should not execute. Put it in the selected branch and inspect the reported operation ID.

These experiments connect the file you edit to the graph the reader creates and the C rules the evaluator applies. Do not start by rewriting memory management.

## What this base does not establish

It is a small runnable interpreter, not yet a spatial OS. It has no geometry or dimensional transformations, renderer, parallel execution, arbitrary running-image persistence, graph-resident failure handlers, or self-hosted compiler. Those should be designed explicitly. The runtime now gives us a concrete place to test those ideas without pretending they are already solved.
