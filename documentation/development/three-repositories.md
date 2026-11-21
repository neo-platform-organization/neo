# Surface, depth, and image repositories

The working checkout is three independent sibling Git repositories:

```text
workdir/
  neo/          surface VM
  depth/        depth kernel and native plugin implementations
  neo-image/    live image and image-side programs
```

There are no nested repositories or submodules. The names avoid a collision:
the existing surface repository already occupies ../neo from inside itself.
The image repository therefore uses ../neo-image. Its canonical document is
neo-os/image.neo. It is never copied into the VM source or build directory.

## Surface

The surface defines the public language and platform contracts, validates their
versioned native interfaces, installs the selected trusted provider once, and
starts the preOS entry. Read main.c, depth.h, language.h, and platform.c first.
Public language operations are routed by language.c through the validated table.

The surface contains no OS SDK calls, GPU setup, interrupt handlers, allocator,
resource broker, launcher GUI, or compiler opcode emitter. Public headers still
specify the current bounded graph/native ABI. Reducing that exposed representation
further is future ABI work; the split does not complete the languages.

## Depth

Depth holds the actual implementation of platform and language mechanisms:

- language/: Warp graph engine, JIT code generation, Weft lowering, literal
  machinery, and language-provider registration.
- plugins/: hosted Windows/Linux implementations and standalone x86_64 boot,
  memory, interrupt, input, framebuffer, and serial code.
- common/: resource ownership, async service requests, events, CLI/REPL,
  launch orchestration, presentation transport, and acceptance harnesses.
- tools/: regression fixtures, image embedding, and QEMU/OVMF boot checks.

NEO_DEPTH_ABI and NEO_LANGUAGE_ABI identify the current interfaces. Version,
structure size, mandatory functions, and fixed-provider lifetime are checked
before startup. Implementations are currently statically linked providers.
This is useful on both hosted and standalone targets; dynamic shared-library
loading is not required for early boot and is not implemented yet.

Plugins are trusted native code in the VM's protection domain. They are not
sandboxed by moving to another repository. A defective driver or compiler can
still compromise the system. Package grants and host worker isolation retain
their existing, narrower guarantees. The published language contract belongs
to the surface; its implementation is supplied by the language plugin.

CPU scheduling policies and GPU/device drivers can grow in depth without
adding OS-specific code to the surface. This change does not claim to implement
new schedulers, a general driver framework, or hot replacement of active devices.
Those require their own typed interfaces and tests rather than an untyped
plugin callback for arbitrary operations.

## Image

The image repository owns neo programs, pure calculations, application state,
GUI behavior, and the future OS/engine world. Its current image contains small
acceptance exercises and the text tool. It has no platform C implementation.
Its whitepaper remains a preserved design reference.

## Build and reproducibility

The only build file remains neo/Makefile. It builds the sibling depth sources
and uses ../neo-image/neo-os/image.neo by default. DEPTH_DIR and IMAGE_DIR override
these sibling paths. Target selection and output names have not changed.

components.json records the paired depth/image revisions for a surface commit.
Clone the three repositories as siblings and use matching revisions. The ABI
checks reject incompatible native providers; a source revision pin also captures
behavior and test fixtures that a version number alone cannot describe.

The original foundation snapshot repository remains archived and untouched.
The three development repositories continue independently.
