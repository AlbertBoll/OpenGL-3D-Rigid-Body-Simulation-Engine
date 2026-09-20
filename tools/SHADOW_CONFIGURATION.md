# Shadow quality and memory

All four graphical applications default to **High (4096 x 4096)**, preserving the
owner-approved Phase 03 visual tradeoff. The RigidBodySimulation **Shadows** menu
selects Low/Medium/High/Custom, a depth-image byte budget and optional lower-tier
fallback. Choices are queued for the next application frame before Update/extraction.

| Tier | Resolution | Six cascade layers | Six point faces | Depth-image total |
| --- | --- | --- | --- | --- |
| Low | 1024 | 24 MiB | 24 MiB | 48 MiB |
| Medium | 2048 | 96 MiB | 96 MiB | 192 MiB |
| High (default) | 4096 | 384 MiB | 384 MiB | 768 MiB |
| Custom | 1..8192 | 6 x width x height x 4 bytes | Same | 12 x resolution squared x 4 bytes |

`GENGINE_SHADOW_QUALITY=Low|Medium|High|Custom` selects a startup tier (lowercase
spellings are also accepted). The existing `GENGINE_SHADOW_RESOLUTION=1..8192`
explicitly selects Custom and takes precedence over a valid tier. Custom requires
that resolution variable. Invalid/empty/malformed values fail startup with a typed
diagnostic. Unset both variables for High. 8192 remains an explicit Custom choice;
no automatic path selects it. Existing capture/probe resolution overrides still work.

```powershell
$env:GENGINE_SHADOW_QUALITY = 'Medium'
python tools/run.py config=Release prj=RigidBodySimulation
Remove-Item Env:GENGINE_SHADOW_QUALITY
```

Normal consumers use native-free `Renderer/ShadowQuality.h`: `ShadowQualityDesc`,
`PlanShadowQuality`, `CreateShadowTargets`, and `BaseApp::RequestShadowQuality` /
`GetShadowQuality`. Requests belong to the application thread. BaseApp applies them
on the current main context before rendering, publishes a complete pair, then retires
the old pair. An unchanged resolution is allocation-free; a failed request retains
both previous storage identities, effective quality and memory values, with a typed
error available from `GetShadowQualityError`. A later explicit request can retry.
Successful replacement count excludes startup, no-ops and failed transactions.

The default byte budget is 3 GiB of depth-image payload. It is an admission limit
for the new pair, **not a physical VRAM limit**: runtime replacement temporarily
retains the old pair while creating the new pair. Budget rejection, hardware limits
and allocation failures do not silently lower quality. `ShadowFallback::LowerTiers`
is an explicit opt-in to try strictly smaller High/Medium/Low resolutions in order.
It reports requested/effective quality and the original failure. If every candidate
fails, the old pair remains intact. Startup defaults to strict failure; lower fallback
is never implied by choosing High.

Both targets currently allocate sized 32-bit float depth: six array layers and six
cube faces. This supersedes the old Phase 03 unsized point-format description;
current source has already migrated that storage. Estimates derive from the selected
resolution. Allocated depth bytes derive from queried texture extents and precision
after successful allocation. `physicalBytes` is explicitly unavailable because
portable OpenGL does not expose driver overhead, compression or residency. The menu
and logging identify the reported values as depth-image bytes, excluding overhead.
No texture queries occur per frame; accounting is captured at successful allocation.

The approved 2048 default rejection remains relevant: lower resolution can make
camera-following cascade boundaries more visible. High stays the default. Cascade
selection, bias, filtering, layer counts, light equations and shaders are unchanged.
This phase does not claim to eliminate seams or improve frame timing.

Validation uses the maintained toolchain, C++23 and static CRT:

```powershell
python tools/test_frame_submission.py --configuration Debug --output logs/rendering/phase57/final/Debug --smoke
python tools/test_frame_submission.py --configuration Release --output logs/rendering/phase57/final/Release --smoke
```

The real-GL probe validates every tier, custom extents, malformed inputs, budget and
hardware limits, deterministic partial-allocation failure, explicit fallback, move
ownership and retirement, runtime queued changes/no-op/failure/retry, counters and
worker rejection. A frozen two-caster directional/point-light scene is captured per
tier in `shadow-reference-*.ppm` (64x64 linear RGBA8, current driver). High must equal
the approved explicit 4096 factory reference byte-for-byte on the same GPU. Existing
nearest-occluder contact tests and the complete submission suite remain mandatory.
`shadow-application.log` exercises actual BaseApp frame transitions and context shutdown.
The historical Phase 03 runner remains historical evidence, not the current gate.
