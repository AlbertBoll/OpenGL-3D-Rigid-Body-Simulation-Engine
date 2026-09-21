# Lighting and material reference validation

Run from the repository root with the maintained VS2022/v143 toolchain:

```powershell
python tools/test_lighting_reference.py --configuration Debug --output logs/rendering/phase62/final/Debug
python tools/test_lighting_reference.py --configuration Release --output logs/rendering/phase62/final/Release --compare logs/rendering/phase62/final/Debug/run-0
```

The runner builds the maintained affected consumers, runs the submission/scheduler
regressions and four graphical application lifecycle smokes, then captures three
fresh-process sets of frozen 256x256 production images. `--probe` is development
reuse only; it makes no build/integration claim. Use an unused output directory.
Metadata records GPU/vendor/driver/GLSL, cameras, dimensions, attachment encoding,
material texels, sampler policy, lights, shadow dimensions and draw counts. Source,
probe and evidence identity belong to the current checkpoint spec and results.
PPM captures are losslessly encoded to PNG for `reference-gallery.html`.

The target is linear RGBA8 with framebuffer sRGB disabled. The existing shader
encodes RGB with power 1/1.8; these are **not** physical linear radiance or sRGB
reference files. No exposure/gamma/BRDF change is made to directional or point
lighting. No Physics, time animation, jitter or MSAA enters these frozen scenes.

## Declared tolerances

Before measurement, all same-GPU/process and Debug/Release image pairs require:

- maximum error <= 2 eight-bit codes in each RGB channel;
- mean absolute error <= 0.1 codes in each RGB channel;
- minimum SSIM >= 0.9995 across every non-overlapping 8x8 luminance tile.

Luminance uses Rec.709 weights on the stored encoded RGB for a perceptual check;
SSIM uses population variance, C1=0.01 squared and C2=0.03 squared. Luminance RMSE
is reported too. All pixels, including clear background and silhouette edges,
participate. A private +8-code brightness control must fail. Same frozen frame
repeat and serial/instanced color/depth equivalence additionally require exact
identity. The spot angular/range oracle and pre-gamma light addition allow
2.5 codes per channel, accounting for inversion of two RGBA8 measurements. Alpha
source-over allows 1.5 codes. Straight/premultiplied RGB parity allows one code
for their different fixed-function blend rounding. These thresholds are not fitted after measurement.

## Coverage

- Point and directional lights use existing production equations. Ambient/point/
  directional images must also match the approved Phase 61 shader exactly when
  linked through the same current engine and fixture (not a historical engine rebuild). Spot uses one
  typed world-space ray, position, linear color/intensity, finite range and ordered
  cone half-angles. Its contribution uses the existing BRDF and 80/distance scale,
  multiplied by squared max(0,1-distance/range) and linear interpolation between
  outer/inner cone cosines. Equal cone angles are a hard edge. At the light's
  coincident origin (distance <= 0.0001) contribution is zero because no ray exists.
- A front-facing plate compares spot output with independent CPU geometry,
  attenuation and inversion of separately rendered point/ambient images. Soft
  cone, hard edge, range cutoff, rotation, disabled contribution, mixed lights and
  retained immutable frames are checked. Spot shadows and additional spot lights
  fail with UnsupportedLights before image/upload/draw mutation. Three private
  shader controls remove spot contribution, cone weighting or range weighting;
  each must fail its corresponding image oracle.
- Opaque, checker-masked, straight-alpha and premultiplied materials use actual
  SceneRenderResources and FrameSubmission; alpha coverage and source-over are
  checked. The full submission suite additionally verifies matching color,
  picking and directional/point shadow discard and transparent depth policy.
- Explicit material preparation fallback is exercised for stale texture and
  stale sampler identities. Failure without opt-in, preserved failure reason and
  exact image equality against the explicitly authored fallback pair are checked.
  The real engine checker texture and default sampler are retained through draw.
- Repeated spheres and a receiver validate directional cascades/point shadows,
  disabled-shadow differences, serial/broadcast versus instanced/culled exact
  color and depth. All shadow layer readbacks are captured. Existing shadow
  contact, cache invalidation, culling and High-versus-4096 tests remain required.

## Limits

The compact reference scene uses 256 shadow maps to make the layer images cheap
and repeatable; it is not a recommendation to lower the application's approved
4096 default. Cascade discontinuities, bias and filtering remain unchanged.
Point lighting retains its historical attenuation/range semantics. The bounded
spot prerequisite does not implement spot shadows or multiple spot lights.

This is a recorded-driver baseline, not cross-GPU byte certification. Geometry
shaders, OpenGL 4.5 facilities and the maintained Windows runtime are required.
Hardware/driver changes require a separately reviewed reference comparison;
never silently refresh expected images or loosen tolerances. Accepted vendor
warnings remain baseline-owned. No performance, sanitizer or unrelated Physics
claim is made. All GPU work/readback/destruction stays on the owning context.
