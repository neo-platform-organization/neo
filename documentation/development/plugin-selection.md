# VM plugin selection

Depth is a passive collection of implementation material. It does not select,
load, or manage itself. The VM's builder reads standard plugin descriptions
from an input/plugin catalog and decides what goes into the VM executable.
The live image is not being configured by this question.

## First build

From the surface repository, run make (mingw32-make on Windows). If no saved
configuration exists, the build asks:

```text
Include depth components in this VM? [y/N]
```

Answering No saves a bootstrap-only configuration. It compiles no depth
implementation sources and does not require the depth or image repositories.
This executable can report its build selection; it cannot execute neo programs
without the language and supporting platform components.

Answering Yes lists compatible entries from input/plugins/catalog.json and asks
which to include. Choose IDs or list numbers. The builder displays the resolved
dependency closure and saves .neo-config.json before compiling. Dependencies
cannot override an explicit decision to disable depth. Cancellation or invalid
selection does not overwrite the previous saved configuration.

```text
make configure
make plugins
make
build\x86_64\Windows\neo.exe --build-info
```

The current catalog contains:

| ID | Material | Dependency |
| --- | --- | --- |
| neo.platform | Target-specific allocation, execution, console, resource and device support | None |
| neo.language | Graph engine, Weft translation, and Knight/native compilation | neo.platform |
| neo.preos | CLI, REPL, launcher mechanisms, and acceptance harnesses | neo.language |
| neo.image.exercise | Canonical exercise graph as data | neo.language |

These are currently coarse components. Selecting individual GPU drivers or CPU
scheduling policies will require separating those implementations and declaring
more plugins. Merely adding a name to the list does not implement that driver.

Standalone x86_64 requires a boot/platform component. The builder reports that
constraint instead of silently adding depth after a No answer. Its current
preOS additionally requires the bundled exercise image; these target-specific
dependencies are declared in the manifests.

## Reproducible noninteractive configuration

```text
make configure CONFIG=my-vm.json CONFIGURE_ARGS="--select neo.preos"
make CONFIG=my-vm.json
make test CONFIG=my-vm.json

make configure CONFIG=bootstrap.json CONFIGURE_ARGS=--no-depth
make test CONFIG=bootstrap.json
```

Choosing only neo.platform omits the language and preOS code. Choosing
neo.language includes its platform dependency but omits the preOS. User
configuration is local and .neo-config.json is ignored by Git. Custom CONFIG
files are under the user's control; do not accidentally commit private choices.

## Standard bundle description: neo.plugin/1

The initial common format is declarative JSON. This is packaging/build metadata,
not a new executable syntax for Warp, Weft, or Knight. A native bundle might be:

```json
{
  "format": "neo.plugin/1",
  "id": "example.utility",
  "version": 1,
  "collection": "depth",
  "kind": "native",
  "role": "library",
  "requires": [],
  "sources": ["utility.c"],
  "includes": ["include"],
  "targets": {"x86_64/Windows": {}, "x86_64/Linux": {}}
}
```

The manifest sits at its bundle root beside the declared relative payloads.
Native roles platform, language, and entry also name an entry symbol and its
interface ABI. Current supported interface ABIs are platform 2, language 1,
and preOS entry 1. A native library contributes selected support code without
introducing another top-level entry point.

Target variants can add sources, includes, libraries, dependencies, and assembly.
The standalone image bootstrap can declare which source consumes its generated
embedded-image data. Build options are data fields, not executable Make fragments
or shell commands. The single root Makefile supplies the build rules.

Data bundles instead use kind data and a relative payload path. Their role may
describe an image or another asset. They cannot introduce native sources,
compiler include directories, assembly, or linker options. The current boot
assembler supports one selected image payload. General multi-image discovery
remains a launcher milestone.

## Input/plugin catalog

input/plugins/catalog.json is the general inventory. It is not a depth manager.
It registers bundle manifests, including manifests stored in the independent
sibling repositories:

```json
{
  "format": "neo.plugin-catalog/1",
  "bundles": ["../../../depth/platform.plugin.json"]
}
```

Catalog registration paths are relative to the catalog. Payload and source paths
are relative to the registered bundle root and must remain inside it after
resolving symbolic links. CATALOG selects another inventory. This allows one
logical input/plugin list while keeping the source repositories separate.
The VM does not implicitly include everything found in depth.

## Validation and output

The builder rejects missing/duplicate IDs, dependency cycles, conflicting
exclusive roles, unsupported targets/interface versions, escaping payload paths,
and unsafe source/linker values. Native input is trusted compiler material;
selection is not a sandbox or a cryptographic trust guarantee.

Generated selection.c, plugins.mk, and selection.json live only under the target
build directory. A fingerprint records exactly which components were compiled.
Switching configurations forces the executable to be rebuilt, and tests compare
its --build-info output with the selection plan. A quick profile switch must not
reuse an executable from another configuration.

Names remain neo.exe, neo, or neo.bin in the corresponding build directory.
There are no alternate VM executable names. The generated Make include contains
selection data; build recipes remain in the one root Makefile.

This implementation selects source components at VM build time. It does not yet
load shared libraries at runtime, hot-swap active providers, resolve version
ranges, authenticate remote catalogs, or offer a graphical configuration screen.
Source/include paths currently exclude whitespace and Make/shell syntax; the
relative directory segment __parent__ is reserved for standalone object paths.

The executable's small surface installs only its generated platform, language,
and preOS entries. There is no mandatory combined whole-runtime provider.
