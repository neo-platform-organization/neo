# Pharo Launcher review

Reviewed 2026-11-21. This records behavior to adopt in neo's launcher plan;
it does not claim those features are implemented. No Pharo source was copied.
The existing preOS/C boundary and internal-window default remain unchanged.

Documentation describes workflows across several Pharo releases. Implementation
observations below are pinned to revision
c92e2dc10c444c10f5e64fcf65459abc8189d357 on the dev branch.

## Important omissions from our original plan

| Pharo feature | Adaptation for neo | Milestones |
| --- | --- | --- |
| Automatically chosen VM and multiple named launch configurations | Record an explicit runtime compatibility requirement; choose a compatible installed VM, allow a pinned version, and keep named profiles per image. Include VM arguments, program arguments, working directory, service grants, presentation override, and renderer. | M3-M5 |
| Basic launch without global image preferences | A diagnostic clean profile bypasses optional personal startup customizations. It does not bypass capability enforcement, core initialization, or required image compatibility checks. | M3, M5 |
| Image metadata, description, origin template, and last selected profile | Separate portable image identity/requirements/provenance from launcher-local paths, preferences, and secrets. Show origin, exact artifact, compatibility, and last launch outcome. | M4-M6 |
| Launch from an arbitrary path and import associated files or the whole parent directory | Distinguish Open once, Register in place, and Import copy. Copy only a declared image bundle by default; whole-directory import is an explicit choice. No silent relocation or unrelated-file harvesting. | M5-M6 |
| Create local templates from an image | Freeze a consistent saved checkpoint into a reusable template. Creating a world from it does not mutate the template. Creating a template, cloning a world, and instantiating an application are separate operations. | M4-M6 |
| Recreate an image from its original template | Preserve provenance and a creation recipe. Recreate alongside the existing world by default; offer replacement only with a preserved recovery checkpoint. If the exact artifact is unavailable, report that instead of substituting latest. | M4-M5, M8 |
| One-time initialization scripts | Explicit creation recipes can initialize a newly created world. Discovery/download never runs them. Record pending/running/succeeded/failed states; spawning a process does not prove initialization succeeded. | M3-M5, M8 |
| Package an image with its VM and launch entry | Export a portable distribution containing the selected image, declared binary assets, compatible runtime, launch configuration, notices, and integrity manifest. Support both minimal and full environments without rewriting packages. | M4, M9 |
| Copy image path or exact launch command | Show the resolved launch plan and provide Copy command / Export launch configuration. CLI commands must reproduce GUI choices, including arguments and runtime, without leaking credentials. | M5-M6 |
| List and stop running image processes | A Running view shows world/session ID, profile, runtime, start time, current state, logs, and exit reason. Offer attach/focus, graceful stop, and separate force stop. | M3, M5-M6 |
| Create an image from an exact build or commit; bisect versions | Pin source artifacts by digest and immutable version/build metadata. Later add a diagnostic that tests throwaway copies across ordered builds to locate a regression. Bisection is useful but not a first-launcher blocker. | M4, M8; later diagnostic |
| Cached templates, source categories, fixed URLs, HTTP listings, CI providers | Use one inert catalog contract and provider adapters. Start with a static catalog and explicit artifacts. Add CI/GitHub/private providers when useful rather than embed each provider in GUI code. | M8 |
| Proxy settings and private sources | Support explicit network configuration and credentials supplied through a credential service. Do not store tokens in exported image metadata, catalog files, commands, or logs. | M2, M8 |
| Keep launcher open or quit after launching | Make this a launcher preference. Exit only after a readiness acknowledgement; on standalone systems preserve supervision when the launcher is responsible for resource lifetime. | M3, M5-M7 |

Named profiles do not create images, application instances, or host windows
by themselves. They configure an explicit launch. Application window placement
remains internal unless developer metadata or an explicit user override says
otherwise. The default profile is derived from developer intent; local overrides
must be visible and removable.

## Reusable templates and mutable worlds

Our first design treated remote downloads mostly as ready-to-run images.
Pharo's stronger distinction is useful: a reusable source artifact and a world
created from that artifact have different lifecycles.

For neo, maintain:

1. A catalog record describing an available artifact.
2. An immutable downloaded template identified by its digest.
3. A mutable installed image/world with its provenance and saved state.
4. A running session using that installed image and an effective launch profile.
5. Application instances and windows within that session.

A library folder is only organization; it is not any of those identities.
Moving a window does not move or copy a world. Installing a template is not
executing its initialization recipe. Actual world/graph identity rules belong
to M4's specification, not to path names or display labels.

## Data records to plan before building the GUI

These are proposed contracts, not new Warp syntax:

- Image descriptor: format and language versions, required runtime features,
  declared assets and program entries, origin artifact, and portable recipe.
- Library entry: installation identity, registered root, relative location,
  display name, description, selected profile, and availability state.
- Launch profile: runtime constraint or pin, argument arrays, working directory
  mapping, requested service grants, presentation and renderer choices, and
  optional user startup customization policy.
- Resolved launch: exact runtime/artifact identifiers, effective configuration,
  and actual granted capabilities. Inspectable before starting execution.
- Run record: session ID, originating library entry/profile, readiness state,
  start/end time, host process ID when available, output/log endpoint, and exit
  reason. A host PID is not the portable identity of a running world.
- Template cache: artifact digest, origin, verification result, retained/pinned
  state, and last use. Deleting cache content cannot delete an installed world.

The portable descriptor can live with an image bundle, using inert graph data.
Launcher-local records must also work with a read-only ISO or a moved directory.
Local settings and secrets are never written into downloaded immutable content.
The canonical development image stays ../neo-image/neo-os/image.neo.

## Changes we should make rather than copy literally

### Compatibility must be inspectable without running the image

Pharo documents using a private VM to execute an image when version metadata
is absent. neo must report missing compatibility information or inspect the
serialized graph; discovery cannot execute it. Downloading a native runtime is
a separate operation from obtaining an image. An image cannot nominate an
arbitrary host executable or grant itself stronger access through a profile.

### Cache identity must not depend on a mutable name or URL

Pharo's template documentation warns that cached content may differ from an
updated server template. neo should keep downloaded artifacts by digest and
track a catalog label separately. Refreshing a source updates catalog metadata,
not a developer's mutable world. Detect corrupt cache entries; show whether a
catalog result is fresh, stale, unavailable, or already cached.

### Recreate and initialization need recoverable publication

At the inspected revision, Pharo's recreate operation deletes the existing
image before extracting its source template. neo should stage and validate the
new world first, retaining the old one until publication succeeds. Keep a known
recovery point rather than equating reset with deletion.

Initialization runs as requested program execution under explicit grants.
Report success only after the image acknowledges readiness and the relevant
state is committed. External side effects cannot generally be made exactly-once
by a launcher journal; recipes need declared idempotency or explicit retry
handling. Do not retry failed initialization silently.

### Launching must not depend on shell string construction

The inspected Pharo process configurator has platform-specific shell and
library-path handling. neo should use structured argument arrays and explicit
working-directory/environment mappings through its platform process service.
A user-facing Copy command is a formatted view of the structured launch record,
not the internal representation. Libraries required by the runtime must not
silently depend on the launcher's own host environment.

### Package declared contents, not a user's entire directory

Pharo offers whole-directory import and runtime-bundled export. neo should
retain the useful choices while making declared bundle contents the default.
A consistent checkpoint is required before export; a live world is not copied
file-by-file while changing. Relative bundle paths must survive relocation.
Reject existing output destinations instead of overwriting unrelated content.
Retain executable names and Make output conventions already agreed.

### Make settings persistence visible

Pharo's settings guide requires a separate Store settings action. neo should
save changes atomically or provide an unmistakable Apply/Cancel workflow.
Users should not discover after restarting that a directory root or source
was only registered for one session. Temporary overrides should be labeled.

## Acceptance additions

- A source path or argument containing spaces, quotes, or non-ASCII text survives
  GUI, CLI, copy-command, and direct-process launch without changing meaning.
- A clean profile leaves personal defaults untouched and never widens authority.
- Pinning a runtime/artifact gives the same selection after a catalog refresh.
- An absent compatible runtime produces an explanation before image execution.
- Import registers or copies exactly the selected bundle; moving the library
  preserves relative assets and profiles without exporting secrets.
- Template creation leaves its source world intact; two created worlds have
  separate mutable state and share no writable template files.
- Failed recreation preserves the original checkpoint and library entry.
- Failed initialization remains visible and does not become a successful launch
  merely because a process was created.
- The running registry distinguishes image sessions, application instances,
  and their internal/host windows. On standalone targets it works without PIDs.
- Logs and reproducible launch details remain available after an image exits.
- Closing the launcher follows an explicit supervision policy and does not
  accidentally terminate or orphan a world.
- Cached templates launch without network access; cache cleanup leaves installed
  worlds and pinned artifacts intact.
- Minimal and full distributions run after extraction to another directory on
  a clean supported machine, without a separate launcher installation.

## Sources

Official documentation:

- [UI commands](https://pharo-project.github.io/pharo-launcher/commands/)
- [Launch configurations](https://pharo-project.github.io/pharo-launcher/launch-configurations/)
- [VM management](https://pharo-project.github.io/pharo-launcher/manage-vms/)
- [Templates and custom sources](https://pharo-project.github.io/pharo-launcher/templates/)
- [CLI commands, exact builds, packaging, recreate, and bisection](https://pharo-project.github.io/pharo-launcher/commands-cmd-line/)
- [Settings](https://pharo-project.github.io/pharo-launcher/settings/)

Implementation inspected at the pinned revision:

- [Launch configuration](https://github.com/pharo-project/pharo-launcher/blob/c92e2dc10c444c10f5e64fcf65459abc8189d357/src/PharoLauncher-Core/PhLLaunchConfiguration.class.st)
- [Default launch configuration](https://github.com/pharo-project/pharo-launcher/blob/c92e2dc10c444c10f5e64fcf65459abc8189d357/src/PharoLauncher-Core/PhLDefaultLaunchConfiguration.class.st)
- [Platform process configurator](https://github.com/pharo-project/pharo-launcher/blob/c92e2dc10c444c10f5e64fcf65459abc8189d357/src/PharoLauncher-Core/PhLLaunchImageProcessConfigurator.class.st)
- [Image metadata and initialization](https://github.com/pharo-project/pharo-launcher/blob/c92e2dc10c444c10f5e64fcf65459abc8189d357/src/PharoLauncher-Core/PhLImage.class.st)
- [Directory repository and recreation](https://github.com/pharo-project/pharo-launcher/blob/c92e2dc10c444c10f5e64fcf65459abc8189d357/src/PharoLauncher-Core/PhLDirectoryBasedImageRepository.class.st)
- [Image process record](https://github.com/pharo-project/pharo-launcher/blob/c92e2dc10c444c10f5e64fcf65459abc8189d357/src/PharoLauncher-Core/PhLImageProcess.class.st)
- [CLI packaging](https://github.com/pharo-project/pharo-launcher/blob/c92e2dc10c444c10f5e64fcf65459abc8189d357/src/PharoLauncher-CLI/PhLImagePackageCliCommand.class.st)
- [Packaging tests](https://github.com/pharo-project/pharo-launcher/blob/c92e2dc10c444c10f5e64fcf65459abc8189d357/src/PharoLauncher-CLI-Tests/PhLImagePackageCliCommandTest.class.st)
- [Template listing cache](https://github.com/pharo-project/pharo-launcher/blob/c92e2dc10c444c10f5e64fcf65459abc8189d357/src/PharoLauncher-Core/PhLCacheTemplateGroup.class.st)

These findings are selective source review, not execution or exhaustive testing
of Pharo Launcher. neo's adaptations above are design choices and remain planned
until implemented and tested in their milestones.
