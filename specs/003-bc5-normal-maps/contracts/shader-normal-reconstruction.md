# Contract: Shader Normal Reconstruction

**Status**: Design contract | **Owner**: normal-sampling shaders in `indra/newview/app_settings/shaders/`

## Purpose

BC5 stores only tangent-space X (R) and Y (G); the shader must reconstruct Z before the mikktspace tangent-basis transform. One canonical snippet across all normal-map sampling sites.

## Canonical Reconstruction

```glsl
vec4 raw = texture(sampler, uv);     // full sample — keeps .a where a site reads it
vec3 vNt = raw.rgb * 2.0 - 1.0;
vNt.z = sqrt(max(0.0, 1.0 - dot(vNt.xy, vNt.xy)));
```

- `sampler` = the bound normal-map sampler (`bumpMap`, `normalmapN`, etc., per shader).
- `uv` = the normal `vec2` texcoord in use (`normal_texcoord.xy`, `vary_texcoord1.xy`, …).
- The `max(0.0, …)` clamp guarantees no NaN for over-length XY (defensive; correctly-authored unit normals never trip it).
- Downstream code (`.x*vT + .y*vB + .z*vN` then `normalize`) is unchanged.
- Sites that read alpha (legacy gloss: `glossiness *= vNt.a`) use `raw.a`. For BC5 (2-channel) `raw.a == 1.0`; the source value it replaced was exactly `255/255 = 1.0` — bit-identical. No gloss handling change.
- **Reconstruction is UNCONDITIONAL** at every normal-map sampling site, including the informative-alpha legacy normals that stay on the color pipeline (BC1/BC7, stored B preserved). `sqrt` equals stored B for unit-length normals (the DXT5nm convention) and is *more* accurate than BC1's 5-bit blue. Same trade already accepted for PBR.

## Application Rules

1. Apply **only** where a normal-map texture is decoded into tangent space. Do NOT touch sites that decode sun/dir/vector-0 or generate normals (`deferredUtil.glsl`, `dofCombineF`, `globalF`, `postDeferredF`, `screenSpaceReflUtil`, `shadowUtil`, `avatarV`, `normgenV`). Verify each grep hit before editing.
2. For legacy-material shaders with gloss-in-alpha (`materialF.glsl:149-151`, `materialIndexedF.glsl:228-231`): reconstruct X/Y/Z as above and KEEP the `glossiness *= vNt.a` line untouched. It is a provable no-op for both (a) BC5 textures — a 2-channel sample returns `.a = 1.0` — and (b) exact-255 sources — `255/255 = 1.0`. DECIDED: only exact-255 legacy normal maps go BC5; legacy normals with informative alpha (any texel < 255) stay on the color pipeline with alpha preserved, so the same pos she line continues to carry real gloss for those. No alpha decision is left for implementation.
3. Water normal maps (`waterF.glsl`, `underWaterF.glsl`) are NOT material-slot normals — do not change unless water normals are explicitly given the Normal role (out of scope → verify-only).

## Touchpoint Inventory (verified against repo at plan time)

| Shader | Site | Change |
|--------|------|--------|
| class1/deferred/pbropaqueF.glsl | :93 | reconstruct |
| class2/deferred/pbralphaF.glsl | :179 | reconstruct |
| class1/deferred/pbropaqueIndexedF.glsl | :221 (sample_normal caller) | reconstruct |
| class1/deferred/materialIndexedF.glsl | :228-231 | reconstruct xyz; keep `.a` line (no-op) |
| class3/deferred/materialF.glsl | :149-151 | reconstruct xyz; keep `.a` line (no-op) |
| class1/deferred/bumpF.glsl | :58 | reconstruct |
| class3/environment/waterF.glsl | (render path only) | no change (verify-only) |
| class3/environment/underWaterF.glsl | :68-70 | no change (verify-only) |

## Verification

- Visual: no dimples; detail orientation correct; gloss behavior preserved for informative-alpha legacy normals (unchanged), and exact-255 legacy normals (no-op by construction).
- Automated: no automated shader test exists; verify via capture + `analyze_vayu_dump.py` that BC5 entries are emitted, and rely on visual before/after for shader-correctness.