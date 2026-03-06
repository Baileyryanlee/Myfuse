#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "z64.h"
#include <stdint.h>

typedef enum {
    FUSE_MATERIAL_NONE = 0,
    FUSE_MATERIAL_ROCK = 1,
    FUSE_MATERIAL_DEKU_NUT = 2,
    FUSE_MATERIAL_FROZEN_SHARD = 3,
    FUSE_MATERIAL_STICK = 4,
    FUSE_MATERIAL_BOMB = 5,
    FUSE_MATERIAL_KEESE_EYE = 6,
    FUSE_MATERIAL_FIRE_JELLY = 7,
    FUSE_MATERIAL_FIRE_KEESE_EYE = 8,
    FUSE_MATERIAL_BEAMOS_HEAD = 9,
} FuseMaterialId;

s32 Fuse_ShieldHasExplosion(PlayState* play, s32* outMaterialId, s32* outDurabilityCur, s32* outDurabilityMax,
                            u8* outLevel);

void Fuse_ShieldTriggerExplosion(PlayState* play, s32 materialId, u8 level, const Vec3f* pos);

void Fuse_AddMaterialById(s32 materialId, s32 amount);

void Fuse_DebugPrintf(const char* fmt, ...);

void Fuse_SetCachedBeamosVmSeg06(uintptr_t seg06, int frame);

uintptr_t Fuse_GetCachedBeamosVmSeg06(void);

int Fuse_GetCachedBeamosVmSeg06Frame(void);


#ifdef __cplusplus
}
#endif
