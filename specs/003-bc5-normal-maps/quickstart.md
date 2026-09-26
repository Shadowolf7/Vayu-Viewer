# Quickstart: BC5 Normal Map Encoding

Validation scenarios that prove the feature end-to-end. Prerequisites, commands, expected outcomes. Full tech detail in `research.md`, `data-model.md`, `contracts/`.

> **Constitution (non-negotiable)**: Never build or launch without explicit turn-by-turn instruction. Pre-build guard: `pgrep -x vayu-bin || pgrep -x vayu`.

## Scenario 1 — Compressor emits BC5 linear for the Normal job (unit/probe, no build of viewer)

**Setup**: `build-Linux-ninja-perf` configured (per `vayu-build` skill). No viewer process running.

**Commands**:
```
./scripts/safe-build.sh cmake --build build-Linux-ninja-perf --config Release --target llimage-tests    # only on explicit instruction
# probe test<20>: existing probe harness covering block compression; drive a 3/4-ch normal-pattern input through EVayuTextureJob::Normal
```

**Expected**: compressed output header = `mFormat = BC5`, `is_srgb = false`, 2 components, RGTC2 block layout; analyzer/reference decoding reproduces XY; reconstructed `z = sqrt(max(0, 1-x²-y²))` within tolerance. Color job output unaffected.

## Scenario 2 — Cache writes normal entries under a role-qualified name

**Setup**: build installed; texture cache writable at `~/.vayu/cache/bccache/`.

**Commands**:
```
VAYU_DUMP_DIR=/tmp/vayu/dump ./vayu-bin   # only on explicit instruction
# load the reproduction vehicle; quit viewer
./scripts/perf/analyze_vayu_dump.py <dump dir> --filter job=normal
```

**Expected**:
- Normal-slot texture(s) present as `<uuid>.bc5` with header BC5 + linear.
- Legacy normals with informative alpha (any texel < 255) are NOT BC5 — they remain color-path entries with alpha preserved (FR-003a), and this is expected.
- Color entries byte-identical vs a pre-change build capture at same preset (SC-004) — compare a control texture's dumps.
- A UUID previously cached as color that later binds as normal: a `<uuid>.bc5` file appears; the `<uuid>.bc` color entry is not deleted.

## Scenario 3 — In-world visual (primary acceptance, SC-001/SC-002)

**Setup**: build installed; machine quiescent; the reproduction vehicle ("known-problematic PBR object") available.

**Commands**: launch viewer (explicit instruction only), teleport to the vehicle, screenshot its painted panels from multiple distances; walk the same camera path before and after the change.

**Expected**: no repeating dot/dimple grid at any distance on either glTF or legacy-normal-mapped panels; rivets/seams read correctly (no inversion); mixed-material scene shows no new artifacts (SC-005).

## Scenario 4 — Same UUID in two roles

**Setup**: find/assemble an asset used as both a diffuse and a normal across two objects.

**Commands**: load both objects; inspect `bccache/` for both filenames.

**Expected**: both cache files exist and neither overwrites the other (FR-007); both surfaces render plausibly.

## Contract References

- Cache naming: [contracts/cache-role-keying.md](contracts/cache-role-keying.md)
- Shader reconstruct: [contracts/shader-normal-reconstruction.md](contracts/shader-normal-reconstruction.md)
- Entities/invariants: [data-model.md](data-model.md)
- Decisions: [research.md](research.md)