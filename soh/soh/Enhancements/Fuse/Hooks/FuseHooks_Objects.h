#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "z64.h"
#include "z64actor.h"
#include "z64collision_check.h"

void FuseHooks_OnApplyDamage(PlayState* play, Actor* target, Collider* atCollider, ColliderInfo* atInfo);

#ifdef __cplusplus
}
#endif
