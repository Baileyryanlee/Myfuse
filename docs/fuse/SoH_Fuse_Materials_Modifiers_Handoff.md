# Ship of Harkinian Fuse Mod

Materials Modifiers and Effects Handoff Reference

Updated September 7 2026 for the Myfuse development workspace on SoH 9.2.3. Prepared for ongoing Fuse development from the supplied handoff and checked against the local source.

Source status: this reference describes the current local working tree, including recent menu and Beamos changes that are not yet committed. The committed baseline is ee9c98af710d9130cd93dfd5162e2d602a118416. Source-confirmed behavior, historical design intent and unfinished work are distinguished below. User-reported gameplay tests are smoke tests, not exhaustive certification.

## Executive Summary

Fuse is a data-driven gameplay system inspired by Tears of the Kingdom and adapted to Ocarina of Time / Ship of Harkinian. A Fuse material contributes an Attack attribute, durability, and one or more context-aware modifiers. Each fuse-capable item owns an independent slot containing material ID, current durability, maximum durability, and slot-specific state. Effects are executed by the core/hook layers according to weapon context.

- Inventory is consumed only on a successful fusion.

- The pause Fuse modal locks an already-fused slot. The in-game ranged menu can replace a queued selection; this is an exception to the original no-overwrite design rule.

- The pause modal does not offer manual unfusing. The ranged menu includes NONE to clear its selected slot, and developer controls can clear fuses.

- Durability reaching 0 breaks the fuse and returns the item to vanilla behavior.

- Material + current/max durability are treated as coupled persistent state.

- Material Attack contributes additive direct-hit damage in supported contexts. Separate effect damage paths and the new beam tuning controls must be evaluated independently; do not assume a universal damage formula.

- Historical design intent calls for even material Attack values so 50% shield-bash scaling is integral. Current source still has Rock=1 and Bomb=1; the rule is not enforced by the registry or debug overrides.

- The original two-behavior limit is a design guideline, not a validated runtime invariant. Rock currently declares four modifier entries with context-dependent effects.

## Fuse Capable Item and Slot Model

| Category | Slots / Items | Fuse Model / Notes |
| --- | --- | --- |
| Swords | Kokiri Sword; Master Sword; Biggoron Sword | Independent persistent slot per sword. Pause-menu Fuse flow. |
| Hammer | Megaton Hammer | Independent slot. Melee and ground/BG impact modifier paths. |
| Shields | Deku Shield; Hylian Shield; Mirror Shield | Independent shield slots. Passive guard effects + active Shield Bash effects. |
| Thrown | Boomerang | Independent persistent slot; melee-style/pause Fuse rules; durability drains per processed hit/surface event. |
| Ranged | Arrows; Slingshot; Hookshot / Longshot | Runtime/aiming Fuse UI. Queue/active lifecycle. Hookshot is intentionally exempt from material Attack bonus. |

Ranged selections are made while aiming, using queued/active state. The ranged menu can clear or replace a queued selection. Projectile-hit finalization decrements active durability and clears that active state. Beamos Head is explicitly forced to effective durability 1 for arrows and slingshot; its emitter snapshots eligibility per shot and does not consume material on each beam tick.

## Material ID Registry and Stability

Material IDs are persisted in save data and mirrored across the C/C++ bridge. Numeric IDs must be treated as stable ABI/save identifiers. Do not renumber or reuse IDs without a save migration and bridge update.

| ID | Material | Base Attack | Base durability | Registry Modifiers |
| --- | --- | --- | --- | --- |
| 0 | None | 0 | 0 | None |
| 1 | Rock | 1 | 10 | Hammerize 1; Knockback 1; Pound Up 1; Negate Knockback 1 |
| 2 | Deku Nut | 0 | 5 | Stun 1; Mega Stun 1 |
| 3 | Frozen Shard | 0 | 8 | Freeze 1 |
| 4 | Stick | 2 | 3 | Range Up 3; Wide Range 3 |
| 5 | Bomb | 1 | 1 | Explosion 1 |
| 6 | Keese Eye | 0 | 4 | Seek 1 |
| 7 | Fire Jelly | 0 | 10 | Burn 1 |
| 8 | Fire Keese Eye | 0 | 1 | Seek 1; Burn 1 |
| 9 | Beamos Head | 4 | 100 | Beam 1 |

Verified registry discrepancy: Rock and Bomb are both Attack 1 in FuseMaterials.cpp. Historical Rock Attack 4 and the even-Attack rule remain design references. No material value or persisted ID was changed during this documentation update. Material Tuning overrides can make runtime values differ from registry defaults.

## Material Catalog

### ROCK

Source: world rocks / debris. Heavy blunt fusion material.

- Current registry Attack: 1. Historical design notes describe Attack 4; that value is not the default in this source.

- Base durability: 10 in registry.

- Hammerize: swords. Makes sword strikes interact with hammer-only objects.

- Knockback: swords, Hammer, Shields, Boomerang; increases enemy impact force.

- Pound Up Level 1 is registered, but hammer impact-radius expansion remains a TODO in FuseHooks_Objects.cpp.

- Negate Knockback: registry includes Level 1 for shield context.

- Applicable slots in material list: Kokiri Sword, Master Sword, Biggoron Sword, Megaton Hammer, Shields, Boomerang.

- Status: implemented; broad non-beam combat smoke tests passed. Not every object/modifier combination was independently retested.

- Acquisition hook notes: thrown/liftable rock tracking exists in object hooks; rock material acquisition is tracked after throw behavior.

- Visuals: current FuseVisual implementation only has a Rock mesh path (gSilverRockDL) for sword-hand/shield attachments.

### DEKU NUT

Source: vanilla Deku Nut inventory. Utility material centered on stun.

- Attack: 0. Base durability: 5 in registry.

- Stun: Deku Nut-style stun on enemy hit.

- Mega Stun: the current helper emits six Deku Nut-style effects on a radius of 160 world units, with a 60-gameplay-frame cooldown. The historical design described 5–7 effects.

- Melee: brief stun on hit. Ranged: AoE stun on impact.

- Applicable slots in material list: Swords, Shields, Slingshot, Boomerang.

- Inventory behavior: vanilla-like material; core inventory API proxies Deku Nut to vanilla ammo/inventory behavior.

- Ranged refund/consume logic is documented as fixed; AoE stun behavior tuned.

- Status: implemented; covered by broad user-reported non-beam combat testing.

### FROZEN SHARD

Source: custom material; obtained from Freezard loot logic.

- Attack: 0 in registry. Base durability: 8 in registry.

- Object-hook acquisition reference: Freezard despawn/health==0 has a 25% Frozen Shard award chance, once per actor.

- Freeze: freezes enemy on impact; movement and animation are halted.

- Freeze reapplication is gated by timers/no-reapply windows to prevent immediate spam.

- Shatter: follow-up impacts can run the shatter damage/impulse path. The current multiplier constant is 1.5; the helper also reads material Attack, so the original claim that shatter is completely independent of Attack is too broad.

- Project status describes freeze/shatter as functional but still tuning knockback direction/damage behavior.

- Historical elemental design: Freeze against a burning target should deal double damage. This is not established as a general implemented rule by the reviewed source; validate each path before promising it.

- Applicable slots: Swords, Hammer, Shields, Boomerang, Arrows, Slingshot, Hookshot / Longshot.

- Status: Playable / tuning.

### STICK

Source: vanilla Deku Stick inventory. Lightweight, fragile reach material.

- Registry Attack: 2. Registry base durability: 3.

- Range Up Level 3 gives +30% effective sword reach in current code. Levels 1 and 2 give +10% and +20%. Historical +25/+50/+100% values are not the implementation.

- Wide Range Level 3 in registry: boomerang hitbox width +300%.

- Material-list design wording also mentions Pierce / Extended Reach as design intent, but no Pierce ModifierId is documented in the current modifier enum.

- Applicable slots: Swords, Boomerang, with selective ranged use mentioned as design intent.

- Inventory behavior: vanilla-like material; core inventory API proxies Stick to vanilla inventory/ammo.

- Status: Experimental / tuning in material list; current-status document says implemented across applicable slots and still tuning.

### BOMB

Source: vanilla Bomb inventory. Explosive material.

- Current registry Attack: 1; base durability: 1. The even-Attack design discrepancy remains open.

- Explosion Level 1.

- Triggers on enemy actor impact and hard-surface impact (floors, walls, ceilings) where supported.

- Fuse-spawned explosions are single-hit to prevent multi-frame damage stacking.

- Melee explosions can damage Link; shield-based explosions damage enemies without harming Link.

- Bombable assist can re-anchor an explosion toward a nearby bombable actor/anchor.

- King Dodongo / designated explosion-immune victims are skipped by documented immunity logic.

- Applicable slots: Swords, Hammer, Shields, Boomerang, Arrows, Slingshot, Hookshot / Longshot.

- Inventory behavior: vanilla-like material; core inventory API proxies Bomb to vanilla inventory/ammo.

- Status: implemented; broad non-beam combat smoke tests passed. Immunity and world-object edge cases remain regression scenarios.

### KEESE EYE

Source: Keese enemy drops. Accuracy/targeting utility material.

- Attack: 0. Base durability: 4.

- Seek Level 1: arrows and slingshot projectiles home toward the nearest valid enemy.

- Seek is tuned through radius, forward-dot threshold, acquisition delay, maximum turn rate, and steering CVars.

- Design rule: Seek materials should remain low durability so homing does not dominate ranged combat.

- Hookshot queue explicitly rejects Seek materials.

- Applicable slots: Arrows, Slingshot.

- Status: implemented. Seek also exists on Fire Keese Eye; this is no longer the only registered homing material.

### FIRE JELLY

Source: Torch Slug (ACTOR_EN_BW). Fire/status-control material.

- Drop rate: material list says 60%; object-hook reference also documents 60% award chance, once per Torch Slug actor.

- Attack: 0. Base durability: 10.

- Burn Level 1: applies burning damage-over-time on impact; Burn DOT does not scale with material Attack.

- Burn does not stack; reapplication refreshes duration.

- Fire-based enemies / blacklist are immune to Burn, but durability still drains on hit.

- Projectile Burn uses vanilla fire-hit behavior for initial application where applicable, with Fuse DOT used to equalize total burn behavior.

- Can light torches and other ignitable objects through supported vanilla fire projectile behavior.

- Historical elemental design: Burn against a frozen target should deal double damage. The reviewed shatter path still contains a TODO for a Burn/Flame multiplier of 2.0, so do not describe this as a completed universal interaction.

- Applicable slots: Swords, Hammer, Shields, Boomerang, Arrows, Slingshot; Hookshot / Longshot is marked optional in the material list.

- Status: implemented; duration/balance remains under tuning. Current burn constants are 120 gameplay frames, a 30-frame tick interval, and tick damage 2; source retains a TODO to restore a 60-frame duration after validation.

### FIRE KEESE EYE

- Current registered material ID 8. Attack 0; base durability 1; Seek 1 and Burn 1.

- Acquisition: the Fire Keese health-zero object hook awards one with a 50% chance, once per tracked actor.

- Homing applies to arrows/slingshot; Burn is routed according to supported item context. Hookshot queueing rejects Seek-bearing materials.

- Status: implemented. This entry was missing from the supplied historical handoff.

### BEAMOS HEAD

- Current registered material ID 9. Attack 4; base durability 100; Beam 1.

- Acquisition: Beamos health-zero tracking awards one with a 50% chance, once per tracked actor.

- Beam contexts are swords, shields, arrows and slingshot. Effective durability is forced to 1 for arrows/slingshot.

- Sword, shield and projectile beams now have independent tuning controls. Child Hylian Shield crouch behavior has its own profile. See the current development section for lifecycle and damage rules.

- Status: implemented with recent extensions; the user reported shield tuning and projectile emitter tests successful. Beam behavior remains subject to targeted edge-case testing.

### BABA VINE

Historical design entry sourced from Deku Baba drops. Baba Vine remains absent from the current MaterialId and MaterialDef registry.

- Primary modifier: Bash Attack (Shields only).

- Shield bash damage becomes 50% of material Attack. Without Bash Attack, shield bash damage is 0.

- Additional modifiers are intentionally deferred; material is intended as a future versatile/foundational material.

- Applicable slots currently documented: Shields; additional slots TBD.

- Status: design-only in the reviewed source; no registered material or acquisition implementation was established.

- Registry note: valid registered material IDs run from None 0 through Beamos Head 9. Baba Vine requires an explicit future ID and implementation; do not reuse a persisted ID.

## Modifier Catalog and Level Scaling

| Modifier | Domain | Levels / Numeric Effect | Behavior and implementation status |
| --- | --- | --- | --- |
| Hammerize | Implemented sword/object integration; never a projectile hammer effect. | Obj_Bombiwa: L1 requires 2 hits; L2+ requires 1. Other historical per-object level promises require separate verification. | Enables hammer-only world-object interaction. |
| Knockback | Various weapons + shields | L1 Light; L2 Medium; L3 Heavy. Shield bash has baseline L1 knockback. | Applies impact force to enemies. |
| Pound Up | Megaton Hammer only | Design: +25 / +50 / +100% radius. | Registered; radius expansion is not implemented. The ground-impact hook logs a TODO. |
| Range Up | Swords only | Current: +10 / +20 / +30% reach. | Implemented sword reach scaling. Historical +25 / +50 / +100% proposal differs. |
| Wide Range | Boomerang only | L1 +100%; L2 +200%; L3 +300%. | Increases boomerang hitbox width. |
| Chain Length Up | Hookshot / Longshot only | Design only: L1 +20%; L2 +40%; L3 +60%. | Extends hookshot chain length. Design definition exists; not listed in current ModifierId implementation reference. |
| Seek | Arrows / Slingshot | No 1-3 numeric scaling defined in design doc. | Homes projectile toward nearest valid enemy. |
| Attack Reflect | Shields only | Design only: Reflected damage = 50% enemy attack. | Reflects guarded enemy attack; player takes no damage. Design definition exists; not listed in current ModifierId implementation reference. |
| Negate Knockback | Shields only | No levels specified in design doc. | Cancels enemy-attack knockback while guarding; excludes environmental/non-living forces. |
| Bash Attack | Shields only | Integer material Attack / 2. | Damage formula implemented, but no current material declares BashAttack. |
| Stun | Hit-based | No numeric levels specified in design doc. | Deku Nut-style stun on enemy hit. |
| Mega Stun | Very rare / shield-bash-oriented | Current helper: 6 effects; radius 160; cooldown 60 gameplay frames. | Large-area stronger stun/interruption. |
| Freeze | Hit-based elemental | No level scale specified. | Freeze on impact; follow-up shatter bonus + extreme knockback; strong vs fire. Historical double-damage elemental promises are not generally verified. |
| Burn | Hit-based elemental | No level scale specified. | Burn DOT; fire immunity; ignites supported objects; strong vs ice/water. Historical double-damage elemental promises are not generally verified. |
| Range Trap | Arrows / Slingshot | Design only: 5 sec; 50% slow; 10% material Attack per sec. | Deploys a slowing/damaging trap on enemy impact. Design-only definition; absent from current ModifierId implementation reference. |
| Slam Trap | Megaton Hammer | Design only: One trigger; max 2 active; despawn beyond max distance. | Ground trap at hammer impact. Marked for further design discussion; absent from current ModifierId implementation reference. |
| Explosion | Multiple weapon types | Current registry uses L1 for Bomb. | Triggers Fuse explosion on eligible actor/BG impacts. Modifier enum implemented although not separately defined in Modifier Definitions document. |
| Beam | Swords; shields; arrows; slingshot | Context-specific and tunable. | Beamos laser. Sword attack phases, guard/sweep shields, and in-flight projectile emitters have separate damage/lifecycle paths. |

## Implemented Modifier Enum vs Design Definitions

Current ModifierId enum: Hammerize=1, Stun=2, MegaStun=3, Freeze=4, Knockback=5, PoundUp=6, NegateKnockback=7, RangeUp=8, WideRange=9, Explosion=10, Seek=11, BashAttack=12, Burn=13, Beam=14.

Chain Length Up, Attack Reflect, Range Trap and Slam Trap remain historical design definitions, absent from the current ModifierId enum. Their numeric proposals below are not shipped behavior.

## Attack Attribute Rules

- Supported direct-hit paths add material Attack to vanilla base damage. Beam damage is context-specific and independently tunable; see the Beamos section.

- Swords + Hammer: additive Attack bonus is applied to base melee damage.

- Arrows + Slingshot: additive Attack bonus is applied on enemy hit.

- Boomerang: additive Attack bonus is applied on enemy hit.

- Hookshot / Longshot: intentionally exempt from material Attack bonus; treated as utility.

- Shield bash base damage is 0. If BashAttack is present, current code uses integer materialAttack / 2, which truncates odd values. No current registry material declares BashAttack; Baba Vine remains planned.

- Burn DOT and Explosion use separate effect paths. Shatter currently reads material Attack in its calculation; beam damage uses its own context-specific rules. Trap damage remains design-only.

- Even Attack values are a historical design target, not a source invariant. Rock=1 and Bomb=1 are confirmed defaults, and debug overrides are not constrained to even values.

## Status Effects and Elemental Interactions

### Freeze

- Applies on eligible enemy impact and pins/halts movement and animation.

- Core tracks freeze timers, original gravity/position, pinned state, apply/shatter frames, and no-reapply windows.

- A follow-up hit can invoke Freeze Shatter: bonus damage plus strong knockback/impulse.

- Burning target hit by Freeze: double damage is historical intent, not a verified general guarantee in the current implementation.

### Burn

- Applies DOT independent of material Attack.

- Core owns per-victim burn state and periodic tick/VFX timing.

- Reapplication refreshes rather than stacks.

- Fire-immune enemies do not receive Burn but still consume fuse durability.

- Frozen target hit by Burn: double damage remains historical intent; a relevant shatter multiplier TODO is still present.

- Ranged Burn can mark arrow/seed projectiles with vanilla fire damage/lighting behavior.

## Weapon Specific Effect Behavior

### Swords

- Material Attack is additive to base melee damage.

- Hammerize temporarily injects hammer-like hitbox flags and must restore vanilla flags afterward.

- Range Up changes effective sword reach.

- Explosion can trigger on eligible actor hits and supported BG impacts; BG path uses cooldown logic.

- Freeze, Burn, Stun, Knockback and shatter are dispatched through melee/core hook paths.

- Normal melee impacts consume durability. Beamos sword beams additionally charge a configurable cost once per attack phase, default 4; slash, charge and released spin are distinct phases. Zero beam cost does not disable normal melee-hit costs.

### Megaton Hammer

- Supports material Attack and status/effect modifiers.

- Pound Up is registered and logged, but expanding the hammer impact radius is still a TODO. Historical level proposals are +25/+50/+100%.

- Ground/BG impact logic has per-swing guards to prevent repeated durability drains.

- Explosion and Mega Stun can be associated with hammer impact events when the material provides them.

### Shields and Shield Bash

- Shields can expose passive guarding effects and active Shield Bash effects.

- Shield bash always has baseline knockback but deals 0 damage unless Bash Attack is present.

- BashAttack damage uses integer division materialAttack / 2. It does not independently change stun or knockback. No current material definition contains this modifier.

- Explosion shield behavior is documented as enemy-damaging without self-damage.

- Negate Knockback is a shield utility modifier in the design/registry.

### Boomerang

- Queries the persistent boomerang fuse state at actor/surface impact.

- Actor-hit path can apply Attack bonus, Explosion, Freeze/Shatter, Knockback, Stun, and Burn.

- Durability drains once per processed actor hit or eligible surface hit, not once per throw.

- Surface Explosion uses a per-boomerang 12-frame cooldown to prevent rapid re-triggering.

- Wide Range scales boomerang hitbox width; Stick registry provides Level 3.

### Arrows and Slingshot

- Runtime ranged menu is used while aiming; queue/active state is committed into projectile use.

- Actor-hit path supports Attack bonus, Explosion, Freeze/Shatter, Stun, Burn; Seek steering runs from core per-frame logic.

- Burn marks arrow/seed projectile fire behavior at spawn/hit where supported.

- Seek is supported for arrows/slingshot and is registered on both Keese Eye and Fire Keese Eye. Hookshot still rejects Seek materials.

- Ranged Knockback is logged as a TODO in the ranged hook reference rather than fully applied there.

### Hookshot and Longshot

- Uses ranged queue/lifecycle but is intentionally exempt from material Attack bonus.

- Enemy hit, surface hit, latch, retract, and kill lifecycle callbacks are integrated.

- Seek materials are explicitly rejected for Hookshot queueing.

- Explosion/Freeze/Stun/Burn may route through ranged effect handling where supported by material/applicability.

## Durability Lifecycle

- A usable fused slot requires material != None and current durability > 0.

- Fuse operations initialize current/max durability from the material effective base durability.

- Normal melee/boomerang durability drains on concrete impact events. Beamos sword beams also charge once per slash/charge/spin phase; shield beams use timed drain and an optional boost cost. These costs are tunable.

- Ranged active durability is decremented during projectile-hit finalization, then active state is cleared.

- At 0 durability the fuse breaks and the item returns to vanilla behavior.

- Sword break additionally restores vanilla hitbox flags.

- Current/max durability are clamped and normalized by persistence code. UI layout and beam tuning are saved as console variables, separate from game-save material state.

- Save/load must always preserve material + current durability + max durability together.

## Material Acquisition and Inventory

| Material | Acquisition / Inventory Path |
| --- | --- |
| Rock | World/liftable rock tracking; custom material inventory helpers exist. |
| Deku Nut | Vanilla inventory/ammo proxy. |
| Frozen Shard | Freezard award logic; object-hook reference documents 25% chance. |
| Stick | Vanilla Deku Stick inventory proxy. |
| Bomb | Vanilla Bomb inventory/ammo proxy. |
| Keese Eye | Keese fall/death path: 50% chance when auraType is NONE. This condition is the implementation boundary, not a guarantee for every variant. |
| Fire Jelly | Torch Slug award logic; 60% documented drop chance. |
| Baba Vine | Historical Deku Baba design source; material is not registered. |
| Fire Keese Eye | Fire-variant Keese health-zero hook: 50%, once per tracked actor. |
| Beamos Head | Beamos health-zero hook: 50%, once per tracked actor. |

The core inventory system splits vanilla-like materials (Deku Nut, Stick, Bomb) from custom material quantities stored in the Fuse material inventory map.

## Persistence and Save Safety Requirements

- Material IDs are persisted and must remain stable.

- FuseSaveData owns persistent slot containers. Runtime state includes ranged queues, status timers, beam actors and per-shot emitter state; runtime actor pointers are not serialized.

- SaveManager sections include enhancements.fuse (current nested schema version 5), enhancements.fuse.materials (custom inventory), and enhancements.fuse.debugOverrides (material tuning overrides).

- Sword, shield, boomerang, and hammer states are documented as persistent through SaveManager.

- Equip switching must restore the correct item-specific slot without resetting or overwriting another slot.

- Normalization clears invalid/depleted slots back to unfused.

- Do not change MaterialId numbering without migration + C bridge alignment.

## Known Conflicts Limitations and TODOs

| Area | Handoff Issue |
| --- | --- |
| Attack values | Even Attack remains design intent. Source confirms Rock=1 and Bomb=1; do not silently rebalance them. |
| Baba Vine | Baba Vine is not registered. Current IDs include Fire Keese Eye 8 and Beamos Head 9. |
| Design-only modifiers | Chain Length Up, Attack Reflect, Range Trap, Slam Trap are defined in design docs but absent from documented current ModifierId enum. |
| Stick wording | Material List mentions Pierce / Extended Reach, but no Pierce ModifierId is documented. |
| Ranged Knockback | Ranged hook reference logs knockback level/TODO; concrete victim knockback is not implemented there. |
| Freeze | Functional but shatter damage/knockback direction remains under tuning in status docs. |
| Fire Jelly | Implemented/new but still under tuning. |
| Projectile collision | Some non-living actors do not register projectile actor collision in vanilla; surface/bombable assist paths compensate only where implemented. |
| Visuals | Rock remains the static attachment mesh path; Beamos uses separate laser actors. Other material attachment meshes are not implied. |
| Range Up | Actual scale is +10/+20/+30%, differing from historical design. |
| Pound Up | Registered modifier; impact-radius expansion remains TODO. |
| Elemental damage | Do not promise universal Freeze/Burn double damage; relevant TODOs and path differences remain. |
| Burn timing | Current 120-frame duration retains a TODO to revert to 60 after validation. |
| Ranged slowdown | Menu slowdown is not wired to a safe engine timing hook. |
| Beam scope | Projectile beams damage enemy focus points within their configured path and stop at world collision. No new switch/pot/prop interactions are supplied. |
| Validation | Recent checks are user-reported gameplay smoke tests. Spin/charge edge cases and every material/weapon combination are not independently certified. |

## Source Code Ownership and Where to Look

| Subsystem | Primary Paths | Responsibility |
| --- | --- | --- |
| Core | soh/soh/Enhancements/Fuse/Fuse.cpp; Fuse.h | Runtime APIs, durability, material inventory, ranged queues, Freeze/Burn/Seek/Explosion helpers. |
| Materials | .../Fuse/FuseMaterials.cpp; .h | MaterialId, MaterialDef registry, Attack/base durability/modifier arrays. |
| Modifiers | .../Fuse/FuseModifiers.cpp; .h | ModifierId, ModifierSpec, HasModifier lookup. |
| State | .../Fuse/FuseState.cpp; .h | Slot ownership, persistence, SaveManager/material inventory serialization. |
| Object hooks | .../Fuse/Hooks/FuseHooks_Objects.cpp; .h | Sword/Hammer collision, Hammerize, impact effects, Freezard/Torch Slug material awards. |
| Ranged hooks | .../Fuse/Hooks/FuseHooks_Ranged.cpp; .h | Arrow/seed/hookshot lifecycle and ranged modifier routing. |
| Boomerang hooks | .../Fuse/Hooks/FuseHooks_Boomerang.cpp; .h | Boomerang actor/surface modifier execution and durability. |
| Pause UI | .../Fuse/UI/FusePauseBridge.cpp; .h | Pause modal material browsing/preview/confirm/locked flow. |
| Ranged UI | .../Fuse/RangedFuseMenu.cpp; .h | Hold-to-open aiming Fuse selection. |
| Visuals | .../Fuse/Visuals/FuseVisual.cpp; .h | Visual attachments; documented current implementation is Rock-specific. |
| C bridge | .../Fuse/FuseCBridge.h | Stable C ABI material IDs and shield/material helper declarations. |
| Debug and tuning | .../Fuse/UI/FuseMenuWindow.cpp; FuseUILayout.h; .../Fuse/FuseShieldBeamTuning.h | Dev Tools window; UI Layout and Beamos Tuning controls; console-variable settings. |
| Beam rendering | soh/src/overlays/actors/ovl_En_Fuse_Beam/z_en_fuse_beam.c; .h | Laser render actor and projectile ownership. |
| Projectile bridge | soh/src/overlays/actors/ovl_En_Arrow/z_en_arrow.c; .h | Per-shot Beamos snapshot, flying-state updates and cleanup. |

## Modifier and Material Regression Checks

- For every changed material, verify UI name/quantity, Attack bonus, modifier level, durability, and save/load.

- Verify modifier effects on enemy and non-enemy impacts where applicable.

- Confirm no effect runs for Material=None or durability=0.

- Confirm durability drains on the intended impact event even when a modifier is immune/skipped.

- Confirm Freeze -> Shatter and Burn/Freeze elemental interactions remain ordered correctly.

- Compare bow and slingshot behavior for the same material.

- Confirm Hookshot remains exempt from Attack bonus and rejects Seek.

- Confirm boomerang actor hits drain once per processed hit and surface Explosion cooldown prevents spam.

- Confirm material ID values remain aligned between MaterialId, save data, and FuseMaterialId C bridge.

- Confirm fused material + current/max durability survive save/load and equipment switching.

## Recommended New Project Handoff Rule

Use this document as a versioned reference, not an instruction to change code to match historical design. Confirm relevant source, debug overrides and save compatibility before implementation. When behavior changes, update this handoff with the same change and distinguish completed code from intended behavior and reported test coverage.

## Current Development and Validation

The workspace is B:\Fused Ship, with one repository root. The verified SoH 9.2.3 baseline was selectively integrated with Myfuse 9.1.1. The integration history is on main at ee9c98af7; origin fetch/push points to https://github.com/Baileyryanlee/Myfuse.git. Recent source changes described here remain local until separately committed.

User-reported tests passed save persistence, Fuse menu controls/remapping, disabled-Fuse behavior, non-beam combat, the UI Layout controls, Beamos shield tuning and the projectile emitter update. The latest projectile build completed with 0 errors. These reports do not establish exhaustive coverage of all listed regression scenarios.

The initial sword beam spin/charge issue received a code fix and successful build. A separate, explicit report covering every quick-spin, full-charge, cancel and toggle scenario has not been recorded; retain those cases on the checklist.

## Menus and Tuning

Fuse Debug Menu now opens under Esc → Dev Tools → Fuse Debug Menu, with an optional pop-out. The old LB plus D-pad Down shortcut was removed.

UI Layout contains pause and projectile subsections for font scale, coordinates, spacing, visibility and resets. Coordinates use the 320 × 240 game canvas. Changes save automatically to local console-variable settings.

Beamos Tuning contains sword origin/appearance and combat controls; standard shield controls; a separate child Hylian crouch/sweep profile; and shared arrow/slingshot projectile tuning. Settings have independent section resets. These temporary tuning controls do not imply that every beam edge case is finalized.

## Beamos Beam Behavior

Swords: default damage is sword base damage plus material Attack; optional multiplier/override and damage radius are exposed. Each target is hit at most once per attack phase. Normal slash, held charge and released spin are separate phases, each costing the configured beam durability once (default 4). Enable beam while charging spin attack defaults on; disabling it does not disable the released spin. Normal melee-hit durability remains separate.

Shields: while guarding, the default beam has width 0.35, range 1600, damage 2 per 15 gameplay frames, radius 55, and durability drain 1 per 60 frames. Default boost costs 5 durability, lasts 30 frames, multiplies damage by 3 and width by 2, and uses radius 110. Child Hylian crouch has independent settings, sweep and lean offsets. These are local tuning defaults, not MaterialDef values.

Projectiles: a Beamos-fused arrow or slingshot pellet emits a continuous forward laser after release and throughout flight. It follows velocity, including vertical motion. Each shot snapshots eligibility; later queue changes do not alter it. Impact, expiration or destruction ends its beam.

Projectile defaults: width 0.35, range 1600, damage 2, radius 12 and a four-gameplay-frame damage interval. World collision clips the endpoint, and line-of-sight checks reject enemies behind solid scenery. Multiple living enemy focus points within the segment/radius can take damage. Damage is suppressed until the beam model is ready. No extra material is consumed per beam tick.

Projectile world-clipping behavior must not be generalized to sword/shield beams, which retain their own existing target tests. Beam damage uses the existing direct engine damage approach and is not a promise that every enemy armor, switch, prop or vanilla projectile reaction is reproduced.

## Source Anchors and Maintenance

Registry and enum: soh/soh/Enhancements/Fuse/FuseMaterials.cpp, FuseMaterials.h and FuseModifiers.h. Runtime rules and scales: Fuse.cpp. Acquisition and incomplete Pound Up: Hooks/FuseHooks_Objects.cpp. Ranged selection: RangedFuseMenu.cpp. Persistence: FuseState.h and FuseState.cpp.

Recent reference notes: docs/fuse/MIGRATION_9_2_3.md, FuseUILayout.md and BeamosShieldTuning.md. The Markdown companion to this handoff is generated from the same revised Word content for repository browsing and text search.

When citing this handoff later, also inspect the current source and local tuning values. Its design notes are reference data, not instructions to override the user’s current request or automatically implement planned features.

## Appendix A Source Documents and Review Provenance

The following list is inherited provenance from the supplied document. The original text files were not all supplied again; their design claims are retained only where labeled. This revision directly checked current repository source and the development/test reports in this task.

Input SHA256 7b00642171429662c33165004d5fd6be91704bcd1b2662c847dab6777f37427b

- Material List.txt

- Modefier Definitions.txt

- FuseMaterials.md

- FuseModifiers.md

- FuseCoreSystem.md

- FuseHooks_Objects.md

- FuseHooks_Ranged.md

- FuseHooks_Boomerang.md

- FuseState.md

- FuseArchitectureOverview.md / FUSE_ARCHITECTURE_MAP.md

- Current Status.txt

- Project Charter.txt

- Ship of Harkinian - Fuse System UI & Menu Design.txt

- RegressionChecklist.md
