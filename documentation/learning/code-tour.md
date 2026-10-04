# Read the VM

[VM documentation](../README.md)

Start with [main.c](../../neo-vm/main.c): it forwards process arguments to
[cli.c](../../neo-vm/cli/cli.c). Utility arguments inspect or run an image;
[launch.c](../../neo-vm/cli/launch.c) selects CLI, GUI, or both interfaces, loads one
image, grants resources, and invokes the selected receiver. Both interfaces share
that VM and image lifetime.

Read [objects.c](../../neo-vm/objects/objects.c) next. It allocates identities,
contains payloads, validates capabilities, and performs independent deep copying
with internal-reference remapping. C storage implements the graph; it is not a
class system. Duplication stays in the kernel.

[image.c](../../neo-vm/image/image.c) constructs an inert graph, then resolves
connections. Loading does not execute it. [evaluator.c](../../neo-vm/execution/evaluator.c)
walks receiver-owned behavior objects, dispatching protected primitive mechanisms
in C. `==`, `!=`, `<`, `>`, `<=`, and `>=` are primitive object names with two
operand children. They retain the object-graph syntax.

[messages.c](../../neo-vm/messages/messages.c) controls message authority and
acceptance. [scheduler.c](../../neo-vm/execution/scheduler.c) supplies the current
sequential host policy. Queues/activations are not yet fully graph-resident.

[platform.c](../../neo-vm/platform/platform.c) supplies the generic host contract.
The [Linux adapter](../../neo-vm/platform/x86_64/Linux/platform.c) opens sources,
streams, windows, and waits. [io.c](../../neo-vm/io/io.c) and
[display.c](../../neo-vm/display/display.c) validate protected resource access;
[POSIX streams](../../neo-vm/platform/posix/streams.c) and
[X11](../../neo-vm/platform/x11/x11.c) translate native mechanisms.

For a language example, read [shaded-cube.neo](../../neo/shaded-cube.neo). Vertex
coordinates are a packed integer payload owned by the cube, not individual active
objects. Rotation, projection, and rasterization run in neo. The kernel presents
completed bytes. General dimensional protocols are not implemented by this demo.

## Inspecting and changing the world from neo

[graph.c](../../neo-vm/execution/graph.c) evaluates operand values first, then uses
held capabilities to resolve objects. Reference results carry VM-owned handles;
text cannot manufacture one. Mutation uses existing kernel APIs. A chain of active
receivers protects every suspended caller's behavior while `invoke` runs another
receiver under its own authority and the shared instruction budget.

[image.neo](../../neo/neo-os/image.neo) uses these mechanisms for a prototype
catalogue and workspace. The browser reads the catalogue; copying gives workspace
objects their own arrays and behavior. Their maths/shape handlers execute through
`invoke`. [selftest.c](../../neo-vm/cli/selftest.c) is a command in the same binary,
covering authority and lifetime cases plus that actual image.
