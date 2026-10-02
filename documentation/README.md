# neo documentation

This is the entry point for learning, using, and extending neo. Commands in these guides run from the repository root—the directory containing Makefile—unless stated otherwise. Source links point directly into this checkout.

## Start here

- **Learning the implementation:** follow the [code tour](learning/code-tour.md), with the [example image](../neo/image.neo) open beside it.
- **Building or changing code:** read [build and test](development/build-and-test.md) and the [C conventions](development/c-conventions.md).
- **Writing an image:** start with the [syntax cheat sheet](reference/syntax-cheat-sheet.md), then use the [implemented format and primitives](reference/runtime-format.md) for details.
- **Understanding the design:** read the [architecture](design/architecture.md), then the [implementation plan](planning/implementation-plan.md).

## Structure

```text
documentation/
  README.md
  learning/
    code-tour.md
  reference/
    runtime-format.md
    syntax-cheat-sheet.md
    window-interface.md
    io-interface.md
    software-renderer.md
  development/
    build-and-test.md
    c-conventions.md
    writing-docs.md
  design/
    architecture.md
    language-design.md
    io-and-display.md
    whitepaper.md
    decisions/
      README.md
  planning/
    implementation-plan.md
```

Each section answers a different question:

| Section | Purpose | Future additions belong here when needed |
| --- | --- | --- |
| learning/ | How does this work? Guided explanations and exercises. | Memory, ownership, message flow, evaluator walkthroughs, spatial examples. |
| reference/ | What does the implementation actually accept or guarantee? | CLI, C API, image format versions, primitives, message protocol, error statuses. |
| development/ | How do I build, test, debug, or contribute? | Debugger guides, profiling, portability, releases, test strategy. |
| design/ | Why is it built this way, and what alternatives are being explored? | Object storage, authority, execution, persistence, spatial model, rendering. |
| design/decisions/ | What significant decision was made, and why? | Numbered decision records with status and supersession links. |
| planning/ | What is done, what comes next, and what blocks it? | Milestones, experiments, migration plans. |

Do not create empty subject folders in advance. Split a long guide when the new subject has substantive content, then link both pages from this index.

## Subjects

| Subject | Explanation/design | Current implementation reference |
| --- | --- | --- |
| Objects, identity, containment, copying | [Architecture](design/architecture.md), [code tour](learning/code-tour.md) | [Build guide and limitations](development/build-and-test.md) |
| Authority and ETHER messages | [Architecture](design/architecture.md) | [Runtime format and lifecycle](reference/runtime-format.md) |
| Execution, control flow, scheduling, failures | [Code tour](learning/code-tour.md) | [Primitive and scheduler rules](reference/runtime-format.md) |
| Images and persistence | [Architecture](design/architecture.md) | [Graph format and persistence boundary](reference/runtime-format.md) |
| Streams, terminal, and input | [I/O design](design/io-and-display.md) | [I/O interface](reference/io-interface.md) |
| Windows and pixel buffers | [I/O and display design](design/io-and-display.md), [code tour](learning/code-tour.md) | [Window interface](reference/window-interface.md) |
| Software rendering | [Code tour](learning/code-tour.md) | [Renderer and cube](reference/software-renderer.md) |
| Spatial model and long-term vision | [Whitepaper](design/whitepaper.md), [plan](planning/implementation-plan.md) | Not implemented; do not infer support from the vision. |

## Status and authority

The [runtime reference](reference/runtime-format.md) describes the implemented subset. The [development guide](development/build-and-test.md) records tests and limitations. The [architecture](design/architecture.md) explicitly distinguishes agreed requirements, proposed defaults, and open decisions. The [language-design document](design/language-design.md) contains broader proposals, including syntax that the current reader does not implement.

The whitepaper is an unchanged reference copy of the original workspace document. It preserves the initial vision, including claims and details later refined in discussion; it is not an executable specification. Do not edit either copy without explicit user direction.

Later explicit user decisions take precedence. A planned or proposed feature is not implemented merely because it appears in documentation. Record consequential choices through the [decision-record process](design/decisions/README.md).

README.md remains at the repository root as a concise landing page. Applicable AGENTS.md files remain at their discovery roots rather than inside this documentation tree. See [writing documentation](development/writing-docs.md) before adding or reorganizing pages.
