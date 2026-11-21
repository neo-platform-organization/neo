# Current source tour

The three repositories are siblings. Start with the surface's small C entry and
ABI forwarding layer, then inspect the relevant depth implementation.

| Location | Responsibility |
| --- | --- |
| neo/neo-vm/main.c | Validate/install a trusted provider and enter the preOS |
| neo/neo-vm/depth.h and platform.c | Versioned platform interface and forwarding |
| neo/neo-vm/language.h and language.c | Versioned language interface and forwarding |
| neo/neo-vm/jit.h and weft.h | Public graph, execution, source-view, and translation contracts |
| neo/neo-vm/services.h and events.h | Shared resource and event interface types |
| depth/provider.c and bindings.h | Register the selected linked provider |
| depth/language/ | Graph engine, JIT, Weft implementation, and language registration |
| depth/plugins/ | OS SDK calls and standalone x86_64 hardware mechanisms |
| depth/common/ | Resources, ports, events, CLI/REPL, launcher, and transport |
| depth/tools/ | Regression driver/fixtures, embedding, and boot checks |
| neo-image/neo-os/image.neo | Canonical live source and program behavior |

Build only from the surface root Makefile. See the
[architecture](../development/three-repositories.md) and
[milestones](../development/launch-milestones.md) for scope and unfinished work.
