#pragma once
#ifdef __cplusplus
extern "C" {
#endif

struct PlayState;
struct Actor;
struct EnBoom;
struct Vec3f;

void FuseHooks_OnBoomerangHitActor(struct PlayState* play, struct Actor* victim);
void FuseHooks_OnBoomerangHitSurface(struct EnBoom* boom, struct PlayState* play, struct Vec3f* hitPos);

#ifdef __cplusplus
}
#endif
