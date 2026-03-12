# FuseVisuals Implementation Reference

Related docs: [FuseArchitectureOverview](./FuseArchitectureOverview.md), [FuseCoreSystem](./FuseCoreSystem.md), [FuseState](./FuseState.md).

## Repository path anchors (Shipwright-root)
- Visual implementation: `soh/soh/Enhancements/Fuse/Visuals/FuseVisual.cpp`, `soh/soh/Enhancements/Fuse/Visuals/FuseVisual.h`

## Purpose / scope
- `FuseVisual` draws fused attachment meshes on player left hand and shield.
- Current implementation only draws the rock display list (`gSilverRockDL`) from `OBJECT_GAMEPLAY_FIELD_KEEP`.

## Entry points
- C++: `FuseVisual::DrawLeftHandAttachments`, `FuseVisual::DrawShieldAttachments`.
- C bridge wrappers:
  - `FuseVisual_DrawLeftHandAttachments`
  - `FuseVisual_DrawShieldAttachments`

## Branch conditions and constraints
- Left hand draw requires all true:
  - `Fuse::IsEnabled()`
  - `Fuse::IsSwordFused()`
  - `Fuse::GetSwordMaterial() == MaterialId::Rock`
  - `Fuse::GetSwordFuseDurability() > 0`
- Shield draw requires all true:
  - `Fuse::IsEnabled()`
  - active shield slot material is `MaterialId::Rock`
  - active shield durability > 0
  - `player->rightHandType == PLAYER_MODELTYPE_RH_FF`
- Object/bank constraints:
  - ensures object loaded via `EnsureObjectLoaded` with spawn retries every `kObjectSpawnRetryFrames` (30 frames).
  - requires valid player segment restore pointer.

## Attachment transforms and order
- Age-specific defaults:
  - `kLeftHandAdult`, `kLeftHandChild`, `kShieldAdult`, `kShieldChild`.
- CVar override surface read by `ReadAttachmentTransform`.
- Transform order:
  - Left hand: `Matrix_Translate` -> `Matrix_RotateZYX` -> `Matrix_Scale` -> draw `gSilverRockDL`.
  - Shield: `Matrix_Put(&player->shieldMf)` -> `Matrix_Translate` -> `Matrix_RotateZYX` -> `Matrix_Scale` -> draw `gSilverRockDL`.

## CVar surface (with defaults and where used)
- Debug toggle:
  - `gFuse.Vis.DebugLog` (default `0`), used in `LogAttachmentTransform`.
- Left-hand adult (`GetLeftHandTransform`):
  - `gFuse.Vis.LeftHandAdult.OffsetX` (default `0.0`)
  - `gFuse.Vis.LeftHandAdult.OffsetY` (default `-80.0`)
  - `gFuse.Vis.LeftHandAdult.OffsetZ` (default `1200.0`)
  - `gFuse.Vis.LeftHandAdult.RotX` (default `0`)
  - `gFuse.Vis.LeftHandAdult.RotY` (default `0`)
  - `gFuse.Vis.LeftHandAdult.RotZ` (default `0`)
  - `gFuse.Vis.LeftHandAdult.Scale` (default `0.45`)
- Left-hand child (`GetLeftHandTransform`):
  - `gFuse.Vis.LeftHandChild.OffsetX` (default `0.0`)
  - `gFuse.Vis.LeftHandChild.OffsetY` (default `-70.0`)
  - `gFuse.Vis.LeftHandChild.OffsetZ` (default `1050.0`)
  - `gFuse.Vis.LeftHandChild.RotX` (default `0`)
  - `gFuse.Vis.LeftHandChild.RotY` (default `0`)
  - `gFuse.Vis.LeftHandChild.RotZ` (default `0`)
  - `gFuse.Vis.LeftHandChild.Scale` (default `0.40`)
- Shield adult (`GetShieldTransform`):
  - `gFuse.Vis.ShieldAdult.OffsetX` (default `0.0`)
  - `gFuse.Vis.ShieldAdult.OffsetY` (default `0.0`)
  - `gFuse.Vis.ShieldAdult.OffsetZ` (default `900.0`)
  - `gFuse.Vis.ShieldAdult.RotX` (default `0`)
  - `gFuse.Vis.ShieldAdult.RotY` (default `0`)
  - `gFuse.Vis.ShieldAdult.RotZ` (default `0`)
  - `gFuse.Vis.ShieldAdult.Scale` (default `0.55`)
- Shield child (`GetShieldTransform`):
  - `gFuse.Vis.ShieldChild.OffsetX` (default `0.0`)
  - `gFuse.Vis.ShieldChild.OffsetY` (default `0.0`)
  - `gFuse.Vis.ShieldChild.OffsetZ` (default `820.0`)
  - `gFuse.Vis.ShieldChild.RotX` (default `0`)
  - `gFuse.Vis.ShieldChild.RotY` (default `0`)
  - `gFuse.Vis.ShieldChild.RotZ` (default `0`)
  - `gFuse.Vis.ShieldChild.Scale` (default `0.50`)
- Scale clamp behavior:
  - read from `CVarGetFloat(scaleKey, default)` then clamped to `[0.01, 5.0]`.

## Debugging signals
- Log tags:
  - `[FuseDBG]` transform dump every 60 frames when debug CVar enabled.
  - `[FuseVisual]` object load/missing/spawn status.
- Quick alignment validation:
  - enable `gFuse.Vis.DebugLog` and verify offset/rot/scale logs while adjusting CVars.

## Known limitations
- Visual attachment currently material-specific (`MaterialId::Rock` only).
- No per-material display-list switching in this file.
