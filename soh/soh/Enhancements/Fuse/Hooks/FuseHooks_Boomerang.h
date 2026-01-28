#pragma once
#include "z64math.h"

#ifdef __cplusplus
extern "C" {
#endif

struct PlayState;
struct Actor;
struct EnBoom;

void FuseHooks_OnBoomerangHitActor(struct PlayState* play, struct Actor* victim);
void FuseHooks_OnBoomerangHitSurface(struct EnBoom* boom, struct PlayState* play, const Vec3f* hitPos);

#ifdef __cplusplus
}
#endif
