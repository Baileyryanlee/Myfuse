#include "soh/Enhancements/Fuse/ShieldBashRules.h"

#include <cstddef>

namespace {
static constexpr s16 kShieldBashActorBlacklist[] = {
    ACTOR_EN_SKB,      // Stalchild
    ACTOR_EN_DH,       // Dead Hand
    ACTOR_EN_BIGOKUTA, // Big Octo
    ACTOR_EN_POH,      // Poe
    ACTOR_EN_PEEHAT,   // Peehat
    ACTOR_EN_MB,       // Moblin
    ACTOR_EN_VALI,     // Bari
    ACTOR_EN_SW,       // Skullwalltula/Gold Skulltula
    ACTOR_EN_FLOORMAS, // Floormaster
    ACTOR_EN_RD,       // Redead/Gibdo
};

static constexpr size_t kShieldBashActorBlacklistCount =
    sizeof(kShieldBashActorBlacklist) / sizeof(kShieldBashActorBlacklist[0]);

bool FuseBash_IsActorBlacklisted(const Actor* actor) {
    if (actor == nullptr) {
        return false;
    }

    for (size_t i = 0; i < kShieldBashActorBlacklistCount; ++i) {
        if (actor->id == kShieldBashActorBlacklist[i]) {
            return true;
        }
    }

    return false;
}

float FuseBash_GetPerActorScalarOverride(const Actor* actor) {
    (void)actor;
    // TODO: Add per-actor scalar overrides for shield bash knockback.
    return 1.0f;
}

float FuseBash_GetActorSizeScalar(const Actor* actor) {
    (void)actor;
    // TODO: Add size-based scaling for shield bash knockback.
    return 1.0f;
}
} // namespace

bool FuseBash_IsEnemyActor(const Actor* actor) {
    return actor != nullptr && actor->category == ACTORCAT_ENEMY;
}

bool FuseBash_IsBossActor(const Actor* actor) {
    return actor != nullptr && actor->category == ACTORCAT_BOSS;
}

bool FuseBash_IsKnockbackAllowed(const Actor* actor) {
    return FuseBash_EvaluateKnockback(actor).allowed;
}

float FuseBash_GetKnockbackScalar(const Actor* actor) {
    FuseBashKnockbackResult result = FuseBash_EvaluateKnockback(actor);
    return result.allowed ? result.scalar : 0.0f;
}

FuseBashKnockbackResult FuseBash_EvaluateKnockback(const Actor* actor) {
    FuseBashKnockbackResult result = {};
    result.allowed = false;
    result.scalar = 0.0f;
    result.reason = FUSE_BASH_KNOCKBACK_REASON_NON_ENEMY;

    if (actor == nullptr) {
        return result;
    }

    if (FuseBash_IsBossActor(actor)) {
        result.reason = FUSE_BASH_KNOCKBACK_REASON_BOSS;
        return result;
    }

    if (!FuseBash_IsEnemyActor(actor)) {
        result.reason = FUSE_BASH_KNOCKBACK_REASON_NON_ENEMY;
        return result;
    }

    if (FuseBash_IsActorBlacklisted(actor)) {
        result.reason = FUSE_BASH_KNOCKBACK_REASON_BLACKLIST;
        return result;
    }

    result.allowed = true;
    result.reason = FUSE_BASH_KNOCKBACK_REASON_ALLOWED;
    result.scalar = FuseBash_GetPerActorScalarOverride(actor) * FuseBash_GetActorSizeScalar(actor);
    return result;
}
