#include "soh/Enhancements/Fuse/Fuse.h"

#include <cstdint>
#include <cmath>
#include <unordered_map>

extern "C" {
#include "variables.h"
#include "z64.h"
#include "z64actor.h"
#include "z64collision_check.h"
#include <functions.h>
extern PlayState* gPlayState;
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
static int gLastCollisionDrainFrame = -1;
static std::unordered_map<uintptr_t, int> gLastCollisionDrainFrameByActor;
static bool gHammerFlagsLatched = false;
static uint32_t gSwordLatchedBaseDmgFlags[4] = {};

static constexpr int kSwordDamageDrainAmount = 1;

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

static bool IsPlayerSwordDamage(Collider* atCollider, ColliderInfo* atInfo) {
    if (!atCollider || !atInfo) {
        return false;
    }

    Actor* attacker = atCollider->actor;
    if (!attacker || attacker->category != ACTORCAT_PLAYER) {
        return false;
    }

    return (atInfo->toucher.dmgFlags & DMG_SWORD) != 0;
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

static void ResetHammerFlagsToLatchedBase(PlayState* play, Player* player, const char* reason) {
    if (!player || !gHammerFlagsLatched) {
        return;
    }

    uint32_t before[4];
    uint32_t after[4];

    for (int i = 0; i < 4; i++) {
        before[i] = player->meleeWeaponQuads[i].info.toucher.dmgFlags;
        player->meleeWeaponQuads[i].info.toucher.dmgFlags = gSwordLatchedBaseDmgFlags[i];
        after[i] = player->meleeWeaponQuads[i].info.toucher.dmgFlags;
    }

    const int frame = play ? play->gameplayFrames : -1;
    const char* tag = reason ? reason : "unspecified";

    Fuse::Log(
        "[FuseMVP] Reset hammer flags frame=%d reason=%s dmgFlags before=[%08X %08X %08X %08X] restored=[%08X %08X %08X %08X]\n",
        frame, tag, before[0], before[1], before[2], before[3], after[0], after[1], after[2], after[3]);

    gHammerFlagsLatched = false;
    for (uint32_t& v : gSwordLatchedBaseDmgFlags) {
        v = 0;
    }
}

static bool ApplyHammerFlagsToSwordHitbox(PlayState* play, Player* player) {
    if (!player)
        return false;

    const uint32_t flags = (kHammerDmgFlags0 | kHammerDmgFlags1);
    const int frame = play ? play->gameplayFrames : -1;
    const bool previouslyLatched = gHammerFlagsLatched;

    if (!gHammerFlagsLatched) {
        for (int i = 0; i < 4; i++) {
            gSwordLatchedBaseDmgFlags[i] = player->meleeWeaponQuads[i].info.toucher.dmgFlags;
        }
        gHammerFlagsLatched = true;
    }

    uint32_t before[4];
    uint32_t after[4];

    for (int i = 0; i < 4; i++) {
        before[i] = player->meleeWeaponQuads[i].info.toucher.dmgFlags;
        player->meleeWeaponQuads[i].info.toucher.dmgFlags |= flags;
        after[i] = player->meleeWeaponQuads[i].info.toucher.dmgFlags;
    }

    Fuse::Log(
        "[FuseMVP] Apply hammer flags frame=%d fused=%d latchedBase=%d dmgFlags before=[%08X %08X %08X %08X] after=[%08X %08X %08X %08X] addFlags=0x%08X\n",
        frame, Fuse::IsSwordFusedWithRock() ? 1 : 0, previouslyLatched ? 0 : 1, before[0], before[1], before[2],
        before[3], after[0], after[1], after[2], after[3], flags);

    return true;
}

// -----------------------------------------------------------------------------
// Cooldown helpers (per victim actor when available, otherwise once per frame)
// -----------------------------------------------------------------------------
static void CleanupCollisionCooldown(int currentFrame) {
    if (gLastCollisionDrainFrameByActor.empty()) {
        return;
    }

    // Keep the table small by pruning entries that have not been touched for a while.
    for (auto it = gLastCollisionDrainFrameByActor.begin(); it != gLastCollisionDrainFrameByActor.end();) {
        if (it->second + 120 < currentFrame) {
            it = gLastCollisionDrainFrameByActor.erase(it);
        } else {
            ++it;
        }
    }
}

static bool ShouldDrainThisCollision(PlayState* play, Actor* target, const int currentFrame, const char** reasonOut) {
    if (!play) {
        if (reasonOut) {
            *reasonOut = "no-play";
        }
        return false;
    }

    CleanupCollisionCooldown(currentFrame);

    if (target != nullptr) {
        const uintptr_t key = reinterpret_cast<uintptr_t>(target);
        auto [it, inserted] = gLastCollisionDrainFrameByActor.emplace(key, currentFrame);

        if (!inserted) {
            if (it->second == currentFrame) {
                if (reasonOut) {
                    *reasonOut = "victim-cooldown";
                }
                return false;
            }

            it->second = currentFrame;
        }

        return true;
    }

    if (gLastCollisionDrainFrame == currentFrame) {
        if (reasonOut) {
            *reasonOut = "frame-cooldown";
        }
        return false;
    }

    gLastCollisionDrainFrame = currentFrame;
    return true;
}

static void DrainSwordDurabilityOnATCollision(PlayState* play, Collider* atCollider, ColliderInfo* atInfo,
                                              Collider* acCollider, ColliderInfo* /*acInfo*/) {
    if (!Fuse::IsEnabled()) {
        return;
    }

    if (!IsPlayerSwordDamage(atCollider, atInfo)) {
        return;
    }

    const int currentFrame = play ? play->gameplayFrames : -1;
    Actor* attacker = atCollider ? atCollider->actor : nullptr;
    Actor* victim = acCollider ? acCollider->actor : nullptr;

    const bool fused = Fuse::IsSwordFusedWithRock();
    const int before = Fuse::GetSwordFuseDurability();
    int after = before;
    const int drainAmount = kSwordDamageDrainAmount;

    const char* skipReason = nullptr;
    bool shouldDrain = fused;

    if (!fused) {
        skipReason = "fuse-inactive";
    }

    if (shouldDrain) {
        shouldDrain = ShouldDrainThisCollision(play, victim, currentFrame, &skipReason);
    }

    if (shouldDrain) {
        const bool broke = Fuse::DamageSwordFuseDurability(drainAmount, "AT-collision");
        after = Fuse::GetSwordFuseDurability();
        Fuse::Log("[FuseMVP] Sword AT collision frame=%d fused=%d attacker=%p victim=%p amount=%d durability=%d->%d%s\n",
                  currentFrame, fused ? 1 : 0, (void*)attacker, (void*)victim, drainAmount, before, after,
                  broke ? " (broke)" : "");
    } else {
        Fuse::Log("[FuseMVP] Sword AT collision skipped frame=%d fused=%d attacker=%p victim=%p amount=%d durability=%d->%d reason=%s\n",
                  currentFrame, fused ? 1 : 0, (void*)attacker, (void*)victim, drainAmount, before, after,
                  skipReason ? skipReason : "unknown");
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

void OnLoadGame_ResetObjects() {
    gPrevHeldActor = nullptr;
    gPendingThrownRockCheck = false;
    gTrackedThrownRock = nullptr;
    gFramesUntilThrownRockCheck = 0;
    gLastCollisionDrainFrame = -1;
    gLastCollisionDrainFrameByActor.clear();
    gHammerFlagsLatched = false;
    for (uint32_t& v : gSwordLatchedBaseDmgFlags) {
        v = 0;
    }
}

void OnFrame_Objects_Pre(PlayState* play) {
    Player* player = GetPlayerSafe(play);

    if (!Fuse::IsEnabled()) {
        ResetHammerFlagsToLatchedBase(play, player, "fuse-disabled");
        return;
    }

    if (!IsPlayerSafeForInput(player)) {
        gPrevHeldActor = player ? player->heldActor : nullptr;
        ResetHammerFlagsToLatchedBase(play, player, "player-unsafe");
        return;
    }

    UpdateThrownRockAcquisition(play, player);

    const bool fused = Fuse::IsSwordFusedWithRock();
    const bool swinging = IsPlayerSwingingSword(player);
    const bool rocksNearby = IsAnyLiftableRockNearPlayer(play, player);

    // Rock-breaking behavior (works): apply hammer flags only when rocks are nearby and sword is fused
    if (fused && swinging && rocksNearby) {
        ApplyHammerFlagsToSwordHitbox(play, player);
    } else {
        const char* reason = !fused ? "fuse-inactive" : (!swinging ? "not-swinging" : "no-rock-nearby");
        ResetHammerFlagsToLatchedBase(play, player, reason);
    }
}

// Keep for compatibility if your FuseSystem calls it; unused now.
void OnFrame_Objects_Post(PlayState* /*play*/) {
}

void OnPlayerUpdate(PlayState* play) {
    if (!Fuse::IsEnabled())
        return;

    static int sHeartbeatCounter = 0;

    const int curFrame = play ? play->gameplayFrames : -1;
    Player* player = GetPlayerSafe(play);

    if ((sHeartbeatCounter++ % 30) == 0) {
        const bool playerExists = player != nullptr;
        const bool swordEquipped = playerExists && player->currentSwordItemId != ITEM_NONE;
        const bool fused = Fuse::IsSwordFusedWithRock();
        const int durability = Fuse::GetSwordFuseDurability();

        Fuse::Log("[FuseMVP] PlayerUpdate heartbeat frame=%d player=%d swordEquipped=%d fused=%d durability=%d\n",
                  curFrame, playerExists ? 1 : 0, swordEquipped ? 1 : 0, fused ? 1 : 0, durability);
    }

    (void)player;
}

void OnApplyDamage(PlayState* play, Actor* target, Collider* atCollider, ColliderInfo* atInfo) {
    (void)play;
    (void)target;
    (void)atCollider;
    (void)atInfo;
}

void OnSwordATCollision(PlayState* play, Collider* atCollider, ColliderInfo* atInfo, Collider* acCollider,
                        ColliderInfo* acInfo) {
    (void)acInfo;
    DrainSwordDurabilityOnATCollision(play, atCollider, atInfo, acCollider, acInfo);
}

void OnSwordFuseBroken() {
    PlayState* play = gPlayState;
    Player* player = GetPlayerSafe(play);

    ResetHammerFlagsToLatchedBase(play, player, "fuse-broke");
}

} // namespace FuseHooks

extern "C" void FuseHooks_OnApplyDamage(PlayState* play, Actor* target, Collider* atCollider, ColliderInfo* atInfo) {
    FuseHooks::OnApplyDamage(play, target, atCollider, atInfo);
}

extern "C" void FuseHooks_OnSwordATCollision(PlayState* play, Collider* atCollider, ColliderInfo* atInfo,
                                              Collider* acCollider, ColliderInfo* acInfo) {
    FuseHooks::OnSwordATCollision(play, atCollider, atInfo, acCollider, acInfo);
}

extern "C" void FuseHooks_OnSwordFuseBroken() {
    FuseHooks::OnSwordFuseBroken();
}
