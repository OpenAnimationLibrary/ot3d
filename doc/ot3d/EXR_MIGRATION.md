# OT3D OpenEXR migration

## Library foundation

Selectively ports OT-Dev PR #196, merge commit
`f195223e58e77706d8e8126834a9f705c108a41a`, using its first-parent delta.
Source: https://github.com/OpenAnimationLibrary/opentoonz-dev/pull/196
Original merge author: Rodney <rodney.baker@gmail.com>.
Original source-file attribution and bundled notices are retained.

The pinned OpenEXR 3.5.0 / Imath 3.2.3 build replaces TinyEXR. The optional
`OT_USE_SYSTEM_OPENEXR=ON` path requires OpenEXR 3.5 or newer. Static dependency
builds keep portable application packages self-contained.

Adaptation: Windows keeps OT3D's existing GLB checks and adds only the EXR
check; the neighboring OT-Dev LUT test option is excluded. No other OT-Dev
feature or branch history is imported.

This is the file I/O foundation. Native multipart selection, retained-source
levels, EXR-aware Viewer inspection and deep-sample transport are separate
changes. Installing the complete library alone does not make the ordinary
single-RGBA raster pipeline preserve arbitrary channels or deep samples.

## Acceptance

Configure with `BUILD_EXR_TESTS=ON`, build `tiio_exr_test`, and run
`ctest --output-on-failure -R '^tiio_exr$'`. Verify an ordinary EXR round trip
and a portable package on Windows, macOS and Linux. Existing GLB tests remain
required. Test results from the source PR are provenance, not OT3D validation.

## AOV workflow

Selectively ports OT-Dev PR #197, merge commit
`0805d02e4b6e6e21aca8a82eca0e3765cb345063` (first-parent delta).
Source: https://github.com/OpenAnimationLibrary/opentoonz-dev/pull/197
Original merge author: Rodney <rodney.baker@gmail.com>.

The OpenEXR AOV Switchboard exposes flat multipart/layer/channel selection,
custom RGBA routing, scalar visualization and explicit scene-linear/raw-data
semantics. Deep parts are identified but rejected until native deep transport
exists. UINT samples are converted to float for the current raster path;
exact integer-ID transport remains work for the document/channel service.

Adaptation: keep EXR and GLB file controls without OT-Dev LUT controls or LUT
tests. Existing GLB validation is retained. Configure with
`BUILD_EXR_AOV_FX_TESTS=ON`, build `openexraovfx_test`, and run
`ctest --output-on-failure -R '^(tiio_exr|openexraovfx)$'`.
