# Contributing

Keep changes readable and explain what they do. Use plain language in code
comments, documentation, commit messages and pull requests. Avoid marketing copy,
stock AI phrases and em dashes. Write about the software rather than the chat that
led to a change. State what works, how it was checked and what is still missing.

The language name is `neo`. Follow the
[C conventions](documentation/development/c-conventions.md) and keep GUI behavior
and system policy in the image. The C runtime provides execution, protection and
host mechanisms.

Keep the normal Windows outputs named `neo.exe` and `image.neo`. Build artifacts
belong under `build/ARCH/OS/` and do not go in Git. The whitepaper is a preserved
reference; do not rewrite it.

Run checks that fit the change. Use `neo selftest` for runtime changes and
`neo selftest gui` for the desktop and editor. See the
[build guide](documentation/development/build.md) for setup and test limits.
