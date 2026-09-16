# OT3D Downstream and Upstream Policy

## Repository roles

OT3D has three distinct source relationships:

| Repository | Role | Policy |
| --- | --- | --- |
| [`opentoonz/opentoonz`](https://github.com/opentoonz/opentoonz) | Official OpenToonz upstream | Source of periodic upstream synchronization and generally useful fixes |
| [`OpenAnimationLibrary/opentoonz-dev`](https://github.com/OpenAnimationLibrary/opentoonz-dev) | Historical/staging source for reviewed 3D experiments | Selective provenance source; its branch is never imported wholesale |
| [`OpenAnimationLibrary/ot3d`](https://github.com/OpenAnimationLibrary/ot3d) | Integrated OT3D downstream and release home | OT3D mission, roadmap, issues, pull requests, and program releases |

OT3D is a standalone downstream rather than GitHub's fork object, but the source and
license relationship is the same: it derives from OpenToonz and must preserve that
history and attribution.

## Independence and identity

OT3D is maintained by the Open Animation Library community. It is not an official
OpenToonz distribution, and its roadmap or support requests are not obligations of the
OpenToonz maintainers.

The project is not endorsed by or affiliated with DWANGO, Digital Video, Studio Ghibli,
the Blender Foundation, or Hash, Inc. Product names are used only to identify source
and interoperability relationships.

Early development builds use a minimal visible suffix such as `OT3D Experimental`
while retaining internal OpenToonz executable, application, and settings identifiers.
A complete rename has consequences for packaging, preferences, plug-ins, file
associations, automation, and scene behavior; it requires a dedicated design and test
plan. Portable pre-releases are preferred until that work is complete.

## Initial baseline rule

The initial OT3D source baseline is the named official OpenToonz commit recorded in
[`THREE_D_MIGRATION.md`](THREE_D_MIGRATION.md), plus only the reviewed 3D changes listed
there. The repository's original license-only commit is preserved with the
`ot3d-bootstrap-2026-09-15` tag.

The initial migration uses each selected merge commit's first-parent change rather
than taking the complete `opentoonz-dev` branch. Conflict resolution must reproduce the
3D intent without pulling in neighboring experiments. Source commit authorship is
preserved, and cherry-picked commits record their origin.

## What belongs where

Prefer an OpenToonz-ready change when it:

- fixes a bug present in OpenToonz without requiring the OT3D architecture;
- improves a shared test, parser safety check, build rule, or platform abstraction;
- is small enough to review and useful without accepting the OT3D roadmap; and
- preserves OpenToonz behavior and project policies.

Keep work in OT3D when it:

- implements the integrated mixed 2D/3D Viewer plan;
- adds experimental scene state or interaction that is not yet compatibility-frozen;
- coordinates several changes whose immediate value exists only in the OT3D workflow;
  or
- relies on a downstream release flag, compatibility matrix, or fixture set.

An OT3D feature can later be decomposed into focused upstream proposals. No OpenToonz
merge is assumed, and OT3D must remain maintainable if upstream declines or redesigns a
proposal.

## Periodic upstream synchronization

Upstream synchronization is performed through a dedicated pull request. It must:

1. record the old and new full OpenToonz commit SHAs;
2. summarize upstream changes that overlap OT3D-owned areas;
3. preserve public OT3D history—do not rebase or force-push released commits;
4. resolve conflicts as explicit downstream adaptations, not “ours/theirs” bulk picks;
5. run inherited OpenToonz tests plus the relevant OT3D fixtures; and
6. update compatibility, build, and migration records if behavior changed.

A normal merge from the official upstream line is preferred after the one-time
bootstrap because it leaves the synchronization boundary auditable. A different method
requires the PR to explain how provenance and later conflict analysis remain possible.

Upstream tags are not OT3D release tags. OT3D's `ot3d-v...` namespace remains separate.

## Pull-request expectations

Every OT3D pull request should be focused enough to review and should state:

- the user-facing outcome and current limitation;
- whether it changes Viewer, final rendering, saved scenes, preferences, or packaging;
- an issue or roadmap milestone;
- test assets and steps, including platform/GPU where relevant;
- new external data, allocation, or graphics-state trust boundaries; and
- upstream source/provenance when code is migrated or adapted.

Do not combine opportunistic OpenToonz cleanup with a 3D feature unless the cleanup is
required and separately reviewable. Avoid drive-by formatting in inherited code.

## Provenance and licensing

When migrating a change:

- retain original author information and commit references;
- keep license headers, copyrights, and third-party notices intact;
- record the source repository, full SHA, source PR when known, and adaptation notes;
- verify that fixtures, models, textures, and screenshots are redistributable; and
- add third-party code only with its complete license and a maintainable update path.

The Modified BSD License in the repository root continues to govern the inherited code
described by it. Files in `thirdparty`, brush libraries, and other named components may
carry different terms. A downstream documentation or branding change cannot replace
those terms.

## Issue routing

Report bugs found in an OT3D build to OT3D first. If maintainers reproduce the problem
on an unmodified supported OpenToonz build, it can be reported upstream with a minimal
case and without OT3D-specific assumptions.

Likewise, OT3D should link rather than duplicate an existing OpenToonz issue when the
same problem is already tracked there. Cross-project references should describe the
relationship without asking one project's maintainers to support the other's build.

## Scene and preference compatibility

OpenToonz scene compatibility is a release gate, but it is not assumed automatically.
Downstream-only state should be additive and versioned where practical. Each release
must document:

- whether OpenToonz can open an OT3D-authored scene;
- which downstream data OpenToonz ignores or loses;
- whether reopening that scene in OT3D is safe;
- whether OT3D shares or isolates preferences; and
- the backup/rollback path.

Until a round trip is fixture-tested, users should edit copies and must not rely on
OpenToonz to preserve OT3D-only data.

## Release responsibility

Only assets published from the OT3D repository under an `ot3d-v...` tag are OT3D
releases. They must include upstream provenance, license notices, checksums, feature
matrix, limitations, and tested-platform information as defined in
[`RELEASES.md`](RELEASES.md).
