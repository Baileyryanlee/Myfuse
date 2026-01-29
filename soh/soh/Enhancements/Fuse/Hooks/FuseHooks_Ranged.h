#pragma once

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

	typedef enum RangedFuseSlotId {
		RANGED_FUSE_SLOT_ARROWS = 0,
		RANGED_FUSE_SLOT_SLINGSHOT = 1,
		RANGED_FUSE_SLOT_HOOKSHOT = 2,
	} RangedFuseSlotId;

	void Fuse_OnRangedHitActor(struct PlayState* play, RangedFuseSlotId slot, struct Actor* victim,
	                           const Vec3f* impactPos);
	void FuseHooks_OnArrowProjectileFired(struct PlayState* play, int32_t isSeed);
	void FuseHooks_OnRangedProjectileHit(struct PlayState* play, struct Actor* victim, Vec3f* impactPos, int32_t isSeed);
	void FuseHooks_OnRangedProjectileHitSurface(struct PlayState* play, Vec3f* impactPos, int32_t isSeed);
	void FuseHooks_OnHookshotShotStarted(PlayState* play);
	void FuseHooks_OnHookshotEnemyHit(PlayState* play, struct Actor* victim, Vec3f* impactPos);
	void FuseHooks_OnHookshotSurfaceHit(PlayState* play, Vec3f* impactPos);
	void FuseHooks_OnHookshotLatched(PlayState* play);
	void FuseHooks_OnHookshotRetracted(PlayState* play);
	void FuseHooks_OnHookshotKilled(PlayState* play);

#ifdef __cplusplus
}
#endif
