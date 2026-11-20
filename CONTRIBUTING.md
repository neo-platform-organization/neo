# Contributing

The language name is `neo`. Use plain language in code and documentation.
Follow the [C conventions](documentation/development/c-conventions.md).
Keep application behavior, layout, and system policy in the image. C provides
execution, protection, resource lifetime, and platform mechanisms.

The canonical image is `neo/neo-os/image.neo`, never a build-directory copy.
Windows outputs use `build/x86_64/Windows/neo.exe`; Linux uses
`build/x86_64/Linux/neo`; standalone x86_64 uses `build/x86_64/x86_64/`.
The root Makefile is the only build entry point. The whitepaper is preserved.

Run `make test` for the affected target. Runtime changes shared with the
standalone backend also require its QEMU boot tests. See the
[build guide](documentation/development/build.md) for dependencies and limits.
Do not commit binaries, generated docs, caches, test stores, or credentials.
