# Fuse Regression Checklist

Related docs: [FuseArchitectureOverview](./FuseArchitectureOverview.md), [FuseCoreSystem](./FuseCoreSystem.md), [FuseHooks_Objects](./FuseHooks_Objects.md), [FuseHooks_Ranged](./FuseHooks_Ranged.md), [FuseHooks_Boomerang](./FuseHooks_Boomerang.md), [FuseState](./FuseState.md), [FuseVisuals](./FuseVisuals.md).

## Repository path anchors (Shipwright-root)
- Fuse root: `soh/soh/Enhancements/Fuse/`
- Fuse top-level registration: `soh/soh/Enhancements/FuseSystem.cpp`
- Beam overlay: `soh/src/overlays/actors/ovl_En_Fuse_Beam/`

## 1) If you changed Hooks (Objects / Ranged / Boomerang)
- Verify sword fused hit applies expected effect per material (stun/freeze/burn/explosion/knockback).
- Hit a torch/web with burn-capable projectile and confirm ignition behavior.
- Hit bombable wall with explosion-capable material and confirm break.
- Compare bow vs slingshot behavior for same fused material.
- Fire hookshot with fused slot and confirm enemy/surface behavior remains stable.
- Boomerang actor hit: confirm one durability drain per processed hit.
- Boomerang surface hit: confirm cooldown prevents explosion spam.
- Watch logs for `[FuseDBG]` and hook-specific effect traces.

## 2) If you changed FuseState / Save behavior
- Fuse sword/shield/boomerang/hammer, save, reload, and verify exact material + durability restore.
- Switch equipped sword before save and confirm slot-specific persistence.
- Scene reload after fuse changes should not clear valid slots.
- Unfuse then save/load should remain unfused (`None`, 0/0 durability).
- Material inventory save/load should preserve quantities.
- Legacy migration path: load pre-slot save and verify active sword receives migrated fuse.
- Watch logs for `[FuseDBG] Load/Save/LoadLegacy`, `[FuseSave] ReadBoom/WriteBoom/ReadHammer/WriteHammer`.

## 3) If you changed Modifiers
- Validate each touched modifier on enemy hit and non-enemy hit cases.
- Verify combined modifiers on one material still all execute (e.g., effect + durability drain).
- Confirm freeze-then-shatter and burn-on-hit still trigger in expected sequence.
- Confirm knockback levels remain consistent across weapon types.
- Confirm no modifier effect runs when material is `None` or durability is zero.
- Watch logs for `[FuseDBG]` effect proc lines.

## 4) If you changed Materials table / IDs
- Verify every material appears in UI lists with expected name and quantity.
- Fuse each material once and confirm expected attack bonus/modifiers apply.
- Save/load after fusing multiple materials to ensure ID stability.
- Confirm C/C++ bridge material IDs still match internal `MaterialId` values.
- Confirm no crashes on invalid/removed IDs in old saves (if migration added).
- Watch logs for `[FuseDBG] MatLoad` / `[FuseDBG] MatSave` and invalid-material fallbacks.

## 5) If you changed UI (Pause / Ranged)
- Open pause modal and ranged hold menu in valid contexts; ensure they do not conflict.
- Ranged menu: hold to open, navigate, release to commit, verify selection applied.
- Ranged menu: stop aiming while open and confirm auto-close without bad state.
- Confirm D-pad/stick navigation repeat feels correct and does not leak inputs.
- Confirm selecting `NONE` properly clears existing fuse.
- Confirm list scroll/highlight alignment for long material lists.
- Watch logs for `[FuseDBG] RangedFuseMenu: Open/Select/Close`.

## 6) If you changed Visuals
- With rock-fused sword, verify left-hand attachment appears and tracks motion.
- With rock-fused shield, verify attachment appears only in supported right-hand model state.
- Test child/adult link transforms separately.
- Tune offset/rot/scale CVars and verify immediate alignment changes.
- Confirm missing object-bank scenarios recover after spawn retry window.
- Ensure non-rock materials do not incorrectly draw rock mesh.
- Watch logs for `[FuseVisual]` object load events and `[FuseDBG]` transform dumps.
