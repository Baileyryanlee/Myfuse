# FuseModifiers Implementation Reference

Related docs: [FuseArchitectureOverview](./FuseArchitectureOverview.md), [FuseCoreSystem](./FuseCoreSystem.md), [FuseMaterials](./FuseMaterials.md), [FuseHooks_Objects](./FuseHooks_Objects.md), [FuseHooks_Ranged](./FuseHooks_Ranged.md), [FuseHooks_Boomerang](./FuseHooks_Boomerang.md).

## Repository path anchors (Shipwright-root)
- Modifier execution: `soh/soh/Enhancements/Fuse/FuseModifiers.cpp`, `soh/soh/Enhancements/Fuse/FuseModifiers.h`
- Related behavior rules: `soh/soh/Enhancements/Fuse/ShieldBashRules.cpp`, `soh/soh/Enhancements/Fuse/ShieldBashRules.h`

## Purpose / scope
- `FuseModifiers` defines the modifier ID enum and provides one helper API: `HasModifier`.
- Execution logic for effects (freeze/burn/explosion/seek/etc.) is **not** in this file; hooks/core query modifiers and execute behavior elsewhere.

## Modifier IDs defined here
- `ModifierId::{Hammerize, Stun, MegaStun, Freeze, Knockback, PoundUp, NegateKnockback, RangeUp, WideRange, Explosion, Seek, BashAttack, Burn}`.
- `ModifierSpec` pairs:
  - `id` (`ModifierId`)
  - `level` (`uint8_t`, comment says 1–3).

## Modifier pipeline responsibility in this file
- `HasModifier(const ModifierSpec* mods, size_t count, ModifierId id, uint8_t* outLevel)`:
  - null/empty guard (`!mods || count == 0` -> `false`).
  - linear scan for first matching `id`.
  - writes `outLevel` if provided.
  - returns `true` on match, else `false`.

## Execution timing / stacking / runtime state
- This file does **not** implement timing stages (spawn/update/hit).
- This file does **not** define stacking rules.
- This file does **not** define per-modifier runtime state structs.
- TODO (verify): inspect core/hook symbols for timing/stacking behavior:
  - `Fuse::GetMaterialModifierLevel`
  - `FuseHooks_OnSwordATCollision`
  - `FuseHooks_OnRangedProjectileHit`
  - `FuseHooks_OnBoomerangHitActor`

## Actor filtering and safety notes
- No actor filtering logic exists in this file.
- No whitelist/blacklist tables exist in this file.

## CVars referenced in this file
- None.

## Debug log tags/prefixes in this file
- None.
