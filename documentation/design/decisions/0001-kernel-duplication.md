# Kernel duplication

[Documentation index](../../README.md) · [Architecture](../architecture.md)

Status: accepted by explicit user direction; supersedes OS-level duplication.

The earlier roadmap moved traversal, allocation, and internal connection remapping
into neo. The user now directs that duplication remain in the kernel. Keep the
existing native mechanism and its protected authority/lifetime boundary. This
avoids making an image-level deep-copy implementation a prerequisite for the OS.

This changes placement, not semantics: copies have new identities; internal links
are remapped; permissions cannot increase; incomplete copies must not be published.
Move remains duplication followed by deletion. The placement of move orchestration
and the exact image-facing API are still open. Existing restrictions on copying
active state and external resources remain in force.

Evidence: native duplication already exists; this documentation change adds no API
or new copying capability. Follow [OS sessions](../../planning/os-sessions.md).
