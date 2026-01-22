#include "variables.h"
#include "z64.h"
#include "z64actor.h"
#include "overlays/actors/ovl_En_Fz/z_en_fz.h"
#include "functions.h"

#include "soh/Enhancements/Fuse/Fuse.h"

#include <cstdint>
#include <cmath>
#include <algorithm>
#include <unordered_set>

// Liftable rock actor
static constexpr int16_t kLiftableRockActorId = ACTOR_EN_ISHI;

// Thrown-rock acquisition timing
static constexpr int kFramesAfterThrowToCheck = 18;

// Proximity gate for enabling hammer flags (does NOT break rocks by itself; still requires real hit)
static constexpr float kRockGateRadius = 140.0f;
static constexpr float kRockGateRadiusSq = kRockGateRadius * kRockGateRadius;
static constexpr float kRockGateMaxYDiff = 90.0f;

// Local state
static int gHammerizeAppliedFrame = -1;
static Actor* gPrevHeldActor = nullptr;
static bool gPendingThrownRockCheck = false;
static Actor* gTrackedThrownRock = nullptr;
static int gFramesUntilThrownRockCheck = 0;
static int gLastImpactDrainFrame = -1;
static int gSwordATVictimCooldownFrame = -1;
static bool gWasHammerAttacking = false;
static std::unordered_set<void*> gSwordATVictimCooldown;
static std::unordered_set<void*> gAwardedFrozenShards;
static uint32_t gSwordBaseDmgFlags[4];
static bool gSwordBaseValid = false;

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------
static Player* GetPlayerSafe(PlayState* play) {
    if (!play)
        return nullptr;
    Actor* playerActor = play->actorCtx.actorLists[ACTORCAT_PLAYER].head;
    return (Player*)playerActor;
}

static bool IsActorStillInLists(PlayState* play, Actor* target) {
    if (!play || !target)
        return false;

    ActorContext* actorCtx = &play->actorCtx;
    for (int cat = 0; cat < ACTORCAT_MAX; cat++) {
        for (Actor* a = actorCtx->actorLists[cat].head; a; a = a->next) {
            if (a == target)
                return true;
        }
    }
    return false;
}

static bool IsPlayerSafeForInput(const Player* player) {
    if (!player)
        return false;

    if (player->stateFlags1 & (PLAYER_STATE1_INPUT_DISABLED | PLAYER_STATE1_IN_ITEM_CS | PLAYER_STATE1_IN_CUTSCENE |
                               PLAYER_STATE1_TALKING | PLAYER_STATE1_DEAD)) {
        return false;
    }

    if (player->stateFlags2 & PLAYER_STATE2_OCARINA_PLAYING) {
        return false;
    }

    return true;
}

static bool IsPlayerSwingingSword(const Player* player) {
    return player && player->meleeWeaponState != 0;
}

static bool IsPlayerHammerAttack(const Player* player) {
    return player && (player->heldItemAction == PLAYER_IA_HAMMER || player->itemAction == PLAYER_IA_HAMMER);
}

static bool IsHammerAttackActive(const Player* player) {
    return IsPlayerHammerAttack(player) && player->meleeWeaponState != 0;
}

static bool IsHammerGroundImpactFrame(Player* player) {
    if (!player) {
        return false;
    }

    if (player->meleeWeaponAnimation == PLAYER_MWA_HAMMER_FORWARD) {
        return LinkAnimation_OnFrame(&player->skelAnime, 7.0f);
    }

    if (player->meleeWeaponAnimation == PLAYER_MWA_HAMMER_SIDE) {
        return LinkAnimation_OnFrame(&player->skelAnime, 7.0f);
    }

    if (player->meleeWeaponAnimation == PLAYER_MWA_JUMPSLASH_FINISH) {
        return LinkAnimation_OnFrame(&player->skelAnime, 2.0f);
    }

    return false;
}

static bool IsAnyLiftableRockNearPlayer(PlayState* play, Player* player) {
    if (!play || !player)
        return false;

    const Vec3f p = player->actor.world.pos;

    ActorContext* actorCtx = &play->actorCtx;
    for (int cat = 0; cat < ACTORCAT_MAX; cat++) {
        for (Actor* a = actorCtx->actorLists[cat].head; a; a = a->next) {
            if (a->id != kLiftableRockActorId)
                continue;
            if (player->heldActor == a)
                continue;

            const float dx = a->world.pos.x - p.x;
            const float dy = a->world.pos.y - p.y;
            const float dz = a->world.pos.z - p.z;

            if (std::fabs(dy) > kRockGateMaxYDiff)
                continue;

            const float distSq = dx * dx + dz * dz;
            if (distSq <= kRockGateRadiusSq) {
                return true;
            }
        }
    }

    return false;
}

static bool IsPlayerSwordCollider(Player* player, Collider* collider) {
    if (!player || !collider)
        return false;

    if (collider->actor != &player->actor)
        return false;

    for (int i = 0; i < 4; i++) {
        Collider* quadCollider = (Collider*)&player->meleeWeaponQuads[i];
        if (collider == quadCollider || collider == &player->meleeWeaponQuads[i].base) {
            return true;
        }
    }

    return false;
}

static void CleanupAwardedFreezards(PlayState* play) {
    if (!play) {
        gAwardedFrozenShards.clear();
        return;
    }

    for (auto it = gAwardedFrozenShards.begin(); it != gAwardedFrozenShards.end();) {
        Actor* tracked = static_cast<Actor*>(*it);

        if (!IsActorStillInLists(play, tracked)) {
            it = gAwardedFrozenShards.erase(it);
        } else {
            ++it;
        }
    }
}

static void MaybeAwardFrozenShard(PlayState* play) {
    if (!play) {
        return;
    }

    CleanupAwardedFreezards(play);

    ActorContext* actorCtx = &play->actorCtx;

    for (int cat = 0; cat < ACTORCAT_MAX; cat++) {
        for (Actor* actor = actorCtx->actorLists[cat].head; actor; actor = actor->next) {
            if (actor->id != ACTOR_EN_FZ) {
                continue;
            }

            if (gAwardedFrozenShards.count(actor) > 0) {
                continue;
            }

            EnFz* freezard = reinterpret_cast<EnFz*>(actor);
            const bool flaggedForDespawn = freezard && freezard->isDespawning;
            const bool zeroHealth = actor->colChkInfo.health == 0;

            if (!flaggedForDespawn && !zeroHealth) {
                continue;
            }

            gAwardedFrozenShards.insert(actor);

            if (Rand_ZeroOne() < 0.25f) {
                Fuse::AddMaterial(MaterialId::FrozenShard, 1);
                const int newCount = Fuse::GetMaterialCount(MaterialId::FrozenShard);
                const char* reason = flaggedForDespawn ? "despawn" : "health0";

                Fuse::Log("[FuseDBG] MatGain: mat=%d qty=%d actor=%p pos=(%.2f,%.2f,%.2f) reason=%s\n",
                          static_cast<int>(MaterialId::FrozenShard), newCount, actor, actor->world.pos.x,
                          actor->world.pos.y, actor->world.pos.z, reason);
            }
        }
    }
}

// -----------------------------------------------------------------------------
// Hammer-sim flags (used to make En_Ishi break with native logic)
// -----------------------------------------------------------------------------

// From z_player.c table D_80854488 (last entry appears to be the hammer flags)
static constexpr uint32_t kHammerDmgFlags0 = 0x00000040;
static constexpr uint32_t kHammerDmgFlags1 = 0x40000000;

static void CaptureSwordBaseDmgFlags(PlayState* play, Player* player) {
    if (!play || !player)
        return;

    for (int i = 0; i < 4; i++) {
        gSwordBaseDmgFlags[i] = player->meleeWeaponQuads[i].info.toucher.dmgFlags;
    }

    gSwordBaseValid = true;
}

static void RestoreSwordBaseDmgFlags(Player* player) {
    if (!player || !gSwordBaseValid)
        return;

    for (int i = 0; i < 4; i++) {
        player->meleeWeaponQuads[i].info.toucher.dmgFlags = gSwordBaseDmgFlags[i];
    }
}

static uint32_t GetHammerFlagsForLevel(uint8_t level) {
    (void)level;
    return (kHammerDmgFlags0 | kHammerDmgFlags1);
}

static void ApplyHammerFlagsToSwordHitbox(Player* player, uint8_t level) {
    if (!player)
        return;

    const uint32_t flags = GetHammerFlagsForLevel(level);

    // OR in (dont replace). Called only when rocks are nearby to preserve enemy damage behavior.
    for (int i = 0; i < 4; i++) {
        const uint32_t baseFlags = gSwordBaseDmgFlags[i];
        player->meleeWeaponQuads[i].info.toucher.dmgFlags = baseFlags | flags;
    }

    Fuse::Log("[FuseMVP] Hammerize applied base0=0x%08X new0=0x%08X\n", gSwordBaseDmgFlags[0],
              player->meleeWeaponQuads[0].info.toucher.dmgFlags);
}

// -----------------------------------------------------------------------------
// Impact detection: read the quad AT flags that CollisionCheck_SetATvsAC / SetBounce set.
// In your hook timing, these are often most reliable from the PREVIOUS frame,
// so durability will drain 1 frame after the impact occurred.
// -----------------------------------------------------------------------------
static bool SwordHadImpactFlags(Player* player, const char** reasonOut = nullptr) {
    if (!player)
        return false;

    for (int i = 0; i < 4; i++) {
        ColliderQuad* quad = &player->meleeWeaponQuads[i];

        if (quad->base.atFlags & AT_HIT) {
            if (reasonOut) {
                *reasonOut = "AT_HIT";
            }
            return true;
        }

#ifdef AT_BOUNCED
        if (quad->base.atFlags & AT_BOUNCED) {
            if (reasonOut) {
                *reasonOut = "AT_BOUNCED";
            }
            return true;
        }
#endif
    }

    return false;
}

[[maybe_unused]] static void DrainSwordDurabilityOnImpact(PlayState* play, const char* reason) {
    const int curFrame = play ? play->gameplayFrames : -1;

    // Small cooldown: only drain once per frame to avoid multi-collisions in the same frame.
    if (curFrame == gLastImpactDrainFrame) {
        Fuse::Log("[FuseMVP] Impact hook fired but skipped (cooldown) event=hit reason=%s frame=%d\n", reason,
                  curFrame);
        return;
    }

    const bool fused = Fuse::IsSwordFused();
    const int before = Fuse::GetSwordFuseDurability();

    Fuse::Log("[FuseMVP] Impact hook fired event=hit amount=1 reason=%s durability=%d fused=%d\n", reason, before,
              fused ? 1 : 0);

    if (!fused) {
        // Still confirm the hook is running when no durability exists.
        return;
    }

    const bool broke = Fuse::DamageSwordFuseDurability(play, 1, reason);
    const int after = Fuse::GetSwordFuseDurability();

    Fuse::Log("[FuseMVP] Durability drained event=hit amount=1 reason=%s durability=%d->%d%s\n", reason, before, after,
              broke ? " (broke)" : "");

    gLastImpactDrainFrame = curFrame;
}

extern "C" void FuseHooks_OnSwordATCollision(PlayState* play, Collider* atCollider, ColliderInfo* atInfo,
                                             Collider* acCollider, ColliderInfo* acInfo) {
    if (!Fuse::IsEnabled())
        return;

    Player* player = GetPlayerSafe(play);
    if (!IsPlayerSwordCollider(player, atCollider))
        return;

    const int curFrame = play ? play->gameplayFrames : -1;
    if (gSwordATVictimCooldownFrame != curFrame) {
        gSwordATVictimCooldownFrame = curFrame;
        gSwordATVictimCooldown.clear();
    }

    void* victimPtr = nullptr;
    Actor* victimActor = nullptr;
    if (acCollider && acCollider->actor) {
        victimPtr = (void*)acCollider->actor;
        victimActor = acCollider->actor;
    } else if (acCollider) {
        victimPtr = (void*)acCollider;
    } else if (acInfo) {
        victimPtr = (void*)acInfo;
    }

    const bool isHammerAttack = IsPlayerHammerAttack(player);
    const bool fused = isHammerAttack ? Fuse::IsHammerFused() : Fuse::IsSwordFused();
    const int before = isHammerAttack ? Fuse::GetHammerFuseDurability() : Fuse::GetSwordFuseDurability();

    if (victimPtr && gSwordATVictimCooldown.count(victimPtr) > 0) {
        Fuse::Log(
            "[FuseMVP] %s AT collision DRAIN frame=%d fused=%d victim=%p durability=%d->%d reason=victim-cooldown\n",
            isHammerAttack ? "Hammer" : "Sword", curFrame, fused ? 1 : 0, victimPtr, before, before);
        return;
    }

    if (!fused) {
        Fuse::Log(
            "[FuseMVP] %s AT collision DRAIN frame=%d fused=%d victim=%p durability=%d->%d reason=fuse-inactive\n",
            isHammerAttack ? "Hammer" : "Sword", curFrame, 0, victimPtr, before, before);
        return;
    }

    const bool broke = isHammerAttack ? Fuse::DamageHammerFuseDurability(play, 1, "Hammer hit actor")
                                      : Fuse::DamageSwordFuseDurability(play, 1, "AT collision");
    const int after = isHammerAttack ? Fuse::GetHammerFuseDurability() : Fuse::GetSwordFuseDurability();
    Fuse::Log("[FuseMVP] %s AT collision DRAIN frame=%d fused=%d victim=%p durability=%d->%d%s\n",
              isHammerAttack ? "Hammer" : "Sword", curFrame, 1, victimPtr, before, after, broke ? " (broke)" : "");
    if (isHammerAttack) {
        Fuse::SetHammerHitActorThisSwing(true);
        Fuse::SetHammerDrainedThisSwing(true);
    }

    const char* shatterSrcLabel = isHammerAttack ? "hammer" : "sword";
    if (victimActor && victimPtr && gSwordATVictimCooldown.count(victimPtr) == 0 &&
        Fuse::TryFreezeShatter(play, victimActor, player ? &player->actor : nullptr, shatterSrcLabel)) {
        gSwordATVictimCooldown.insert(victimPtr);
        return;
    }

    if (victimPtr) {
        gSwordATVictimCooldown.insert(victimPtr);
    }

    if (victimActor) {
        if (isHammerAttack) {
            Fuse::OnHammerMeleeHit(play, victimActor);
        } else {
            Fuse::OnSwordMeleeHit(play, victimActor);
        }
    }
}

// -----------------------------------------------------------------------------
// Thrown rock acquisition logic (unchanged for now)
// -----------------------------------------------------------------------------
static void UpdateThrownRockAcquisition(PlayState* play, Player* player) {
    if (!play || !player)
        return;

    Actor* curHeld = player->heldActor;

    const bool prevWasRock = (gPrevHeldActor && gPrevHeldActor->id == kLiftableRockActorId);
    const bool releasedPrev = prevWasRock && (curHeld != gPrevHeldActor);

    if (releasedPrev) {
        gTrackedThrownRock = gPrevHeldActor;
        gPendingThrownRockCheck = true;
        gFramesUntilThrownRockCheck = kFramesAfterThrowToCheck;

        Fuse::SetLastEvent("Threw rock; tracking");
        Fuse::Log("[FuseMVP] Threw En_Ishi -> tracking ptr=%p\n", (void*)gTrackedThrownRock);
    }

    gPrevHeldActor = curHeld;

    if (!gPendingThrownRockCheck)
        return;

    if (--gFramesUntilThrownRockCheck > 0)
        return;

    const bool stillAlive = IsActorStillInLists(play, gTrackedThrownRock);
    if (!stillAlive) {
        Fuse::SetLastEvent("Rock broke; acquired ROCK");
        Fuse::Log("[FuseMVP] Tracked rock gone -> award ROCK\n");
        Fuse::AwardRockMaterial();
    } else {
        Fuse::SetLastEvent("Rock survived");
        Fuse::Log("[FuseMVP] Tracked rock still alive -> no award\n");
    }

    gPendingThrownRockCheck = false;
    gTrackedThrownRock = nullptr;
}

// -----------------------------------------------------------------------------
// Hooks
// -----------------------------------------------------------------------------
namespace FuseHooks {

void RestoreSwordHitboxVanillaNow(PlayState* play) {
    Player* player = GetPlayerSafe(play);
    const int frame = play ? play->gameplayFrames : -1;

    if (player && gSwordBaseValid) {
        RestoreSwordBaseDmgFlags(player);
        Fuse::Log("[FuseMVP] Break->vanilla: restored sword hitbox flags immediately frame=%d base0=0x%08X\n", frame,
                  gSwordBaseDmgFlags[0]);
    } else {
        Fuse::Log("[FuseMVP] Break->vanilla: restore skipped frame=%d player=%d baseValid=%d\n", frame, player ? 1 : 0,
                  gSwordBaseValid ? 1 : 0);
    }

    gHammerizeAppliedFrame = -1;
}

void OnSwordFuseBroken(PlayState* play) {
    Fuse::OnSwordFuseBroken(play);
}

void OnLoadGame_RestoreObjects() {
    gHammerizeAppliedFrame = -1;
    gPrevHeldActor = nullptr;
    gPendingThrownRockCheck = false;
    gTrackedThrownRock = nullptr;
    gFramesUntilThrownRockCheck = 0;
    gLastImpactDrainFrame = -1;
    gSwordATVictimCooldownFrame = -1;
    gWasHammerAttacking = false;
    gSwordATVictimCooldown.clear();
    gAwardedFrozenShards.clear();
    gSwordBaseValid = false;
    Fuse::ResetSwordFreezeQueue();
}

void OnFrame_Objects_Pre(PlayState* play) {
    if (!Fuse::IsEnabled())
        return;

    Player* player = GetPlayerSafe(play);
    if (!IsPlayerSafeForInput(player)) {
        gPrevHeldActor = player ? player->heldActor : nullptr;
        return;
    }

    if (gHammerizeAppliedFrame != -1 && play->gameplayFrames > gHammerizeAppliedFrame) {
        RestoreSwordBaseDmgFlags(player);
        Fuse::Log("[FuseMVP] Restored sword flags at frame=%d (from appliedFrame=%d)\n", play->gameplayFrames,
                  gHammerizeAppliedFrame);
        gHammerizeAppliedFrame = -1;
    }

    CaptureSwordBaseDmgFlags(play, player);

    UpdateThrownRockAcquisition(play, player);
    MaybeAwardFrozenShard(play);

    // Rock-breaking behavior (works): apply hammer flags only when rocks are nearby and fuse is active
    const uint8_t hammerLevel = Fuse::GetSwordModifierLevel(ModifierId::Hammerize);
    bool hammerApplied = false;

    if (hammerLevel > 0 && Fuse::IsSwordFused() && IsPlayerSwingingSword(player) && IsAnyLiftableRockNearPlayer(play, player)) {
        ApplyHammerFlagsToSwordHitbox(player, hammerLevel);
        gHammerizeAppliedFrame = play->gameplayFrames;
        hammerApplied = true;
        Fuse::Log("[FuseMVP] Hammerize applied at frame=%d\n", gHammerizeAppliedFrame);
    } else {
        RestoreSwordBaseDmgFlags(player);
    }

    if (!hammerApplied) {
        RestoreSwordBaseDmgFlags(player);
    }
}

void OnFrame_Objects_Post(PlayState* play) {
    Fuse::ProcessDeferredSwordFreezes(play);
}

void OnPlayerUpdate(PlayState* play) {
    if (!Fuse::IsEnabled())
        return;

    Player* player = GetPlayerSafe(play);
    if (!player)
        return;

    const bool isHammerAttacking = IsHammerAttackActive(player);
    if (isHammerAttacking && !gWasHammerAttacking) {
        Fuse::IncrementHammerSwingId();
        const s16 swingId = Fuse::GetHammerSwingId();
        Fuse::ResetHammerSwingTracking(swingId);
        Fuse::Log("[FuseMVP] Hammer swing start id=%d frame=%d\n", swingId, play ? play->gameplayFrames : -1);
    }

    gWasHammerAttacking = isHammerAttacking;

    if (isHammerAttacking && IsHammerGroundImpactFrame(player)) {
        const bool fused = Fuse::IsHammerFused();
        const int before = Fuse::GetHammerFuseDurability();

        if (fused) {
            const MaterialId materialId = Fuse::GetHammerMaterial();
            const MaterialDef* def = Fuse::GetMaterialDef(materialId);
            uint8_t poundLevel = 0;
            uint8_t megaStunLevel = 0;
            if (def) {
                HasModifier(def->modifiers, def->modifierCount, ModifierId::PoundUp, &poundLevel);
                HasModifier(def->modifiers, def->modifierCount, ModifierId::MegaStun, &megaStunLevel);
            }

            if (poundLevel > 0) {
                // TODO: Apply 25% hammer impact radius increase when a ground-impact radius hook exposes the value.
                Fuse::Log("[FuseDBG] HammerPoundUp: event=ground-impact item=hammer mat=%d lvl=%u dura=%d/%d note=no-radius\n",
                          static_cast<int>(materialId), static_cast<unsigned int>(poundLevel), before,
                          Fuse::GetHammerFuseMaxDurability());
            }

            if (megaStunLevel > 0) {
                Fuse_TriggerMegaStun(play, player, materialId, ITEM_HAMMER);
            }
        }

        if (!Fuse::HammerDrainedThisSwing() && fused && before > 0) {
            const bool broke = Fuse::DamageHammerFuseDurability(play, 1, "Hammer ground impact");
            const int after = Fuse::GetHammerFuseDurability();
            Fuse::SetHammerDrainedThisSwing(true);
            Fuse::Log("[FuseMVP] Hammer GROUND impact DRAIN frame=%d durability=%d->%d%s\n",
                      play ? play->gameplayFrames : -1, before, after, broke ? " (broke)" : "");
        }
    }

    if (!IsPlayerSwingingSword(player))
        return;

    const char* reason = "unknown";
    if (SwordHadImpactFlags(player, &reason)) {
        Fuse::Log("[FuseMVP] Impact hook observed reason=%s (drain handled in AT collision hook)\n", reason);
    }
}

} // namespace FuseHooks
