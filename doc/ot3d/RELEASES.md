# OT3D Release Policy

## Where releases live

OT3D program releases are published from
[OpenAnimationLibrary/ot3d](https://github.com/OpenAnimationLibrary/ot3d/releases).
Official OpenToonz releases remain on the OpenToonz project. An OT3D package must never
present itself as an official OpenToonz build.

CI artifacts from untagged commits are developer snapshots. They may be useful for pull
request testing, but they are not releases and receive no compatibility promise.

## Version and tag format

OT3D uses Semantic Versioning for its downstream release line:

```text
Tag:           ot3d-vMAJOR.MINOR.PATCH[-PRERELEASE]
Release title: OT3D MAJOR.MINOR.PATCH[-PRERELEASE]
```

Examples:

- `ot3d-v0.1.0-alpha.1` — first M3 test build;
- `ot3d-v0.3.0-beta.2` — second beta of the M5 feature line;
- `ot3d-v1.0.0-rc.1` — release candidate; and
- `ot3d-v1.0.0` — first stable compatibility line.

The `ot3d-` prefix prevents confusion with tags inherited from OpenToonz. Tags are
immutable. A bad release is superseded by a new patch or pre-release number; its tag
and assets are not silently replaced.

Before `1.0.0`, a minor version may contain compatibility changes. Pre-release notes
must identify scene, preference, asset, or command-line behavior that changed.

## Release channels

| Channel | GitHub setting | Intended audience | Compatibility expectation |
| --- | --- | --- | --- |
| Developer snapshot | No GitHub Release | Contributors testing a commit/PR | None beyond that commit |
| Alpha | Pre-release | Workflow and architecture testers | Data may change; known gaps expected |
| Beta | Pre-release | Project-copy production trials | Core workflow stabilizing; upgrades documented |
| Release candidate | Pre-release | Final compatibility and packaging validation | No intentional feature changes |
| Stable | Full release | Supported workflow described by its matrix | Patch line preserves documented behavior |

“Stable” applies only to the support matrix shipped with that release. It does not mean
that OT3D implements all of OpenToonz, glTF, Blender, or Animation:Master behavior.

## Platform sequence

Early tagged releases prioritize a portable Windows x64 archive. This provides the
shortest path to the project's current primary build/test coverage and can keep test
preferences beside the package instead of sharing an installed OpenToonz profile.

A normal installer is deferred until executable identity, bundle/product identifiers,
file associations, uninstall behavior, and preference migration have been designed and
tested together. A non-portable experimental build may still identify internally as
OpenToonz; testers must not assume its settings are isolated.

macOS and Linux assets are added only when the same tag builds reproducibly and passes
the declared milestone checks on those platforms. Source availability does not itself
constitute a binary support claim. Release notes list each platform actually tested.

## Package naming and contents

Recommended binary names are unambiguous and sortable:

```text
OT3D-0.1.0-alpha.1-windows-x64-portable.zip
OT3D-0.1.0-alpha.1-windows-x64-portable.zip.sha256
```

Every binary package must contain or link to:

- the Modified BSD license and all required third-party notices;
- `BUILD_INFO` with exact OT3D and upstream provenance;
- the release's supported-feature/known-limitations matrix;
- installation or extraction and removal instructions;
- preference/project isolation and rollback instructions; and
- a SHA-256 checksum published beside the asset.

`BUILD_INFO` records at minimum:

- OT3D version, tag, and full commit SHA;
- official OpenToonz baseline and later upstream synchronization SHA(s);
- the migrated 3D source revisions or a link to the migration record;
- UTC build time, compiler/toolchain, architecture, and CI workflow run;
- enabled experimental feature flags; and
- support-matrix document revision.

The application About/version text must visibly include `OT3D Experimental` for alpha
and beta builds. A broader rename of executables, bundles, settings keys, and file
associations is a separate packaging project, not a documentation-only change.

## Release gates

A maintainer may cut a pre-release only when:

1. the tag points to a reviewed commit on the intended release line;
2. required CI is green and the exact packaging job is reproducible;
3. migrated loader/evaluator tests and relevant OpenToonz regression tests pass;
4. the milestone's reference scene and GLB fixtures pass on every claimed platform;
5. install/extract, first launch, save/reopen, and removal smoke tests pass;
6. licenses, provenance, checksums, and `BUILD_INFO` are present; and
7. release notes identify limitations, breaking changes, and rollback steps.

Stable releases additionally require an upgrade test from the previous supported OT3D
line, a documented OpenToonz interoperability result, and no unresolved known data-loss
issue in the supported workflow.

## Release notes template

Each release description should contain:

```markdown
## Scope
Milestone and intended testers.

## What works
Only fixture-backed supported behavior.

## Known limitations
Viewer/final differences, unsupported GLB data, scene and platform constraints.

## Compatibility
Upstream baseline, DCC/exporter fixture versions, scene format notes.

## Installation and isolation
Package type, preference location, coexistence warning.

## Verification
Checksums, BUILD_INFO, CI link, manual test summary.

## Upgrade and rollback
Backup requirements and path back to the previous build.
```

## Branch and maintenance policy

`main` is the integrated development line. Release stabilization may use a short-lived
`release/MAJOR.MINOR` branch. Only regression, packaging, documentation, and security
fixes enter that branch after a release candidate; feature work continues on `main`.

Supported stable lines receive patch releases according to available maintainer and
test capacity. The release page must state when a line is no longer maintained.

If a binary is later found unsafe or destructive, mark the release prominently as
withdrawn and publish the reason. Preserve the tag and provenance record unless a legal
or security requirement prevents doing so; publish a corrected version under a new tag.

## Initial planned progression

- F0–M2: developer snapshots only;
- M3: `0.1.0-alpha` — static hybrid camera/depth proof;
- M4: `0.2.0-alpha` — animation and reload;
- M5: `0.3.0-beta` — selection and staging workflow;
- M6 plus a stable M7 subset: `0.4.0-beta`; and
- M8's compatibility-frozen subset: `1.0.0`.

Milestones can produce multiple `.alpha.N`, `.beta.N`, or `.rc.N` builds. They are
quality gates, not promised calendar dates.
