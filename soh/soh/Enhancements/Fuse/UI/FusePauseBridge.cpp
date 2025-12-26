#include "soh/Enhancements/Fuse/UI/FusePauseBridge.h"

#include "global.h"
#include <libultraship/libultra/gbi.h>
#include "functions.h"
#include "soh/Enhancements/Fuse/Fuse.h"
#include <algorithm>
#include <cstdio>
#include <vector>

namespace {
constexpr s16 kPromptYOffset = 0;
constexpr s16 kPromptPadding = 8;
constexpr s16 kBarHeight = 4;
constexpr s16 kBarWidth = 48;
constexpr s16 kStatusYOffset = -16;

constexpr s32 kPanelX = 40;
constexpr s32 kPanelY = 40;
constexpr s32 kPanelW = 240;
constexpr s32 kPanelH = 160;
constexpr s32 kTitleY = kPanelY + 12;
constexpr s32 kListX = kPanelX + 12;
constexpr s32 kListY = kPanelY + 36;
constexpr s32 kRowH = 14;
constexpr s32 kVisibleRows = 8;

struct FuseModalState {
    bool open = false;
    int cursor = 0;
    int scroll = 0;
    bool pendingSelect = false;
};

static FuseModalState sModal;
static int sLastModalFrame = -1;

struct MaterialEntry {
    MaterialId id;
    const MaterialDef* def;
    int quantity;
    bool enabled;
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

        const int qty = Fuse::GetMaterialCount(id);
        materials.push_back({ id, def, qty, qty > 0 });
    }

    return materials;
}

int MoveCursor(int delta, const std::vector<MaterialEntry>& materials) {
    const int entryCount = static_cast<int>(materials.size());
    if (entryCount == 0) {
        return 0;
    }

    int newCursor = std::clamp(sModal.cursor + delta, 0, entryCount - 1);

    while (entryCount > 0 && !materials[newCursor].enabled && newCursor != sModal.cursor) {
        const int next = std::clamp(newCursor + delta, 0, entryCount - 1);
        if (next == newCursor) {
            break;
        }
        newCursor = next;
    }

    return newCursor;
}

void UpdateModalBounds(const std::vector<MaterialEntry>& materials) {
    const int entryCount = static_cast<int>(materials.size());
    const int maxCursor = (entryCount > 0) ? (entryCount - 1) : 0;
    sModal.cursor = std::clamp(sModal.cursor, 0, maxCursor);

    if (sModal.cursor < sModal.scroll) {
        sModal.scroll = sModal.cursor;
    }
    if (sModal.cursor >= sModal.scroll + kVisibleRows) {
        sModal.scroll = sModal.cursor - kVisibleRows + 1;
    }

    const int maxScroll = std::max(0, entryCount - kVisibleRows);
    sModal.scroll = std::clamp(sModal.scroll, 0, maxScroll);
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

struct FusePromptContext {
    bool isPauseOpen = false;
    bool isEquipmentPage = false;
    bool isEquipmentGridCell = false;
    bool isSwordRow = false;
    bool isOwnedEquip = false;
    EquipValueSword hoveredSword = EQUIP_VALUE_SWORD_NONE;
    EquipValueSword equippedSword = EQUIP_VALUE_SWORD_NONE;
    bool isSwordAlreadyEquippedSlot = false;
    bool shouldShowFusePrompt = false;
};

FusePromptContext BuildPromptContext(PlayState* play) {
    FusePromptContext context;

    if (play == nullptr) {
        return context;
    }

    PauseContext* pauseCtx = &play->pauseCtx;

    context.isPauseOpen = pauseCtx->state == 6;
    context.isEquipmentPage = pauseCtx->pageIndex == PAUSE_EQUIP;
    context.isEquipmentGridCell = pauseCtx->cursorX[PAUSE_EQUIP] != 0;
    context.isSwordRow = context.isEquipmentGridCell && pauseCtx->cursorY[PAUSE_EQUIP] == 0;
    context.isOwnedEquip =
        context.isEquipmentGridCell && CHECK_OWNED_EQUIP(pauseCtx->cursorY[PAUSE_EQUIP], pauseCtx->cursorX[PAUSE_EQUIP] - 1);
    context.hoveredSword = (context.isSwordRow && context.isOwnedEquip) ? HoveredSwordForCursor(pauseCtx) : EQUIP_VALUE_SWORD_NONE;
    context.equippedSword = static_cast<EquipValueSword>(CUR_EQUIP_VALUE(EQUIP_TYPE_SWORD));
    context.isSwordAlreadyEquippedSlot = (context.hoveredSword != EQUIP_VALUE_SWORD_NONE) &&
                                         (context.equippedSword == context.hoveredSword);
    context.shouldShowFusePrompt = context.isPauseOpen && context.isEquipmentPage && context.isSwordRow && context.isOwnedEquip &&
                                  (context.hoveredSword != EQUIP_VALUE_SWORD_NONE) && context.isSwordAlreadyEquippedSlot;

    return context;
}

} // namespace

extern "C" {

void FusePause_UpdateModal(PlayState* play) {
    if (play == nullptr) {
        return;
    }

    PauseContext* pauseCtx = &play->pauseCtx;

    if (pauseCtx->state != 6) {
        sModal.open = false;
        sModal.pendingSelect = false;
        return;
    }

    if (!sModal.open) {
        return;
    }

    if (pauseCtx->pageIndex != PAUSE_EQUIP) {
        sModal.open = false;
        sModal.pendingSelect = false;
        return;
    }

    Input* input = &play->state.input[0];
    const u16 pressed = input->press.button;

    if (pressed & (BTN_B | BTN_START)) {
        sModal.open = false;
        sModal.pendingSelect = false;
        input->press.button = 0;
        input->cur.button &= (u16)~(BTN_B | BTN_START);
        input->press.stick_x = 0;
        input->press.stick_y = 0;
        input->rel.stick_x = 0;
        input->rel.stick_y = 0;
        return;
    }

    std::vector<MaterialEntry> materials = BuildMaterialList();
    const int entryCount = static_cast<int>(materials.size());

    if (entryCount > 0 && (pressed & BTN_DUP || input->rel.stick_y > 30)) {
        sModal.cursor = MoveCursor(-1, materials);
    }

    if (entryCount > 0 && (pressed & BTN_DDOWN || input->rel.stick_y < -30)) {
        sModal.cursor = MoveCursor(1, materials);
    }

    UpdateModalBounds(materials);

    if (pressed & BTN_A) {
        sModal.pendingSelect = true;
    }

    input->press.button = 0;
    input->press.stick_x = 0;
    input->press.stick_y = 0;
    input->rel.stick_x = 0;
    input->rel.stick_y = 0;
    input->cur.button &= (u16)~(BTN_DUP | BTN_DDOWN | BTN_DLEFT | BTN_DRIGHT | BTN_L | BTN_R | BTN_Z | BTN_CUP |
                                BTN_CDOWN | BTN_CLEFT | BTN_CRIGHT);
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

    (void)XLU;

    FusePromptContext context = BuildPromptContext(play);

    if (sModal.open) {
        return;
    }

    static bool sShowDebugOverlay = false;
    if (sShowDebugOverlay && context.isPauseOpen) {
        GfxPrint debugPrinter;

        GfxPrint_Init(&debugPrinter);
        GfxPrint_Open(&debugPrinter, OPA);
        GfxPrint_SetColor(&debugPrinter, 255, 255, 0, 255);
        GfxPrint_SetPosPx(&debugPrinter, 20, 20);
        GfxPrint_Printf(&debugPrinter, "page:%d state:%d\n", pauseCtx->pageIndex, pauseCtx->state);
        GfxPrint_Printf(&debugPrinter, "cursorSlot:%d cursorX:%d cursorY:%d\n", pauseCtx->cursorSlot[PAUSE_EQUIP],
                        pauseCtx->cursorX[PAUSE_EQUIP], pauseCtx->cursorY[PAUSE_EQUIP]);
        GfxPrint_Printf(&debugPrinter, "equippedSword:%d hoveredSword:%d\n", context.equippedSword, context.hoveredSword);
        GfxPrint_Printf(&debugPrinter,
                        "isPauseOpen:%d isEquip:%d isSwordRow:%d isOwned:%d isEquippedSlot:%d\n",
                        context.isPauseOpen, context.isEquipmentPage, context.isSwordRow, context.isOwnedEquip,
                        context.isSwordAlreadyEquippedSlot);
        GfxPrint_Printf(&debugPrinter, "shouldShow:%d\n", context.shouldShowFusePrompt);

        OPA = GfxPrint_Close(&debugPrinter);
        GfxPrint_Destroy(&debugPrinter);
    }

    Fuse::Log("[FuseMVP] FusePause_DrawPrompt called\n");

    if (!context.shouldShowFusePrompt) {
        return;
    }

    const bool swordFused = Fuse::IsSwordFused();

    GfxPrint printer;

    gDPPipeSync(OPA++);
    gDPSetPrimColor(OPA++, 0, 0, 255, 255, 255, 255);

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
}

void FusePause_DrawModal(PlayState* play, Gfx** polyOpaDisp, Gfx** polyXluDisp) {
    if (play == nullptr || polyOpaDisp == nullptr || *polyOpaDisp == nullptr || polyXluDisp == nullptr ||
        *polyXluDisp == nullptr) {
        return;
    }

    PauseContext* pauseCtx = &play->pauseCtx;
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    Input* input = &play->state.input[0];
    Gfx*& OPA = *polyOpaDisp;
    Gfx*& XLU = *polyXluDisp;

    (void)XLU;

    FusePromptContext context = BuildPromptContext(play);
    const bool isPauseOpen = context.isPauseOpen;
    const bool isEquipmentPage = context.isEquipmentPage;
    const bool swordFused = Fuse::IsSwordFused();

    if (!isPauseOpen) {
        sModal.open = false;
        sModal.pendingSelect = false;
        return;
    }

    if (!sModal.open && isEquipmentPage && context.shouldShowFusePrompt && !swordFused &&
        CHECK_BTN_ALL(input->press.button, BTN_A)) {
        sModal.open = true;
        sModal.cursor = 0;
        sModal.scroll = 0;
        sModal.pendingSelect = false;
        input->press.button &= ~BTN_A;
        Fuse::Log("[FuseUI] Modal opened\n");
    }

    if (!sModal.open) {
        return;
    }

    if (pauseCtx->state != 6) {
        sModal.open = false;
        sModal.pendingSelect = false;
        return;
    }

    if (!isEquipmentPage) {
        sModal.open = false;
        sModal.pendingSelect = false;
        return;
    }

    const int currentFrame = play->state.frames;
    if (sLastModalFrame == currentFrame) {
        return;
    }
    sLastModalFrame = currentFrame;

    static int sModalDrawCount = 0;
    if ((sModalDrawCount++ % 30) == 0) {
        Fuse::Log("[FuseUI] modal open, drawing\n");
    }

    std::vector<MaterialEntry> materials = BuildMaterialList();
    UpdateModalBounds(materials);
    const int entryCount = static_cast<int>(materials.size());

    if (sModal.pendingSelect) {
        if (entryCount > 0) {
            const MaterialEntry& entry = materials[sModal.cursor];
            if (!entry.enabled) {
                Fuse::Log("[FuseUI] Selected material disabled\n");
            } else {
                Fuse::FuseResult result = Fuse::TryFuseSword(entry.id);
                Fuse::Log("[FuseUI] Selected material: %s (qty %d) result=%d\n",
                          entry.def ? entry.def->name : "Unknown", entry.quantity, (int)result);
                if (result == Fuse::FuseResult::Ok) {
                    sModal.open = false;
                }
            }
        } else {
            Fuse::Log("[FuseUI] Selected material: <none available>\n");
        }
        sModal.pendingSelect = false;
    }

    GfxPrint printer;

    Gfx_SetupDL_39Opa(gfxCtx);

    gDPPipeSync(OPA++);
    gDPSetScissor(OPA++, G_SC_NON_INTERLACE, 0, 0, 320, 240);

    gDPSetPrimColor(OPA++, 0, 0, 0, 0, 0, 160);
    gDPFillRectangle(OPA++, 0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1);

    constexpr s32 border = 2;

    gDPSetPrimColor(OPA++, 0, 0, 20, 20, 20, 240);
    gDPFillRectangle(OPA++, kPanelX, kPanelY, kPanelX + kPanelW, kPanelY + kPanelH);

    gDPSetPrimColor(OPA++, 0, 0, 200, 200, 200, 255);
    gDPFillRectangle(OPA++, kPanelX, kPanelY, kPanelX + kPanelW, kPanelY + border);
    gDPFillRectangle(OPA++, kPanelX, kPanelY + kPanelH - border, kPanelX + kPanelW, kPanelY + kPanelH);
    gDPFillRectangle(OPA++, kPanelX, kPanelY, kPanelX + border, kPanelY + kPanelH);
    gDPFillRectangle(OPA++, kPanelX + kPanelW - border, kPanelY, kPanelX + kPanelW, kPanelY + kPanelH);

    for (int i = 0; i < kVisibleRows; i++) {
        const int entryIndex = sModal.scroll + i;
        if (entryIndex >= entryCount) {
            break;
        }

        if (entryIndex == sModal.cursor) {
            const s32 rowY = kListY + (i * kRowH);
            const s32 left = kPanelX + 6;
            const s32 right = kPanelX + kPanelW - 6;
            const s32 top = rowY - 4;
            const s32 bottom = rowY + kRowH - 1;
            Gfx_SetupDL_39Opa(gfxCtx);
            gDPSetPrimColor(OPA++, 0, 0, 60, 120, 255, 255);
            gDPFillRectangle(OPA++, left, top, right, bottom);
        }
    }

    gDPPipeSync(OPA++);
    gDPSetPrimColor(OPA++, 0, 0, 255, 255, 255, 255);

    GfxPrint_Init(&printer);
    GfxPrint_Open(&printer, OPA);
    GfxPrint_SetColor(&printer, 255, 255, 255, 255);

    GfxPrint_SetPosPx(&printer, kListX, kTitleY);
    GfxPrint_Printf(&printer, "Fuse");

    GfxPrint_SetPosPx(&printer, kListX, kTitleY + 14);
    GfxPrint_Printf(&printer, "A: Select   B: Back");

    if (entryCount == 0) {
        GfxPrint_SetPosPx(&printer, kListX, kListY);
        GfxPrint_Printf(&printer, "No materials available");
    } else {
        for (int i = 0; i < kVisibleRows; i++) {
            const int entryIndex = sModal.scroll + i;
            if (entryIndex >= entryCount) {
                break;
            }

            const MaterialEntry& entry = materials[entryIndex];
            if (!entry.enabled) {
                GfxPrint_SetColor(&printer, 140, 140, 140, 255);
            } else {
                GfxPrint_SetColor(&printer, 255, 255, 255, 255);
            }

            GfxPrint_SetPosPx(&printer, kListX, kListY + (i * kRowH));
            GfxPrint_Printf(&printer, "%s  x%d", entry.def ? entry.def->name : "Unknown", entry.quantity);
        }
    }

    OPA = GfxPrint_Close(&printer);
    GfxPrint_Destroy(&printer);
}

} // extern "C"
