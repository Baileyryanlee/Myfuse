#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "z64.h"

typedef enum {
    FUSE_BASH_KNOCKBACK_REASON_NON_ENEMY,
    FUSE_BASH_KNOCKBACK_REASON_BOSS,
    FUSE_BASH_KNOCKBACK_REASON_BLACKLIST,
    FUSE_BASH_KNOCKBACK_REASON_ALLOWED,
} FuseBashKnockbackReason;

typedef struct {
    bool allowed;
    float scalar;
    FuseBashKnockbackReason reason;
} FuseBashKnockbackResult;

bool FuseBash_IsEnemyActor(const Actor* actor);
bool FuseBash_IsBossActor(const Actor* actor);
bool FuseBash_IsKnockbackAllowed(const Actor* actor);
float FuseBash_GetKnockbackScalar(const Actor* actor);
FuseBashKnockbackResult FuseBash_EvaluateKnockback(const Actor* actor);

#ifdef __cplusplus
}
#endif
