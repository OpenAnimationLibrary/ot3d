# otlut Alpha - 2026-10-02

This is an early **otlut** command-line alpha published through the OT3D repository for testing.

Source project: https://github.com/OpenAnimationLibrary/otlut

## Included

- Windows x64 `otlut.exe`
- identity `.cube` LUT generation
- OpenToonz-compatible `3DMESH` `.3dl` LUT generation
- configurable LUT grid size
- configurable `.3dl` output bit depth
- sample 33 x 33 x 33 identity LUTs in both formats
- format compatibility documentation

## Example

```text
otlut --identity --output identity.cube --size 33
otlut --identity --output identity.3dl --size 33 --output-bit-depth 12
```

## OpenToonz .3dl compatibility

OpenToonz-compatible `.3dl` mesh sizes are currently:

`2, 3, 5, 9, 17, 33, 65, 129`

The especially useful production sizes are 17, 33 and 65.

## Current limitation

This alpha validates the standalone executable and LUT writers. It does **not yet derive a LUT from image pairs or image sequences**. Paired-image fitting is the next implementation stage.

## Alpha source revision

`OpenAnimationLibrary/otlut@59d426ae5371d09c70131b48829d0d225b902925`

Use this release for LUT writer and OpenToonz compatibility testing rather than production color work.
