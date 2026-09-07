# Fuse Architecture Overview

## Repository path anchors (Shipwright-root)
- Fuse root: `soh/soh/Enhancements/Fuse/`
- System registration: `soh/soh/Enhancements/FuseSystem.cpp`
- Actor overlay touchpoint: `soh/src/overlays/actors/ovl_En_Fuse_Beam/`

## 1) Scope & Goals

### What Fuse adds (high-level)
- **Materials + modifiers**: weapons/projectiles can be fused with a `MaterialId`, then modifier checks drive effects like `Explosion`, `Freeze`, `Burn`, `Stun`, `Knockback`, `Seek`, etc. (`Fuse::GetMaterialDef`, `Fuse::GetMaterialModifierLevel`, hook-side `HasModifier` checks).
- **Durability lifecycle**: fused slots carry `durabilityCur`/`durabilityMax`, and hooks drain durability on concrete hit/impact events (`Fuse::Damage*FuseDurability`, `Fuse::MarkRangedHitResolved`, `Fuse::OnRangedProjectileHitFinalize`).
- **Pause UI workflow**: a dedicated pause modal (`sModal`) handles open/selection/confirm/locked states and calls `Fuse::TryFuse*` entry points.
- **Runtime combat integration**: hook files bridge game events (AT collision, projectile fired/spawned/hit, boomerang hit/surface) into Fuse core behaviors.

### What this document covers
- Subsystem responsibilities and boundaries for:
  - `soh/soh/Enhancements/Fuse/Fuse.cpp`
  - `soh/soh/Enhancements/Fuse/UI/FusePauseBridge.cpp`
  - `soh/soh/Enhancements/Fuse/Hooks/FuseHooks_Objects.cpp`
  - `soh/soh/Enhancements/Fuse/Hooks/FuseHooks_Ranged.cpp`
  - `soh/soh/Enhancements/Fuse/Hooks/FuseHooks_Boomerang.cpp`
- Observable data ownership/state lifetimes in those files.
- Observable event flow and hook lifecycles in those files.

### What this document does **not** cover
- Full material catalog / modifier catalog project-wide.
- Non-listed files and non-local systems (save schema internals, unrelated UI, other hooks).
- Build/runtime validation of behavior (doc is source-evidence only).

---

## 2) Subsystem Map (Responsibilities)

## A. Core Fuse System
- **Purpose**
  - Own fused slot state, runtime queues, durability accounting, modifier execution helpers, and cross-hook services (freeze/burn/explosion/seek/ranged queues).
- **Primary file(s)**
  - `soh/soh/Enhancements/Fuse/Fuse.cpp`
- **Key entry points / public functions (grouped)**
  - **Global lifecycle / ticks**: `Fuse::OnLoadGame`, `Fuse::OnGameFrameUpdate`, `Fuse::TickStatusEffects`, `Fuse::TickRangedProjectileSeek`, `Fuse::TickRangedProjectileBombableProximity`, `Fuse::ProcessPendingStuns`, `Fuse::ProcessDeferredSwordFreezes`, `Fuse::ResetSwordFreezeQueue`.
  - **Fuse state queries**: `Fuse::Is*Fused`, `Fuse::Get*Material`, `Fuse::Get*FuseDurability`, `Fuse::Get*FuseMaxDurability`, `Fuse::GetActive*Slot`.
  - **Fuse mutation**: `Fuse::TryFuse*`, `Fuse::TryUnfuse*`, `Fuse::Fuse*WithMaterial`, `Fuse::Clear*Fuse`, `Fuse::Set*FuseDurability`, `Fuse::Set*FuseMaxDurability`.
  - **Ranged queue/active orchestration**: `Fuse::TryQueueRangedFuse`, `Fuse::CommitQueuedRangedFuse`, `Fuse::CancelQueuedRangedFuse_Refund`, `Fuse::ClearQueuedRangedFuse_NoRefund`, `Fuse::ClearActiveRangedFuse`, `Fuse::MarkRangedHitResolved`, `Fuse::OnRangedProjectileHitFinalize`, `Fuse::OnHookshotShotStarted`, `Fuse::OnHookshotRetractedOrKilled`, `Fuse::TryMarkRangedProjectileAsFire`.
  - **Combat effects**: `Fuse::OnSwordMeleeHit`, `Fuse::OnHammerMeleeHit`, `Fuse::QueueSwordFreeze`, `Fuse::TryFreezeShatter`, `Fuse::ApplyBurn`, `Fuse_TriggerExplosion`, `Fuse_GetExplosionParams`, `Fuse_TriggerDekuNutAtPos`.
  - **Durability drains**: `Fuse::DamageSwordFuseDurability`, `Fuse::DamageBoomerangFuseDurability`, `Fuse::DamageHammerFuseDurability`.

## B. Pause UI Modal
- **Purpose**
  - Detect pause-context eligibility, open modal on `BTN_CUSTOM_FUSE_MENU`, show material list and durability panel, manage preview/confirm/locked state machine, invoke Fuse core fusing APIs.
- **Primary file(s)**
  - `soh/soh/Enhancements/Fuse/UI/FusePauseBridge.cpp`
- **Key entry points / public functions (grouped)**
  - **Modal control**: `FusePause_IsModalOpen`, `FusePause_UpdateModal`.
  - **Rendering**: `FusePause_DrawPrompt`, `FusePause_DrawModal`.
  - **Input/context helpers**: `IsFuseMenuPressed`, `BuildPromptContext`, `ResolveSlotForPauseItem`, `WeaponViewFromSlot`, `WeaponViewForPauseItem`.
  - **Material/UI helpers**: `BuildMaterialList`, `MoveCursor`, `ClampScrollToCursor`, `SetUiState`, `TriggerPrompt`.

## C. Hook Layer: Objects (melee, hammer, thrown objects)
- **Purpose**
  - Connect sword/hammer AT collisions and player-frame transitions to core modifier and durability logic; apply/restore hammerized sword hitbox flags; track thrown rock material acquisition; object-specific event mediation.
- **Primary file(s)**
  - `soh/soh/Enhancements/Fuse/Hooks/FuseHooks_Objects.cpp`
- **Key entry points / public functions (grouped)**
  - `FuseHooks_OnSwordATCollision`
  - `OnFrame_Objects_Pre`
  - `OnFrame_Objects_Post`
  - `OnPlayerUpdate`
  - `OnSwordFuseBroken`
  - `OnLoadGame_RestoreObjects`

## D. Hook Layer: Ranged (arrow/slingshot/hookshot)
- **Purpose**
  - Translate ranged fire/spawn/hit/surface/hookshot lifecycle events into queue commit, projectile burn/fire marking, actor/surface modifier processing, and finalize/cleanup calls.
- **Primary file(s)**
  - `soh/soh/Enhancements/Fuse/Hooks/FuseHooks_Ranged.cpp`
- **Key entry points / public functions (grouped)**
  - `FuseHooks_OnArrowProjectileFired`
  - `FuseHooks_OnArrowProjectileSpawned`
  - `FuseHooks_OnRangedProjectileHit`
  - `FuseHooks_OnRangedProjectileHitSurface`
  - `FuseHooks_OnHookshotShotStarted`
  - `FuseHooks_OnHookshotEnemyHit`
  - `FuseHooks_OnHookshotSurfaceHit`
  - `FuseHooks_OnHookshotLatched`
  - `FuseHooks_OnHookshotRetracted`
  - `FuseHooks_OnHookshotKilled`

## E. Hook Layer: Boomerang
- **Purpose**
  - Apply boomerang-specific modifier execution and durability drains on actor/surface collisions, including explosion assist and a local BG explosion cooldown.
- **Primary file(s)**
  - `soh/soh/Enhancements/Fuse/Hooks/FuseHooks_Boomerang.cpp`
- **Key entry points / public functions (grouped)**
  - `FuseHooks_OnBoomerangHitActor`
  - `FuseHooks_OnBoomerangHitSurface`

---

## 3) Data Model & Ownership

## Core-owned state (`Fuse.cpp`)
- **Persistent-ish fuse save object**: `static FuseSaveData gFuseSave` (commented as persistent-ready, not serialized yet in this file).
- **Runtime session object**: `static FuseRuntimeState gFuseRuntime`.
- **Ranged state arrays**:
  - `gRangedQueued` (`std::array<RangedFuseState, 3>`) for pre-commit intent.
  - `gRangedActive` (`std::array<RangedFuseState, 3>`) for committed in-flight state.
- **Material inventory**: `sMaterialInventory` + `sMaterialInventoryInitialized`.
- **Status effect transient maps** (core-owned): freeze timers/frames/pinning, burn states, seek states, projectile previous positions, shatter impulse maps, etc.

## UI-owned modal state (`FusePauseBridge.cpp`)
- `struct FuseModalState` + singleton `static FuseModalState sModal` own pause-only interaction state:
  - modal visibility (`open`), list focus (`cursor`, `scroll`), flow state (`uiState`), lock (`isLocked`), active item class (`activeItem`), material IDs (`highlightedMaterialId`, `previewMaterialId`, `confirmedMaterialId`), prompt (`promptType`, `promptTimer`), carousel motion (`carouselPos`, `carouselVel`).

## Hook-local state patterns
- **Objects hook local state**: multiple static cooldown/edge-tracking fields (`gHammerizeAppliedFrame`, victim cooldown sets, thrown-rock tracking, etc.) used to debounce and preserve expected behavior around melee impact events.
- **Boomerang hook local state**: `sBoomerangBgLastExplodeFrame` map keyed by `EnBoom*` to apply `kBoomerangBgCooldownFrames`.
- **Ranged hook**: mostly stateless dispatch; state ownership intentionally delegated to core via queue/active APIs.

## Code-provable invariants
- A slot being “usable fused” requires both non-`None` material and positive durability checks in many call paths (e.g., ranged checks gate on `materialId != None && curDurability > 0`).
- Pause modal sets `isLocked = weaponView.isFused` on open and blocks direct refusion in locked state by showing `AlreadyFused` prompt until closed/reopened.
- Ranged hit finalization explicitly calls `Fuse::MarkRangedHitResolved`/`Fuse::OnRangedProjectileHitFinalize` to converge active/queued state transitions.
- Hookshot queue policy explicitly rejects seek materials in `Fuse::TryQueueRangedFuse` (`slot == Hookshot && Fuse_MaterialHasSeek(mat)` path).

---

## 4) Data Flow Diagrams (text)

## A) Pause UI fuse flow
1. **Eligibility detection**
   - `FusePause_UpdateModal` builds `FusePromptContext` (page, slot, owned-equip, item type).
   - If `context.shouldShowFusePrompt` and `IsFuseMenuPressed()` (checks `BTN_CUSTOM_FUSE_MENU`) then modal open is attempted.
2. **Open + initialize state**
   - Resolve currently hovered weapon slot (`ResolveSlotForPauseItem`), derive current fused view (`WeaponViewFromSlot`).
   - Initialize `sModal` fields (open, cursor/scroll/carousel, lock flag, active item, confirmed material, prompts).
3. **Selection/preview stage**
   - `BuildMaterialList` supplies entries; cursor movement updates `highlightedMaterialId` and `previewMaterialId` when enabled.
   - State machine transitions via `SetUiState` (`Browse`/`Preview`/`Confirm`/`Locked`).
4. **Confirm stage**
   - First `BTN_A` in preview moves to `Confirm`; second `BTN_A` executes one of:
     - `Fuse::TryFuseSword`
     - `Fuse::TryFuseBoomerang`
     - `Fuse::TryFuseHammer`
     - shield path `TryFuseShield`
5. **State update + lock/prompt**
   - On success: lock modal to fused material (`isLocked = true`, `confirmedMaterialId = preview`, state `Locked`).
   - On failure: move to preview state; if already locked and A pressed, trigger `AlreadyFused` prompt.
6. **Close paths**
   - Pause state/page mismatch, `BTN_B`, or lifecycle conditions close modal; prompt rendering handled separately by `FusePause_DrawPrompt` when modal is not open.

## B) Ranged / boomerang impact flow
1. **Material selection / queue**
   - Queue for ranged is prepared by core (`Fuse::TryQueueRangedFuse`) and committed by hooks on fire/hit lifecycle (`Fuse::CommitQueuedRangedFuse`).
2. **Projectile spawn setup (ranged)**
   - `FuseHooks_OnArrowProjectileSpawned` checks active ranged fuse; if burn modifier is present, marks projectile fire/lit state (`EnArrow_SetLitByFire` or `EnArrow_SetFireDmgFlagsOnly`).
3. **Hit dispatch**
   - Actor hit: `FuseHooks_OnRangedProjectileHit` => fire-mark attempt + `Fuse_OnRangedHitActor` + finalize.
   - Surface hit: `FuseHooks_OnRangedProjectileHitSurface` => commit + fire-mark + `HandleRangedSurfaceHit` + finalize.
   - Boomerang actor/surface: `FuseHooks_OnBoomerangHitActor` / `FuseHooks_OnBoomerangHitSurface` run boomerang modifier logic directly.
4. **Modifier trigger phase (hook + core)**
   - Explosion: hooks compute explosion level and call core `Fuse_TriggerExplosion` (+ bombable assist adjustment where used).
   - Freeze/Burn/Stun: hooks call `Fuse::QueueSwordFreeze`, `Fuse::ApplyBurn`, `Fuse_TriggerDekuNutAtPos` / `Fuse_EnqueuePendingStun`.
   - Freeze shatter short-circuit: `Fuse::TryFreezeShatter` can consume flow and finalize early.
5. **Durability drain + cleanup**
   - Boomerang drains through `Fuse::DamageBoomerangFuseDurability` on hit/surface.
   - Ranged resolution uses `MarkRangedHitResolved` + `OnRangedProjectileHitFinalize` to decrement/clear active ranged fuse state (inside core finalize path).

---

## 5) Integration Points / Hook Sites

## `FuseHooks_Objects.cpp`
- **Called functions / sites**
  - `FuseHooks_OnSwordATCollision` (AT collision path).
  - `OnFrame_Objects_Pre` and `OnFrame_Objects_Post` (frame hooks).
  - `OnPlayerUpdate` (player update hook).
- **Event lifecycle evidenced**
  - Pre-frame: cleanups, hammerize application, rock tracking setup.
  - Collision: melee victim processing (`Fuse::OnSwordMeleeHit` / `Fuse::OnHammerMeleeHit`), attack bonus application, durability drain.
  - Post-frame: deferred freeze processing (`Fuse::ProcessDeferredSwordFreezes`).
  - Player update: hammer swing id tracking, ground/bg impact explosion/drain handling.
- **Preservation / exclusions visible**
  - Sword hitbox flags are restored to vanilla state (`RestoreSwordHitboxVanillaNow`) after temporary hammerize patching.
  - Cooldown sets/maps avoid repeated drains/hits in same temporal window.

## `FuseHooks_Ranged.cpp`
- **Called functions / sites**
  - Arrow/slingshot fire + spawn + actor hit + surface hit hooks.
  - Hookshot shot started/enemy hit/surface hit/latched/retracted/killed hooks.
- **Event lifecycle evidenced**
  - Fire: commit queued fuse to active (`Fuse::CommitQueuedRangedFuse`).
  - Spawn: attach burn/fire projectile flags.
  - Hit/surface: apply modifiers, explosion/stun/freeze/burn paths, then finalize (`Fuse::OnRangedProjectileHitFinalize`).
  - Hookshot lifecycle: start/retract/killed callbacks call core hookshot state functions.
- **Preservation / exclusions visible**
  - `Fuse_ShouldSkipExplosionVictim` uses `Fuse_IsExplosionImmuneVictim` (Dodongo immunity).
  - Explosion only triggers on permitted actor classes (`Fuse_ShouldTriggerExplosionOnActor`).

## `FuseHooks_Boomerang.cpp`
- **Called functions / sites**
  - `FuseHooks_OnBoomerangHitActor`, `FuseHooks_OnBoomerangHitSurface`.
- **Event lifecycle evidenced**
  - Actor hit: evaluate modifiers, optional explosion/freeze/burn/stun/knockback, drain durability.
  - Surface hit: explosion-only path with per-boomerang cooldown map, then durability drain.
- **Preservation / exclusions visible**
  - Surface explosions are rate-limited (`kBoomerangBgCooldownFrames`).
  - Explosion skip path uses `Fuse_IsExplosionImmuneVictim` for actor exclusions.

---

## 6) Modifier Surfaces (only what these files clearly show)

- **Seek**
  - **Logic location**: core (`Fuse::TickRangedProjectileSeek` in `Fuse.cpp`).
  - **When it runs**: per-frame projectile seek tick.
  - **Hook involvement**: indirect (ranged hook manages active ranged state that seek tick consumes).
  - **CVar tuning**: `gFuseSeekRadius`, `gFuseSeekDotMin`, `gFuseSeekMaxTurnDeg`, `gFuseSeekTurnScaleFar`, `gFuseSeekAcquireDelay`, `gFuseSeekDisableStop`, `gFuseSeekStopGraceTicks`, debug gate `gFuseSeekDebug`.

- **Burn**
  - **Logic location**: core apply/tick (`Fuse::ApplyBurn`, `Fuse::TickStatusEffects`), ranged spawn assist in hook via arrow fire-flag setters.
  - **When it runs**: on hit application + periodic status ticking; projectile flagged at spawn for arrows/seeds.

- **Explosion**
  - **Logic location**: explosion params/trigger in core (`Fuse_GetExplosionParams`, `Fuse_TriggerExplosion`), hit/surface decision logic in hooks.
  - **When it runs**: melee actor/bg hits, ranged actor/surface/hookshot hits, boomerang actor/surface hits.

- **Freeze / Freeze-shatter**
  - **Logic location**: core queue/process/shatter (`Fuse::QueueSwordFreeze`, `Fuse::ProcessDeferredSwordFreezes`, `Fuse::TryFreezeShatter*`), trigger conditions in hooks.
  - **When it runs**: on actor hit paths where freeze/shatter conditions pass.

- **Stun**
  - **Logic location**: core dekunut/pending stun processing (`Fuse_TriggerDekuNutAtPos`, `Fuse_EnqueuePendingStun`, `Fuse::ProcessPendingStuns`), trigger checks in hooks.
  - **When it runs**: actor hit paths.

- **Knockback**
  - **Logic location**: boomerang hook has concrete implementation (`ApplyBoomerangKnockback`); ranged hook logs TODO only (no victim-applied knockback yet).
  - **When it runs**: boomerang actor hit.

- **Hammerize / RangeUp / WideRange / PoundUp (visible surfaces)**
  - **Hammerize**: objects hook temporarily patches sword hitbox flags based on sword modifier level.
  - **RangeUp/WideRange**: scale helpers in core (`Fuse_GetSwordRangeUpScale`, `Fuse_GetBoomerangWideRangeScale`, `Fuse::GetRangeUpScale`, `Fuse::GetWideRangeScale`).
  - **PoundUp**: objects hook checks hammer explosion modifier level on hammer impact.

---

## 7) CVar Surface (master list by file)

## `Fuse.cpp`
- **Logging / debug gates**
  - `gFuseLogDbg` (fallback 0 via `CVarGetInteger`).
  - `gFuseLogMvp` (fallback 0).
- **Seek CVars (registered defaults in `Fuse_RegisterSeekCVars`)**
  - `gFuseSeekRadius` = **1500.0f** (register default); read fallback in tick: 900.0f.
  - `gFuseSeekDotMin` = **0.60f** (register default); read fallback: 0.65f.
  - `gFuseSeekMaxTurnDeg` = **16.0f** (register default); read fallback: 6.0f.
  - `gFuseSeekTurnScaleFar` = **1.9f** (register default); read fallback: 0.4f.
  - `gFuseSeekAcquireDelay` = **2** (register default + read fallback 2).
  - `gFuseSeekDebug` = **0** (register default + read fallback via helper).
  - `gFuseSeekDisableStop` = **0** (register default + read fallback 0).
  - `gFuseSeekStopGraceTicks` = **2** (registered; TODO (verify) where consumed in this file’s seek logic path).
- **Feature/debug controls**
  - `CVAR_ENHANCEMENT("FuseDekuNutSpawn")` fallback 1 (dekunut spawn gate).
  - Enemy HP override controls:
    - `gFuse.DebugEnemyHpOverride.Enable` (fallback 0)
    - `gFuse.DebugEnemyHpOverride.Sticky` (fallback 0)
    - `gFuse.DebugEnemyHpOverride.Reset` (fallback 0)
    - Per-enemy keys returned by `GetEnemyHpOverrideKey` and read via `CVarGetInteger(key, 0)`:
      - `gFuse.DebugEnemyHpOverride.Keese`
      - `gFuse.DebugEnemyHpOverride.BigDekuBaba`
      - `gFuse.DebugEnemyHpOverride.DekuBaba`
      - `gFuse.DebugEnemyHpOverride.BlueTektite`
      - `gFuse.DebugEnemyHpOverride.RedTektite`
      - `gFuse.DebugEnemyHpOverride.Dinolfos`
      - `gFuse.DebugEnemyHpOverride.Lizalfos`
      - `gFuse.DebugEnemyHpOverride.Peahat`
      - `gFuse.DebugEnemyHpOverride.Wolfos`
      - `gFuse.DebugEnemyHpOverride.Stalfos`

## `FusePauseBridge.cpp`
- **Pause modal render/layout toggles**
  - `CVAR_DEVELOPER_TOOLS("Fuse.DurabilityBarEnabled")` fallback 1.
  - `CVAR_DEVELOPER_TOOLS("Fuse.UiPauseCardNameScale")` fallback 1.0f.
  - `CVAR_DEVELOPER_TOOLS("Fuse.UiPauseCardQtyScale")` fallback 0.70f.
  - `CVAR_DEVELOPER_TOOLS("Fuse.UiPauseFooterPromptScale")` fallback 0.90f.
  - `CVAR_DEVELOPER_TOOLS("Fuse.UiPauseFooterStatusScale")` fallback 0.85f.
  - `CVAR_DEVELOPER_TOOLS("Fuse.UiPauseUseOrderedFont")` fallback 1.
  - `CVAR_DEVELOPER_TOOLS("Fuse.Pause.OrderedTighten")` fallback 0.75f (clamped 0.0..2.0 where read).

---

## 8) Debugging Quick Map

- **Pause menu won’t open**
  - Check `FusePause_UpdateModal` eligibility (`BuildPromptContext`) and `IsFuseMenuPressed()` (`BTN_CUSTOM_FUSE_MENU`).
  - Inspect logs: `PauseFuseOpenAttempt`, `PauseFuseDenied`, `Activation`, `UI:Open`.

- **Modal opens but cannot fuse/select**
  - Check `sModal.isLocked` initialization (`weaponView.isFused`), `BuildMaterialList` entry `enabled`, and `previewMaterialId` assignment.
  - Confirm confirm-path reaches `Fuse::TryFuse*` / `TryFuseShield`.

- **Ranged fuse not applying**
  - Check queue-to-active path: `Fuse::TryQueueRangedFuse` -> `Fuse::CommitQueuedRangedFuse` (fire/hit hooks).
  - Confirm finalize path runs: `Fuse::MarkRangedHitResolved` + `Fuse::OnRangedProjectileHitFinalize`.

- **Seek not homing**
  - Check seek CVars and gates: radius/dot/max-turn/far-scale/acquire-delay/disable-stop/debug.
  - Inspect `Fuse::TickRangedProjectileSeek` state transitions and debug logs (`SeekAcquire`, `SeekSteer`, `SeekNoTarget`, `SeekStop`).

- **Explosion visuals/effects missing but durability drains**
  - Check hook-side explosion level checks and actor/category filters:
    - ranged: `Fuse_OnRangedHitActor` + `HandleRangedSurfaceHit`
    - boomerang: `FuseHooks_OnBoomerangHitActor` / `FuseHooks_OnBoomerangHitSurface`
    - objects/melee: `Fuse::OnSwordMeleeHit`, `Fuse::OnHammerMeleeHit`, `OnPlayerUpdate` BG impact paths.
  - Validate skip conditions such as `Fuse_IsExplosionImmuneVictim` and boomerang BG cooldown.

- **Hammer behavior odd (double drains / missing drains)**
  - Check swing tracking flags in core (`HammerDrainedThisSwing`, `HammerHitActorThisSwing`, swing id) and object-hook use in `OnPlayerUpdate` / AT collision hook.

- **Freeze behavior inconsistent**
  - Check deferred queue and shatter gates in core (`QueueSwordFreeze`, `ProcessDeferredSwordFreezes`, `TryFreezeShatter`, no-reapply windows).

## Log tags/prefixes seen in these files
- `[FuseDBG]`
- `[FuseDBG_UI]`
- `[FuseMVP]`

