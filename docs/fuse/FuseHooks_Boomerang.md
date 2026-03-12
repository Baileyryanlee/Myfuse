# FuseHooks_Boomerang Hook Integration Layer

## Repository path anchors (Shipwright-root)
- Hook impl/header: `soh/soh/Enhancements/Fuse/Hooks/FuseHooks_Boomerang.cpp`, `soh/soh/Enhancements/Fuse/Hooks/FuseHooks_Boomerang.h`

## 1) Purpose & Scope

`FuseHooks_Boomerang.cpp` is the boomerang-specific Fuse integration layer. It wires boomerang hit/surface events into Fuse modifier and durability systems without implementing the core Fuse data model.

This file is **not** the core Fuse material system and is **not** the ranged arrow/slingshot hook layer.

## 2) Hook Entry Points

Externally visible functions defined in this file:

- `extern "C" void FuseHooks_OnBoomerangHitActor(PlayState* play, Actor* victim, const Vec3f* impactPos)`
  - Actor-hit callback for boomerang impacts.
  - Runs boomerang material modifier behavior (attack bonus, explosion, freeze/shatter, knockback, stun, burn), then drains boomerang fuse durability.
  - This is a hook-facing callback surface (exact engine patch site is not declared in this file).

- `extern "C" void FuseHooks_OnBoomerangHitSurface(EnBoom* boom, PlayState* play, const Vec3f* hitPos)`
  - Surface/background-hit callback for boomerang impacts.
  - Handles explosion-on-surface behavior, including nearby bombable-assist anchoring and a per-boomerang cooldown guard, then drains durability.
  - Also exposed as a hook-facing callback surface; patch-site location is not declared here.

Internal helper:

- `ApplyBoomerangKnockback(...)` (anonymous namespace)
  - Applies knockback velocity/rotation to enemy actors when the boomerang material has a knockback modifier.
  - Not externally visible.

## 3) Boomerang Actor Identification

- Surface-hook signature takes `EnBoom* boom`, so boomerang identity is carried by the concrete boomerang actor pointer type.
- This file does **not** perform an explicit `actor->id == ...` check for boomerang before logic.
- No boomerang actor enum constant is referenced directly in this file.

## 4) Fuse Material Binding

- Material/effect state is read from boomerang-specific Fuse accessors on impact:
  - `Fuse::IsBoomerangFused()`
  - `Fuse::GetBoomerangMaterial()`
  - `Fuse::GetBoomerangFuseDurability()` / `Fuse::GetBoomerangFuseMaxDurability()`
- This file does not perform a "bind at throw time" operation; it assumes boomerang fuse state is already active and queries it when hit callbacks fire.
- Because both actor-hit and surface-hit callbacks query the same boomerang state, modifier behavior is preserved across the throw/flight/return lifecycle as long as boomerang fused state remains active.

## 5) Modifier Integration

### Knockback
- Logic runs on actor hit (`FuseHooks_OnBoomerangHitActor`) when `ModifierId::Knockback` level > 0.
- Uses internal `ApplyBoomerangKnockback` helper.
- Enemy-only gate uses `FuseBash_IsEnemyActor`.
- Knockback direction is computed from player -> victim vector and writes victim velocity/speed/rotation.

### Stun
- Logic runs on actor hit when `ModifierId::Stun` level > 0 and victim is an enemy.
- Calls `Fuse_EnqueuePendingStun(victim, stunLevel, materialId, ITEM_BOOMERANG)` (helper outside this file).

### Freeze
- Logic runs on actor hit in two phases:
  1. Early shatter attempt via `Fuse::TryFreezeShatter(...)`.
  2. If victim is already frozen (`Fuse::IsFuseFrozen(victim)` or `victim->freezeTimer > 0`), attempts shatter again and exits.
  3. Otherwise, if freeze modifier exists, queues freeze via `Fuse::QueueSwordFreeze(...)`.

### Burn
- Logic runs on actor hit when `ModifierId::Burn` level > 0.
- Calls `Fuse::ApplyBurn(play, victim, burnLevel, materialId, "boomerang", "Boomerang")`.

### Explosion
- Actor hit:
  - Reads explosion level with `Fuse::GetMaterialModifierLevel(materialId, FuseItemType::Boomerang, ModifierId::Explosion)`.
  - Skips immune victims via `Fuse_IsExplosionImmuneVictim(victim)`.
  - Triggers explosion for enemy or bombable victims via `Fuse_TriggerExplosion(...)`.
  - Uses bombable anchor adjustment (`Fuse_GetBombableAnchorPos`, `Fuse_AdjustExplosionPosForBombable`) for bombable targets.
- Surface hit:
  - Runs only when boomerang is fused, material def exists, and explosion level > 0.
  - Optionally finds nearby bombable via `Fuse_FindNearbyBombable(...)` and re-anchors explosion.
  - Triggers `Fuse_TriggerExplosion(...)` with source label `"BoomerangBG"`.

### Attack Bonus (non-enum material property)
- On actor hit with durability > 0, gets `Fuse::GetMaterialAttackBonus(materialId)`.
- If bonus > 0, temporarily overrides `victim->colChkInfo.damage`, calls `Actor_ApplyDamage(victim)`, then restores prior damage value.

### Seek
- No seek-specific logic is present in this file.

### Calls into `Fuse.cpp` (as referenced through `Fuse::` in this hook file)
- `Fuse::IsBoomerangFused`
- `Fuse::GetBoomerangMaterial`
- `Fuse::GetMaterialDef`
- `Fuse::GetMaterialModifierLevel`
- `Fuse::GetBoomerangFuseDurability`
- `Fuse::GetBoomerangFuseMaxDurability`
- `Fuse::Log`
- `Fuse::GetMaterialAttackBonus`
- `Fuse::TryFreezeShatter`
- `Fuse::DamageBoomerangFuseDurability`
- `Fuse::ApplyBurn`
- `Fuse::IsFuseFrozen`
- `Fuse::QueueSwordFreeze`

(These are invoked from this hook file; their definitions are external to this file and part of Fuse core implementation in `Fuse.cpp`.)

## 6) Multi-Hit / Return Behavior

- Actor hits: no per-victim dedupe map exists in this file; each actor-hit callback processes logic and drains durability once.
- Surface hits: duplicate rapid re-triggering is throttled with `sBoomerangBgLastExplodeFrame[boom]` and `kBoomerangBgCooldownFrames = 12`.
  - This is keyed by boomerang actor pointer (`EnBoom*`) and frame count.
- Return path:
  - No boomerang return-state mutation is performed here.
  - Hook logic is event-driven on hit callbacks only, leaving return mechanics to vanilla boomerang logic.

Durability granularity visible here: drain occurs per processed hit event (actor hit or eligible surface hit), not once-per-throw.

## 7) Durability Interaction

- Durability drain is explicitly triggered in this file via:
  - `Fuse::DamageBoomerangFuseDurability(play, 1, "Boomerang hit")` in actor-hit flow.
  - `Fuse::DamageBoomerangFuseDurability(play, 1, "Boomerang hit surface")` in surface-hit flow.
- Actor-hit path is structured so freeze-shatter early returns still drain durability before return, and the non-early-return path drains at function end.
- Explosion/burn/stun/knockback success is not required for the final actor-hit drain call; impact processing itself leads to durability drain.

## 8) Vanilla Preservation

Visible safeguards in this file:

- Null guards (`!play`, `!victim`, `!boom`, `!hitPos`) prevent invalid hook-side execution.
- Hook logic is mostly Fuse-gated by `Fuse::IsBoomerangFused()`, so non-fused boomerang behavior is minimally touched.
- Surface explosion spam is limited by the 12-frame per-boomerang cooldown map.
- Explosion target filtering avoids indiscriminate triggers:
  - immune victim skip path,
  - actor-hit requirement of enemy or bombable actor,
  - optional bombable-assist anchoring instead of unfiltered arbitrary placement.
- No boomerang motion/return state writes are performed; this helps preserve vanilla boomerang return behavior and base collision flow.

## 9) Debug Signals

Log prefix used in this file:

- `[FuseDBG]`

Observed log message groups and subsystem meaning:

- `BoomerangHit` — actor-hit snapshot (material, level, victim, durability).
- `AtkBonusApply` — attack bonus damage override application on victim.
- `ExplosionProc` — explosion modifier proc intent on actor hit.
- `ExplodeSkip` — explosion skipped for immune victim class.
- `Explode` — actual explosion trigger details (actor or BG/candidate-assisted contexts).
- `ExplodeAssist` — nearby bombable assist detection and adjusted explosion anchor.

