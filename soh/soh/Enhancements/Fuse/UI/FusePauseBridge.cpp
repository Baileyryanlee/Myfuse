#include "soh/Enhancements/Fuse/UI/FusePauseBridge.h"

#include "global.h"
#include <libultraship/libultra/gbi.h>
#include "functions.h"
#include "soh/Enhancements/Fuse/Fuse.h"
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace {
constexpr s32 kPanelX = 40;
constexpr s32 kPanelY = 40;
constexpr s32 kPanelW = 240;
constexpr s32 kPanelH = 160;
constexpr s32 kTitleY = kPanelY + 12;
constexpr s32 kListX = kPanelX + 12;
constexpr s32 kListY = kPanelY + 36;
constexpr s32 kRowH = 14;
constexpr s32 kVisibleRows = 8;

constexpr s16 kPromptYOffset = 0;
constexpr s16 kPromptPadding = 8;
constexpr s16 kPromptAnchorX = kPanelX + 12;
constexpr s16 kPromptAnchorY = kPanelY + 20;
constexpr s16 kPromptLineSpacing = 14;
constexpr s16 kStatusYOffset = -16;

// Fuse Pause UI durability meter dimensions (file-local). Do not use kBarWidth/kBarHeight.
constexpr s32 kDurabilityBarWidth = 88;
constexpr s32 kDurabilityBarHeight = 8;
constexpr s32 kDurabilityBarOffsetX = 12;
constexpr s32 kDurabilityBarOffsetY = 10;

void DrawSolidRectOpa(GraphicsContext* gfxCtx, Gfx** gfxp, s32 x, s32 y, s32 w, s32 h, u8 r, u8 g, u8 b, u8 a) {
    if (gfxCtx == nullptr || gfxp == nullptr || *gfxp == nullptr) {
        return;
    }

    if (w <= 0 || h <= 0) {
        return;
    }

    Vtx* vtx = Graph_Alloc(gfxCtx, 4 * sizeof(Vtx));
    if (vtx == nullptr) {
        return;
    }

    vtx[0] = VTX(x, y, 0, 0, 0, r, g, b, a);
    vtx[1] = VTX(x + w, y, 0, 0, 0, r, g, b, a);
    vtx[2] = VTX(x + w, y + h, 0, 0, 0, r, g, b, a);
    vtx[3] = VTX(x, y + h, 0, 0, 0, r, g, b, a);

    Gfx*& opa = *gfxp;

    gDPPipeSync(opa++);
    Gfx_SetupDL_39Opa(gfxCtx);
    gDPSetCombineMode(opa++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(opa++, 0, 0, r, g, b, a);
    gSPVertex(opa++, vtx, 4, 0);
    gSP2Triangles(opa++, 0, 1, 2, 0, 0, 2, 3, 0);
}

static constexpr int kFusePanelLeftX = 4;
static constexpr int kFusePanelLeftY = 2;
static constexpr int kFusePanelRightX = 22;
static constexpr int kFusePanelRightY = 2;
static constexpr int kFuseModalYOffset = 3;

enum class FuseUiState {
    Locked,
    Browse,
    Preview,
    Confirm,
};

enum class FusePromptType {
    None,
    AlreadyFused,
};

struct FuseModalState {
    bool open = false;
    int cursor = 0;
    int scroll = 0;
    FuseUiState uiState = FuseUiState::Browse;
    bool isLocked = false;
    MaterialId highlightedMaterialId = MaterialId::None;
    MaterialId previewMaterialId = MaterialId::None;
    MaterialId confirmedMaterialId = MaterialId::None;
    FusePromptType promptType = FusePromptType::None;
    int promptTimer = 0;
};

static FuseModalState sModal;
static int sLastModalFrame = -1;

struct MaterialEntry {
    MaterialId id;
    const MaterialDef* def;
    int quantity;
    bool enabled;
};

static const char* SwordNameFromEquip(EquipValueSword sword) {
    switch (sword) {
        case EQUIP_VALUE_SWORD_KOKIRI:
            return "Kokiri Sword";
        case EQUIP_VALUE_SWORD_MASTER:
            return "Master Sword";
        case EQUIP_VALUE_SWORD_BIGGORON:
            return "Biggoron Sword";
        default:
            return "Selected Sword";
    }
}

static const char* ModifierName(ModifierId id) {
    switch (id) {
        case ModifierId::Hammerize:
            return "Hammerize";
        case ModifierId::Stun:
            return "Stun";
        default:
            return "Unknown";
    }
}

const char* UiStateName(FuseUiState state) {
    switch (state) {
        case FuseUiState::Locked:
            return "LOCKED";
        case FuseUiState::Browse:
            return "BROWSE";
        case FuseUiState::Preview:
            return "PREVIEW";
        case FuseUiState::Confirm:
            return "CONFIRM";
        default:
            return "UNKNOWN";
    }
}

void SetUiState(FuseUiState next) {
    if (sModal.uiState == next) {
        return;
    }
    Fuse::Log("[FuseDBG] UI:State %s->%s\n", UiStateName(sModal.uiState), UiStateName(next));
    sModal.uiState = next;
}

void TriggerPrompt(FusePromptType type, int duration) {
    sModal.promptType = type;
    sModal.promptTimer = duration;
}

std::vector<MaterialEntry> BuildMaterialList() {
    std::vector<MaterialEntry> materials;

    size_t materialDefCount = 0;
    const MaterialDef* materialDefs = Fuse::GetMaterialDefs(&materialDefCount);

    for (size_t i = 0; i < materialDefCount; i++) {
        const MaterialDef& def = materialDefs[i];
        if (def.id == MaterialId::None) {
            continue;
        }

        const int qty = Fuse::GetMaterialCount(def.id);
        materials.push_back({ def.id, &def, qty, qty > 0 });
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

bool FusePause_IsModalOpen(void) {
    return sModal.open;
}

void FusePause_UpdateModal(PlayState* play) {
    if (play == nullptr) {
        return;
    }

    PauseContext* pauseCtx = &play->pauseCtx;

    if (pauseCtx->state != 6) {
        sModal.open = false;
        return;
    }

    Input* input = &play->state.input[0];
    const u16 pressed = input->press.button;

    FusePromptContext context = BuildPromptContext(play);

    if (!sModal.open) {
        if (context.shouldShowFusePrompt && (pressed & BTN_A)) {
            const FuseWeaponView weaponView = Fuse_GetEquippedSwordView(play);

            sModal.open = true;
            sModal.cursor = 0;
            sModal.scroll = 0;
            sModal.isLocked = weaponView.isFused;
            sModal.confirmedMaterialId = weaponView.materialId;
            sModal.highlightedMaterialId = MaterialId::None;
            sModal.previewMaterialId = MaterialId::None;
            sModal.promptType = FusePromptType::None;
            sModal.promptTimer = 0;
            SetUiState(sModal.isLocked ? FuseUiState::Locked : FuseUiState::Browse);

            Fuse::Log("[FuseDBG] UI:Open item=%d confirmedMat=%d locked=%d\n", static_cast<int>(context.hoveredSword),
                      static_cast<int>(weaponView.materialId), sModal.isLocked ? 1 : 0);

            input->press.button &= ~BTN_A;
        }

        return;
    }

    if (pauseCtx->pageIndex != PAUSE_EQUIP) {
        sModal.open = false;
        return;
    }

    if (sModal.promptTimer > 0) {
        sModal.promptTimer--;
        if (sModal.promptTimer == 0) {
            sModal.promptType = FusePromptType::None;
        }
    }

    if (pressed & BTN_START) {
        sModal.open = false;
        input->press.button = 0;
        input->cur.button &= (u16)~(BTN_B | BTN_START);
        input->press.stick_x = 0;
        input->press.stick_y = 0;
        input->rel.stick_x = 0;
        input->rel.stick_y = 0;
        return;
    }

    if ((pressed & BTN_B) && sModal.uiState == FuseUiState::Confirm && !sModal.isLocked) {
        SetUiState(FuseUiState::Preview);
        input->press.button &= ~BTN_B;
    } else if (pressed & BTN_B) {
        sModal.open = false;
        input->press.button = 0;
        input->cur.button &= (u16)~BTN_B;
        input->press.stick_x = 0;
        input->press.stick_y = 0;
        input->rel.stick_x = 0;
        input->rel.stick_y = 0;
        return;
    }

    std::vector<MaterialEntry> materials = BuildMaterialList();
    const int entryCount = static_cast<int>(materials.size());

    if (!sModal.isLocked && entryCount > 0 && (pressed & BTN_DUP || input->rel.stick_y > 30)) {
        sModal.cursor = MoveCursor(-1, materials);
        SetUiState(FuseUiState::Preview);
    }

    if (!sModal.isLocked && entryCount > 0 && (pressed & BTN_DDOWN || input->rel.stick_y < -30)) {
        sModal.cursor = MoveCursor(1, materials);
        SetUiState(FuseUiState::Preview);
    }

    UpdateModalBounds(materials);

    const bool hasHighlight = entryCount > 0 && sModal.cursor >= 0 && sModal.cursor < entryCount;
    sModal.highlightedMaterialId = hasHighlight ? materials[sModal.cursor].id : MaterialId::None;
    const bool highlightEnabled = hasHighlight && materials[sModal.cursor].enabled;
    sModal.previewMaterialId = (!sModal.isLocked && highlightEnabled) ? sModal.highlightedMaterialId : MaterialId::None;

    if (pressed & BTN_A) {
        if (sModal.isLocked) {
            TriggerPrompt(FusePromptType::AlreadyFused, 60);
        } else if (sModal.previewMaterialId == MaterialId::None) {
            // Nothing selectable
        } else if (sModal.uiState != FuseUiState::Confirm) {
            SetUiState(FuseUiState::Confirm);
        } else {
            const Fuse::FuseResult result = Fuse::TryFuseSword(sModal.previewMaterialId);
            const bool success = result == Fuse::FuseResult::Ok;

            Fuse::Log("[FuseDBG] UI:Confirm item=%d mat=%d result=%d\n", static_cast<int>(context.hoveredSword),
                      static_cast<int>(sModal.previewMaterialId), success ? 1 : 0);

            if (success) {
                sModal.isLocked = true;
                sModal.confirmedMaterialId = sModal.previewMaterialId;
                sModal.previewMaterialId = MaterialId::None;
                SetUiState(FuseUiState::Locked);
            } else {
                SetUiState(FuseUiState::Preview);
            }
        }
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

        if (maxDurability > 0) {
            const s32 curDurability = Fuse::GetSwordFuseDurability();
            const f32 ratio = CLAMP((f32)curDurability / (f32)maxDurability, 0.0f, 1.0f);
            const s32 filled = (s32)(ratio * kDurabilityBarWidth);

            DrawSolidRectOpa(gfxCtx, &OPA, barX, barY, kDurabilityBarWidth + 1, kDurabilityBarHeight + 1, 30, 30, 30,
                             255);

            if (filled > 0) {
                DrawSolidRectOpa(gfxCtx, &OPA, barX, barY, filled, kDurabilityBarHeight + 1, 60, 200, 60, 255);
            }

            Gfx_SetupDL_42Opa(gfxCtx);
        }
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

    if (!isPauseOpen) {
        sModal.open = false;
        return;
    }

    if (!sModal.open) {
        return;
    }

    if (pauseCtx->state != 6) {
        sModal.open = false;
        return;
    }

    if (!isEquipmentPage) {
        sModal.open = false;
        return;
    }

    const int currentFrame = play->state.frames;
    if (sLastModalFrame == currentFrame) {
        // PROOF OVERLAY: if anything draws after this, you will still see it on top.
        gDPPipeSync(OPA++);
        gDPSetScissor(OPA++, G_SC_NON_INTERLACE, 0, 0, 320, 240);
        DrawSolidRectOpa(gfxCtx, &OPA, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 255, 0, 255, 255);
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

    GfxPrint printer;

    gDPPipeSync(OPA++);
    gDPSetScissor(OPA++, G_SC_NON_INTERLACE, 0, 0, 320, 240);
    gDPPipeSync(XLU++);
    gDPSetScissor(XLU++, G_SC_NON_INTERLACE, 0, 0, 320, 240);
    // Cover bottom strip (tune Y if needed)
    DrawSolidRectOpa(gfxCtx, &OPA, 0, 200, SCREEN_WIDTH, SCREEN_HEIGHT - 200, 0, 0, 0, 200);

    Gfx_SetupDL_39Opa(gfxCtx);

    gDPPipeSync(OPA++);
    gDPSetScissor(OPA++, G_SC_NON_INTERLACE, 0, 0, 320, 240);

    DrawSolidRectOpa(gfxCtx, &OPA, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 0, 0, 0, 160);

    constexpr s32 border = 2;

    DrawSolidRectOpa(gfxCtx, &OPA, kPanelX, kPanelY, kPanelW + 1, kPanelH + 1, 20, 20, 20, 240);

    DrawSolidRectOpa(gfxCtx, &OPA, kPanelX, kPanelY, kPanelW + 1, border, 200, 200, 200, 255);
    DrawSolidRectOpa(gfxCtx, &OPA, kPanelX, kPanelY + kPanelH - border + 1, kPanelW + 1, border, 200, 200, 200, 255);
    DrawSolidRectOpa(gfxCtx, &OPA, kPanelX, kPanelY, border, kPanelH + 1, 200, 200, 200, 255);
    DrawSolidRectOpa(gfxCtx, &OPA, kPanelX + kPanelW - border + 1, kPanelY, border, kPanelH + 1, 200, 200, 200, 255);

    // Temporary: highlight bar disabled in favor of text color cues.
    // for (int i = 0; i < kVisibleRows; i++) {
    //     const int entryIndex = sModal.scroll + i;
    //     if (entryIndex >= entryCount) {
    //         break;
    //     }
    //
    //     if (entryIndex == sModal.cursor) {
    //         const s32 rowY = kListY + (i * kRowH);
    //         const s32 left = kPanelX + 6;
    //         const s32 right = kPanelX + kPanelW - 6;
    //         const s32 top = rowY - 4;
    //         const s32 bottom = rowY + kRowH - 1;
    //         Gfx_SetupDL_39Opa(gfxCtx);
    //         DrawSolidRectOpa(gfxCtx, &OPA, left, top, right - left + 1, bottom - top + 1, 60, 120, 255, 255);
    //     }
    // }

    Gfx_SetupDL_42Opa(gfxCtx);

    gDPPipeSync(OPA++);
    gDPSetPrimColor(OPA++, 0, 0, 255, 255, 255, 255);

    GfxPrint_Init(&printer);
    GfxPrint_Open(&printer, OPA);
    GfxPrint_SetColor(&printer, 255, 255, 255, 255);

    const int yOffsetCells = kFuseModalYOffset;
    const int yOffsetPx = kFuseModalYOffset * 8;

    GfxPrint_SetPosPx(&printer, kListX, kTitleY + yOffsetPx);
    GfxPrint_Printf(&printer, "Fuse");

    const bool locked = sModal.isLocked;
    const bool confirmMode = sModal.uiState == FuseUiState::Confirm;

    const s32 promptX = kPromptAnchorX;
    const s32 promptY = kPromptAnchorY + yOffsetPx;

    GfxPrint_SetPosPx(&printer, promptX, promptY);
    if (locked) {
        GfxPrint_SetColor(&printer, 255, 120, 120, 255);
        GfxPrint_Printf(&printer, "ITEM ALREADY FUSED");
    } else if (confirmMode) {
        GfxPrint_Printf(&printer, "A: Confirm   B: Cancel");
    } else {
        GfxPrint_Printf(&printer, "A: Select   B: Back");
    }

    if (sModal.promptTimer > 0 && sModal.promptType == FusePromptType::AlreadyFused) {
        GfxPrint_SetPosPx(&printer, promptX, promptY + kPromptLineSpacing);
        GfxPrint_SetColor(&printer, 255, 120, 120, 255);
        GfxPrint_Printf(&printer, "ITEM ALREADY FUSED");
        GfxPrint_SetColor(&printer, 255, 255, 255, 255);
    }

    GfxPrint_SetColor(&printer, 255, 255, 255, 255);

    if (entryCount == 0) {
        GfxPrint_SetPosPx(&printer, kListX, kListY + yOffsetPx);
        GfxPrint_Printf(&printer, "No materials available");
    } else {
        for (int i = 0; i < kVisibleRows; i++) {
            const int entryIndex = sModal.scroll + i;
            if (entryIndex >= entryCount) {
                break;
            }

            const MaterialEntry& entry = materials[entryIndex];
            const bool isSelected = (entryIndex == sModal.cursor);
            const bool enabled = entry.enabled;

            if (locked) {
                GfxPrint_SetColor(&printer, 140, 140, 140, 180);
            } else if (!enabled) {
                GfxPrint_SetColor(&printer, 140, 140, 140, 255);
            } else if (isSelected) {
                if (confirmMode) {
                    GfxPrint_SetColor(&printer, 120, 200, 255, 255);
                } else {
                    GfxPrint_SetColor(&printer, 255, 255, 0, 255);
                }
            } else {
                GfxPrint_SetColor(&printer, 255, 255, 255, 255);
            }

            GfxPrint_SetPosPx(&printer, kListX, kListY + (i * kRowH) + yOffsetPx);
            GfxPrint_Printf(&printer, "%s  x%d", entry.def ? entry.def->name : "Unknown", entry.quantity);
        }
    }

    const char* selectedItemName = SwordNameFromEquip(context.hoveredSword);
    const FuseWeaponView weaponView = Fuse_GetEquippedSwordView(play);

    const MaterialEntry* highlightedEntry = nullptr;
    if (entryCount > 0 && sModal.cursor >= 0 && sModal.cursor < entryCount) {
        highlightedEntry = &materials[sModal.cursor];
    }

    const MaterialId materialToDisplay = locked
                                             ? sModal.confirmedMaterialId
                                             : (sModal.previewMaterialId != MaterialId::None
                                                    ? sModal.previewMaterialId
                                                    : (highlightedEntry ? highlightedEntry->id : MaterialId::None));
    const MaterialDef* displayDef = Fuse::GetMaterialDef(materialToDisplay);

    std::string matName = displayDef ? displayDef->name : "--";
    int matQty = (materialToDisplay != MaterialId::None) ? Fuse::GetMaterialCount(materialToDisplay) : 0;
    std::string modifierText = "None";
    if (displayDef && displayDef->modifierCount > 0) {
        modifierText.clear();
        for (size_t i = 0; i < displayDef->modifierCount; i++) {
            const ModifierSpec& mod = displayDef->modifiers[i];
            if (!modifierText.empty()) {
                modifierText += ", ";
            }
            modifierText += ModifierName(mod.id);
        }
    }

    GfxPrint_SetColor(&printer, 255, 255, 255, 255);

    GfxPrint_SetPos(&printer, kFusePanelLeftX, kFusePanelLeftY + yOffsetCells);
    GfxPrint_Printf(&printer, "Selected:");

    GfxPrint_SetPos(&printer, kFusePanelLeftX, kFusePanelLeftY + 1 + yOffsetCells);
    GfxPrint_Printf(&printer, "%s", selectedItemName);

    GfxPrint_SetPos(&printer, kFusePanelLeftX, kFusePanelLeftY + 2 + yOffsetCells);
    if (!weaponView.isFused) {
        GfxPrint_Printf(&printer, "Durability: --");
    } else {
        GfxPrint_Printf(&printer, "Durability: %d / %d", weaponView.curDurability, weaponView.maxDurability);

        const int maxDurability = weaponView.maxDurability;
        if (maxDurability > 0) {
            const int curDurability = std::clamp(weaponView.curDurability, 0, maxDurability);
            const f32 ratio = static_cast<f32>(curDurability) / static_cast<f32>(maxDurability);
            const s32 filled =
                std::clamp(static_cast<s32>(ratio * kDurabilityBarWidth), 0, kDurabilityBarWidth);

            const s32 durabilityTextY = (kFusePanelLeftY + 2 + yOffsetCells) * 8;
            const s32 barX = kPanelX + kDurabilityBarOffsetX;
            const s32 barY = durabilityTextY + kDurabilityBarOffsetY;

            DrawSolidRectOpa(gfxCtx, &OPA, barX, barY, kDurabilityBarWidth + 1, kDurabilityBarHeight + 1, 10, 10, 10,
                             200);

            if (filled > 0) {
                DrawSolidRectOpa(gfxCtx, &OPA, barX, barY, filled, kDurabilityBarHeight + 1, 220, 240, 220, 255);
            }

            Gfx_SetupDL_42Opa(gfxCtx);
        }
    }

    GfxPrint_SetPos(&printer, kFusePanelRightX, kFusePanelRightY + yOffsetCells);
    GfxPrint_Printf(&printer, "Material:");

    GfxPrint_SetPos(&printer, kFusePanelRightX, kFusePanelRightY + 1 + yOffsetCells);
    GfxPrint_Printf(&printer, "%s", matName.c_str());

    GfxPrint_SetPos(&printer, kFusePanelRightX, kFusePanelRightY + 2 + yOffsetCells);
    GfxPrint_Printf(&printer, "Qty: %d", matQty);

    GfxPrint_SetPos(&printer, kFusePanelRightX, kFusePanelRightY + 3 + yOffsetCells);
    GfxPrint_Printf(&printer, "Effect:");

    GfxPrint_SetPos(&printer, kFusePanelRightX, kFusePanelRightY + 4 + yOffsetCells);
    GfxPrint_Printf(&printer, "%s", modifierText.c_str());

    OPA = GfxPrint_Close(&printer);
    GfxPrint_Destroy(&printer);
}

} // extern "C"
