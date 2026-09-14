# Shadow allocation configuration

All four graphical applications use 4096 x 4096 shadow targets by default.
Set `GENGINE_SHADOW_RESOLUTION` to a decimal integer from 1 through 8192 before
launch to select a different square resolution for both targets. For example:

```powershell
$env:GENGINE_SHADOW_RESOLUTION = '4096'
python tools/run.py config=Release prj=RigidBodySimulation
Remove-Item Env:GENGINE_SHADOW_RESOLUTION
```

`8192` explicitly restores the original dimensions. It is never selected
automatically. Invalid values stop startup with exit code 1 and a diagnostic.
The setting is read once during initialization; it does not implement runtime
quality tiers, caching, culling, or a different shadow technique.

| Resolution | Cascade array | Point cube | Estimated total |
| --- | --- | --- | --- |
| 2048 (explicit; rejected as default) | 6 layers, 96 MiB | 6 faces, about 96 MiB | about 192 MiB |
| 4096 (default) | 6 layers, 384 MiB | 6 faces, about 384 MiB | about 768 MiB |
| 8192 (original, explicit) | 6 layers, 1536 MiB | 6 faces, about 1536 MiB | about 3072 MiB / 3 GiB |

The cascade array retains `GL_DEPTH_COMPONENT32F`; its constructor argument of
5 still allocates 6 layers. Point shadows retain the unsized `GL_DEPTH_COMPONENT`
internal format with `GL_FLOAT` input and six cube faces. The point texture's
actual depth precision/storage is driver-selected. Estimates assume four bytes
per texel and exclude driver overhead, other render targets, and other assets.
The default uses approximately one quarter of the original estimated depth
storage. Human review rejected 2048 as the safe default: a visible shadow
boundary moves with forward/backward camera motion, and 4096 materially reduces
that artifact. This is consistent with existing abrupt cascade transitions and
coarser sampling becoming more visible at 2048. Final visual acceptance of the
remaining seam is a human-review gate; 4096 does not remove that architectural
limitation.

Both Debug and Release print the selected resolution, selection source, estimated
storage, each target's layers/faces and format, and framebuffer completeness.
Allocation/attachment errors stop startup with an actionable diagnostic and exit
code 1; partially created shadow objects are deleted before context teardown.

After building the graphical targets in each configuration, run:

```powershell
python tools/test_shadow_configuration.py probe Debug
python tools/test_shadow_configuration.py probe Release
python tools/test_shadow_configuration.py smoke Debug
python tools/test_shadow_configuration.py smoke Release
```

The probe uses the existing VS2022/MSVC 14.44.35207, SDK 10.0.26100.0, C++20,
and static CRT policy. It queries actual texture dimensions, clears complete
targets, verifies destruction, and injects allocation errors without exhausting
GPU memory. The smoke checks all four applications at defaults, both required
applications at 4096, and malformed/out-of-range settings. Logs and commands go
to `logs/rendering/phase03/`. GPU allocation and destruction stay on the creating
context thread. These are focused startup/resource checks, not image-quality or
performance benchmarks.
