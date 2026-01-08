#pragma once
#ifdef __cplusplus
extern "C" {
#endif

struct PlayState;
struct Actor;

void FuseHooks_OnBoomerangHitActor(struct PlayState* play, struct Actor* victim);

#ifdef __cplusplus
}
#endif
