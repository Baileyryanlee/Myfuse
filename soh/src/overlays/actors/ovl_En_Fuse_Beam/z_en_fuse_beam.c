/*
 * File: z_en_fuse_beam.c
 * Overlay: ovl_En_Fuse_Beam
 * Description: Minimal Beamos laser render host for Fuse experiments
 */

#include "z_en_fuse_beam.h"
#include "objects/object_vm/object_vm.h"
#include "soh/Enhancements/Fuse/FuseCBridge.h"

#define FLAGS (ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED)

void EnFuseBeam_Init(Actor* thisx, PlayState* play);
void EnFuseBeam_Destroy(Actor* thisx, PlayState* play);
void EnFuseBeam_Update(Actor* thisx, PlayState* play);
void EnFuseBeam_Draw(Actor* thisx, PlayState* play);

const ActorInit En_Fuse_Beam_InitVars = {
    ACTOR_UNSET_1AA,
    ACTORCAT_PROP,
    FLAGS,
    OBJECT_VM,
    sizeof(EnFuseBeam),
    (ActorFunc)EnFuseBeam_Init,
    (ActorFunc)EnFuseBeam_Destroy,
    (ActorFunc)EnFuseBeam_Update,
    (ActorFunc)EnFuseBeam_Draw,
    NULL,
};

void EnFuseBeam_Init(Actor* thisx, PlayState* play) {
    EnFuseBeam* this = (EnFuseBeam*)thisx;

    ActorShape_Init(&this->actor.shape, 0.0f, NULL, 0.0f);
    this->active = true;
    this->beamPos1 = this->actor.world.pos;
    this->beamRot.x = 0;
    this->beamRot.y = this->actor.world.rot.y;
    this->beamRot.z = 0;
    this->beamScale.x = 10.0f;
    this->beamScale.y = 10.0f;
    this->beamScale.z = 2400.0f;
    this->beamTexScroll = 0;
    this->loggedMissingObject = false;

    Fuse_DebugPrintf("[FuseDBG] EnFuseBeam spawned actor=%p pos=(%.2f,%.2f,%.2f) rotY=%d\n", (void*)this,
                     this->beamPos1.x, this->beamPos1.y, this->beamPos1.z, this->beamRot.y);
}

void EnFuseBeam_Destroy(Actor* thisx, PlayState* play) {
}

void EnFuseBeam_Update(Actor* thisx, PlayState* play) {
    EnFuseBeam* this = (EnFuseBeam*)thisx;

    if (this->active) {
        this->beamTexScroll += 0xC;
    }
}

void EnFuseBeam_Draw(Actor* thisx, PlayState* play) {
    EnFuseBeam* this = (EnFuseBeam*)thisx;

    if (!this->active) {
        return;
    }

    if ((this->actor.objBankIndex < 0) || !Object_IsLoaded(&play->objectCtx, this->actor.objBankIndex)) {
        if (!this->loggedMissingObject) {
            Fuse_DebugPrintf("[FuseDBG] EnFuseBeam draw skipped actor=%p objBankIndex=%d\n", (void*)this,
                             this->actor.objBankIndex);
            this->loggedMissingObject = true;
        }
        return;
    }

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPSegment(POLY_OPA_DISP++, 0x08, func_80094E78(play->state.gfxCtx, 0, this->beamTexScroll));
    Matrix_Translate(this->beamPos1.x, this->beamPos1.y, this->beamPos1.z, MTXMODE_NEW);
    Matrix_RotateZYX(this->beamRot.x, this->beamRot.y, this->beamRot.z, MTXMODE_APPLY);
    Matrix_Scale(this->beamScale.x * 0.1f, this->beamScale.x * 0.1f, this->beamScale.z * 0.0015f, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_OPA_DISP++, gBeamosLaserDL);

    CLOSE_DISPS(play->state.gfxCtx);
}
