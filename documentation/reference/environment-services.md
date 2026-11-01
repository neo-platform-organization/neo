# Image environment services

[VM documentation](../README.md) · [Live terminal](live-image.md)

`packages/system/environment` is an owned service prototype. Copy it, configure
its input fields and explicitly grant external connections. The terminal's
`service PATH` selects that instance; `request ACTION` writes its `action` field
and invokes `execute` with a 100000-step budget. Results and failures come from
image behavior. `service native` returns to the native recovery path.

## A first request

In `neo live neo/neo-os/image.neo`, from a fresh image:

```text
copy /packages/system/environment /workspace environment
add /workspace (subject (n 0))
link /workspace/environment target /workspace/subject/n 3
link /workspace/environment destination /workspace/subject 5
service /workspace/environment
request inspect-value
set /workspace/environment/value 41
request edit
set /workspace/environment/name "created"
request create
show /workspace/subject
```

The same subject now contains `n = 41` and `created = 41`. The service never
receives the image root implicitly. READ|WRITE (3) permits the edit; READ alone
permits inspection and denies editing. The destination grant READ|INSERT (5)
permits creation. Root-level native commands remain trusted operator operations.

## Request contract

Inputs are ordinary fields on the service, configured between bounded requests.
Connections are capabilities, never path strings or numeric diagnostic IDs.

| Action | Inputs/grants | Result |
| --- | --- | --- |
| `inspect-value`, `inspect-name`, `inspect-kind`, `inspect-count` | `target` with READ | Current payload, name, kind, or child count. |
| `inspect-child` | `target` with READ; `index` | Authorized child reference. |
| `edit` | `target` with WRITE; `value` | Written payload. |
| `create` | `destination` with INSERT; `name`, `value` | New object reference. |
| `duplicate` | `target` with READ|COPY; destination INSERT; `name` | Independent duplicate reference. |
| `move` | Same as duplicate plus source DELETE | Duplicate reference after original deletion. |
| `remove` | `target` with DELETE | Unit. |
| `invoke` | `target` with READ|ACT and rights needed by its behavior; `selector` | Handler result. |
| `tick`, `enable`, `retry`, `discard` | `scheduler` connection with rights needed by its handlers | Scheduler handler result. Configure its `selected` field for recovery. |
| `failure-status`, `failure-operation`, `failure-receiver` | Service's latest ordinary failure | Status text or historical diagnostic ID. |
| `clear-failure` | Service WRITE | Clear the latest failure record. |

`execute` dispatches `action` through an explicit invocation on the service.
Ordinary failure updates `last-error`, `last-operation`, `last-receiver`, then
rethrows to the caller. Successful requests retain the latest failure until
explicitly cleared. Step/allocation limits bypass image recovery and still appear
in the native report; no complete debugger snapshot or rollback is implied.

The service is a trusted tool using its own grants. ACT access authorizes use of
those grants; handlers are not private or a security boundary between untrusted
clients. Configure separate instances/grants for separate trust domains. Inputs
are shared mutable graph fields, not activation-local parameters: serialize
frontend requests and do not change inputs through callbacks during a request.

## Scheduling and saving

For the reference scheduler, grant the rights needed to create/copy/delete its
turn metadata and delegate snapshot links, in addition to invocation:

```text
copy /packages/system/scheduler /workspace scheduler
link /workspace/environment scheduler /workspace/scheduler 127
set /workspace/environment/scheduler-attached true
request tick
```

127 is READ|WRITE|INSERT|COPY|DELETE|DELEGATE|ACT. Worker authority remains in the
scheduler's separately granted worker connections. Ordinary READ|WRITE|ACT (67)
is insufficient for this scheduler's internal metadata operations.

With a service selected, terminal `save FILE` first invokes its `save-ready`.
It must return boolean true. The reference policy requires `saving-enabled=true`
and, when `scheduler-attached=true`, `scheduler/running=false`. Missing grants,
wrong result types and failures deny the write. Native checkpoint validation and
host file permissions still apply after approval. The service has no implicit
filesystem capability and cannot bypass native validation.

`service native` explicitly bypasses the image policy for operator recovery;
native `set`, `call`, `drive`, `pause`, inspection and repair remain accessible.
Service selection, like driver selection, is host session state and is not saved.

## One live graph, multiple frontends

The frontend contract is one VM and root, serialized bounded requests, and shared
service/object identities. A frontend holding an appropriate service capability
writes the request fields and calls `neo_behavior_run(..., "execute", ...)`, or
invokes the same handler from graph behavior. It releases each native report.
Rendering caches and editor buffers may be derived views, not replacement worlds.

| Interface | Current entry point and contract |
| --- | --- |
| Live terminal | `neo live IMAGE`, with `service`/`request` and native repair. |
| GUI application | `neo --gui IMAGE ACTOR [HANDLER]`; current app runner creates one VM/image and grants display resources. The future live GUI frontend must attach its graph views to the same environment service. |
| Combined application | `neo --cli --gui IMAGE ACTOR [HANDLER]`; terminal/display grants share that actor's VM and image. A combined live editor must serialize both frontends through the same service, not launch a second process. |

The existing GUI runner is not yet the live OS GUI or package browser. Implementing
its controls and live startup remains in the later GUI milestones. A headless graph
view client exercises the shared service contract now without claiming GUI QA.

Long work uses explicit graph state machines: each `tick` performs a finite chunk,
stores progress and returns. Polling nonblocking I/O can defer work to later ticks;
do not retain C activations or wait in an unbounded handler. General asynchronous
completion queues remain future work. Host polling/waiting remains a mechanism.

## Verification

Windows strict build and 78 expression cases plus environment groups pass. The
new group checks read-only edit denial, failure inspection/clearing, creation,
editing through both native requests and a graph view client against one object,
scheduler invocation, save-policy refusal/reset and native recovery after deleting
the service handler. Tracked allocations balance. No live GUI frontend or Linux
validation is claimed by these checks.
