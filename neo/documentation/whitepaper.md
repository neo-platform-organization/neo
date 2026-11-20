# neo
## A live platform for making, using, and sharing interactive things

### Abstract

An instrument should remain editable while it plays. A tool that began as
an experiment should still be understandable when someone else opens it
months later. neo proposes a programming platform, operating system, and
game engine built around a shared live object graph. You can use an
object, inspect how it works, change it within an explicit scope, and
publish a version others can run independently.

The platform separates durable identity, changing behavior, authority,
and presentation. Its canonical graph is called Warp. Weft is the
language for writing object behavior, and Knight is the language for
pure computation. This paper describes the design for the rebuild.
The examples show proposed syntax and semantics.

## 1. From use to edit and back

Suppose a synth is playing a loop. You open its inspector, select a
filter, and change the calculation. The sequencer keeps its place and
the voices retain their state where the edit permits it. At the next
audio-block boundary, the synth takes up the new filter. If the change
cannot be applied, the previous filter keeps playing.

The same synth can be published as a versioned package. Someone else
can open that version in a clean image, play it, and save settings.
They can inspect what their references allow or fork a copy they
control. The author's next experiment leaves their version intact
until they choose to update.

These two transitions give the platform its purpose. The language makes
the synth understandable and editable. The engine runs the interactive
workload. The operating system gives the synth a place alongside other
tools and controls the resources it can use.

## 2. Warp, Weft, and Knight

**Knight is lawful. Warp is chaotic. Weft is neutral.** These names
describe what each part of neo permits.

**Warp** is the canonical representation of the object graph. It records
identities, values, containment, links, and executable forms. Shared
links and cycles survive serialization. A document can keep its
identity after a rename, and a synth can keep its identity after a
change to its behavior. A saved graph records durable relationships.
Live device handles and authority tokens have their own lifetime.
Warp is chaotic in the sense that it can represent many kinds of
structure. Code, data, and descriptions of other structures can
coexist in the graph. An evaluator determines how to interpret a form.

**Weft** is the language for writing live object behavior. There are no
classes. Objects receive messages and may delegate to prototypes.
Weft makes the receiver, state changes, and effects visible in the
source. A caller can depend on a published message without depending
on the shape of the receiver's private graph.
Weft is neutral. It gives objects a way to act in a changing world,
using Knight when a calculation calls for it and Warp when a tool
needs to inspect or transform structure.

**Knight** expresses pure computations and deterministic transforms.
A synth can handle events in Weft and calculate its samples in Knight.
Knight receives values and returns results. The calling object decides
when to apply those results and which effects to request. Determinism
helps with replay and testing. Evaluation still needs a resource budget.
Knight is lawful because the same inputs determine the same result
under the same semantics. It cannot request external effects.

Weft and Knight lower to Warp. A browser can show the same behavior in
the notation suited to reading it. An editor can inspect or transform
executable graph structure as data without invoking it. Execution
begins through an explicit evaluator operation and an authorized
reference.

An illustrative Weft view containing a Knight function follows.

```text
object voice {
  phase = 0

  on advance(step) {
    phase = next-phase(phase, step)
  }

  knight next-phase(phase, step) = (phase + step) % 1
}
```

The message handler owns the update. The function computes a new value.
The canonical graph records the relationship between the handler, the
state slot, and the function. The spelling of this example is provisional.

## 3. Identity, revisions, references, and views

An **identity** answers which object an operation refers to. A
**revision** answers which state and behavior it observed. A
**reference** carries authority for particular operations. A
**view** selects what to show, including its level of detail.

These distinctions have practical consequences. A file name helps find
a document. Reusing that name after deletion must never give an old
editor permission to write the replacement. A meter can read a synth's
levels, a sequencer can send it notes, and a debugger can replace its
filter when granted that power. All three refer to the same synth.

An inspector can explain what a patch will affect by showing known
prototype links and current bindings. Editing an instance, editing
its shared prototype, and changing an exported contract have
different scopes. The preview describes known effects. At commit,
the runtime checks authority and the expected revision again.

Code and state that must change together are prepared before
activation and published together. An active handler observes a
defined revision. A stale or invalid patch leaves the running
version intact. An earlier revision can restore graph state where
an inverse is meaningful. External effects, such as audio already
played or a message already sent, retain their own history.

## 4. An OS and engine inside the image

The desktop, editor, package tools, filesystem workflows, and engine
services are live image packages. Ordinary use should require no
knowledge of the graph. An authorized inspector can still follow a
visible control to its receiver, operation, and resulting change.
A click and a script can use the same operation and authorization rules.

The desktop is a two-dimensional viewport over an extensible spatial
model. Pan and zoom change the view. Moving an object changes its
placement. A synth can appear as a small tile, a panel of knobs, or an
inspected graph while remaining the same object. Packages can choose
useful representations. Rendering packages handle composition and
drawing.

The filesystem is another useful view of the graph. Choosing a file
hands the editor a scoped reference to that document. A save uses
the revision the editor opened. If another session has committed a
new revision, the editor keeps its unsaved changes and can explain
the conflict.

## 5. Compatibility and sharing

A package declares the messages it offers, the services it needs, and
the version it was built to use. Compatibility concerns observable
behavior, including failure and permission rules. Private fields and
layout can evolve. A practical compatibility test runs an unchanged
consumer against a provider whose private graph has been reorganized.

Distribution and authority are separate decisions. A recipient may
obtain a package from a bundle, repository, or another image. Loading
it does not grant access to the recipient's documents or devices.
Network peers exchange explicitly exposed data and capabilities.
Applications choose their ownership and reconciliation protocols.
A copied identifier or a replica alone carries no extra authority.

## 6. The kernel and platform boundary

The C kernel provides minimal bootstrapping, graph memory and identity,
evaluation, bounded execution, and enforcement of authority and revision
commits. An optional JIT can implement the same execution contract.
Application behavior, UI policy, rendering policy, and package workflows
belong in the image. A trusted boot graph can provide pre-OS selection
and console workflows, with a minimal kernel fallback for boot failure.

Platform adapters expose normalized input, console I/O, byte streams,
graphics and audio buffers, storage, networking, memory, and time.
They translate between the VM and the machine. The same image should
run through hosted, headless, and x86-64 metal adapters. Given the same
logical resolution and input, the image should produce the same
framebuffer. Services that a target lacks report that through the
shared interface.

## 7. What would count as evidence

First, round-trip a Warp graph with aliases and cycles and run one
image-owned interaction under replayed input. Then open two editor
sessions on the same document revision. The first save commits.
The second preserves its changes and reports a conflict. A revoked
or read-only reference cannot write.

Next, run two synths with a shared prototype and a meter with read
authority. Patch one synth at a deterministic block boundary. Verify
that the other synth keeps its behavior, the meter cannot apply the
patch, and a failed migration publishes neither code nor state.
Publish the instrument and open it in a clean image. The recipient
should be able to use it while the author keeps working independently.

The platform earns its complexity if this workflow is more direct than
the tools people already have. If a player needs to understand the graph
to use a synth, the interface has exposed too much. If an author cannot
trace or scope a live change, the interface has hidden too much. The
rebuild should be judged against both cases.
