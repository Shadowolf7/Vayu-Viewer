# Data Model: BC5 Normal Map Encoding

Entities and contracts for the feature, built from spec.md FR-001…FR-007.

## Entities

### Texture Role (`EVayuTextureJob` — existing enum, extended usage)

| Field | Description |
|-------|-------------|
| `Normal` | Texture is bound to a normal slot (glTF `mNormalTexture` or legacy `setNormalMap`). glTF: always BC5 linear, 2-channel. Legacy: BC5 linear ONLY when alpha is exact-255 (trivial — `255/255 = 1.0`, bit-identical to BC5's implicit alpha); informative alpha (any texel < 255) → fall back to color path, alpha preserved. |
| `MetallicRoughness` | Existing PBR linear job (unchanged). |
| `Albedo` / `Emissive` / `SingleChannelMask` / `Default` | Existing jobs (unchanged). |

**State transitions**: role is asserted once at bind time. Role is *persistent* once asserted (a texture is not silently demoted to `Default` on refetch — FR-001a). A texture may receive roles from multiple bound materials; the *fetch request* carries the role of the current requesting bind.

### Cache Entry Key — `(texture UUID × format/role)`

Previously keyed by `(UUID, discard)`, single file per UUID. Now the format/role dimension is added to the *filename portion* so both roles coexist on disk:

| Key component | Color path | Normal path |
|---------------|-----------|-------------|
| Partition subdir | `bccache/<first-hex>/` | same |
| Filename | `<uuid>.bc` | `<uuid>.bc5` |
| Discard-level variant | `<uuid>_<discard>.bc` | `<uuid>_<discard>.bc5` |

**Validation rule (FR-007)**: on read, the store's header `mFormat` must match the role-derived expected format for the requesting `EVayuTextureJob`. Mismatch → cache miss, fall through to re-encode. 

### Cache Entry Header (`VayuBCCacheEntryHeader` — existing struct)

Recorded unchanged (already contains `mFormat`, `mPreset`, `mIsMask`, `mDiscardLevel`, `mMipLevels`, `mWidth`, `mHeight`, `mComponents`, GL formats). `mFormat` gains `EVayuBlockCompressionFormat::BC5` for normal roles. **No schema change** — `kFormatVersion` stays 4.

### Block-Compressed Result (`VayuBlockCompressionResult` — existing)

Now produced by the Normal job with:
- `mFormat = BC5`
- `mComponents = 2` (R,G of tangent normal; B is reconstructed)
- `mIsMask = false`
- `is_srgb = false` (linear)

## Relationships

```
Material slot (glTF or legacy)
   │  binds UUID with a role
   ▼
LLViewerFetchedTexture  ──role──▶  createRequest(..., job)
   │                                    │
   ▼                                    ▼
Fetch worker (mTextureJob) ──job──▶ decodeImage ──▶ LLImageRaw.setTextureJob
                                            │
                                            ▼
                                   VayuImageBlockCompressor::encode(Normal)
                                            │
    glTF: 3/4ch → 2ch RG; BC5 linear (is_srgb=false)
    legacy: alpha == 255 everywhere? → 2ch RG; BC5 linear
            alpha informative?          → skip Normal path → color encode (Default job)
                                            │
                                            ▼
                                    cached as <uuid>.bc5 (mFormat=BC5, is_srgb=false)
```

Read side:
```
Fetch worker cache-hit path
   │  Normal role: probe <uuid>.bc5, then <uuid>.bc
   │  other roles: probe <uuid>.bc only
   ▼
entry found?  mFormat matches expected?
   ├── yes ──▶ serve result (no decode)
   └── no  ──▶ cache miss → fetch+decode → format resolves at encode → write under the
               key that matches the resolved format (BC5 → .bc5; color fallback → .bc)
```

## Invariants

1. `kFormatVersion == 4` — never bumped for this feature.
2. Color-path output is byte-identical to today (SC-004) — extension change only on the normal key.
3. A normal map is never gamma-transformed (FR-003).
4. Two roles of one UUID never overwrite each other's cache file (FR-007).
5. Non-normal textures never take the BC5 path (FR-004).