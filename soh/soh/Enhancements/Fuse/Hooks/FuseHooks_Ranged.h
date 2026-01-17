#pragma once

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

void FuseHooks_OnArrowProjectileFired(PlayState* play, int32_t arrowParams);
void FuseHooks_OnHookshotShotStarted(PlayState* play);
void FuseHooks_OnHookshotEnemyHit(PlayState* play);
void FuseHooks_OnHookshotLatched(PlayState* play);
void FuseHooks_OnHookshotRetracted(PlayState* play);
void FuseHooks_OnHookshotKilled(PlayState* play);

#ifdef __cplusplus
}
#endif
