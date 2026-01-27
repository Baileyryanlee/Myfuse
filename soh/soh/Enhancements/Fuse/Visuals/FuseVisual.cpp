#include "soh/Enhancements/Fuse/Visuals/FuseVisual.h"
#include "soh/Enhancements/Fuse/Fuse.h"
#include "soh/frame_interpolation.h" // <-- REQUIRED when using OPEN_DISPS/CLOSE_DISPS in some TUs

#include <cstdint>

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "z64.h"
#include "objects/gameplay_field_keep/gameplay_field_keep.h"
}

extern "C" s32 Object_Spawn(ObjectContext* objectCtx, s16 objectId);


namespace {
    struct AttachmentTransform {
        Vec3f offset;
        Vec3s rot;
        float scale;
    };

    constexpr s32 kObjectSpawnRetryFrames = 30;

    const AttachmentTransform kLeftHandAdult = { { 0.0f, -80.0f, 1200.0f }, { 0, 0, 0 }, 0.45f };
    const AttachmentTransform kLeftHandChild = { { 0.0f, -70.0f, 1050.0f }, { 0, 0, 0 }, 0.40f };
    const AttachmentTransform kShieldAdult = { { 0.0f, 0.0f, 900.0f }, { 0, 0, 0 }, 0.55f };
    const AttachmentTransform kShieldChild = { { 0.0f, 0.0f, 820.0f }, { 0, 0, 0 }, 0.50f };

    s32 EnsureObjectLoaded(PlayState* play, s16 objectId, const char* tag) {
        static s32 sLastObjIndex = -2;
        static bool sLastLoaded = false;
        static bool sLastMissing = false;
        static s32 sLastSpawnFrame = -1000;

        if (play == nullptr) {
            return -1;
        }

        s32 objIdx = Object_GetIndex(&play->objectCtx, objectId);
        if (objIdx >= 0) {
            const bool isLoaded = Object_IsLoaded(&play->objectCtx, objIdx);
            if (objIdx != sLastObjIndex || isLoaded != sLastLoaded || sLastMissing) {
                Fuse::Log("[FuseVisual] %s object idx=%d loaded=%d\n", tag, objIdx, isLoaded ? 1 : 0);
                sLastObjIndex = objIdx;
                sLastLoaded = isLoaded;
                sLastMissing = false;
            }
            return objIdx;
        }

        if (!sLastMissing) {
            Fuse::Log("[FuseVisual] %s object missing; requesting load\n", tag);
            sLastMissing = true;
            sLastObjIndex = objIdx;
            sLastLoaded = false;
        }

        if (play->gameplayFrames - sLastSpawnFrame >= kObjectSpawnRetryFrames) {
            sLastSpawnFrame = play->gameplayFrames;
            s32 spawnedIdx = Object_Spawn(&play->objectCtx, objectId);
            Fuse::Log("[FuseVisual] %s object spawn queued idx=%d\n", tag, spawnedIdx);
            sLastObjIndex = spawnedIdx;
            sLastLoaded = false;
            sLastMissing = false;
            return spawnedIdx;
        }

        return -1;
    }

    void DrawDisplayListSegment06(PlayState* play, s32 objIdx, uintptr_t restoreSeg06, Gfx* dl) {
        if (play == nullptr || play->state.gfxCtx == nullptr || objIdx < 0 || dl == nullptr) {
            return;
        }

        GraphicsContext* gfxCtx = play->state.gfxCtx;

        // Pull the current display list pointer
        Gfx* polyOpa = gfxCtx->polyOpa.p;
        if (polyOpa == nullptr) {
            return;
        }

        Gfx_SetupDL_25Opa(gfxCtx);

        gDPSetPrimColor(polyOpa++, 0, 0, 255, 255, 255, 255);
        gSPSegment(polyOpa++, 0x06, (uintptr_t)play->objectCtx.status[objIdx].segment);
        gSPMatrix(polyOpa++, MATRIX_NEWMTX(gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(polyOpa++, dl);
        gSPSegment(polyOpa++, 0x06, restoreSeg06);

        // Write back the advanced pointer
        gfxCtx->polyOpa.p = polyOpa;
    }

    const AttachmentTransform& GetLeftHandTransform() {
        return LINK_IS_ADULT ? kLeftHandAdult : kLeftHandChild;
    }

    const AttachmentTransform& GetShieldTransform() {
        return LINK_IS_ADULT ? kShieldAdult : kShieldChild;
    }

    bool IsSegmentValid(uintptr_t segment) {
        return segment != 0;
    }

    uintptr_t GetPlayerSegment(PlayState* play, const Player* player) {
        if (play == nullptr || player == nullptr) {
            return 0;
        }

        s32 playerObjIdx = player->actor.objBankIndex;
        if (playerObjIdx < 0) {
            return 0;
        }

        return reinterpret_cast<uintptr_t>(play->objectCtx.status[playerObjIdx].segment);
    }
} // namespace

namespace FuseVisual {
    void DrawLeftHandAttachments(PlayState* play, Player* player) {
        if (play == nullptr || player == nullptr) {
            return;
        }

        if (!Fuse::IsEnabled() || !Fuse::IsSwordFused() || Fuse::GetSwordMaterial() != MaterialId::Rock ||
            Fuse::GetSwordFuseDurability() <= 0) {
            return;
        }

        s32 rockObjIdx = EnsureObjectLoaded(play, OBJECT_GAMEPLAY_FIELD_KEEP, "Ishi");
        if (rockObjIdx < 0 || !Object_IsLoaded(&play->objectCtx, rockObjIdx)) {
            return;
        }

        uintptr_t restoreSeg06 = GetPlayerSegment(play, player);
        if (!IsSegmentValid(restoreSeg06)) {
            return;
        }

        const AttachmentTransform& xf = GetLeftHandTransform();

        Matrix_Push();
        Matrix_Translate(xf.offset.x, xf.offset.y, xf.offset.z, MTXMODE_APPLY);
        Matrix_RotateZYX(xf.rot.x, xf.rot.y, xf.rot.z, MTXMODE_APPLY);
        Matrix_Scale(xf.scale, xf.scale, xf.scale, MTXMODE_APPLY);
        DrawDisplayListSegment06(play, rockObjIdx, restoreSeg06, (Gfx*)gSilverRockDL);
        Matrix_Pop();
    }

    void DrawShieldAttachments(PlayState* play, Player* player) {
        if (play == nullptr || player == nullptr) {
            return;
        }

        const FuseSlot slot = Fuse::GetActiveShieldSlot();
        if (!Fuse::IsEnabled() || slot.materialId != MaterialId::Rock || slot.durabilityCur <= 0 ||
            player->rightHandType != PLAYER_MODELTYPE_RH_FF) {
            return;
        }

        s32 rockObjIdx = EnsureObjectLoaded(play, OBJECT_GAMEPLAY_FIELD_KEEP, "Ishi");
        if (rockObjIdx < 0 || !Object_IsLoaded(&play->objectCtx, rockObjIdx)) {
            return;
        }

        uintptr_t restoreSeg06 = GetPlayerSegment(play, player);
        if (!IsSegmentValid(restoreSeg06)) {
            return;
        }

        const AttachmentTransform& xf = GetShieldTransform();

        Matrix_Push();
        Matrix_Put(&player->shieldMf);
        Matrix_Translate(xf.offset.x, xf.offset.y, xf.offset.z, MTXMODE_APPLY);
        Matrix_RotateZYX(xf.rot.x, xf.rot.y, xf.rot.z, MTXMODE_APPLY);
        Matrix_Scale(xf.scale, xf.scale, xf.scale, MTXMODE_APPLY);
        DrawDisplayListSegment06(play, rockObjIdx, restoreSeg06, (Gfx*)gSilverRockDL);
        Matrix_Pop();
    }
} // namespace FuseVisual

extern "C" void FuseVisual_DrawLeftHandAttachments(PlayState* play, Player* player) {
    FuseVisual::DrawLeftHandAttachments(play, player);
}

extern "C" void FuseVisual_DrawShieldAttachments(PlayState* play, Player* player) {
    FuseVisual::DrawShieldAttachments(play, player);
}
