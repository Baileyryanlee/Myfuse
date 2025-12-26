#include "soh/Enhancements/Fuse/Fuse.h"

#include <cstdint>
#include <cmath>

extern "C" {
#include "variables.h"
#include "z64.h"
#include "z64actor.h"
#include <functions.h>
}

// Liftable rock actor
static constexpr int16_t kLiftableRockActorId = ACTOR_EN_ISHI;

// Thrown-rock acquisition timing
static constexpr int kFramesAfterThrowToCheck = 18;

// Proximity gate for enabling hammer flags (does NOT break rocks by itself; still requires real hit)
static constexpr float kRockGateRadius = 140.0f;
static constexpr float kRockGateRadiusSq = kRockGateRadius * kRockGateRadius;
static constexpr float kRockGateMaxYDiff = 90.0f;

// Local state
static Actor* gPrevHeldActor = nullptr;
static bool gPendingThrownRockCheck = false;
static Actor* gTrackedThrownRock = nullptr;
static int gFramesUntilThrownRockCheck = 0;
static int gLastImpactDrainFrame = -1;
static int gSwordFlagsFrame = -1;
static uint32_t gSwordBaseDmgFlags[4];
static bool gSwordBaseCaptured = false;

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

// -----------------------------------------------------------------------------
// Hammer-sim flags (used to make En_Ishi break with native logic)
// -----------------------------------------------------------------------------

// From z_player.c table D_80854488 (last entry appears to be the hammer flags)
static constexpr uint32_t kHammerDmgFlags0 = 0x00000040;
static constexpr uint32_t kHammerDmgFlags1 = 0x40000000;

static void CaptureSwordBaseDmgFlags(PlayState* play, Player* player) {
    if (!play || !player)
        return;

    const int curFrame = play->gameplayFrames;
    if (curFrame == gSwordFlagsFrame)
        return;

    gSwordFlagsFrame = curFrame;

    for (int i = 0; i < 4; i++) {
        gSwordBaseDmgFlags[i] = player->meleeWeaponQuads[i].info.toucher.dmgFlags;
    }

    gSwordBaseCaptured = true;

    if ((curFrame % 60) == 0) {
        Fuse::Log("[FuseMVP] Captured sword base dmgFlags frame=%d base0=0x%08X\n", curFrame,
                  gSwordBaseDmgFlags[0]);
    }
}

static void RestoreSwordBaseDmgFlags(Player* player) {
    if (!player || !gSwordBaseCaptured)
        return;

    for (int i = 0; i < 4; i++) {
        player->meleeWeaponQuads[i].info.toucher.dmgFlags = gSwordBaseDmgFlags[i];
    }
}

static void ApplyHammerFlagsToSwordHitbox(Player* player) {
    if (!player)
        return;

    const uint32_t flags = (kHammerDmgFlags0 | kHammerDmgFlags1);

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

static void DrainSwordDurabilityOnImpact(PlayState* play, const char* reason) {
    const int curFrame = play ? play->gameplayFrames : -1;

    // Small cooldown: only drain once per frame to avoid multi-collisions in the same frame.
    if (curFrame == gLastImpactDrainFrame) {
        Fuse::Log("[FuseMVP] Impact hook fired but skipped (cooldown) event=hit reason=%s frame=%d\n", reason,
                  curFrame);
        return;
    }

    const bool fused = Fuse::IsSwordFusedWithRock();
    const int before = Fuse::GetSwordFuseDurability();

    Fuse::Log("[FuseMVP] Impact hook fired event=hit amount=1 reason=%s durability=%d fused=%d\n", reason,
              before, fused ? 1 : 0);

    if (!fused) {
        // Still confirm the hook is running when no durability exists.
        return;
    }

    const bool broke = Fuse::DamageSwordFuseDurability(1);
    const int after = Fuse::GetSwordFuseDurability();

    Fuse::Log("[FuseMVP] Durability drained event=hit amount=1 reason=%s durability=%d->%d%s\n", reason,
              before, after, broke ? " (broke)" : "");

    gLastImpactDrainFrame = curFrame;
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

void OnLoadGame_ResetObjects() {
    gPrevHeldActor = nullptr;
    gPendingThrownRockCheck = false;
    gTrackedThrownRock = nullptr;
    gFramesUntilThrownRockCheck = 0;
    gLastImpactDrainFrame = -1;
    gSwordFlagsFrame = -1;
    gSwordBaseCaptured = false;
}

void OnFrame_Objects_Pre(PlayState* play) {
    if (!Fuse::IsEnabled())
        return;

    Player* player = GetPlayerSafe(play);
    if (!IsPlayerSafeForInput(player)) {
        gPrevHeldActor = player ? player->heldActor : nullptr;
        return;
    }

    CaptureSwordBaseDmgFlags(play, player);

    UpdateThrownRockAcquisition(play, player);

    // Rock-breaking behavior (works): apply hammer flags only when rocks are nearby
    if (IsPlayerSwingingSword(player) && IsAnyLiftableRockNearPlayer(play, player)) {
        ApplyHammerFlagsToSwordHitbox(player);
    } else {
        RestoreSwordBaseDmgFlags(player);
    }
}

void OnFrame_Objects_Post(PlayState* play) {
    if (!Fuse::IsEnabled())
        return;

    Player* player = GetPlayerSafe(play);
    if (!player)
        return;

    RestoreSwordBaseDmgFlags(player);
}

void OnPlayerUpdate(PlayState* play) {
    if (!Fuse::IsEnabled())
        return;

    Player* player = GetPlayerSafe(play);
    if (!IsPlayerSwingingSword(player))
        return;

    const char* reason = "unknown";
    if (SwordHadImpactFlags(player, &reason)) {
        DrainSwordDurabilityOnImpact(play, reason);
    }
}

} // namespace FuseHooks
