# EXR document inspection and deep-sample foundation

This draft builds on the native OpenEXR library migration. It establishes an
independent C++ document/transport boundary for an EXR-aware Viewer, Python
bindings and future deep-aware FX. It does not enable deep loading in existing
single-RGBA level/FX decoders.

## Implemented

- `inspectDocument()` uses the official OpenEXR multipart API to list part
  names/storage, data/display windows, named channels, sample types and channel
  sampling. A native-path stream supports Unicode paths on Windows. This is
  metadata inspection, not pixel decoding or an implicit flatten operation.
- `exr_inspect FILE.exr` exposes that inspection independently of the GUI.
- Deep transport retains per-pixel sample counts, arbitrary channel names,
  offset windows and typed channel buffers. HALF expands exactly into float;
  UINT stays uint32, preserving IDs beyond float's exact-integer range.
- Count/prefix planning checks pixel, sample, per-pixel, channel and buffer
  budgets before a future decoder allocates sample planes.
- Explicit point-sample flattening keeps the source unchanged, sorts by depth
  with deterministic ties, and composites premultiplied scene-linear samples.
  Negative and HDR colors survive. Volume intervals are rejected explicitly.

## Still required

1. Consolidate the retained-level and AOV decoders with this document boundary.
2. Connect an on-demand EXR Inspector dock and Viewer layer/channel selectors,
   raw pixel readout, exposure/OCIO, window overlays and A/B comparisons.
   mrv2 is a workflow reference, not an imported runtime dependency.
3. Decode deep scanline and deep tiled samples into this transport with bounded
   allocations, cancellation and cache accounting. Never silently flatten on load.
4. Display point/volume samples, counts and depth intervals in the Inspector;
   document raw versus display-transformed values.
5. Define volume sample integration, deep merge/holdout and flatten boundaries
   in the FX graph, then implement native deep EXR output.

Point-preview output/sorting buffers have a separate bounded budget from the
retained deep-source buffers. OpenEXR's internal allocations, file parsing,
threading and application-wide cache accounting still need integration review.
The document inspector has initial file/part/channel limits; it does not claim
complete hostile-file resource isolation. No deep reader/writer or native GUI
support is advertised by this first slice.

## Validation

Build standalone with `cmake -S toonz/sources/exr -B exr-build`, build, then run
`ctest --test-dir exr-build --output-on-failure`. Uses the same pinned official
OpenEXR/Imath foundation as the application. The fixture writes a real deep
EXR, discovers its metadata through a Unicode filename, and checks typed
transport, zero-sample pixels, offset coordinates, HDR/signed values, sample
limits, malformed files and explicit volume rejection. The sample buffers
in the transport test are constructed directly; deep pixel decoding is pending.
