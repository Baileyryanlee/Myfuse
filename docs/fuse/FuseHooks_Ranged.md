# FuseHooks_Ranged Hook Integration Layer

## Repository path anchors (Shipwright-root)
- Hook impl/header: `soh/soh/Enhancements/Fuse/Hooks/FuseHooks_Ranged.cpp`, `soh/soh/Enhancements/Fuse/Hooks/FuseHooks_Ranged.h`
- Key actor overlay touched by Fuse beam behavior: `soh/src/overlays/actors/ovl_En_Fuse_Beam/`

## 1) Purpose & Scope

`FuseHooks_Ranged.cpp` is the ranged hook bridge for Fuse projectile flows. It handles projectile-time behavior for ranged slots, including bow arrows, slingshot seeds, and hookshot events.

This file is **not** the core Fuse state machine and does not define material databases or global fuse rules; it routes events into `Fuse::*` / `Fuse_*` helpers and applies ranged-specific glue logic (slot selection, projectile fire flags, hit/surface routing).

It is also separate from object/melee hook handling (`FuseHooks_Objects.cpp`).

## 2) Hook Entry Points

Externally visible functions defined in this file:

### Actor/projectile hit routing
- `extern "C" void Fuse_OnRangedHitActor(PlayState* play, RangedFuseSlotId slot, Actor* victim, const Vec3f* impactPos)`
  - Runs actor-hit modifier logic for ranged slots (attack bonus, explosion, freeze/shatter, stun, burn), then marks/clears ranged-hit state.
  - Called by projectile/hookshot entry points in this file (not shown as an engine patch site here).

### Arrow/slingshot lifecycle hooks
- `extern "C" void FuseHooks_OnArrowProjectileFired(PlayState* play, int32_t isSeed)`
  - Fire-time hook. Chooses slingshot slot when `isSeed != 0`, otherwise arrow slot.
  - Commits queued fuse selection at projectile fire (`Fuse::CommitQueuedRangedFuse`) and logs knockback status.
  - Slingshot special-case: if no queued fuse/durability, clears active ranged fuse (`NoQueuedOnFire`) instead of committing.

- `extern "C" void FuseHooks_OnArrowProjectileSpawned(PlayState* play, Actor* projectile, int32_t isSeed)`
  - Spawn-time hook for `ACTOR_EN_ARROW` only.
  - Applies Burn projectile fire setup:
    - arrows: `EnArrow_SetLitByFire`
    - slingshot seeds: `EnArrow_SetFireDmgFlagsOnly`

- `extern "C" void FuseHooks_OnRangedProjectileHit(PlayState* play, Actor* projectile, Actor* victim, Vec3f* impactPos, int32_t isSeed)`
  - Actor-hit hook for arrow/seed projectiles.
  - Marks projectile fire state (`Fuse::TryMarkRangedProjectileAsFire`), calls `Fuse_OnRangedHitActor`, then finalizes (`Fuse::OnRangedProjectileHitFinalize`).

- `extern "C" void FuseHooks_OnRangedProjectileHitSurface(PlayState* play, Actor* projectile, Vec3f* impactPos, int32_t isSeed)`
  - Surface-hit hook for arrow/seed projectiles.
  - Commits queued fuse at impact, marks projectile fire state for BG hit, runs `HandleRangedSurfaceHit`, marks resolved, and finalizes.

### Hookshot entries (same ranged layer)
- `extern "C" void FuseHooks_OnHookshotShotStarted(PlayState* play)`
  - Delegates to `Fuse::OnHookshotShotStarted` (**defined in `Fuse.cpp`**).

- `extern "C" void FuseHooks_OnHookshotEnemyHit(PlayState* play, Actor* victim, Vec3f* impactPos)`
  - Commits queued hookshot fuse, logs knockback status, runs `Fuse_OnRangedHitActor`, then finalizes.

- `extern "C" void FuseHooks_OnHookshotSurfaceHit(PlayState* play, Vec3f* impactPos)`
  - Commits queued hookshot fuse, runs surface-hit handling, marks resolved, finalizes.

- `extern "C" void FuseHooks_OnHookshotLatched(PlayState* play)`
  - Commits queued hookshot fuse, logs knockback status, finalizes.

- `extern "C" void FuseHooks_OnHookshotRetracted(PlayState* play)`
- `extern "C" void FuseHooks_OnHookshotKilled(PlayState* play)`
  - Both delegate to `Fuse::OnHookshotRetractedOrKilled` (**defined in `Fuse.cpp`**).

> Note: this file shape indicates hook callback surfaces, but exact engine patch-site locations are not declared here.

## 3) Projectile Identification

- Bow vs slingshot is selected via `isSeed` in all arrow-projectile entry points.
  - `isSeed == 0` => `RangedFuseSlot::Arrows` / `RANGED_FUSE_SLOT_ARROWS`
  - `isSeed != 0` => `RangedFuseSlot::Slingshot` / `RANGED_FUSE_SLOT_SLINGSHOT`
- Both flows require projectile actor ID `ACTOR_EN_ARROW` in spawn logic.
- This implies slingshot seeds use the `En_Arrow` actor pathway in this hook layer, with behavior differentiated by `isSeed` and different fire helper call (`SetFireDmgFlagsOnly` instead of `SetLitByFire`).

## 4) Fuse Material Binding

- **Queue commit at fire-time**:
  - `FuseHooks_OnArrowProjectileFired` commits queued slot fuse (`Fuse::CommitQueuedRangedFuse`) when firing.
  - Slingshot checks queued material + queued durability first via `Fuse_GetRangedQueuedStatus`; if missing/depleted, active fuse is cleared instead of committed.

- **Queue commit at some impact-time events**:
  - `FuseHooks_OnRangedProjectileHitSurface` also commits queued fuse before applying surface-hit effects.
  - Hookshot enemy/surface/latch entries commit queued hookshot fuse at those events.

- **Active fuse reads for modifier application**:
  - Hit/surface logic fetches active slot fuse via `Fuse_GetRangedFuseStatus` before processing modifiers.

- **Refund/cancel handling visible here**:
  - No explicit “refund material to inventory” operation is present in this file.
  - The explicit cancel-like path shown is slingshot `NoQueuedOnFire`, which clears active slot state when no valid queued fuse exists at fire time.

## 5) Modifier Integration

### Seek
- No Seek-specific logic appears in this file.

### Burn
- **Spawn-time projectile fire setup** (`FuseHooks_OnArrowProjectileSpawned`):
  - Requires active material with Burn level > 0.
  - Arrow slot uses `EnArrow_SetLitByFire` (visibly lit fire arrow behavior).
  - Slingshot slot uses `EnArrow_SetFireDmgFlagsOnly` (damage flags without forcing full lit-arrow params).
- **Actor-hit burn application** (`Fuse_OnRangedHitActor`):
  - If material Burn modifier exists, calls `Fuse::ApplyBurn` (**defined in `Fuse.cpp`**).
- **Hit-time fire state bridge**:
  - `Fuse::TryMarkRangedProjectileAsFire` is called on actor and surface hit paths before handling/finalize (**defined in `Fuse.cpp`**).

### Explosion
- **Surface hit** (`HandleRangedSurfaceHit`):
  - Reads Explosion level via `Fuse::GetMaterialModifierLevel`.
  - Optionally finds nearby bombable actor (`Fuse_FindNearbyBombable`) within `kBombableAssistRadius` (120.0f), adjusts explosion anchor, and calls `Fuse_TriggerExplosion`.
- **Actor hit** (`Fuse_OnRangedHitActor`):
  - Explosion gated by victim eligibility and immunity checks.
  - Skips immune victims via `Fuse_ShouldSkipExplosionVictim` -> `Fuse_IsExplosionImmuneVictim`.
  - Triggers explosion for enemy/bombable victims with adjusted bombable anchor when needed.

### Attack bonus (material attack power)
- **Actor hit** (`Fuse_OnRangedHitActor`):
  - For non-hookshot slots only, applies `Fuse::GetMaterialAttackBonus(materialId)` by temporarily overriding `victim->colChkInfo.damage`, calling `Actor_ApplyDamage`, and restoring prior damage.

### Freeze + Freeze-shatter
- **Actor hit** (`Fuse_OnRangedHitActor`):
  - Early shatter attempt via `Fuse::TryFreezeShatter`.
  - If victim already frozen (`Fuse::IsFuseFrozen` or `victim->freezeTimer > 0`), retries shatter path and exits.
  - Otherwise queues freeze via `Fuse::QueueSwordFreeze` when Freeze modifier exists.

### Stun
- **Actor hit** (`Fuse_OnRangedHitActor`):
  - If Stun modifier exists, triggers deku-nut effect at impact using `Fuse_TriggerDekuNutAtPos`.

### Knockback
- `LogRangedKnockbackStatus` reads/logs Knockback level on fire/latch/enemy-hit events.
- Actual knockback application is not implemented here (explicit TODO: no victim actor available for that path).

## 6) Durability Interaction

- This file does **not** directly call a durability drain API.
- It gates behavior on current durability (`Fuse_GetRangedFuseStatus` / queued status checks), but drain/consumption appears delegated.
- The explicit end-of-hit delegate is `Fuse::OnRangedProjectileHitFinalize(...)` (**defined in `Fuse.cpp`**), called after actor hits, surface hits, and hookshot hit/latch events.
- Therefore, in this hook file, durability consumption timing is effectively “post hit handling/finalization call,” but the actual decrement conditions are defined outside this file.

## 7) Vanilla Preservation

Visible safeguards that avoid broad vanilla breakage:

- **Projectile-type guard**: spawn-time fuse logic only runs for `ACTOR_EN_ARROW`.
- **No always-on slingshot fire**:
  - Burn is required and durability must be > 0 before setting any fire flags.
  - Slingshot uses `EnArrow_SetFireDmgFlagsOnly`, which avoids forcing full lit-arrow params path used for normal fire arrows.
- **Explosion target filtering**:
  - No explosion on player category victims.
  - Immune victim skip path (`Fuse_IsExplosionImmuneVictim`) avoids invalid explosion damage behavior for excluded actors.
- **Bombable-assisted placement**:
  - Explosion location is adjusted through bombable-anchor helpers rather than blindly exploding at raw contact point.
- **Hookshot damage safety**:
  - Attack-bonus direct damage override is skipped for hookshot slot.

About torch/web interactions:
- This file does not include explicit torch/web actor-special-case code.
- Preservation appears indirect through use of vanilla arrow helpers (`EnArrow_SetLitByFire` / `EnArrow_SetFireDmgFlagsOnly`) and slot-conditional Burn gating.

About vanilla bombs:
- This file does not spawn/modify bomb actors directly; explosion behavior is injected via `Fuse_TriggerExplosion` calls with target filtering/assist logic.

## 8) Debug Signals

Log prefix used in this file:
- `[FuseDBG]`

Observed categories/messages:
- `RangedSurfaceHit` — surface-impact slot/material/durability snapshot.
- `ExplodeAssist` — nearby bombable candidate chosen and adjusted anchor.
- `Explode` / `ExplodeSkip` — explosion trigger details or immunity skip.
- `RangedHit` — actor-hit slot/material/victim metadata.
- `AtkBonusApply` — material attack bonus override application.
- `RangedKnockback` / `RangedKnockbackTODO` — knockback level logging and TODO status.
- `BurnLitArrow` / `BurnLitSeed` — Burn modifier projectile fire-flag application.
- `ArrowHealthProbe` — temporary fire-arrow victim health/damage probe log in actor-hit path.
