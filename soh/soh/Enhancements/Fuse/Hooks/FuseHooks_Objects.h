#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "z64.h"
#include "z64actor.h"
#include "z64collision_check.h"

void FuseHooks_OnApplyDamage(PlayState* play, Actor* target, Collider* atCollider, ColliderInfo* atInfo);
void FuseHooks_OnSwordATCollision(PlayState* play, Collider* atCollider, ColliderInfo* atInfo, Collider* acCollider,
                                  ColliderInfo* acInfo);
void FuseHooks_OnSwordFuseBroken();

#ifdef __cplusplus
}

namespace FuseHooks {
void OnSwordFuseBroken();
}
#endif
