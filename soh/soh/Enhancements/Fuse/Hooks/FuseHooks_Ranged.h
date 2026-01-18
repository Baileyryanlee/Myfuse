#pragma once

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

	void FuseHooks_OnArrowProjectileFired(struct PlayState* play, int32_t isSeed);
	void FuseHooks_OnRangedProjectileHit(struct PlayState* play, struct Actor* victim, int32_t isSeed);
	void FuseHooks_OnHookshotShotStarted(PlayState* play);
	void FuseHooks_OnHookshotEnemyHit(PlayState* play, struct Actor* victim);
	void FuseHooks_OnHookshotLatched(PlayState* play);
	void FuseHooks_OnHookshotRetracted(PlayState* play);
	void FuseHooks_OnHookshotKilled(PlayState* play);

#ifdef __cplusplus
}
#endif
