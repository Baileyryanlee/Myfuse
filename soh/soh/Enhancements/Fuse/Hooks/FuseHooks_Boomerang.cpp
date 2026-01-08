#include "FuseHooks_Boomerang.h"
#include "soh/Enhancements/Fuse/Fuse.h"

extern "C" void FuseHooks_OnBoomerangHitActor(PlayState* play, Actor* victim) {
    if (!play || !victim) {
        return;
    }

    Fuse::DamageBoomerangFuseDurability(play, 1, "Boomerang hit");
}
