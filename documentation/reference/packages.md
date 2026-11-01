# Local packages

[VM documentation](../README.md) · [OS seed](project-substrate.md)

The OS image has an image-owned `package-manager` with an initially empty
`catalogue` and inert `staging` area. Ordinary `packages/system` objects remain
bootstrap infrastructure. Application examples are fixtures, not installed boot
packages. The manager explicitly chooses validation, install, update, resolution,
copy instantiation, removal and export through protected native mechanisms.

## Install, browse and instantiate

From the repository root, run `neo live neo/neo-os/image.neo`. The following
explicitly installs two test artifacts; no package handler runs during import:

```text
pkg-load neo/fixtures/packages/clock.neo
pkg validate
pkg install
pkg clear
pkg-load neo/fixtures/packages/counter.neo
pkg install
pkg-list
pkg-info local.counter
set /package-manager/id "local.counter"
set /package-manager/export-name "counter"
set /package-manager/instance-name "my-counter"
pkg instance
call /workspace/my-counter tick
pkg-export counter-package.neo
```

The explicit invocation returns 1. The installed counter stays unchanged.
`pkg clear` deletes the staged candidate; it does not uninstall a package.
A new `pkg-load` refuses an occupied candidate slot. `pkg validate` checks inert
artifact structure; dependency compatibility is checked by image installation policy.
Load/validation failures
leave installed packages unchanged. `pkg-info` reads the manifest, versions,
dependencies, exports, imports and binding sites; native `show` can inspect deeper
fields. These terminal commands present the same graph used by image behavior.

The manager inputs `id`, `export-name`, `instance-name` and `rights` are ordinary
editable fields. `pkg resolve` returns a reference limited by the export ceiling
and manager authority. `pkg instance` permits copy exports and creates independent
owned behavior/data in the workspace. Service exports are deliberately shared;
resolve them and grant explicit connections rather than implicitly cloning them.

## Artifact contract

Schema 1 is an ordinary graph with `manifest` and `contents`. The manifest has
`schema`, `id`, `version`, `dependencies`, `exports`, `imports` and `bindings`.
The two fixture artifacts demonstrate the complete shape, including an internal
self-reference and a symbolic binding to an explicitly shared dependency export.

IDs are local dot-separated lowercase components; versions are exact nonnegative
major/minor/patch triples. One version of an ID may be installed. Dependencies pin
exact triples; missing versions, conflicting pins, self/transitive cycles and
unknown schema fields are rejected. There is no download or version-range solver.

An export specifies a containment path, `copy` or `service` mode and maximum rights.
Private contents are not manager exports; this is discovery policy, not a new
kernel barrier against existing READ authority. Copy exports contain their owned
helpers and preserve internal edge topology. Hidden sibling-helper dependencies,
overlapping exports, invalid paths and undeclared external connections fail.
Known primitive operation names are allowed in payload paths/names as well as
ordinary identifier names; ambiguous sequence children cannot be path targets.

Imports identify dependency exports and exact rights. Binding records identify
payload owners and connection names. Exported artifacts contain those symbolic
records, not unresolved external labels or host handles. Installation checks the
export ceiling, actual manager rights and DELEGATE before constructing links.
No numeric identity, filename or matching package ID supplies authority by itself.

No resources, runtime messages, ETHER or native registrations are package contents.
Activation is a separate bounded invocation or explicit scheduler registration,
using separately granted resources. Activation failure retains earlier effects.

## Update, removal and export

`pkg update` uses the staged candidate and requires a greater version of the same
installed ID. Old dependent pins and external incoming links refuse replacement.
`pkg remove` removes the package selected by `id`, after checking dependents,
incoming graph connections and retained runtime work. Stop/recover/disconnect users
explicitly. Independent workspace copies survive origin replacement/removal;
their external service links can still block removal of a shared dependency.

Each record contains an installed `bundle` and closed `baseline`. Comparison
ignores remapped numeric IDs and root spelling but checks payloads, order and
internal topology. Export verifies declared external links and serializes an
independent baseline, converting them to the original symbolic declarations.
Dirty installed edits refuse export/update under the recorded version. Repair the
installation or explicitly author a new artifact from a clean export; no implicit
version increment or live state migration is provided. Shared service mutations
also count as installed edits. The baseline is ordinary trusted operator state,
not a cryptographic integrity or publisher authentication mechanism.

All fallible staging, validation and grants precede commit. Replacement then
deletes the prevalidated unused old region and publishes the prepared name with
no remaining allocation. Failure before commit removes staging and preserves the
old installation. This is a bounded package operation, not general graph rollback.
Native backends export through temporary-file replacement after encoding succeeds.

`save FILE` preserves catalogue records, baselines, instances and actual imported
connections through the normal versioned checkpoint. Reopening is paused. Package
schema 1 and checkpoint version 1 are separate contracts. Existing node/depth/source
limits apply, including staging/baseline storage; large artifacts can hit them.

## Image/native boundary and validation

Image handlers own exact pin matching, topological ordering/cycle detection,
greater-version eligibility, declared-dependent removal and copy-instance policy.
Temporary `resolution` rows carry READ|DELEGATE manifest links and completion flags;
they are removed before native publication. No runtime numeric ID is treated as
authority. The scan is bounded by the existing instruction budget, not C recursion.

Policy failures raise an image failure and set `/package-manager/policy-error` to
`missing-dependency`, `version-conflict`, `dependency-cycle`, `already-installed`,
`not-installed`, `version-not-newer`, or `required-by-package`. Ordinary failure
cleans scratch. A host limit/OOM may leave scratch; the next policy request first
cleans it and starts fresh. This does not acknowledge work or roll back package
behavior. Manager requests must remain sequential. A graph client needs the actual
rights for creating/deleting scratch and delegating read references (127 for the
reference manager); READ|WRITE|ACT alone is insufficient.

Native
`package-validate`, `package-install`, `package-update`, `package-remove`,
`package-resolve` and `package-export` enforce structural schema, baseline integrity,
actual grants, runtime/lifetime protection and safe graph publication. They do not
enforce metadata dependency compatibility or greater-version ordering. Alternate
image policies may choose different compatibility rules but cannot bypass these
native checks. The low-level C interface is a mechanism, not the full reference
manager policy. `pkg-load` supplies an inert file artifact;
`pkg-export` writes the image handler's returned text using the host file service.
No install hook executes, no source include is restored, and no package gets an
implicit host capability. Native recovery commands remain available.

Windows strict build and the full built-in suite pass. Package groups cover
missing/wrong pins, duplicate IDs, schema fields, private exports, denied grants,
binding conflicts, baseline edits, independent copies, export, update and removal
blocking. Failures are injected at every install/update allocation; prior identities
remain usable. Separate processes verify export/reimport, instance invocation and
session save/reopen. Linux/sanitizers and a graphical package browser were not run.

Image-policy groups additionally test direct/transitive cycles, metadata-only
dependents, interrupted-plan recovery and editable version eligibility. A test
changes a requested grant during image preparation; final native publication
rejects the excessive rights and preserves the installed version.
