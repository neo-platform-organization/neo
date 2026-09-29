# neo

neo is a language and runtime project built around a live world of objects. Everything in the language including behavior and primitives is an object. Objects own their internal behavior; prototypes provide examples to duplicate, rather than shared behavior through delegation.

The initial runtime is written in C17. The intended `image.neo` format describes a plain object graph that a separate VM can load and execute. The broader vision is described in [the whitepaper](../whitepaper.md).

## Status

The first milestone is a working in-memory object store. It supports:

- Independent image instances, image duplication, and unloading.
- Named containment and limited-authority connections.
- Integer, boolean, text, and inert primitive-binding objects.
- Deep copies with internal connections remapped to the copies.
- Moves implemented as duplication followed by deletion of the original.
- Safe detection of deleted targets and rollback on allocation failure.

This is not yet an executable language. ETHER messaging, acceptance queues, the evaluator, scheduling, and the `image.neo` reader/writer are still to be implemented. The current demo constructs objects through the C API.

## Build and run

Requirements: a C17 compiler and Make. No external runtime libraries are required for this milestone.

From the `neo/` directory:

```sh
make
make test
make demo
```

The demo moves food into a cell, checks that the original identity is unavailable, duplicates the image, and unloads the original.

To run AddressSanitizer and UndefinedBehaviorSanitizer checks with a supported compiler:

```sh
make sanitize
```

Build outputs go into `build/`. Remove them with `make clean`. See [development.md](../development.md) for validation details and current limitations.

## Project layout

```text
neo/
  include/neo.h        Public C embedding API
  src/object.c         Object store and image management
  examples/demo.c      Runnable substrate demonstration
  tests/test_objects.c Semantic and allocation-failure tests
  Makefile             Build, test, and sanitizer targets
```

Implementation files, this README, and the license live in `neo/`. Architecture notes and agent instructions live in the parent directory.

Design proposals are identified separately from agreed semantics. C is the initial implementation language; a future port or a self-hosted compiled subset of neo remains possible.

## License

neo is licensed under the GNU General Public License, version 3.0 only (`GPL-3.0-only`). See [LICENSE](LICENSE) for the full terms.
