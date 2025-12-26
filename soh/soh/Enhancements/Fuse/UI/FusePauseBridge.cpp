#include "soh/Enhancements/Fuse/UI/FusePauseBridge.h"

#include "soh/Enhancements/Fuse/Fuse.h"
#include <libultraship/libultra/gbi.h>
#include "global.h"
#include "functions.h"
#include <algorithm>
#include <cstdio>
#include <vector>

namespace {
constexpr s16 kPromptYOffset = 0;
constexpr s16 kPromptPadding = 8;
constexpr s16 kBarHeight = 4;
constexpr s16 kBarWidth = 48;
constexpr s16 kStatusYOffset = -16;

struct FuseModalState {
    bool open = false;
    int cursor = 0;
    int scroll = 0;
    int lastEquipSword = 0;
    int lastCursorX = -1;
    int lastCursorY = -1;
};

static FuseModalState sModal;

struct MaterialEntry {
    MaterialId id;
    const MaterialDef* def;
    int quantity;
};

std::vector<MaterialEntry> BuildMaterialList() {
    std::vector<MaterialEntry> materials;

    constexpr MaterialId kMaterialIds[] = {
        MaterialId::Rock,
        MaterialId::DekuNut,
    };

    for (MaterialId id : kMaterialIds) {
        const MaterialDef* def = Fuse::GetMaterialDef(id);
        if (!def) {
            continue;
        }

        materials.push_back({ id, def, Fuse::GetMaterialCount(id) });
    }

    return materials;
}

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

void FusePause_DrawPrompt(PlayState* play, Gfx** polyOpaDisp, Gfx** polyXluDisp) {
    if (play == nullptr || polyOpaDisp == nullptr || *polyOpaDisp == nullptr || polyXluDisp == nullptr ||
        *polyXluDisp == nullptr) {
        return;
    }

    PauseContext* pauseCtx = &play->pauseCtx;
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    Gfx*& OPA = *polyOpaDisp;
    Gfx*& XLU = *polyXluDisp;

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
        GfxPrint_Open(&debugPrinter, OPA);
        GfxPrint_SetColor(&debugPrinter, 255, 255, 0, 255);
        GfxPrint_SetPosPx(&debugPrinter, 20, 20);
        GfxPrint_Printf(&debugPrinter, "page:%d state:%d\n", pauseCtx->pageIndex, pauseCtx->state);
        GfxPrint_Printf(&debugPrinter, "cursorSlot:%d cursorX:%d cursorY:%d\n", pauseCtx->cursorSlot[PAUSE_EQUIP],
                        pauseCtx->cursorX[PAUSE_EQUIP], pauseCtx->cursorY[PAUSE_EQUIP]);
        GfxPrint_Printf(&debugPrinter, "equippedSword:%d hoveredSword:%d\n", equippedSword, hoveredSword);
        GfxPrint_Printf(&debugPrinter, "isPauseOpen:%d isEquip:%d isSwordRow:%d isOwned:%d isEquippedSlot:%d\n",
                        isPauseOpen, isEquipmentPage, isSwordRow, isOwnedEquip, isSwordAlreadyEquippedSlot);
        GfxPrint_Printf(&debugPrinter, "shouldShow:%d\n", shouldShowFusePrompt);

        OPA = GfxPrint_Close(&debugPrinter);
        GfxPrint_Destroy(&debugPrinter);
    }

    Fuse::Log("[FuseMVP] FusePause_DrawPrompt called\n");

    const bool swordFused = Fuse::IsSwordFused();
    Input* input = &play->state.input[0];
    const bool modalGatingPass = shouldShowFusePrompt && !swordFused;

    if (sModal.open) {
        const bool cursorMoved = (sModal.lastCursorX != cursorX) || (sModal.lastCursorY != cursorY);
        const bool equipChanged = sModal.lastEquipSword != equippedSword;
        if (!modalGatingPass || cursorMoved || equipChanged) {
            sModal.open = false;
        }
    }

    if (!sModal.open && modalGatingPass && CHECK_BTN_ALL(input->press.button, BTN_A)) {
        sModal.open = true;
        sModal.cursor = 0;
        sModal.scroll = 0;
        sModal.lastEquipSword = equippedSword;
        sModal.lastCursorX = cursorX;
        sModal.lastCursorY = cursorY;
        Fuse::Log("[FuseUI] Open modal\n");
        input->press.button &= ~BTN_A;
    }

    if (!shouldShowFusePrompt && !sModal.open) {
        return;
    }

    gDPPipeSync(OPA++);
    gDPSetPrimColor(OPA++, 0, 0, 255, 255, 255, 255);

    GfxPrint printer;

    GfxPrint_Init(&printer);
    GfxPrint_Open(&printer, OPA);
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

    OPA = GfxPrint_Close(&printer);
    GfxPrint_Destroy(&printer);

    if (swordFused) {
        const s32 barX = pauseCtx->infoPanelVtx[16].v.ob[0];
        const s32 barY = pauseCtx->infoPanelVtx[16].v.ob[1] + kStatusYOffset + 2;
        const s32 maxDurability = Fuse::GetSwordFuseMaxDurability();
        const s32 curDurability = Fuse::GetSwordFuseDurability();
        const f32 ratio = (maxDurability > 0) ? CLAMP((f32)curDurability / (f32)maxDurability, 0.0f, 1.0f) : 0.0f;
        const s32 filled = (s32)(ratio * kBarWidth);

        Gfx_SetupDL_39Opa(gfxCtx);

        gDPSetPrimColor(OPA++, 0, 0, 30, 30, 30, 255);
        gDPFillRectangle(OPA++, barX, barY, barX + kBarWidth, barY + kBarHeight);

        gDPSetPrimColor(OPA++, 0, 0, 60, 200, 60, 255);
        gDPFillRectangle(OPA++, barX, barY, barX + filled, barY + kBarHeight);
    }

    if (!sModal.open) {
        return;
    }

    if (CHECK_BTN_ALL(input->press.button, BTN_B)) {
        sModal.open = false;
        input->press.button &= ~BTN_B;
        return;
    }

    std::vector<MaterialEntry> materials = BuildMaterialList();
    const int entryCount = static_cast<int>(materials.size());

    const int maxCursor = (entryCount > 0) ? (entryCount - 1) : 0;
    sModal.cursor = std::clamp(sModal.cursor, 0, maxCursor);

    if (entryCount > 0 && CHECK_BTN_ALL(input->press.button, BTN_DUP)) {
        sModal.cursor = std::max(0, sModal.cursor - 1);
        input->press.button &= ~BTN_DUP;
    }

    if (entryCount > 0 && CHECK_BTN_ALL(input->press.button, BTN_DDOWN)) {
        sModal.cursor = std::min(maxCursor, sModal.cursor + 1);
        input->press.button &= ~BTN_DDOWN;
    }

    constexpr int kVisibleRows = 8;
    if (sModal.cursor < sModal.scroll) {
        sModal.scroll = sModal.cursor;
    }
    if (sModal.cursor >= sModal.scroll + kVisibleRows) {
        sModal.scroll = sModal.cursor - kVisibleRows + 1;
    }
    const int maxScroll = std::max(0, entryCount - kVisibleRows);
    sModal.scroll = std::clamp(sModal.scroll, 0, maxScroll);

    if (CHECK_BTN_ALL(input->press.button, BTN_A)) {
        if (entryCount > 0) {
            const MaterialEntry& entry = materials[sModal.cursor];
            char buffer[128];
            std::snprintf(buffer, sizeof(buffer), "[FuseUI] Selected material: %s (qty %d)\n",
                          entry.def ? entry.def->name : "Unknown", entry.quantity);
            Fuse::Log(buffer);
        } else {
            Fuse::Log("[FuseUI] Selected material: <none available>\n");
        }
        input->press.button &= ~BTN_A;
    }

    Gfx_SetupDL_25Xlu(gfxCtx);
    gDPPipeSync(XLU++);
    gDPSetPrimColor(XLU++, 0, 0, 0, 0, 0, 160);
    gDPFillRectangle(XLU++, 0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1);

    Gfx_SetupDL_39Opa(gfxCtx);

    constexpr s32 panelX = 40;
    constexpr s32 panelY = 40;
    constexpr s32 panelW = 240;
    constexpr s32 panelH = 160;
    constexpr s32 border = 2;

    gDPSetPrimColor(OPA++, 0, 0, 20, 20, 20, 240);
    gDPFillRectangle(OPA++, panelX, panelY, panelX + panelW, panelY + panelH);

    gDPSetPrimColor(OPA++, 0, 0, 200, 200, 200, 255);
    gDPFillRectangle(OPA++, panelX, panelY, panelX + panelW, panelY + border);
    gDPFillRectangle(OPA++, panelX, panelY + panelH - border, panelX + panelW, panelY + panelH);
    gDPFillRectangle(OPA++, panelX, panelY, panelX + border, panelY + panelH);
    gDPFillRectangle(OPA++, panelX + panelW - border, panelY, panelX + panelW, panelY + panelH);

    constexpr s32 textStartX = panelX + 12;
    constexpr s32 textStartY = panelY + 16;
    constexpr s32 listStartY = panelY + 40;
    constexpr s32 rowHeight = 14;

    for (int i = 0; i < kVisibleRows; i++) {
        const int entryIndex = sModal.scroll + i;
        if (entryIndex >= entryCount) {
            break;
        }

        if (entryIndex == sModal.cursor) {
            const s32 y0 = listStartY + (i * rowHeight) - 2;
            const s32 y1 = y0 + rowHeight;
            gDPSetPrimColor(OPA++, 0, 0, 60, 60, 80, 255);
            gDPFillRectangle(OPA++, panelX + 6, y0, panelX + panelW - 6, y1);
        }
    }

    GfxPrint_Init(&printer);
    GfxPrint_Open(&printer, OPA);
    GfxPrint_SetColor(&printer, 255, 255, 255, 255);

    GfxPrint_SetPosPx(&printer, textStartX, textStartY);
    GfxPrint_Printf(&printer, "Fuse");

    GfxPrint_SetPosPx(&printer, textStartX, textStartY + 14);
    GfxPrint_Printf(&printer, "A: Select   B: Back");

    if (entryCount == 0) {
        GfxPrint_SetPosPx(&printer, textStartX, listStartY);
        GfxPrint_Printf(&printer, "No materials available");
    } else {
        for (int i = 0; i < kVisibleRows; i++) {
            const int entryIndex = sModal.scroll + i;
            if (entryIndex >= entryCount) {
                break;
            }

            const MaterialEntry& entry = materials[entryIndex];
            GfxPrint_SetPosPx(&printer, textStartX, listStartY + (i * rowHeight));
            GfxPrint_Printf(&printer, "%s  x%d", entry.def ? entry.def->name : "Unknown", entry.quantity);
        }
    }

    OPA = GfxPrint_Close(&printer);
    GfxPrint_Destroy(&printer);
}
