#include "soh/Enhancements/Fuse/UI/FusePauseBridge.h"

#include "soh/Enhancements/Fuse/Fuse.h"
#include "soh/Enhancements/Fuse/FuseMaterials.h"
#include "global.h"
#include "functions.h"

namespace {
constexpr s16 kPromptYOffset = -8;
constexpr s16 kStatusYOffset = -16;
constexpr s16 kBarHeight = 4;
constexpr s16 kBarWidth = 48;
constexpr s16 kPromptPadding = 2;

EquipValueSword HoveredSwordForSlot(u16 cursorSlot) {
    switch (cursorSlot) {
        case 1:
            return EQUIP_VALUE_SWORD_KOKIRI;
        case 2:
            return EQUIP_VALUE_SWORD_MASTER;
        case 3:
            return EQUIP_VALUE_SWORD_BIGGORON;
        default:
            return EQUIP_VALUE_SWORD_NONE;
    }
}
}

void FusePause_DrawPrompt(PlayState* play) {
    if (play == nullptr) {
        return;
    }

    PauseContext* pauseCtx = &play->pauseCtx;
    GraphicsContext* gfxCtx = play->state.gfxCtx;

    const bool isPauseOpen = pauseCtx->state == 6;
    const bool isEquipmentPage = pauseCtx->pageIndex == PAUSE_EQUIP;
    const u16 cursorSlot = pauseCtx->cursorSlot[PAUSE_EQUIP];
    const bool isSwordSlotHovered = cursorSlot >= 1 && cursorSlot <= 3;
    const EquipValueSword equippedSword = static_cast<EquipValueSword>(CUR_EQUIP_VALUE(EQUIP_TYPE_SWORD));
    const EquipValueSword hoveredSword = HoveredSwordForSlot(cursorSlot);
    const bool isSwordAlreadyEquippedSlot = isSwordSlotHovered && (equippedSword == hoveredSword);
    const bool shouldShowFusePrompt =
        isPauseOpen && isEquipmentPage && isSwordSlotHovered && isSwordAlreadyEquippedSlot &&
        (equippedSword != EQUIP_VALUE_SWORD_NONE);

    static bool sShowDebugOverlay = true;
    if (sShowDebugOverlay && isPauseOpen) {
        GfxPrint debugPrinter;
        OPEN_DISPS(gfxCtx);

        GfxPrint_Init(&debugPrinter);
        GfxPrint_Open(&debugPrinter, POLY_OPA_DISP);
        GfxPrint_SetColor(&debugPrinter, 255, 255, 0, 255);
        GfxPrint_SetPosPx(&debugPrinter, 20, 20);
        GfxPrint_Printf(&debugPrinter, "page:%d state:%d\n", pauseCtx->pageIndex, pauseCtx->state);
        GfxPrint_Printf(&debugPrinter, "cursorSlot:%d cursorX:%d cursorY:%d\n", pauseCtx->cursorSlot[PAUSE_EQUIP],
                        pauseCtx->cursorX[PAUSE_EQUIP], pauseCtx->cursorY[PAUSE_EQUIP]);
        GfxPrint_Printf(&debugPrinter, "equippedSword:%d hoveredSword:%d\n", equippedSword, hoveredSword);
        GfxPrint_Printf(&debugPrinter, "isPauseOpen:%d isEquip:%d isSwordSlot:%d isEquippedSlot:%d\n", isPauseOpen,
                        isEquipmentPage, isSwordSlotHovered, isSwordAlreadyEquippedSlot);
        GfxPrint_Printf(&debugPrinter, "shouldShow:%d\n", shouldShowFusePrompt);

        POLY_OPA_DISP = GfxPrint_Close(&debugPrinter);
        GfxPrint_Destroy(&debugPrinter);
        CLOSE_DISPS(gfxCtx);
    }

    Fuse::Log("[FuseMVP] FusePause_DrawPrompt called\n");

    if (!shouldShowFusePrompt) {
        return;
    }

    const bool swordFused = Fuse::IsSwordFused();
    Input* input = &play->state.input[0];
    if (!swordFused && CHECK_BTN_ALL(input->press.button, BTN_A)) {
        Fuse::Log("[FuseMVP] Fuse pressed (stub)\n");
    }

    GfxPrint printer;
    OPEN_DISPS(gfxCtx);

    GfxPrint_Init(&printer);
    GfxPrint_Open(&printer, POLY_OPA_DISP);
    GfxPrint_SetColor(&printer, 255, 255, 255, 255);

    const s32 textX = pauseCtx->infoPanelVtx[16].v.ob[0] + kPromptPadding;
    const s32 textY = pauseCtx->infoPanelVtx[16].v.ob[1] + kPromptYOffset;
    GfxPrint_SetPosPx(&printer, textX, textY);

    if (!swordFused) {
        GfxPrint_Printf(&printer, "A: Fuse");
    } else {
        MaterialId materialId = Fuse::GetSwordMaterial();
        const MaterialDef* def = FuseMaterials::GetMaterialDef(materialId);
        const char* name = (def != nullptr && def->name != nullptr) ? def->name : "Unknown";
        const s32 statusY = pauseCtx->infoPanelVtx[16].v.ob[1] + kStatusYOffset;
        GfxPrint_SetPosPx(&printer, textX, statusY);
        GfxPrint_Printf(&printer, "Fused: %s", name);

        GfxPrint_SetPosPx(&printer, textX, textY);
        GfxPrint_Printf(&printer, "A: Fuse");
    }

    POLY_OPA_DISP = GfxPrint_Close(&printer);
    GfxPrint_Destroy(&printer);

    if (swordFused) {
        const s32 barX = pauseCtx->infoPanelVtx[16].v.ob[0];
        const s32 barY = pauseCtx->infoPanelVtx[16].v.ob[1] + kStatusYOffset + 2;
        const s32 maxDurability = Fuse::GetSwordFuseMaxDurability();
        const s32 curDurability = Fuse::GetSwordFuseDurability();
        const f32 ratio = (maxDurability > 0) ? CLAMP((f32)curDurability / (f32)maxDurability, 0.0f, 1.0f) : 0.0f;
        const s32 filled = (s32)(ratio * kBarWidth);

        Gfx_SetupDL_39Opa(gfxCtx);

        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 30, 30, 30, 255);
        gDPFillRectangle(POLY_OPA_DISP++, barX, barY, barX + kBarWidth, barY + kBarHeight);

        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 60, 200, 60, 255);
        gDPFillRectangle(POLY_OPA_DISP++, barX, barY, barX + filled, barY + kBarHeight);
    }

    CLOSE_DISPS(gfxCtx);
}
