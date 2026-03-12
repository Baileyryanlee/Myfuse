# FuseCBridge Implementation Reference

Related docs: [FuseArchitectureOverview](./FuseArchitectureOverview.md), [FuseCoreSystem](./FuseCoreSystem.md), [FuseMaterials](./FuseMaterials.md).

## Repository path anchors (Shipwright-root)
- C bridge header: `soh/soh/Enhancements/Fuse/FuseCBridge.h`
- Core calls used by the bridge: `soh/soh/Enhancements/Fuse/Fuse.cpp`

## Purpose
- `FuseCBridge.h` is the C ABI surface for Fuse calls used from C translation units.
- Header exports C-compatible material IDs and helper functions, while keeping C++ internals hidden.

## Exported ABI surface

### `extern "C"` guard
- Header wraps declarations in `#ifdef __cplusplus extern "C" { ... }`.
- This preserves C linkage names when included from C++.

### Types
- `FuseMaterialId` (`typedef enum`) with fixed values:
  - `FUSE_MATERIAL_NONE = 0`
  - `FUSE_MATERIAL_ROCK = 1`
  - `FUSE_MATERIAL_DEKU_NUT = 2`
  - `FUSE_MATERIAL_FROZEN_SHARD = 3`
  - `FUSE_MATERIAL_STICK = 4`
  - `FUSE_MATERIAL_BOMB = 5`
  - `FUSE_MATERIAL_KEESE_EYE = 6`
  - `FUSE_MATERIAL_FIRE_JELLY = 7`

### Functions
- `s32 Fuse_ShieldHasExplosion(PlayState* play, s32* outMaterialId, s32* outDurabilityCur, s32* outDurabilityMax, u8* outLevel);`
  - Behavior (by naming/signature): query whether active shield fuse has explosion and return material/durability/modifier level through out-pointers.
  - TODO (verify): inspect implementation/callers for exact return semantics and required null checks.
- `void Fuse_ShieldTriggerExplosion(PlayState* play, s32 materialId, u8 level, const Vec3f* pos);`
  - Behavior (by naming/signature): trigger shield-linked explosion at `pos` with specified material/level.
  - TODO (verify): inspect implementation for ownership/null/side-effect details.
- `void Fuse_AddMaterialById(s32 materialId, s32 amount);`
  - Behavior (by naming/signature): increase material inventory by numeric ID.
  - TODO (verify): inspect implementation for clamping and invalid-ID handling.

## Must remain stable
- Keep `extern "C"` wrappers intact for C++ inclusion.
- Keep `#include "z64.h"` because `PlayState`, `Vec3f`, and integer typedefs come from it.
- Preserve function signatures and parameter order for ABI compatibility.
- Keep material numeric IDs aligned with `MaterialId` and save data expectations.

## Calling convention / ownership notes
- All pointers are caller-owned (header exposes no allocator/free pair).
- Out-parameter ownership stays with caller.
- TODO (verify): null-pointer behavior for each exported function in implementation.

## CVars referenced in this file
- None.

## Debug log tags/prefixes in this file
- None in header (implementation may log elsewhere).
