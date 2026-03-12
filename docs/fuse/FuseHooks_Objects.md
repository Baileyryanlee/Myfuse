# FuseHooks_Objects Hook Integration Layer

## Repository path anchors (Shipwright-root)
- Hook impl/header: `soh/soh/Enhancements/Fuse/Hooks/FuseHooks_Objects.cpp`, `soh/soh/Enhancements/Fuse/Hooks/FuseHooks_Objects.h`

## 1) Purpose & Scope

`FuseHooks_Objects.cpp` is the **object/actor-facing hook layer** for Fuse sword/hammer behavior. It intercepts player collision/update moments and applies Fuse-side effects (durability drain, modifier-driven explosion/freeze/stun triggers, material drops) while trying to preserve native actor behavior. It is not the core fuse state machine; most game-effect logic is delegated into `Fuse::*` and `Fuse_*` helpers.

Key characteristics visible in this file:
- Maintains hook-local transient state (`g*` statics) for per-frame cooldowns, throw tracking, and one-time awards.
- Captures/restores vanilla sword toucher flags so hammer-style flag injection is temporary.
- Routes hit events into Fuse core functions such as `Fuse::OnSwordMeleeHit`, `Fuse::OnHammerMeleeHit`, `Fuse_TriggerExplosion`, `Fuse_TriggerMegaStun`, and durability APIs.

## 2) Hook Entry Points

Externally visible functions defined in this file:

### `extern "C" void FuseHooks_OnSwordATCollision(...)`
- Purpose: AT-vs-AC collision hook for player melee quads.
- Expected trigger: when sword/hammer AT collider collides with an AC target (actor or collider info).
- Actions:
  - Validates Fuse enabled + collider belongs to player melee quad.
  - Applies same-frame victim cooldown to prevent duplicate drains on one victim.
  - Resolves base melee damage and calls `Fuse::OnSwordMeleeHit` / `Fuse::OnHammerMeleeHit`.
  - Applies material attack bonus (with `AC_HARD`, `BUMP_NO_DAMAGE`, and freeze-shatter skip guards).
  - Drains durability (`Fuse::DamageSwordFuseDurability` / `Fuse::DamageHammerFuseDurability`).
- Call source note: declared `extern "C"` and shaped like a low-level collision callback; this strongly indicates an engine/patch callback surface (exact patch site is not shown in this file).

### `void FuseHooks::RestoreSwordHitboxVanillaNow(PlayState* play)`
- Purpose: immediate restoration of cached vanilla sword hitbox damage flags.
- Expected trigger: fuse-break or similar transition back to vanilla behavior.
- Call source note: hook-layer utility; in this file it does not call into an engine site directly.

### `void FuseHooks::OnSwordFuseBroken(PlayState* play)`
- Purpose: hook entry that forwards to `Fuse::OnSwordFuseBroken(play)`.
- Expected trigger: sword fuse break event.
- Call source note: explicitly delegates to `Fuse::OnSwordFuseBroken` (**defined in `Fuse.cpp`**).

### `void FuseHooks::OnLoadGame_RestoreObjects()`
- Purpose: reset all hook-local object/impact tracking state on load.
- Expected trigger: load/restore boundary.
- Call source note: dispatcher/caller is external to this file.

### `void FuseHooks::OnFrame_Objects_Pre(PlayState* play)`
- Purpose: pre-object-frame hook.
- Expected trigger: per-frame pre object update.
- Actions: restores stale hammerized flags, captures current sword flags, runs thrown-rock/material award checks, conditionally applies hammer flags near liftable rocks.
- Call source note: dispatcher/caller is external to this file.

### `void FuseHooks::OnFrame_Objects_Post(PlayState* play)`
- Purpose: post-object-frame hook.
- Expected trigger: per-frame post object update.
- Actions: calls `Fuse::ProcessDeferredSwordFreezes(play)`.
- Call source note: dispatcher/caller is external to this file.

### `void FuseHooks::OnPlayerUpdate(PlayState* play)`
- Purpose: player-update hook for hammer/sword impact-side effects and BG explosions.
- Expected trigger: each player update while Fuse is enabled.
- Actions:
  - Hammer swing lifecycle tracking.
  - Ground-impact handling (pound/mega-stun/explosion + drain once per swing).
  - Hammer BG impact checks via line test and optional explosion.
  - Sword BG impact explosion and cooldowned drain path when explosion modifier is present.
- Call source note: dispatcher/caller is external to this file.

## 3) Actor / Object Categories Intercepted

### Explicit actor IDs
- `ACTOR_EN_ISHI` (`kLiftableRockActorId`): liftable rock proximity/throw tracking and hammer-flag gating.
- `ACTOR_EN_BW` (`kTorchSlugActorId`): torch slug death check for `FireJelly` award.
- `ACTOR_EN_FZ`: freezard despawn/zero-health check for `FrozenShard` award.

### Actor category traversal/filtering
- Iterates all categories `for (cat = 0; cat < ACTORCAT_MAX; cat++)` when:
  - validating whether tracked actors still exist in actor lists,
  - searching nearby liftable rocks,
  - scanning for award-eligible freezards/torch slugs.

### Key filtering conditions
- Fuse global gate: nearly every public hook returns early if `!Fuse::IsEnabled()`.
- Input safety gate in `OnFrame_Objects_Pre`: blocks processing during disabled input/cutscene/talking/dead/ocarina states.
- Rock gating for hammerized sword flags requires all of:
  - Hammerize level > 0,
  - sword currently fused,
  - player currently swinging,
  - qualifying liftable rock near player (`<= 140` XZ radius and `|dy| <= 90`, excluding currently-held rock).
- AT collision path only accepts colliders that are one of player melee quad colliders.

## 4) Explosion Modifier Integration

Explosion behavior is injected in three places:

1. **Hammer ground-impact animation frames** (`OnPlayerUpdate` + `IsHammerGroundImpactFrame`):
   - If hammer fused and hammer material has `ModifierId::Explosion`, triggers `Fuse_TriggerExplosion(..., "HammerBG")` around an impact-forward offset.

2. **Hammer BG impact from AT flags + line test**:
   - On AT_HIT/AT_BOUNCED without actor-hit this swing, performs `BgCheck_EntityLineTest1` between hammer trail points.
   - If fused and explosion modifier present, triggers `Fuse_TriggerExplosion(..., "HammerBG")` at hit or fallback position.

3. **Sword BG impact path**:
   - On AT_HIT/AT_BOUNCED and sword explosion modifier present, triggers `Fuse_TriggerExplosion(..., "SwordBG")` at forward offset.
   - Uses 10-frame cooldown (`gLastSwordBgExplodeFrame`) to avoid repeat triggering.

Bombable integration:
- Before explosion calls, tries `Fuse_FindNearbyBombable`, then adjusts to bombable anchor using `Fuse_GetBombableAnchorPos` + `Fuse_AdjustExplosionPosForBombable`.

Vanilla-preservation behavior visible here:
- Hammerize implementation ORs hammer flags into cached base sword flags, then restores original flags, rather than permanently replacing weapon flags.
- Hammer-flag application is proximity-gated to rocks to avoid broad enemy damage behavior changes.
- Explosion trigger paths are conditional on fuse/material modifier state; no modifier means vanilla non-explosion flow.

## 5) Burn / Fire / Torch / Web / Ice Handling

### Fire / torch-adjacent handling present
- `ACTOR_EN_BW` (torch slug) death detection grants `MaterialId::FireJelly` once per actor (60% chance) using tracked set cleanup.

### Ice handling present
- `ACTOR_EN_FZ` (freezard) despawn or health==0 detection grants `MaterialId::FrozenShard` once per actor (25% chance).
- AT collision path invokes `Fuse::OnSwordMeleeHit`/`Fuse::OnHammerMeleeHit` and has a freeze-shatter protection check (`Fuse::WasFreezeShatterDamageAppliedThisFrame`) when deciding whether to apply bonus attack damage.
- Load reset clears deferred freeze queue via `Fuse::ResetSwordFreezeQueue()`; post-frame hook processes deferred freezes via `Fuse::ProcessDeferredSwordFreezes(play)`.

### Torch-lighting / web-burning
- No explicit torch-lighting trigger code is present in this file.
- No explicit web-burning trigger code is present in this file.

## 6) Safeguards / Limits

Mechanisms in this file that prevent runaway or duplicate behavior:
- **Per-victim same-frame AT cooldown:** `gSwordATVictimCooldown` + `gSwordATVictimCooldownFrame`.
- **Per-swing hammer drain guards:** `Fuse::HammerDrainedThisSwing()` and `Fuse::HammerHitActorThisSwing()` prevent multiple durability drains in one swing.
- **Sword BG explosion cooldown:** `gLastSwordBgExplodeFrame` enforces a 10-frame interval.
- **Impact per-frame drain cooldown helper:** `gLastImpactDrainFrame` (in currently `[[maybe_unused]]` helper).
- **One-time material awards per actor:** `gAwardedFrozenShards` and `gAwardedFireJellies`, with cleanup based on actor-list presence.
- **Temporary hammer-flag injection:** captured base flags are restored next frame or immediately on break-to-vanilla path.
- **Throw-tracking bounded check:** thrown-rock award check runs once after `kFramesAfterThrowToCheck` (18) frames, then state is cleared.

Note: there is no explicit “spawn bomb actor” loop here; explosion injection is via function calls and guarded cooldown/state checks.

## 7) Durability Interaction

Durability drains from object/actor interactions occur in multiple hook paths:
- **AT actor collision:** immediate drain in `FuseHooks_OnSwordATCollision` for sword or hammer hits (`Damage*FuseDurability(..., 1, reason)`).
- **Hammer ground impact:** drain once per swing if fused and durability > 0.
- **Hammer BG impact:** drain once per swing when impact flags indicate BG contact and conditions pass.
- **Sword BG explosion path:** drains after explosion trigger.

Decoupling note:
- The AT collision hook can drain durability regardless of any explosion visual trigger, because actor-hit drain occurs in its own path.
- Sword BG drain in this file is tied to explosion-enabled path; when explosion modifier is absent, the log explicitly says drain is handled in AT collision hook.

## 8) Debug Signals

Log tag prefixes used in this file:
- `[FuseDBG]` — detailed diagnostics for subsystem internals:
  - freeze/base damage probes,
  - material gain events,
  - attack-bonus apply/skip,
  - hammer pound-up and BG hit geometry details,
  - explosion call/site diagnostics.
- `[FuseMVP]` — high-level event flow / durability / state-transition logs:
  - hammerize apply/restore,
  - AT collision durability outcomes,
  - break->vanilla restoration,
  - swing start and ground/BG durability drains,
  - impact cooldown skips,
  - thrown-rock acquisition outcomes.
