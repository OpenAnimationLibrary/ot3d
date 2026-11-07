# Image Based Lighting FX

Add **3D → Image Based Lighting**. Connect a model or 3D node chain to **Source**
and an equirectangular panorama to **Environment**. The image illuminates diffuse
surfaces from every direction. The original palette/material supplies the surface
color. A uniform linear white panorama at intensity 100% gives a white Lambertian
surface unit radiance; a black image gives no environment light.

This is a separate control from [Environment Reflections](environment_reflections_fx.md).
IBL supplies diffuse illumination; Environment Reflections supplies reflected
imagery. Use either independently, or connect both to the same panorama. Chain
Three-Point Light to add explicit lights and specular highlights. Their master
intensity controls only that rig. Multiple IBL nodes add their contributions.
Diffuse lighting is evaluated before reflection mixing, so the relative order of
one IBL and one reflection node does not change their material response.

**Intensity**, **Exposure** in stops, **Tint** and XYZ **Rotation** are animatable.
The node replaces the source's default light, avoiding an extra headlight. Zero
intensity therefore means zero IBL rather than a return to default illumination.
Disconnecting the Environment input bypasses the effect. Flat Render bypasses it;
Disable Default Lighting retains this explicitly authored illumination.

The image is evaluated as linear floating-point radiance, with HDR retained for
float output. It can come from an image level, animated sequence or finite FX.
The camera background stays transparent. Coordinates, sampling, alpha treatment,
limits (4 megapixels) and resizing guidance match Environment Reflections.

Diffuse irradiance uses a degree-two spherical harmonic approximation with
solid-angle weighting for latitude/longitude texels, following Ramamoorthi and
Hanrahan, [An Efficient Representation for Irradiance Environment Maps](https://graphics.stanford.edu/papers/envmap/),
SIGGRAPH 2001. A constant component is integrated separately so even a 1 × 1
constant map has the exact response. The image is projected once per cached map;
shading uses nine coefficients rather than tracing a hemisphere per sample.
Negative values from harmonic ringing are clamped to zero. Small, concentrated
sources can therefore differ from a fully sampled illumination integral.

For native A:M materials, the convolution also incorporates DiffuseFalloff's
`min(1, N.L / falloff)` response and AmbianceBlend's diffuse weight; the material's
existing ambiance and direct specular terms remain independent. GLB Model FX
uses its existing base color and faceted preview surface. This does not add a
complete glTF PBR implementation or promise A:M renderer parity.

This node does not generate shadows, occlusion, indirect bounces or glossy
specular IBL. Use the separate reflection node for sharp environment reflections
and Three-Point Light for controllable specular highlights.
