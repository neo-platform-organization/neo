# Universal dimensionality before GUI

[Documentation index](../../README.md) · [Architecture](../architecture.md)

Status: direction accepted by explicit user instruction; concrete protocol pending.

## Intent and decision

The GUI is a 1:1 representation of the live object graph. UI elements are themselves
objects in that graph, with their own geometry and behavior. The OS and applications
inhabit that same graph. A two-dimensional UI element draws along two axes, has a
location, and is described by a vertex or vertices. The system must generalize this
model beyond fixed 2D/3D cases, including objects without spatial extent.

Universal dimensionality must be established before further graphical work. The
first OS libraries/packages supply the shared dimensional and mathematical model.
Their algorithms live in neo; the kernel provides protected storage, evaluation,
and platform-neutral I/O and display mechanisms. A separate scene model that must
be synchronized with OS objects would conflict with the intended 1:1 relationship.

## Meaning and limits

Graph links express relationships; coordinates express spatial placement. A graph
alone does not supply axes, a metric, or projection rules. Those need explicit
shared protocols. One-to-one representation does not imply that a 2D screen can
show every property of an arbitrary-dimensional object without loss. A viewport
is also a graph object; projection and interaction rules remain to be designed.

Universal support means a dimension-independent contract, not infinite storage or
unbounded computation. Packed coordinate/vertex data can belong to one object;
there is no requirement to allocate a separate object for every coordinate or vertex.
The existing cube and pixel renderer are demonstrations, not this common model.

## Proposed first packages and open contracts

Package boundaries below are proposals, not implemented names or approved APIs:

- Maths: numeric representation, vectors, matrices, and checked shape operations.
- Spaces: dimension, axis conventions, coordinate frames, locations, and transforms.
- Geometry: vertex collections and topology associated with graph objects.
- Projection: mapping between spaces and viewport coordinates without changing
  source object identity.

Before implementation, decide scalar precision and units, dimension/shape mismatch
behavior, local versus parent/world locations, transform composition, storage limits,
zero-dimensional versus non-spatial objects, and how projections map input back to
objects. Communication between objects of different dimensions does not inherently
require their geometry to be convertible.

Start with non-graphical tests across several dimensions and invalid shapes. Then
adapt the existing renderer to this model before building GUI elements. Keep one
OS image; packages are contained graph objects, not mandatory separate files.
See [OS sessions](../../planning/os-sessions.md) for sequencing.
