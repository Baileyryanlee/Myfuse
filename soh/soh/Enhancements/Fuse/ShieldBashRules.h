#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "z64.h"

bool FuseBash_IsEnemyActor(const Actor* actor);
bool FuseBash_IsBossActor(const Actor* actor);
bool FuseBash_IsKnockbackAllowed(const Actor* actor);
float FuseBash_GetKnockbackScalar(const Actor* actor);

#ifdef __cplusplus
}
#endif
