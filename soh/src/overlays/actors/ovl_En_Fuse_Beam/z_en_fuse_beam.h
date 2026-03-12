#ifndef Z_EN_FUSE_BEAM_H
#define Z_EN_FUSE_BEAM_H

#include "global.h"

struct EnFuseBeam;

typedef struct EnFuseBeam {
    /* 0x0000 */ Actor actor;
    /* 0x014C */ Vec3f beamPos1;
    /* 0x0158 */ Vec3f beamScale;
    /* 0x0164 */ Vec3s beamRot;
    /* 0x016A */ s16 beamTexScroll;
    /* 0x016C */ s16 active;
    /* 0x016E */ s16 loggedMissingObject;
} EnFuseBeam;

#endif
