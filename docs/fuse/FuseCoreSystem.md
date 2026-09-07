# Fuse Core System (Fuse.cpp) Implementation Reference

## Repository path anchors (Shipwright-root)
- Core runtime: `soh/soh/Enhancements/Fuse/Fuse.cpp`
- Public API/types: `soh/soh/Enhancements/Fuse/Fuse.h`
- Top-level integration entry: `soh/soh/Enhancements/FuseSystem.cpp`

## 1) Purpose & Scope
- This document maps the **core Fuse runtime and state logic** implemented in `soh/soh/Enhancements/Fuse/Fuse.cpp`.
- Scope includes:
  - Core state + save/runtime coordination.
  - Fuse API behavior for sword/boomerang/hammer/ranged slots.
  - Durability, material inventory, ranged queue lifecycle, and status/modifier application.
  - CVar tuning/debug gates referenced in this TU.
- Explicitly out of scope:
  - UI/menu implementation.
  - Per-weapon hook installation files and object hook plumbing details outside this TU.

## 2) File ownership map
### What `Fuse.cpp` owns
- Module-local state containers for runtime effects, queueing, and temporary per-actor/per-projectile bookkeeping.
- Main `Fuse::` API implementation (enablement, material queries, fusion/clear ops, durability ops, ranged queue/commit/cancel flow, frame tick handlers).
- Shield C bridge helpers (`extern "C"`) that expose modifier checks/effects to non-C++ call sites.
- On-load and per-frame entrypoints (`Fuse::OnLoadGame`, `Fuse::OnGameFrameUpdate`).

### What `Fuse.cpp` delegates to
- `FuseMaterials` (`FuseMaterials::GetMaterialDef`, `GetMaterialDefs`) for material registry data (defined elsewhere).
- `FusePersistence` (`BuildRuntimeSwordState`, `WriteSwordStateToContext`, `ApplySwordStateFromContext`, constants) for persistence format/read-write flow (defined elsewhere).
- `FuseSaveData`/`FuseRuntimeState` methods from `FuseState` (`GetActive*Slot`, etc.) for slot routing semantics (defined elsewhere).
- `FuseHooks::RestoreSwordHitboxVanillaNow` for sword-hitbox restoration on sword fuse break (defined elsewhere).
- `FuseBash_*` helpers (enemy/boss/knockback policy) from `ShieldBashRules.h` (defined elsewhere).
- `SaveManager::Instance` for debug override serialization (`enhancements.fuse.debugOverrides`) (defined elsewhere).

## 3) State Model
### Module-local persistent/runtime state in this TU
- `gFuseSave` (`FuseSaveData`): core fused slot storage used by sword/boomerang/shield slot lookups.
- `gFuseRuntime` (`FuseRuntimeState`): runtime flags and hammer-specific active slot/tracking.
- Save-manager load flags and staged data:
  - `sSwordSlotsLoadedFromSaveManager`
  - `sHammerSlotLoadedFromSaveManager`
  - `sLoadedHammerSlot`
- Ranged lifecycle state:
  - `gRangedQueued[Arrows/Slingshot/Hookshot]`
  - `gRangedActive[Arrows/Slingshot/Hookshot]`
  - `kRangedSlots`
- Material inventory/runtime overrides:
  - `sMaterialInventory`, `sMaterialInventoryInitialized`
  - `sMaterialDebugOverrides`, `sUseDebugOverrides`
- Freeze/burn/shatter/seek/per-actor transient maps:
  - `sFuseFrozenTimers`, `sFuseFrozenOrigGravity`, `sFuseFrozenPos`, `sFuseFrozenPinned`
  - `sFreezeAppliedFrame`, `sFreezeShatterFrame`, `sFreezeLastShatterFrame`, `sFreezeNoReapplyUntilFrame`
  - `sFreezeShatterDamageVictim`, `sFreezeShatterDamageFrame`
  - `sShatterImpulseUntilFrame`, `sShatterImpulseDir`, `sShatterImpulseYaw`, `sShatterImpulseFlipped`
  - `sBurnStates`
  - `sSeekStates`
- Projectile and debug bookkeeping:
  - `sProjPrevPos` (projectile prior position cache for proximity sweeps)
  - `sHpOverrideApplied` (enemy HP override application tracking)
  - `gLastSwordBgExplodeFrame`, `gLastSwordActorExplodeFrame` (explosion throttles)

### Runtime structs declared in this TU
- `FuseSeekState`: homing acquisition/steering state (`acquireDelayFramesRemaining`, `targetActor`, logging flags, previous position, etc.).
- `FuseBurnState`: burn duration/tick/VFX state (`endFrame`, `nextTickFrame`, `ticksRemaining`, `tickDamage`, filter params).

### Important invariants / coupling visible in code
- A slot is treated fused only when `materialId != None` **and** current durability is positive.
- Durability setters clamp into `[0, 65535]`; current values are clamped against max when restoring/fusing.
- Ranged slots separate **queued** and **active** states; queued material consumption is refunded on cancel paths.
- Hookshot queued state uses `inFlight`/`hadSuccess`/`hitResolved` to avoid premature refund.
- Freeze reapply is gated by recent shatter/no-reapply windows; shatter and freeze systems share per-actor timestamps/maps.
- Sword restore path tracks `gFuseRuntime.swordFuseLoadedFromSave` to decide whether fuse op should initialize or preserve current durability.

## 4) Public API (grouped)

### A) Slot accessors / active slot selection
- `GetSwordSlots`, `ApplyLoadedSwordSlots`, `HasLoadedSwordSlots`: sword+shield slot load/apply state handoff.
- `GetActiveSwordSlot`, `GetActiveShieldSlot`, `GetActiveBoomerangSlot`, `GetActiveHammerSlot`: resolve active equipped slot.
- `GetBoomerangSlot`, `ApplyLoadedBoomerangSlot`: boomerang slot read/apply.
- `GetHammerSlot`, `ApplyLoadedHammerSlot`, `HasLoadedHammerSlot`, `GetLoadedHammerSlot`: hammer slot load/apply/queries.

### B) Fuse / clear operations per weapon type
- Sword: `IsSwordFused`, `GetSwordMaterial`, `FuseSwordWithMaterial`, `TryFuseSword`, `TryUnfuseSword`, `ClearSwordFuse`.
- Boomerang: `IsBoomerangFused`, `GetBoomerangMaterial`, `FuseBoomerangWithMaterial`, `TryFuseBoomerang`, `TryUnfuseBoomerang`, `ClearBoomerangFuse`.
- Hammer: `IsHammerFused`, `GetHammerMaterial`, `FuseHammerWithMaterial`, `TryFuseHammer`, `TryUnfuseHammer`, `ClearHammerFuse`.
- Ranged wrappers: `TryFuseArrows`, `TryFuseSlingshot`, `TryFuseHookshot`, `TryUnfuseArrows`, `TryUnfuseSlingshot`, `TryUnfuseHookshot`, `ClearArrowsFuse`, `ClearSlingshotFuse`, `ClearHookshotFuse`.

### C) Durability: base/max/current, damage, drain triggers
- Material durability baseline/override:
  - `GetMaterialBaseDurability`, `GetMaterialDurabilityOverride`, `GetMaterialEffectiveBaseDurability`.
- Per-weapon current/max getters/setters:
  - Sword: `GetSwordFuseDurability`, `GetSwordFuseMaxDurability`, `SetSwordFuseDurability`, `SetSwordFuseMaxDurability`.
  - Boomerang: `GetBoomerangFuseDurability`, `GetBoomerangFuseMaxDurability`, `SetBoomerangFuseDurability`, `SetBoomerangFuseMaxDurability`.
  - Hammer: `GetHammerFuseDurability`, `GetHammerFuseMaxDurability`, `SetHammerFuseDurability`, `SetHammerFuseMaxDurability`.
- Drain/break handlers:
  - `DamageSwordFuseDurability`, `DamageBoomerangFuseDurability`, `DamageHammerFuseDurability`.
  - `OnSwordFuseBroken`, `OnBoomerangFuseBroken`, `OnHammerFuseBroken`.
- Trigger sites in this TU:
  - Sword BG explosion path drains 1 durability in `TickSwordBgExplosions`.
  - Shield guard path drains via C bridge `Fuse_ShieldGuardDrain` (calls `DamageSwordFuseDurability(play, 1, "ShieldGuard")` on active shield slot).

### D) Material inventory: add/consume/queries
- Core inventory API:
  - `GetMaterialCount`, `SetMaterialCount`, `HasMaterial`, `AddMaterial`, `ConsumeMaterial`.
  - `GetCustomMaterialInventory`, `ApplyCustomMaterialInventory`, `ClearMaterialInventory`.
  - Back-compat convenience: `HasRockMaterial`, `GetRockCount`, `AwardRockMaterial`.
- Behavior split:
  - Vanilla-like materials (`DekuNut`, `Stick`, `Bomb`) proxy to vanilla ammo functions.
  - Custom materials live in `sMaterialInventory` map.

### E) Ranged queueing: queue/commit/cancel/refund
- `TryQueueRangedFuse`: validates material/slot state, consumes one material, handles swap refund semantics.
- `CommitQueuedRangedFuse`: transfers queued entry into active entry.
- `CancelQueuedRangedFuse_Refund`: cancels queued entry and refunds one material.
- `ClearQueuedRangedFuse_NoRefund`: despite name, implementation currently refunds then clears.
- `ClearActiveRangedFuse`: clears active state without refund.
- `MarkRangedHitResolved`: marks queued state as resolved/no refund path.
- `OnRangedProjectileHitFinalize`: decrements active durability by 1 then clears active state.
- Hookshot-specific queue lifecycle: `OnHookshotShotStarted`, `OnHookshotRetractedOrKilled`.

### F) Modifier application helpers (seek / burn / freeze / explosion)
- Freeze:
  - `QueueSwordFreeze`, `ProcessDeferredSwordFreezes`, `ResetSwordFreezeQueue`, `TryFreezeShatter`, `TryFreezeShatterWithDamage`, `WasFreezeShatterDamageAppliedThisFrame`, `IsFuseFrozen`.
- Burn:
  - `ApplyBurn`, plus frame ticking in `TickStatusEffects`.
- Seek:
  - `TickRangedProjectileSeek` (homing acquisition/steering), plus `Fuse_RegisterSeekCVars` and per-projectile `sSeekStates`.
- Explosion and melee entry processing:
  - `OnSwordMeleeHit`, `OnHammerMeleeHit`, `TickSwordBgExplosions`, `TickRangedProjectileBombableProximity`.
- Ranged fire tagging helper:
  - `TryMarkRangedProjectileAsFire`.

### G) Save sync helpers (equipped sword sync etc.)
- `Fuse_WriteSwordFuseToSave`: serializes current sword state via `FusePersistence`.
- `Fuse_ApplySavedSwordFuse`: validates + applies persisted sword material/durability.
- `Fuse_ClearSavedSwordFuse`: clears persisted sword fields and runtime sword fuse state.
- `OnLoadGame`: reinitializes runtime and applies save-manager/context-derived slot state.

## 5) CVars & tuning surface (referenced in this file only)

| CVar | Type | Default source | Used in |
|---|---|---|---|
| `gFuseLogDbg` | int | `CVarGetInteger(..., 0)` fallback | `Fuse_LogDbgEnabled` gate for `[FuseDBG]` logs |
| `gFuseLogMvp` | int | `CVarGetInteger(..., 0)` fallback | `Fuse_LogMvpEnabled` gate for `[FuseMVP]` logs |
| `gFuseSeekRadius` | float | registered `1500.0f` (`Fuse_RegisterSeekCVars`); read fallback `900.0f` | `TickRangedProjectileSeek` target radius |
| `gFuseSeekDotMin` | float | registered `0.60f`; read fallback `0.65f` | `TickRangedProjectileSeek` min forward-dot threshold |
| `gFuseSeekMaxTurnDeg` | float | registered `16.0f`; read fallback `6.0f` | `TickRangedProjectileSeek` max steering turn |
| `gFuseSeekTurnScaleFar` | float | registered `1.9f`; read fallback `0.4f` | `TickRangedProjectileSeek` far-distance steering scale |
| `gFuseSeekAcquireDelay` | int | registered `2`; read fallback `2` | `TickRangedProjectileSeek` delayed lock acquisition |
| `gFuseSeekDebug` | int | registered `0`; read fallback `0` | seek-specific debug logging in `TickRangedProjectileSeek` |
| `gFuseSeekDisableStop` | int | registered `0`; read fallback `0` | seek stop behavior toggle |
| `gFuseSeekStopGraceTicks` | int | registered `2` | stop hysteresis/grace logic in seek subsystem |
| `gFuse.DebugEnemyHpOverride.Enable` | int | get fallback `0` (not registered in this file) | `TryApplyEnemyHpOverride`, `OnGameFrameUpdate` gating |
| `gFuse.DebugEnemyHpOverride.Sticky` | int | get fallback `0` (not registered in this file) | `TryApplyEnemyHpOverride` sticky logging/reapply behavior |
| `gFuse.DebugEnemyHpOverride.Reset` | int | get fallback `0` (not registered in this file) | `OnGameFrameUpdate` one-shot reset (then `CVarSetInteger(..., 0)`) |
| `gFuse.DebugEnemyHpOverride.Keese` (+ `BigDekuBaba`, `DekuBaba`, `BlueTektite`, `RedTektite`, `Dinolfos`, `Lizalfos`, `Peahat`, `Wolfos`, `Stalfos`) | int | get fallback `0` (not registered here) | selected by `GetEnemyHpOverrideKey`; applied in `TryApplyEnemyHpOverride` |
| `CVAR_ENHANCEMENT("FuseDekuNutSpawn")` | int | `CVarGetInteger(..., 1)` fallback | `SpawnVanillaDekuNutFlash` gate for spawning vanilla deku-nut effect actor |

## 6) Execution flow (high-level)

### Load/reset flow
1. `OnLoadGame` clears runtime/transient maps, resets ranged queues, seek/burn/freeze/shatter state.
2. Ensures material inventory initialized.
3. Applies sword state either from SaveManager-preloaded slots or `FusePersistence::ApplySwordStateFromContext` fallback.

### Per-frame update flow
`OnGameFrameUpdate` runs, in order:
1. HP override reset check (`gFuse.DebugEnemyHpOverride.Reset`).
2. `TickFuseFrozenTimers`.
3. `TickStatusEffects` (burn ticks, burn VFX refresh).
4. `TickShatterImpulse`.
5. `ProcessPendingStuns`.
6. `UpdateRangedFuseLifecycle` (aiming/held-item transition cancel/refund behavior).
7. `TickSwordBgExplosions`.
8. `TickRangedProjectileSeek`.
9. `TickRangedProjectileBombableProximity`.
10. Optional enemy HP override scan/apply when enabled.

### Applying fuse and consuming inventory
- `TryFuseSword` / `TryFuseBoomerang` / `TryFuseHammer`:
  - Validate enabled state/material definition/not already fused.
  - Consume one material.
  - Set material and max/current durability (from effective base durability).
- `TryFuseArrows` / `TryFuseSlingshot` / `TryFuseHookshot` route to ranged queueing (`TryQueueRangedFuse`).

### Durability drain/break behavior
- Weapon damage functions subtract amount, clamp at zero, and invoke corresponding `On*FuseBroken` handler at zero.
- Sword break path also clears saved sword fuse and restores vanilla sword hitbox through `FuseHooks::RestoreSwordHitboxVanillaNow`.
- Ranged active durability is decremented at projectile-hit finalize and active state is then cleared.

### Modifier application behavior visible in this TU
- Melee (sword/hammer) hit entrypoints:
  - Optional explosion trigger against eligible victim types.
  - Frozen target path attempts shatter-damage flow first.
  - Non-frozen path applies stun/burn/freeze/knockback based on material modifiers.
- Freeze system:
  - Tracks no-reapply windows and recent shatter windows; deferred sword freeze queue applies on later frame slot.
- Burn system:
  - `ApplyBurn` initializes per-victim burn state; `TickStatusEffects` applies periodic damage/VFX until expiry.
- Seek system:
  - `TickRangedProjectileSeek` registers/reads seek CVars, acquires best target by radius/dot tests, and steers projectiles incrementally.

### TODOs / unclear from this TU alone
- `// TODO: revert burn duration to 60 frames after validation.` tied to `kBurnDurationFrames` constant.
- Hook installation/call wiring into these entrypoints is outside this TU (symbols exist, implementations elsewhere).

## 7) Debug signals
- Primary log tags:
  - `[FuseDBG]`: verbose debug-level diagnostics (seek, ranged queueing, freeze/burn ticks, explosions, knockback, overrides).
  - `[FuseMVP]`: high-level gameplay/action traces (material consumption, fuse breaks, major events).
- Representative subsystem-tagged strings in messages:
  - Seek: `SeekCVars`, `SeekAcquire`, `SeekSteer`, `SeekStop`, `SeekNoTarget`.
  - Ranged queue/active: `RangedQueue*`, `RangedCommit*`, `RangedRefundQueued`, `RangedHitFinalize`, `RangedBusy`.
  - Freeze/shatter: `FreezeApply`, `FreezeSkip:*`, `FreezeShatter*`, `ShatterImpulse*`.
  - Burn: `BurnApply*`, `BurnTick`, `BurnVfx*`, `BurnFire*`.
  - Explosion/impact: `Explode*`, `SwordBGLineHit`, `RangedBombableSweep`.
  - Stun/deku nut: `dekunut_*`, `DekuNut*`.
