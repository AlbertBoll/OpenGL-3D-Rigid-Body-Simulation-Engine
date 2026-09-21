# Local PBR shader validation

Run from the repository root with the maintained VS2022/v143 toolchain:

```powershell
python tools/test_pbr_shader.py --configuration Debug --output logs/rendering/phase61/pbr-check/Debug
python tools/test_pbr_shader.py --configuration Release --output logs/rendering/phase61/pbr-check/Release
```

The runner builds maintained consumers, runs the production submission/scheduler
suite and checks all four graphical applications for startup, responsiveness and
native close. It compiles the probe against the matching engine library and records
source/staged shader equality, commands, exits and binary hashes. Use a fresh output
directory. --focused-only reuses binaries and omits build/integration/milestone coverage.

The hidden GPU fixture uses actual production fragments through Shader::Create,
two owning-context lifetimes, 64x64 linear RGBA32F/R32I attachments, controlled
nearest/repeat 2x2 float textures and camera (0,0,3). Logs record GPU/driver/GLSL.
All native access is test-only; resources retire before context teardown.

- Flat and both signed tangent X/Y directions, including rotated UVs, verify the
  existing negative-bitangent convention with component tolerance 0.0002.
- Independent UV-gradient/cotangent axes and baseUV versus tiledUV derivatives
  agree on orthogonal charts at positive tilings (1,1), (2,2), (2,0.2), (3,2).
  This bounded check does not certify skewed/mirrored/degenerate charts or arbitrary
  smooth-surface projection. The production frame is the existing normalized
  tangent plus negative cross-product bitangent, not a new general cotangent frame.
- Each PBR channel varies independently. Full lighting output must match an
  independently selected constant texel at the same pixel within 0.0005 per RGB
  component. X/Y tiling and repeat are covered, with a distinct untiled reference.
- PBR and point-light visual programs expose exactly location-0 FragColor, with no
  entity output/uniform. Color-only routing preserves an attached integer sentinel.
  Dedicated picking writes three supplied IDs and preserves the background; the
  integration suite also covers packed/instanced picking.
- Six private shader-copy controls restore undecoded normals, untiled lookup,
  constant IDs, positive bitangent, an extra green flip, or an ignored roughness factor. Each must fail its
  corresponding GPU oracle. Production shader files are never mutated.
- A separate application probe compiles the actual RigidBodySimulation authoring
  source, with private copies of its packaged textures replaced by constant PNGs.
  Both real normal requests must preserve (128,128,255) as approximately
  (0.501961,0.501961,1), then (0.003922,0.003922,1) after signed decode. Metallic,
  roughness and AO preserve their distinct 64/255, 128/255 and 192/255 inputs, and albedo retains an sRGB internal format.
  It reads the actual submitted texture and sampler; a standalone float texture
  cannot detect erroneous sRGB defaults. Six application-source controls restore
  sRGB normal/scalar requests, force linear albedo, swap scalar sources, or undo
  the floor or sphere factor; each must be rejected.

Normal and scalar requests explicitly select Linear; albedo/color defaults remain
SRGB. No gamma, exposure, lighting, green, bitangent-sign, shadow or picking
scheduling change is made. The normal PNGs contain vector data; image orientation
flipping is not a normal-component flip. A source tile neutral can be (126,126,255),
so small encoded bias is distinct from the much larger erroneous sRGB transfer.

Automated application smokes establish lifecycle behavior only. Separate standard
RigidBodySimulation visual/interaction/native-close evidence is required for this
revision, alongside controlled captures that isolate normal and scalar sampling.
No golden-image refresh, timing benchmark or cross-GPU byte identity is implied.

## Floor hotspot revision

The application regression uses distinct linear scalar values (metallic 64, roughness 128, AO 192), rejects swapped floor map sources, and observes the actual floor packed lanes: tiling (2,2), dielectric F0 0.08, and roughnessScale 0.5. The sphere authors 0.28 and the recovered wall authors 0.5; unauthored materials retain the GLSL factor default 1.0. Initialized material uniforms must be replaced by the packed adapter when explicitly authored.

The GPU roughness fixture compares the complete BRDF against independently scaled map data and measures the production GGX half-width at raw roughness 0.28332952, with scale 1 and 0.5. The latter narrows the lobe without changing normal decoding, transfer functions, lights, or TBN signs. Defect controls also remove the factor and restore the floor factor to 1.

For visual evidence, use matched camera, target size, model matrices, source/assets, light settings and GPU; discard unmatched captures. Distinguish the old sRGB normal tilt/dark-region defect from the roughness-driven floor gloss change. The explicit floor factor is an authored material adjustment, not a claim that linear roughness sampling was incorrect.

## Rust sphere revision

The recovered owner sphere factor was 0.9. Its replacement, 0.28, rounds a measured 0.27515 fit of normalized GGX half-width to the Phase60 roughness-transfer response in high-metallic patches (metallic >= 0.95). A deterministic stratified 256x256 source grid avoids periodic image aliasing; source values, GPU samples and matched captures are retained in logs/rendering/phase61/revision5/. This is explicit material authoring over linear maps, not restoration of scalar sRGB transfer. A single material factor also changes rough orange texels and cannot reproduce a nonlinear transfer everywhere.

Actual-application regression observes both sphere (tiling1, F0=0.8, factor0.28) and accepted floor (tiling2, F0=0.08, factor0.5) packed lanes, with default1 on unauthored materials and the recovered wall factor0.5 preserved. A private source control restores sphere factor0.9 and must fail. The GPU fixture checks representative metal-patch roughness values74/255,78/255,95/255: final lobe half-width must be within0.002 radians of legacy and reduce width error by at least90% relative to the recovered0.9 factor.

The recovered owner wall already authors roughnessScale=0.5 with tiling (2,0.2). The actual-app observer checks that separate material without classifying it as the floor. The floor reversal control changes only the floor declaration.
