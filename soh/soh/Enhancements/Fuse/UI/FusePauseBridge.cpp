#include "soh/Enhancements/Fuse/UI/FusePauseBridge.h"

#include "soh/Enhancements/Fuse/Fuse.h"
#include "global.h"
#include "functions.h"

namespace {
constexpr s16 kPromptYOffset = -12;
constexpr s16 kBarHeight = 4;
constexpr s16 kBarWidth = 48;

bool PauseOnEquippedSwordSlot(PauseContext* pauseCtx) {
    if (pauseCtx->pageIndex != PAUSE_EQUIP) {
        return false;
    }

    constexpr u16 kFirstSwordSlot = 1;
    constexpr u16 kLastSwordSlot = 3;

    const u16 cursorSlot = pauseCtx->cursorSlot[PAUSE_EQUIP];
    const bool onSwordSlot = cursorSlot >= kFirstSwordSlot && cursorSlot <= kLastSwordSlot;
    if (!onSwordSlot) {
        return false;
    }

    return CUR_EQUIP_VALUE(EQUIP_TYPE_SWORD) != EQUIP_VALUE_SWORD_NONE;
}
}

void FusePause_DrawPrompt(PlayState* play) {
    if (play == nullptr) {
        return;
    }

    PauseContext* pauseCtx = &play->pauseCtx;

    if ((pauseCtx->state != 6) || !PauseOnEquippedSwordSlot(pauseCtx)) {
        return;
    }

    GfxPrint printer;
    OPEN_DISPS(play->state.gfxCtx);

    GfxPrint_Init(&printer);
    GfxPrint_Open(&printer, POLY_OPA_DISP);
    GfxPrint_SetColor(&printer, 255, 255, 255, 255);

    const s32 textX = pauseCtx->infoPanelVtx[20].v.ob[0] + 4;
    const s32 textY = pauseCtx->infoPanelVtx[16].v.ob[1] + kPromptYOffset;
    GfxPrint_SetPosPx(&printer, textX, textY);

    const bool swordFused = Fuse::IsSwordFused();
    if (!swordFused) {
        GfxPrint_Printf(&printer, "Fuse");

        if (CHECK_BTN_ALL(play->state.input[0].press.button, BTN_A)) {
            Fuse::Log("FusePause_DrawPrompt: stub fuse action (no-op)");
        }
    } else {
        MaterialId materialId = Fuse::GetSwordMaterial();
        const MaterialDef* def = FuseMaterials::GetMaterialDef(materialId);
        const char* name = (def != nullptr && def->name != nullptr) ? def->name : "Unknown";
        GfxPrint_Printf(&printer, "Fused: %s", name);
    }

    POLY_OPA_DISP = GfxPrint_Close(&printer);
    GfxPrint_Destroy(&printer);

    if (swordFused) {
        const s32 barX = pauseCtx->infoPanelVtx[20].v.ob[0];
        const s32 barY = pauseCtx->infoPanelVtx[16].v.ob[1] + 2;
        const s32 maxDurability = Fuse::GetSwordFuseMaxDurability();
        const s32 curDurability = Fuse::GetSwordFuseDurability();
        const f32 ratio = (maxDurability > 0) ? CLAMP((f32)curDurability / (f32)maxDurability, 0.0f, 1.0f) : 0.0f;
        const s32 filled = (s32)(ratio * kBarWidth);

        Gfx_SetupDL_39Opa(play->state.gfxCtx);

        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 30, 30, 30, 255);
        gDPFillRectangle(POLY_OPA_DISP++, barX, barY, barX + kBarWidth, barY + kBarHeight);

        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 60, 200, 60, 255);
        gDPFillRectangle(POLY_OPA_DISP++, barX, barY, barX + filled, barY + kBarHeight);
    }

    CLOSE_DISPS(play->state.gfxCtx);
}

