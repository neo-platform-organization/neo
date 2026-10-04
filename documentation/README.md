# Universal neo VM documentation

These Markdown guides describe the implemented VM, C interfaces, compilation,
and usage. Language/image documentation is isolated in
[neo/documentation](../neo/documentation/README.md).

| Guide | Subject |
| --- | --- |
| [Build](development/build.md) | Compile the single executable and select a target. |
| [Code tour](learning/code-tour.md) | Follow the subsystem implementation. |
| [Platform](reference/platform-interface.md) | Environment metadata and host-service callbacks. |
| [Streams and input](reference/io-interface.md) | Protected byte streams and normalized events. |
| [Windows](reference/window-interface.md) | Buffers, presentation, and resource lifetime. |
| [C conventions](development/c-conventions.md) | C API names, formatting, ownership, and errors. |
| [Writing docs](development/writing-docs.md) | Markdown and source-comment documentation. |

Plans, TODOs, design decisions, and collaboration memory live in the external
`brain/` directory. They are not part of this usage manual.
