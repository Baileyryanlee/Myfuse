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

#ifdef __cplusplus
}

namespace FuseHooks {
void RestoreSwordHitboxVanillaNow(PlayState* play);
void OnSwordFuseBroken(PlayState* play);
void OnLoadGame_RestoreObjects();
void OnFrame_Objects_Pre(PlayState* play);
void OnFrame_Objects_Post(PlayState* play);
void OnPlayerUpdate(PlayState* play);
}
#endif
