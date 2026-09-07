# FuseState Implementation Reference

Related docs: [FuseArchitectureOverview](./FuseArchitectureOverview.md), [FuseCoreSystem](./FuseCoreSystem.md), [FuseMaterials](./FuseMaterials.md).

## Repository path anchors (Shipwright-root)
- Save/runtime state: `soh/soh/Enhancements/Fuse/FuseState.cpp`, `soh/soh/Enhancements/Fuse/FuseState.h`

## Purpose / scope
- `FuseState` is the state-truth + persistence boundary for Fuse slots and material inventory.
- Runtime reads/writes happen through:
  - `FuseSaveData` (slot containers + active-slot lookup helpers).
  - `FuseRuntimeState` (non-save transient state, hammer swing flags, last held action).
  - `FusePersistence::*` helpers (SaveContext and `SaveManager` serialization).

## State ownership and sync boundaries
- SaveContext sword-only legacy path (`FuseSwordSaveState`): `ReadSwordStateFromContext`, `WriteSwordStateToContext`, `ApplySwordStateFromContext`.
- SaveManager path (`enhancements.fuse`): full sword/shield/boomerang/hammer slots via `LoadFuseStateFromManager` and `SaveFuseStateToManager`.
- SaveManager material inventory path (`enhancements.fuse.materials`): `LoadMaterialInventoryFromManager`, `SaveMaterialInventoryToManager`.
- Runtime-only (not directly serialized here): `FuseRuntimeState::{hammerDrainedThisSwing, hammerHitActorThisSwing, hammerSwingId, lastHeldItemAction, lastEvent}`.

## Slot model and identifiers
- Sword equip mapping:
  - `SwordSlotKey::{Kokiri, Master, Biggoron}`.
  - Resolved by `IsSwordEquipValue` + `SwordSlotKeyFromEquipValue`.
- Shield equip mapping:
  - `ShieldSlotKey::{Deku, Hylian, Mirror}`.
  - Stored in `FuseSaveData::swordSlots` with offset `kShieldSlotOffset`.
- Ranged enum in this file only: `RangedFuseSlot::{Arrows, Slingshot, Hookshot}`.
- Boomerang / hammer:
  - boomerang stored as `FuseSaveData::boomerangSlot`.
  - hammer runtime slot via `FuseRuntimeState::hammerSlot` and persisted as separate manager struct key (`kHammerSlotKey`).

## Invariants backed by code
- `NormalizeState(FuseSwordSaveState&)`:
  - clamps durability fields to non-negative.
  - clamps current durability to `[0, durabilityMax]` when max > 0.
  - if `isFused == false`, force `materialId=None`, zero durability, clear explicit-cur + legacy value.
- `NormalizeSlot(SwordFuseSlot&)`:
  - clamps durability non-negative.
  - calls `ResetToUnfused()` if `materialId==None` OR `durabilityMax<=0` OR `durabilityCur<=0`.
  - otherwise clamps current to max.
- Legacy material range check: `IsNoneMaterialId(int)` treats out-of-range IDs and `-1` as none.

## Load/save lifecycle
- Sword SaveContext lifecycle:
  - initialize empty via `ClearedSwordState`.
  - snapshot runtime via `BuildRuntimeSwordState`.
  - read from save fields via `ReadSwordStateFromContext`.
  - write back via `WriteSwordStateToContext`.
  - apply to runtime via `ApplySwordStateFromContext` (`Fuse_ClearSavedSwordFuse` vs `Fuse_ApplySavedSwordFuse`).
- SaveManager lifecycle:
  - current version constant: `kFuseSaveVersion`.
  - migrate legacy sword-only save (`matId` + `curDurability`) when `version < kSwordSlotsSaveVersion`.
  - migration target slot is currently equipped sword key.
  - boomerang/hammer default to unfused if absent in old versions.
- Material inventory lifecycle:
  - read count + array entries from `enhancements.fuse.materials`.
  - persist all provided `(MaterialId, qty)` entries.

## Save keys and section names
- `enhancements.fuse`
  - version key: `version`
  - sword slots array: `slots`
  - per-slot keys: `matId`, `curDurability`, `maxDurability`
  - shield array: `shieldSlots`
  - boomerang struct: `boomerang` (`matId`, `curDurability`, `maxDurability`)
  - hammer struct: `hammer` (`matId`, `curDurability`, `maxDurability`)
- `enhancements.fuse.materials`
  - `count`
  - `materials[]` entries: `id`, `qty`

## Debug log tags/prefixes in this file
- `[FuseDBG]`:
  - slot persistence (`Load`, `Save`, `LoadLegacy`)
  - shield equip restore
  - material inventory save/load (`MatLoad`, `MatSave`)
- `[FuseSave]`:
  - boomerang/hammer read/write snapshots

## Common failure modes and what to inspect
- Slot unexpectedly clears after load/save:
  - inspect `NormalizeSlot`, `SaveFuseStateToManager`, `LoadFuseStateFromManager`.
- Legacy migration puts material on wrong sword:
  - inspect `BuildSlotFromLegacy`, `SwordSlotKeyFromEquipValue`, migration block in `LoadFuseStateFromManager`.
- Sword appears unfused after load:
  - inspect `IsNoneMaterialId`, `ReadSwordStateFromContext`, `ApplySwordStateFromContext`.
- Durability mismatch between displayed and persisted values:
  - inspect `WriteSwordStateToContext`, `fuseSwordCurrentDurability`, `fuseSwordCurDurabilityPresent`, `legacyDurability` usage.
- TODO (verify): end-to-end call sites that invoke `ApplySwordStateFromContext` during load pipeline.
