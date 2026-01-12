#include "soh/Enhancements/Fuse/ShieldBashRules.h"

#include <cstddef>

namespace {
static const s16 kShieldBashActorBlacklist[] = {
    ACTOR_EN_SKB, // Stalchild
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
} // namespace

bool FuseBash_IsEnemyActor(const Actor* actor) {
    return actor != nullptr && actor->category == ACTORCAT_ENEMY;
}

bool FuseBash_IsBossActor(const Actor* actor) {
    return actor != nullptr && actor->category == ACTORCAT_BOSS;
}

bool FuseBash_IsKnockbackAllowed(const Actor* actor) {
    if (!FuseBash_IsEnemyActor(actor)) {
        return false;
    }

    if (FuseBash_IsBossActor(actor)) {
        return false;
    }

    return !FuseBash_IsActorBlacklisted(actor);
}

float FuseBash_GetKnockbackScalar(const Actor* actor) {
    if (!FuseBash_IsKnockbackAllowed(actor)) {
        return 0.0f;
    }

    if (actor == nullptr) {
        return 0.0f;
    }

    switch (actor->id) {
        default:
            return 1.0f;
    }
}
