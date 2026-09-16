# Initial OT3D 3D Source Migration

## Purpose

This record makes the one-time OT3D seed reproducible. It distinguishes reviewed 3D
work from unrelated experiments that share history in the staging repository.

This is a provenance manifest, not a claim that the imported FX renderer already
implements the planned interactive Viewer.

## Lineage anchors

| Anchor | Commit | Meaning |
| --- | --- | --- |
| Original OT3D repository bootstrap | [`2c9ed6bdf12825fc46a482ebf597ae3b8f5445a5`](https://github.com/OpenAnimationLibrary/ot3d/commit/2c9ed6bdf12825fc46a482ebf597ae3b8f5445a5) | Initial license-only OT3D commit, retained as tag `ot3d-bootstrap-2026-09-15` |
| Official OpenToonz source baseline | [`9001fd493e4ec27bf3efc368252eccb7da703928`](https://github.com/opentoonz/opentoonz/commit/9001fd493e4ec27bf3efc368252eccb7da703928) | `opentoonz/opentoonz` source on which the selective 3D seed is applied |
| OT-Dev staging tip reviewed | [`266b6d31090cdacf94f80c1c55eb1cabbd3433fd`](https://github.com/OpenAnimationLibrary/opentoonz-dev/commit/266b6d31090cdacf94f80c1c55eb1cabbd3433fd) | Review boundary, **not** imported wholesale |

The bootstrap replacement is a one-time repository establishment operation. After the
baseline is published, normal OT3D `main` history and release tags are not rewritten.

## Selected 3D change set

Apply these source merge commits in order, preserving author information and adding a
source trailer (for example, Git's `-x` cherry-pick trailer):

| Order | OT-Dev source commit | Imported intent |
| ---: | --- | --- |
| 1 | [`3f883c504c713e443b82e7ec2c792ac378082bf1`](https://github.com/OpenAnimationLibrary/opentoonz-dev/commit/3f883c504c713e443b82e7ec2c792ac378082bf1) | Add GLB Model FX framework |
| 2 | [`b8a7f4fb8c8374c423d800adb99a60318cac262f`](https://github.com/OpenAnimationLibrary/opentoonz-dev/commit/b8a7f4fb8c8374c423d800adb99a60318cac262f) | Render GLB models in grayscale |
| 3 | [`88114adeef1112773ab299acff98bcd4886d2f27`](https://github.com/OpenAnimationLibrary/opentoonz-dev/commit/88114adeef1112773ab299acff98bcd4886d2f27) | Recover animated GLB material colors |
| 4 | [`a5acc50e4c6195b5adf1fd8946842b1a9347e2fb`](https://github.com/OpenAnimationLibrary/opentoonz-dev/commit/a5acc50e4c6195b5adf1fd8946842b1a9347e2fb) | Group 3D effects in the FX menu |
| 5 | [`2d3d95d4d3ad2529bbea61cd5ab43adf7590caa1`](https://github.com/OpenAnimationLibrary/opentoonz-dev/commit/2d3d95d4d3ad2529bbea61cd5ab43adf7590caa1) | Establish Animation:Master–OpenToonz GLB bridge foundation |
| 6 | [`f69f2fce2cf46e6e05c7723d82960158ba2560db`](https://github.com/OpenAnimationLibrary/opentoonz-dev/commit/f69f2fce2cf46e6e05c7723d82960158ba2560db) | Recover GLB animation data and pose evaluator |
| 7 | [`22abc7ac0c0cc295a54d9bc8b04458d8c739c42f`](https://github.com/OpenAnimationLibrary/opentoonz-dev/commit/22abc7ac0c0cc295a54d9bc8b04458d8c739c42f) | Play embedded GLB animation in GLB Model FX |
| 8 | [`87808a8acdb3c3232a5c1c0c944ab2e245162d33`](https://github.com/OpenAnimationLibrary/opentoonz-dev/commit/87808a8acdb3c3232a5c1c0c944ab2e245162d33) | Add Three-Point Light 3D FX |
| 9 | [`266b6d31090cdacf94f80c1c55eb1cabbd3433fd`](https://github.com/OpenAnimationLibrary/opentoonz-dev/commit/266b6d31090cdacf94f80c1c55eb1cabbd3433fd) | Fix Three-Point Light scene reload |

Because these are merge commits from a staging line, the migration takes each merge's
first-parent delta (`-m 1`) rather than merging either side's complete history. Resulting
OT3D commit SHAs differ from the source SHAs; the source trailers and this ordered
manifest provide the mapping.

## Conflict-resolution boundary

The first source merge was created after unrelated work had entered OT-Dev. Applying
its 3D-only delta to the clean official baseline can therefore produce context
conflicts. Resolve them by carrying only the GLB intent:

- Windows CI receives the GLB FX test configure/build/run steps, not recorder or LUT
  jobs and conditions.
- `stdfx/CMakeLists.txt` receives the GLB test option/target only.
- `stuff/config/current.txt` receives GLB Model and other selected 3D strings only.
- parameter controls receive the GLB file-control state and hook, not LUT controls.
- FX settings receive the GLB file-field behavior, not unrelated file-type features.

No conflict resolution should be justified merely because it matches the OT-Dev side.
The acceptance test is whether it is required by one of the nine selected 3D deltas on
the named official baseline.

## Explicit exclusions

The following neighboring OT-Dev work is not part of the initial OT3D baseline:

- 3D LUT Bake work;
- Save and Render experiments;
- Windows recorder/RecorderEncoder work;
- Production Journal publishing and related pages workflows;
- WIA changes;
- Level Strip experiments; and
- any other change not required by the nine-entry manifest above.

This does not judge those changes on their merits. It prevents a 3D downstream from
accidentally adopting features with separate goals, dependencies, and review history.

## PBR exclusion

OT-Dev pull request
[`#177`](https://github.com/OpenAnimationLibrary/opentoonz-dev/pull/177), represented
during this review by head commit
[`6337095b6c85d6af8f225e5eb30bb62cccf5d996`](https://github.com/OpenAnimationLibrary/opentoonz-dev/commit/6337095b6c85d6af8f225e5eb30bb62cccf5d996),
is open/unmerged and is **not** part of the OT3D seed.

Material 3D/PBR work is not required for the first interactive Viewer milestones. It
may be reconsidered as a separate pull request after the shared transform, camera,
depth, graphics-state, color-space, and resource-lifetime contracts exist. A future
review should take the current source diff, not assume this historical head is ready to
merge unchanged.

## What the seed proves—and what it does not

The selected commits provide groundwork for:

- bounded read-only GLB ingestion and immutable scene data;
- node, material-color, skin, and animation data recovery;
- pose evaluation;
- a CPU-rendered GLB Model FX path;
- 3D FX organization and a Three-Point Light FX; and
- initial Animation:Master exchange documentation/tests.

The seed does **not** provide a complete interactive 2D/3D Viewer, native DCC project
support, arbitrary 3D rotation of 2D planes, full PBR, or a supported production release.
Those are governed by [`ROADMAP.md`](ROADMAP.md) and
[`COMPATIBILITY.md`](COMPATIBILITY.md).

## Verification checklist

Before publishing the seeded `main`:

- confirm the official baseline and each source SHA exactly;
- confirm each migrated commit carries its source trailer and original author;
- compare each adaptation with the corresponding first-parent source diff;
- search the resulting tree/diff for the explicitly excluded feature names and files;
- configure/build the changed targets on the primary platform;
- run GLB loader, GLB Model FX, bridge, animation/pose, and Three-Point Light tests that
  exist in the selected changes;
- run relevant inherited OpenToonz tests and open/save a 2D-only smoke scene; and
- record any unavailable build dependency or platform check instead of marking it passed.

The first OT3D foundation pull request should update this record if the final seed needs
any additional source commit or adaptation.
