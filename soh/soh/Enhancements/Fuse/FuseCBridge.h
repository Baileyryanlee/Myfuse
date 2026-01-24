#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "z64.h"

s32 Fuse_ShieldHasExplosion(PlayState* play,
                            s32* outMaterialId,
                            s32* outDurabilityCur,
                            s32* outDurabilityMax,
                            u8* outLevel);

void Fuse_ShieldTriggerExplosion(PlayState* play,
                                 s32 materialId,
                                 u8 level,
                                 const Vec3f* pos);

#ifdef __cplusplus
}
#endif
