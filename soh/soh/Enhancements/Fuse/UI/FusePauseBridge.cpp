#include "soh/Enhancements/Fuse/UI/FusePauseBridge.h"

#include "soh/Enhancements/Fuse/Fuse.h"
#include <libultraship/libultra/gbi.h>
#include "global.h"
#include "functions.h"

namespace {
constexpr s16 kPromptYOffset = 0;
constexpr s16 kPromptPadding = 8;
constexpr s16 kBarHeight = 4;
constexpr s16 kBarWidth = 48;
constexpr s16 kStatusYOffset = -16;

EquipValueSword HoveredSwordForCursor(const PauseContext* pauseCtx) {
    if (pauseCtx == nullptr) {
        return EQUIP_VALUE_SWORD_NONE;
    }

    switch (pauseCtx->cursorX[PAUSE_EQUIP]) {
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

void FusePause_DrawPrompt(PlayState* play, Gfx** polyOpaDisp) {
    if (play == nullptr || polyOpaDisp == nullptr || *polyOpaDisp == nullptr) {
        return;
    }

    PauseContext* pauseCtx = &play->pauseCtx;
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    Gfx*& DISP = *polyOpaDisp;

    const bool isPauseOpen = pauseCtx->state == 6;
    const bool isEquipmentPage = pauseCtx->pageIndex == PAUSE_EQUIP;
    const u8 cursorX = pauseCtx->cursorX[PAUSE_EQUIP];
    const u8 cursorY = pauseCtx->cursorY[PAUSE_EQUIP];
    const bool isEquipmentGridCell = cursorX != 0;
    const bool isSwordRow = isEquipmentGridCell && cursorY == 0;
    const bool isOwnedEquip = isEquipmentGridCell && CHECK_OWNED_EQUIP(cursorY, cursorX - 1);
    const EquipValueSword hoveredSword =
        (isSwordRow && isOwnedEquip) ? HoveredSwordForCursor(pauseCtx) : EQUIP_VALUE_SWORD_NONE;
    const EquipValueSword equippedSword = static_cast<EquipValueSword>(CUR_EQUIP_VALUE(EQUIP_TYPE_SWORD));
    const bool isSwordAlreadyEquippedSlot = (hoveredSword != EQUIP_VALUE_SWORD_NONE) && (equippedSword == hoveredSword);
    const bool shouldShowFusePrompt = isPauseOpen && isEquipmentPage && isSwordRow && isOwnedEquip &&
                                      (hoveredSword != EQUIP_VALUE_SWORD_NONE) && isSwordAlreadyEquippedSlot;

    static bool sShowDebugOverlay = false;
    if (sShowDebugOverlay && isPauseOpen) {
        GfxPrint debugPrinter;

        GfxPrint_Init(&debugPrinter);
        GfxPrint_Open(&debugPrinter, DISP);
        GfxPrint_SetColor(&debugPrinter, 255, 255, 0, 255);
        GfxPrint_SetPosPx(&debugPrinter, 20, 20);
        GfxPrint_Printf(&debugPrinter, "page:%d state:%d\n", pauseCtx->pageIndex, pauseCtx->state);
        GfxPrint_Printf(&debugPrinter, "cursorSlot:%d cursorX:%d cursorY:%d\n", pauseCtx->cursorSlot[PAUSE_EQUIP],
                        pauseCtx->cursorX[PAUSE_EQUIP], pauseCtx->cursorY[PAUSE_EQUIP]);
        GfxPrint_Printf(&debugPrinter, "equippedSword:%d hoveredSword:%d\n", equippedSword, hoveredSword);
        GfxPrint_Printf(&debugPrinter, "isPauseOpen:%d isEquip:%d isSwordRow:%d isOwned:%d isEquippedSlot:%d\n",
                        isPauseOpen, isEquipmentPage, isSwordRow, isOwnedEquip, isSwordAlreadyEquippedSlot);
        GfxPrint_Printf(&debugPrinter, "shouldShow:%d\n", shouldShowFusePrompt);

        DISP = GfxPrint_Close(&debugPrinter);
        GfxPrint_Destroy(&debugPrinter);
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

    gDPPipeSync(DISP++);
    gDPSetPrimColor(DISP++, 0, 0, 255, 255, 255, 255);

    GfxPrint printer;

    GfxPrint_Init(&printer);
    GfxPrint_Open(&printer, DISP);
    GfxPrint_SetColor(&printer, 255, 255, 255, 255);

    // TODO: Fine-tune Fuse prompt placement once Fuse modal UI is implemented.
    // Pause UI is image-based; final alignment may change.
    const s32 baseY = pauseCtx->infoPanelVtx[16].v.ob[1];
    const s32 toEquipX = pauseCtx->infoPanelVtx[20].v.ob[0];
    const s32 toEquipW = pauseCtx->infoPanelVtx[21].v.ob[0] - pauseCtx->infoPanelVtx[20].v.ob[0];
    const s32 baseX = toEquipX + toEquipW + kPromptPadding;

    const s32 xCell = CLAMP(baseX / 8, 0, 39);

    const s32 yCell = CLAMP((baseY + kPromptYOffset) / 8, 0, 29);

    GfxPrint_SetPos(&printer, xCell, yCell);

    GfxPrint_Printf(&printer, "A: Fuse");

    DISP = GfxPrint_Close(&printer);
    GfxPrint_Destroy(&printer);

    if (swordFused) {
        const s32 barX = pauseCtx->infoPanelVtx[16].v.ob[0];
        const s32 barY = pauseCtx->infoPanelVtx[16].v.ob[1] + kStatusYOffset + 2;
        const s32 maxDurability = Fuse::GetSwordFuseMaxDurability();
        const s32 curDurability = Fuse::GetSwordFuseDurability();
        const f32 ratio = (maxDurability > 0) ? CLAMP((f32)curDurability / (f32)maxDurability, 0.0f, 1.0f) : 0.0f;
        const s32 filled = (s32)(ratio * kBarWidth);

        Gfx_SetupDL_39Opa(gfxCtx);

        gDPSetPrimColor(DISP++, 0, 0, 30, 30, 30, 255);
        gDPFillRectangle(DISP++, barX, barY, barX + kBarWidth, barY + kBarHeight);

        gDPSetPrimColor(DISP++, 0, 0, 60, 200, 60, 255);
        gDPFillRectangle(DISP++, barX, barY, barX + filled, barY + kBarHeight);
    }
}
