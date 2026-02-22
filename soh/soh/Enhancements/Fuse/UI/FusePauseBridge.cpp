#include "soh/Enhancements/Fuse/UI/FusePauseBridge.h"

#include "global.h"
#include <libultraship/libultra/gbi.h>
#include "functions.h"
#include "soh/Enhancements/Fuse/Fuse.h"
#include "soh/OTRGlobals.h"
#include <libultraship/controller/controldeck/ControlDeck.h>
#include <libultraship/libultraship.h>
#include "soh/cvar_prefixes.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

/*
# Fuse Pause Menu UI Implementation Report

## 1) FILE INVENTORY
- `soh/soh/Enhancements/Fuse/UI/FusePauseBridge.cpp`
  - Primary Fuse Pause Menu implementation: prompt eligibility detection, modal state updates, input routing while open,
    and all pause-modal rendering (background, list, metadata text, durability bar).
  - Key functions:
    - Entry/context gating: `IsFuseMenuPressed`, `BuildPromptContext`, `IsPausePageForItem`
    - State updates: `FusePause_UpdateModal`, `MoveCursor`, `UpdateModalBounds`, `SetUiState`, `TriggerPrompt`
    - Rendering: `FusePause_DrawPrompt`, `FusePause_DrawModal`, `DrawSolidRectOpa`, `DrawDurabilityBar`,
      `RestorePauseTextState`
    - Text/model composition helpers: `ModifierName`, `PauseItemName`, `BuildMaterialList`, `WeaponViewForPauseItem`
- `soh/soh/Enhancements/Fuse/UI/FusePauseBridge.h`
  - C/C++ bridge interface exported to pause code.
  - Key functions: `FusePause_DrawPrompt`, `FusePause_DrawModal`, `FusePause_UpdateModal`, `FusePause_IsModalOpen`.
- `soh/src/overlays/misc/ovl_kaleido_scope/z_kaleido_scope_PAL.c` (analysis only)
  - Pause-loop call site for Fuse bridge functions.
  - Entry points observed: `FusePause_UpdateModal` (update), `FusePause_DrawPrompt` and `FusePause_DrawModal`
    (render), and `FusePause_IsModalOpen` (input suppression while modal is open).

## 2) ENTRY FLOW
- Activation begins in pause update when `FusePause_UpdateModal(play)` is called from Kaleido Scope.
- `BuildPromptContext` computes whether the currently hovered item is fuse-eligible on current pause page.
- `IsFuseMenuPressed` edge-detects `BTN_CUSTOM_FUSE_MENU`.
- If eligible and pressed, modal is opened by setting `sModal.open = true` and initializing modal fields.
- While open, input is routed in `FusePause_UpdateModal` by consuming/zeroing pause input fields and button masks,
  including directional/buttons used by pause navigation.

## 3) STATE STRUCTURE
- Fuse pause state is file-local `FuseModalState sModal` with fields:
  - `open`: modal visibility.
  - `cursor`: current selected material index.
  - `scroll`: top-of-list scroll index.
  - `uiState`: `Locked/Browse/Preview/Confirm` state machine.
  - `isLocked`: item already fused lock flag.
  - `activeItem`: target item enum for the modal session.
  - `highlightedMaterialId`: currently highlighted list material.
  - `previewMaterialId`: active preview/confirm material.
  - `confirmedMaterialId`: fused material (or resolved current fused material at open).
  - `promptType` + `promptTimer`: transient prompt state, including Already Fused message.
- Selection index tracking: `cursor`.
- Scroll offset tracking: `scroll`.
- Confirmation-state controls: `uiState`, `isLocked`, `promptType/promptTimer`.

## 4) RENDERING PIPELINE
- Draw call hierarchy:
  1. Pause render calls `FusePause_DrawPrompt` then `FusePause_DrawModal`.
  2. `FusePause_DrawModal` builds materials/state and issues rectangle + text draws.
  3. Primitive draws are performed through `DrawSolidRectOpa`; durability uses `DrawDurabilityBar`.
  4. Text state is reset with `RestorePauseTextState` before `GfxPrint` text rendering.
- Order inside `FusePause_DrawModal` (simplified):
  1. Early exits / state guards
  2. Full-screen and panel backdrops/borders
  3. Durability bar draw
  4. List-row background quads
  5. Text pass: title, prompts, status (`ITEM ALREADY FUSED`), list entries, left info block, right info block
- Positioning model: absolute pixel constants (`constexpr s32 ...`) and offsets (`modalYOffsetPx`), no retained layout
  container system.
- Clip/scissor APIs: `gDPSetScissor(..., 0, 0, 320, 240)` is called multiple times but always as full-screen bounds,
  not per-panel clipping regions.
- Hardcoded coordinate anchors include (non-exhaustive):
  - Panel/cards: `leftCardX/Y/W/H`, `rightCardX/Y/W/H`, `kPanelX/Y/W/H`, `kFooterX/Y/W/H`
  - List: `kListX`, `kListY`, `kRowH`, `kVisibleRows`, `kRowBgX/Y/W/H`
  - Text anchors: `kTitleX/Y`, `kLeftTextX`, `kRightTextX`, line Y constants.

## 5) SCROLL LOGIC
- Cursor changes in `FusePause_UpdateModal` on D-pad up/down or stick Y thresholds.
- `MoveCursor` clamps movement and attempts to skip disabled materials.
- `UpdateModalBounds` computes scroll window using `kVisibleRows`; if cursor leaves visible range, `scroll` is adjusted.
- Drawing loops use `entryIndex = sModal.scroll + i` for exactly `kVisibleRows`, so items outside visible window are not
  list-rendered. However, there is no panel clip region; text still renders unconstrained within full-screen scissor.

## 6) DURABILITY BAR
- Responsible function: `DrawDurabilityBar` (called from `FusePause_DrawModal`).
- Percent/range calculation:
  - `ratio = curDurability / maxDurability`
  - `filled = clamp(ratio * innerBarWidth, 0, innerBarWidth)`
- Width is hardcoded-derived (`kDurabilityBarWidth = leftCardW - innerPadding*2`), height fixed
  (`kDurabilityBarHeight = 8`).
- Position is absolute (`barX = leftCardX + kLeftCardInnerPadding`, `barY = durabilityBarY`), not clipped to panel bounds.

## 7) MODIFIER TEXT RENDERING
- Constructed in `FusePause_DrawModal` from `displayDef->modifiers`.
- String concatenation is manual (`modifierText += ", "; modifierText += ModifierName(mod.id);`).
- Word wrapping is absent; modifier text is rendered as one line with `GfxPrint_Printf`.

## 8) PANEL BOUNDING
- Visual panel rectangles exist as hardcoded constants and drawn quads (`kPanel*`, card constants), but these are visual
  only; there is no layout container object enforcing child bounds.
- Left/right panel positions are fixed constants (`leftCard*`, `rightCard*`), not dynamically computed from content.
- Coordinates target fixed pause-space dimensions (320x240 scissor); behavior is not adaptive to dynamic resolutions.

## 9) CLIPPING
- No per-panel clipping/scissor is implemented.
- Existing scissor calls set full-screen scissor only (`0,0,320,240`), so list/details text are not clipped by card/panel.

## 10) KNOWN STRUCTURAL LIMITATIONS
- Text overflow: text uses fixed anchors and single-line `GfxPrint_Printf` without wrapping/truncation.
- Scroll overflow behavior: scroll selects visible row range only, but without per-panel clipping, other content can still
  visually conflict with fixed regions.
- Overlap causes: durability bar/list/info blocks all use absolute Y anchors with no collision/layout solver.
- Missing system: no retained layout/container engine, no clip rectangles per region, no text measurement/wrapping pass.
*/

namespace {
static bool IsFuseMenuPressed() {
    auto ctx = Ship::Context::GetInstance();
    if (!ctx) {
        return false;
    }

    auto deck = ctx->GetControlDeck();
    if (!deck) {
        return false;
    }

    auto pads = std::dynamic_pointer_cast<LUS::ControlDeck>(deck)->GetPads();
    if (!pads) {
        return false;
    }

    const bool down = (pads[0].button & BTN_CUSTOM_FUSE_MENU) != 0;
    static bool sPrevDown = false;
    const bool pressedThisFrame = down && !sPrevDown;
    sPrevDown = down;
    return pressedThisFrame;
}

// Two-card modal bounds (geometry only; visuals unchanged)
constexpr s32 leftCardX = 24;
constexpr s32 leftCardY = 32;
constexpr s32 leftCardW = 152;
constexpr s32 leftCardH = 184;

constexpr s32 rightCardX = 184;
constexpr s32 rightCardY = leftCardY;
constexpr s32 rightCardW = 112;
constexpr s32 rightCardH = leftCardH;

constexpr s32 kCardPaddingX = 12;
constexpr s32 kCardPaddingY = 8;
constexpr s32 kInfoLineSpacing = 12;

constexpr s32 kLeftCardInnerPadding = kCardPaddingX;
constexpr s32 kDurabilitySectionSpacing = 4;

constexpr s32 kPanelPadding = 8;
constexpr s32 kPanelX = leftCardX - kPanelPadding;
constexpr s32 kPanelY = leftCardY - kPanelPadding;
constexpr s32 kPanelW = (rightCardX + rightCardW + kPanelPadding) - kPanelX;
constexpr s32 kPanelH = leftCardH + (2 * kPanelPadding);

constexpr s32 kFooterW = 190;
constexpr s32 kFooterH = 32;
constexpr s32 kFooterBottomPad = 10;
constexpr s32 kFooterX = kPanelX + ((kPanelW - kFooterW) / 2);
constexpr s32 kFooterY = kPanelY + kPanelH - kFooterBottomPad - kFooterH;

constexpr s32 kTitleX = leftCardX + kCardPaddingX;
constexpr s32 kTitleY = leftCardY + kCardPaddingY;
constexpr s32 kListOffsetY = 64;
constexpr s32 kListY = rightCardY + kListOffsetY;
constexpr s32 kRowH = 14;
constexpr s32 kVisibleRows = 7;
constexpr s32 kRowBgYOffset = -2;

constexpr s32 kCarouselInsetY = 12;
constexpr s32 kCarouselLeftBound = 176;
constexpr s32 kCarouselCardW = 132;

constexpr s32 kCarouselCardH = 44;
constexpr s32 kCarouselGap = 10;
constexpr s32 kCarouselStride = kCarouselCardH + kCarouselGap;

constexpr s32 kCarouselVisibleCards = 3;
constexpr s32 kRightCardInnerPad = 6;
constexpr s32 kSpriteBoxSize = 44;
constexpr s32 kQtyBoxW = 24;
constexpr s32 kAttackBoxW = 14;
constexpr s32 kAttackBoxH = 14;
constexpr s32 kModifierIconBoxSize = 6;
constexpr s32 kModifierIconBoxGap = 2;
constexpr s32 kModifierIconRowCount = 6;

constexpr s32 kHeaderY = leftCardY + kCardPaddingY;
constexpr s32 kLeftTextX = leftCardX + kCardPaddingX;
constexpr s32 kSelectedY = kHeaderY + kInfoLineSpacing;
constexpr s32 kItemNameY = kSelectedY + kInfoLineSpacing;
constexpr s32 kDurabilityTextY = kItemNameY + kInfoLineSpacing;

// Fuse Pause UI durability meter dimensions (file-local). Do not use kBarWidth/kBarHeight.
constexpr s32 kDurabilityBarHeight = 8;
constexpr s32 kDurabilityBarWidth = leftCardW - (kLeftCardInnerPadding * 2);

constexpr s16 kPromptLineSpacing = 14;
constexpr s16 kPromptPadding = 8;
constexpr s16 kPromptYOffset = 0;
constexpr s16 kStatusYOffset = -16;

constexpr const char* kDurabilityBarCVar = CVAR_DEVELOPER_TOOLS("Fuse.DurabilityBarEnabled");

void SetScissorRect(Gfx*& opa, s32 x, s32 y, s32 w, s32 h) {
    const s32 minX = std::clamp(x, 0, 320);
    const s32 minY = std::clamp(y, 0, 240);
    const s32 maxX = std::clamp(x + w, 0, 320);
    const s32 maxY = std::clamp(y + h, 0, 240);
    const s32 clampedMaxX = std::max(maxX, minX);
    const s32 clampedMaxY = std::max(maxY, minY);

    gDPPipeSync(opa++);
    gDPSetScissor(opa++, G_SC_NON_INTERLACE, minX, minY, clampedMaxX, clampedMaxY);
}

void SetScissorFullscreen(Gfx*& opa) {
    gDPPipeSync(opa++);
    gDPSetScissor(opa++, G_SC_NON_INTERLACE, 0, 0, 320, 240);
}

void DrawSolidRectOpa(GraphicsContext* gfxCtx, Gfx** gfxp, s32 x, s32 y, s32 w, s32 h, u8 r, u8 g, u8 b, u8 a) {
    if (gfxCtx == nullptr || gfxp == nullptr || *gfxp == nullptr) {
        return;
    }

    if (w <= 0 || h <= 0) {
        return;
    }

    const s32 halfW = SCREEN_WIDTH / 2;
    const s32 halfH = SCREEN_HEIGHT / 2;

    const s32 x0 = x - halfW;
    const s32 x1 = (x + w) - halfW;

    const s32 y0 = halfH - y;
    const s32 y1 = halfH - (y + h);

    Vtx* vtx = (Vtx*)Graph_Alloc(gfxCtx, 4 * sizeof(Vtx));
    if (vtx == nullptr) {
        return;
    }

    vtx[0].v.ob[0] = static_cast<s16>(x0);
    vtx[0].v.ob[1] = static_cast<s16>(y0);
    vtx[1].v.ob[0] = static_cast<s16>(x1);
    vtx[1].v.ob[1] = static_cast<s16>(y0);
    vtx[2].v.ob[0] = static_cast<s16>(x1);
    vtx[2].v.ob[1] = static_cast<s16>(y1);
    vtx[3].v.ob[0] = static_cast<s16>(x0);
    vtx[3].v.ob[1] = static_cast<s16>(y1);

    for (int i = 0; i < 4; i++) {
        vtx[i].v.ob[2] = 0;
        vtx[i].v.flag = 0;
        vtx[i].v.tc[0] = 0;
        vtx[i].v.tc[1] = 0;
        vtx[i].v.cn[0] = r;
        vtx[i].v.cn[1] = g;
        vtx[i].v.cn[2] = b;
        vtx[i].v.cn[3] = a;
    }

    Gfx*& opa = *gfxp;

    gDPPipeSync(opa++);
    Gfx_SetupDL_39Opa(gfxCtx);
    gDPSetCombineMode(opa++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(opa++, 0, 0, r, g, b, a);
    gSPVertex(opa++, (uintptr_t)vtx, 4, 0);
    gSP2Triangles(opa++, 0, 1, 2, 0, 0, 2, 3, 0);
}

// Durability bars must render exclusively through this helper to avoid stray duplicates.
void DrawDurabilityBar(GraphicsContext* gfxCtx, Gfx** gfxp, s32 x, s32 y, s32 width, s32 height, s32 filled) {
    if (gfxCtx == nullptr || gfxp == nullptr || *gfxp == nullptr) {
        return;
    }

    if (width <= 0 || height <= 0) {
        return;
    }

    const s32 barWidth = width;
    const s32 barHeight = height;
    const s32 innerWidth = std::max(barWidth - 2, 0);
    const s32 innerHeight = std::max(barHeight - 2, 0);
    const s32 clampedFilled = std::clamp(filled, 0, innerWidth);

    DrawSolidRectOpa(gfxCtx, gfxp, x, y, barWidth, barHeight, 20, 20, 20, 220);

    if (innerWidth > 0 && innerHeight > 0 && clampedFilled > 0) {
        DrawSolidRectOpa(gfxCtx, gfxp, x + 1, y + 1, clampedFilled, innerHeight, 0, 255, 255, 255);
    }
}

void RestorePauseTextState(GraphicsContext* gfxCtx, Gfx** gfxp) {
    if (gfxCtx == nullptr || gfxp == nullptr || *gfxp == nullptr) {
        return;
    }

    Gfx_SetupDL_42Opa(gfxCtx);

    Gfx*& opa = *gfxp;

    gDPPipeSync(opa++);
    gDPSetTextureLUT(opa++, G_TT_IA16);
    gDPSetTexturePersp(opa++, G_TP_NONE);
    gSPClearGeometryMode(opa++, G_LIGHTING | G_CULL_BACK | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR);
    gSPSetGeometryMode(opa++, G_SHADE);
    gSPTexture(opa++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    gSPTexture(opa++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gDPSetCombineMode(opa++, G_CC_MODULATEIDECALA_PRIM, G_CC_MODULATEIDECALA_PRIM);
}

bool IsDurabilityBarEnabled() {
    return CVarGetInteger(kDurabilityBarCVar, 1) != 0;
}

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

enum class FusePauseItem {
    None,
    Sword,
    Boomerang,
    Hammer,
    DekuShield,
    HylianShield,
    MirrorShield,
};

struct FuseModalState {
    bool open = false;
    int cursor = 0;
    int scroll = 0;
    FuseUiState uiState = FuseUiState::Browse;
    bool isLocked = false;
    FusePauseItem activeItem = FusePauseItem::None;
    MaterialId highlightedMaterialId = MaterialId::None;
    MaterialId previewMaterialId = MaterialId::None;
    MaterialId confirmedMaterialId = MaterialId::None;
    FusePromptType promptType = FusePromptType::None;
    int promptTimer = 0;
    float carouselPos = 0.0f;
    float carouselVel = 0.0f;
};

static FuseModalState sModal;
static int sLastModalFrame = -1;

struct MaterialEntry {
    MaterialId id;
    const MaterialDef* def;
    int quantity;
    bool enabled;
};

std::string TruncateToPx(const char* text, s32 maxPx) {
    if (text == nullptr || maxPx <= 0) {
        return "";
    }

    constexpr s32 kGlyphPx = 8;
    const s32 maxChars = maxPx / kGlyphPx;
    const std::string src(text);

    if (maxChars <= 0) {
        return "";
    }

    if (static_cast<s32>(src.size()) <= maxChars) {
        return src;
    }

    if (maxChars <= 3) {
        return std::string(static_cast<size_t>(maxChars), '.');
    }

    return src.substr(0, static_cast<size_t>(maxChars - 3)) + "...";
}

void DrawMaterialCard(GraphicsContext* gfxCtx, Gfx*& opa, const MaterialEntry& entry, s32 cardX, s32 cardY, s32 cardW,
                      s32 cardH, bool selected, bool enabled, bool locked, bool confirmMode) {
    (void)locked;
    (void)confirmMode;

    u8 r = 35;
    u8 g = 35;
    u8 b = 35;
    u8 a = 180;

    if (selected) {
        r = 40;
        g = 120;
        b = 255;
        a = 220;
    }

    if (!enabled) {
        r = 28;
        g = 28;
        b = 28;
        a = 150;
    }

    DrawSolidRectOpa(gfxCtx, &opa, cardX, cardY, cardW, cardH, r, g, b, a);

    const s32 spriteX = cardX + kRightCardInnerPad;
    const s32 spriteY = cardY + ((cardH - kSpriteBoxSize) / 2);
    DrawSolidRectOpa(gfxCtx, &opa, spriteX, spriteY, kSpriteBoxSize, kSpriteBoxSize, 20, 20, 20, 220);

    const s32 textX = spriteX + kSpriteBoxSize + kRightCardInnerPad;

    const s32 nameY = cardY + kRightCardInnerPad + 2;
    const s32 qtyX = cardX + cardW - kRightCardInnerPad - kQtyBoxW;
    const s32 nameMaxPx = std::max(0, cardW - (kRightCardInnerPad * 3) - kSpriteBoxSize - kQtyBoxW - 2);

    RestorePauseTextState(gfxCtx, &opa);

    GfxPrint printer;
    GfxPrint_Init(&printer);
    GfxPrint_Open(&printer, opa);

    if (selected) {
        GfxPrint_SetColor(&printer, 255, 255, 0, 255);
    } else {
        GfxPrint_SetColor(&printer, 255, 255, 255, 255);
    }

    const std::string materialName = TruncateToPx(entry.def ? entry.def->name : "Unknown", nameMaxPx);
    GfxPrint_SetPosPx(&printer, textX, nameY);
    GfxPrint_Printf(&printer, "%s", materialName.c_str());

    char qtyText[16];
    std::snprintf(qtyText, sizeof(qtyText), "x%d", entry.quantity);
    const s32 qtyTextLen = static_cast<s32>(std::string(qtyText).size());
    constexpr s32 kGlyphPx = 8;
    const s32 qtyDrawX = qtyX + std::max(0, kQtyBoxW - (qtyTextLen * kGlyphPx));

    GfxPrint_SetPosPx(&printer, qtyDrawX, nameY);
    GfxPrint_Printf(&printer, "%s", qtyText);

    opa = GfxPrint_Close(&printer);
    GfxPrint_Destroy(&printer);

    const s32 iconY = cardY + cardH - kRightCardInnerPad - kModifierIconBoxSize;
    for (s32 i = 0; i < kModifierIconRowCount; i++) {
        const s32 iconX = textX + i * (kModifierIconBoxSize + kModifierIconBoxGap);
        DrawSolidRectOpa(gfxCtx, &opa, iconX, iconY, kModifierIconBoxSize, kModifierIconBoxSize, 24, 24, 24, 220);
    }

    const s32 atkX = cardX + cardW - kRightCardInnerPad - kAttackBoxW;
    const s32 atkY = cardY + cardH - kRightCardInnerPad - kAttackBoxH;
    DrawSolidRectOpa(gfxCtx, &opa, atkX, atkY, kAttackBoxW, kAttackBoxH, 24, 24, 24, 220);
}

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

static const char* PauseItemName(FusePauseItem item, EquipValueSword sword) {
    switch (item) {
        case FusePauseItem::Sword:
            return SwordNameFromEquip(sword);
        case FusePauseItem::Boomerang:
            return "Boomerang";
        case FusePauseItem::Hammer:
            return "Megaton Hammer";
        case FusePauseItem::DekuShield:
            return "Deku Shield";
        case FusePauseItem::HylianShield:
            return "Hylian Shield";
        case FusePauseItem::MirrorShield:
            return "Mirror Shield";
        case FusePauseItem::None:
        default:
            return "Selected Item";
    }
}

static size_t ShieldSlotIndex(ShieldSlotKey key) {
    return FusePersistence::kShieldSlotOffset + static_cast<size_t>(key);
}

static FuseSlot ResolveSlotForPauseItem(FusePauseItem item, PlayState* play) {
    (void)play;
    switch (item) {
        case FusePauseItem::Sword:
            return Fuse::GetActiveSwordSlot();
        case FusePauseItem::Boomerang:
            return Fuse::GetActiveBoomerangSlot();
        case FusePauseItem::Hammer:
            return Fuse::GetActiveHammerSlot();
        case FusePauseItem::DekuShield:
        case FusePauseItem::HylianShield:
        case FusePauseItem::MirrorShield: {
            const std::array<SwordFuseSlot, FusePersistence::kSwordSlotCount> slots = Fuse::GetSwordSlots();
            const ShieldSlotKey key =
                (item == FusePauseItem::HylianShield)
                    ? ShieldSlotKey::Hylian
                    : ((item == FusePauseItem::MirrorShield) ? ShieldSlotKey::Mirror : ShieldSlotKey::Deku);
            return slots[ShieldSlotIndex(key)];
        }
        case FusePauseItem::None:
        default:
            return {};
    }
}

static FuseWeaponView WeaponViewFromSlot(const FuseSlot& slot) {
    FuseWeaponView view{};
    view.materialId = slot.materialId;
    view.curDurability = slot.durabilityCur;
    view.maxDurability = slot.durabilityMax;
    view.isFused = slot.materialId != MaterialId::None && slot.durabilityCur > 0;
    return view;
}

static FuseWeaponView WeaponViewForPauseItem(FusePauseItem item, PlayState* play) {
    const FuseSlot slot = ResolveSlotForPauseItem(item, play);
    return WeaponViewFromSlot(slot);
}

static bool IsPausePageForItem(const PauseContext* pauseCtx, FusePauseItem item) {
    if (pauseCtx == nullptr) {
        return false;
    }

    switch (item) {
        case FusePauseItem::Sword:
            return pauseCtx->pageIndex == PAUSE_EQUIP;
        case FusePauseItem::DekuShield:
        case FusePauseItem::HylianShield:
        case FusePauseItem::MirrorShield:
            return pauseCtx->pageIndex == PAUSE_EQUIP;
        case FusePauseItem::Boomerang:
        case FusePauseItem::Hammer:
            return pauseCtx->pageIndex == PAUSE_ITEM;
        case FusePauseItem::None:
        default:
            return false;
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

int ComputeVisibleRows(int entryCount, s32 modalYOffsetPx) {
    const s32 listTop = kListY + modalYOffsetPx;
    const s32 footerBottomLimit = (kFooterY + modalYOffsetPx) - 4;
    const s32 rightInnerBottomLimit = rightCardY + rightCardH - kCardPaddingY;
    const s32 listBottomLimit = std::min(footerBottomLimit, rightInnerBottomLimit);
    const s32 listAvailH = listBottomLimit - listTop;
    int visibleRows = std::clamp(static_cast<int>(listAvailH / kRowH), 0, static_cast<int>(kVisibleRows));

    if (entryCount > 0) {
        visibleRows = std::max(visibleRows, 1);
    }

    return visibleRows;
}

void UpdateModalBounds(const std::vector<MaterialEntry>& materials, int visibleRows) {
    const int entryCount = static_cast<int>(materials.size());
    const int maxCursor = (entryCount > 0) ? (entryCount - 1) : 0;
    sModal.cursor = std::clamp(sModal.cursor, 0, maxCursor);

    if (sModal.cursor < sModal.scroll) {
        sModal.scroll = sModal.cursor;
    }
    if (visibleRows > 0 && sModal.cursor >= sModal.scroll + visibleRows) {
        sModal.scroll = sModal.cursor - visibleRows + 1;
    }

    const int maxScroll = std::max(0, entryCount - visibleRows);
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

int32_t HoveredShieldForCursor(const PauseContext* pauseCtx) {
    if (pauseCtx == nullptr) {
        return EQUIP_VALUE_SHIELD_NONE;
    }

    switch (pauseCtx->cursorX[PAUSE_EQUIP]) {
        case 1:
            return EQUIP_VALUE_SHIELD_DEKU;
        case 2:
            return EQUIP_VALUE_SHIELD_HYLIAN;
        case 3:
            return EQUIP_VALUE_SHIELD_MIRROR;
        default:
            return EQUIP_VALUE_SHIELD_NONE;
    }
}

static s16 ItemIdFromSwordEquipValue(EquipValueSword sword) {
    switch (sword) {
        case EQUIP_VALUE_SWORD_KOKIRI:
            return ITEM_SWORD_KOKIRI;
        case EQUIP_VALUE_SWORD_MASTER:
            return ITEM_SWORD_MASTER;
        case EQUIP_VALUE_SWORD_BIGGORON:
            return ITEM_SWORD_BGS;
        case EQUIP_VALUE_SWORD_NONE:
        default:
            return ITEM_NONE;
    }
}

static s16 ItemIdFromShieldEquipValue(int32_t shield) {
    switch (shield) {
        case EQUIP_VALUE_SHIELD_DEKU:
            return ITEM_SHIELD_DEKU;
        case EQUIP_VALUE_SHIELD_HYLIAN:
            return ITEM_SHIELD_HYLIAN;
        case EQUIP_VALUE_SHIELD_MIRROR:
            return ITEM_SHIELD_MIRROR;
        case EQUIP_VALUE_SHIELD_NONE:
        default:
            return ITEM_NONE;
    }
}

static int PauseItemSlotId(FusePauseItem item, EquipValueSword sword, int32_t shield) {
    switch (item) {
        case FusePauseItem::Sword:
            return (sword != EQUIP_VALUE_SWORD_NONE) ? static_cast<int>(SwordSlotKeyFromEquipValue(sword)) : -1;
        case FusePauseItem::DekuShield:
        case FusePauseItem::HylianShield:
        case FusePauseItem::MirrorShield: {
            const ShieldSlotKey key =
                (item == FusePauseItem::HylianShield)
                    ? ShieldSlotKey::Hylian
                    : ((item == FusePauseItem::MirrorShield) ? ShieldSlotKey::Mirror : ShieldSlotKey::Deku);
            return static_cast<int>(ShieldSlotIndex(key));
        }
        case FusePauseItem::Boomerang:
        case FusePauseItem::Hammer:
        case FusePauseItem::None:
        default:
            return -1;
    }
}

static bool IsSlotFused(const FuseSlot& slot) {
    return slot.materialId != MaterialId::None && slot.durabilityCur > 0;
}

static Fuse::FuseResult TryFuseShield(ShieldSlotKey key, MaterialId id) {
    if (id == MaterialId::None) {
        return Fuse::FuseResult::NotAllowed;
    }

    std::array<SwordFuseSlot, FusePersistence::kSwordSlotCount> slots = Fuse::GetSwordSlots();
    FuseSlot& slot = slots[ShieldSlotIndex(key)];

    if (IsSlotFused(slot)) {
        return Fuse::FuseResult::AlreadyFused;
    }

    if (!Fuse::HasMaterial(id, 1)) {
        return Fuse::FuseResult::NotEnoughMaterial;
    }

    const MaterialDef* def = Fuse::GetMaterialDef(id);
    if (!def) {
        return Fuse::FuseResult::InvalidMaterial;
    }

    if (!Fuse::ConsumeMaterial(id, 1)) {
        return Fuse::FuseResult::NotEnoughMaterial;
    }

    const int maxDurability = Fuse::GetMaterialEffectiveBaseDurability(id);
    slot.materialId = id;
    slot.durabilityMax = std::max(maxDurability, 0);
    slot.durabilityCur = slot.durabilityMax;

    Fuse::ApplyLoadedSwordSlots(slots);
    return Fuse::FuseResult::Ok;
}

struct FusePromptContext {
    bool isPauseOpen = false;
    bool isEquipmentPage = false;
    bool isEquipmentGridCell = false;
    bool isSwordRow = false;
    bool isShieldRow = false;
    bool isOwnedEquip = false;
    bool isItemsPage = false;
    bool isBoomerangItem = false;
    bool isHammerItem = false;
    EquipValueSword hoveredSword = EQUIP_VALUE_SWORD_NONE;
    int32_t hoveredShield = EQUIP_VALUE_SHIELD_NONE;
    EquipValueSword equippedSword = EQUIP_VALUE_SWORD_NONE;
    bool isSwordAlreadyEquippedSlot = false;
    FusePauseItem activeItem = FusePauseItem::None;
    bool shouldShowFusePrompt = false;
    s16 hoverItemId = ITEM_NONE;
    int hoverSlotId = -1;
};

FusePromptContext BuildPromptContext(PlayState* play) {
    FusePromptContext context;

    if (play == nullptr) {
        return context;
    }

    PauseContext* pauseCtx = &play->pauseCtx;

    context.isPauseOpen = pauseCtx->state == 6;
    context.isEquipmentPage = pauseCtx->pageIndex == PAUSE_EQUIP;
    context.isItemsPage = pauseCtx->pageIndex == PAUSE_ITEM;
    context.isEquipmentGridCell = pauseCtx->cursorX[PAUSE_EQUIP] != 0;
    context.isSwordRow = context.isEquipmentGridCell && pauseCtx->cursorY[PAUSE_EQUIP] == 0;
    context.isShieldRow = context.isEquipmentGridCell && pauseCtx->cursorY[PAUSE_EQUIP] == 1;
    context.isOwnedEquip = context.isEquipmentGridCell &&
                           CHECK_OWNED_EQUIP(pauseCtx->cursorY[PAUSE_EQUIP], pauseCtx->cursorX[PAUSE_EQUIP] - 1);
    context.hoveredSword =
        (context.isSwordRow && context.isOwnedEquip) ? HoveredSwordForCursor(pauseCtx) : EQUIP_VALUE_SWORD_NONE;
    context.hoveredShield =
        (context.isShieldRow && context.isOwnedEquip) ? HoveredShieldForCursor(pauseCtx) : EQUIP_VALUE_SHIELD_NONE;
    context.equippedSword = static_cast<EquipValueSword>(CUR_EQUIP_VALUE(EQUIP_TYPE_SWORD));
    context.isSwordAlreadyEquippedSlot =
        (context.hoveredSword != EQUIP_VALUE_SWORD_NONE) && (context.equippedSword == context.hoveredSword);
    context.isBoomerangItem = context.isItemsPage && pauseCtx->cursorItem[PAUSE_ITEM] == ITEM_BOOMERANG;
    context.isHammerItem = context.isItemsPage && pauseCtx->cursorItem[PAUSE_ITEM] == ITEM_HAMMER;

    const bool swordEligible = context.isPauseOpen && context.isEquipmentPage && context.isSwordRow &&
                               context.isOwnedEquip && (context.hoveredSword != EQUIP_VALUE_SWORD_NONE) &&
                               context.isSwordAlreadyEquippedSlot;
    const bool boomerangEligible = context.isPauseOpen && context.isItemsPage && context.isBoomerangItem;
    const bool hammerEligible = context.isPauseOpen && context.isItemsPage && context.isHammerItem;
    const bool shieldEligible = context.isPauseOpen && context.isEquipmentPage && context.isShieldRow &&
                                context.isOwnedEquip && (context.hoveredShield != EQUIP_VALUE_SHIELD_NONE);

    context.activeItem =
        swordEligible
            ? FusePauseItem::Sword
            : (shieldEligible
                   ? ((context.hoveredShield == EQUIP_VALUE_SHIELD_HYLIAN)
                          ? FusePauseItem::HylianShield
                          : ((context.hoveredShield == EQUIP_VALUE_SHIELD_MIRROR) ? FusePauseItem::MirrorShield
                                                                                  : FusePauseItem::DekuShield))
                   : (boomerangEligible ? FusePauseItem::Boomerang
                                        : (hammerEligible ? FusePauseItem::Hammer : FusePauseItem::None)));
    context.shouldShowFusePrompt = swordEligible || boomerangEligible || hammerEligible || shieldEligible;

    if (context.isEquipmentPage && context.isEquipmentGridCell) {
        if (context.isOwnedEquip) {
            if (context.isSwordRow) {
                context.hoverItemId = ItemIdFromSwordEquipValue(context.hoveredSword);
            } else if (context.isShieldRow) {
                context.hoverItemId = ItemIdFromShieldEquipValue(context.hoveredShield);
            }
        }
        context.hoverSlotId = pauseCtx->cursorSlot[PAUSE_EQUIP];
    } else if (context.isItemsPage) {
        context.hoverItemId = static_cast<s16>(pauseCtx->cursorItem[PAUSE_ITEM]);
        context.hoverSlotId = pauseCtx->cursorItem[PAUSE_ITEM];
    }

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
    Input* input = &play->state.input[0];

    if (pauseCtx->state != 6) {
        sModal.open = false;
        return;
    }

    const u16 pressed = input->press.button;
    const bool fusePressed = IsFuseMenuPressed();

    FusePromptContext context = BuildPromptContext(play);

    if (!sModal.open) {
        if (fusePressed) {
            const bool allowed = context.shouldShowFusePrompt;
            Fuse::Log("[FuseDBG] PauseFuseOpenAttempt: hover=%d item=%d allowed=%d\n", context.hoverSlotId,
                      context.hoverItemId, allowed ? 1 : 0);
            if (!allowed) {
                const char* reason = nullptr;
                if (!context.isEquipmentPage && !context.isItemsPage) {
                    reason = "unsupported_context";
                } else if (context.hoverItemId == ITEM_NONE) {
                    reason = "no_item";
                } else {
                    reason = "not_fuse_capable";
                }
                Fuse::Log("[FuseDBG] PauseFuseDenied: reason=%s hover=%d item=%d\n",
                          reason ? reason : "not_fuse_capable", context.hoverSlotId, context.hoverItemId);
            }
        }

        if (context.shouldShowFusePrompt && fusePressed) {
            const FuseSlot resolvedSlot = ResolveSlotForPauseItem(context.activeItem, play);
            const FuseWeaponView weaponView = WeaponViewFromSlot(resolvedSlot);

            Fuse::Log("[FuseDBG_UI] Activation item=%s hoverSlot=%d hoverItem=%d fused=%d\n",
                      PauseItemName(context.activeItem, context.hoveredSword), context.hoverSlotId, context.hoverItemId,
                      weaponView.isFused ? 1 : 0);

            sModal.open = true;
            sModal.cursor = 0;
            sModal.scroll = 0;
            sModal.carouselPos = static_cast<float>(sModal.cursor);
            sModal.carouselVel = 0.0f;
            sModal.isLocked = weaponView.isFused;
            sModal.activeItem = context.activeItem;
            sModal.confirmedMaterialId = weaponView.materialId;
            sModal.highlightedMaterialId = MaterialId::None;
            sModal.previewMaterialId = MaterialId::None;
            sModal.promptType = FusePromptType::None;
            sModal.promptTimer = 0;
            SetUiState(sModal.isLocked ? FuseUiState::Locked : FuseUiState::Browse);

            const std::vector<MaterialEntry> materials = BuildMaterialList();
            if (sModal.isLocked && sModal.confirmedMaterialId != MaterialId::None) {
                for (int i = 0; i < static_cast<int>(materials.size()); ++i) {
                    if (materials[i].id == sModal.confirmedMaterialId) {
                        sModal.cursor = i;
                        sModal.carouselPos = static_cast<float>(i);
                        break;
                    }
                }
            }
            Fuse::Log("[FuseDBG] MaterialsList: item=%s count=%zu\n",
                      PauseItemName(sModal.activeItem, context.hoveredSword), materials.size());
            Fuse::Log("[FuseDBG] UI:ResolvedSlot item=%s mat=%d dur=%d/%d\n",
                      PauseItemName(sModal.activeItem, context.hoveredSword), static_cast<int>(resolvedSlot.materialId),
                      resolvedSlot.durabilityCur, resolvedSlot.durabilityMax);
            if (sModal.activeItem == FusePauseItem::Hammer) {
                const FuseSlot rawHammerSlot = Fuse::GetLoadedHammerSlot();
                Fuse::Log("[FuseDBG] UI:RawHammerSave mat=%d dur=%d/%d\n", static_cast<int>(rawHammerSlot.materialId),
                          rawHammerSlot.durabilityCur, rawHammerSlot.durabilityMax);
            }
            Fuse::Log("[FuseDBG] UI:Open item=%s confirmedMat=%d locked=%d\n",
                      PauseItemName(sModal.activeItem, context.hoveredSword), static_cast<int>(weaponView.materialId),
                      sModal.isLocked ? 1 : 0);
            Fuse::Log("[FuseDBG] PauseFuseOpen: item=%d slot=%d\n", context.hoverItemId,
                      PauseItemSlotId(context.activeItem, context.hoveredSword, context.hoveredShield));

            input->press.button &= ~BTN_L;
        }

        return;
    }

    if (!IsPausePageForItem(pauseCtx, sModal.activeItem)) {
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
        input->cur.button &= (u16) ~(BTN_B | BTN_START);
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
        const int prevCursor = sModal.cursor;
        sModal.cursor = MoveCursor(-1, materials);
        if (sModal.cursor != prevCursor) {
            Fuse::Log("[FuseDBG_UI] ScrollChange dir=up cursor=%d scroll=%d\n", sModal.cursor, sModal.scroll);
        }
        SetUiState(FuseUiState::Preview);
    }

    if (!sModal.isLocked && entryCount > 0 && (pressed & BTN_DDOWN || input->rel.stick_y < -30)) {
        const int prevCursor = sModal.cursor;
        sModal.cursor = MoveCursor(1, materials);
        if (sModal.cursor != prevCursor) {
            Fuse::Log("[FuseDBG_UI] ScrollChange dir=down cursor=%d scroll=%d\n", sModal.cursor, sModal.scroll);
        }
        SetUiState(FuseUiState::Preview);
    }

    const s32 modalYOffsetPx = kFuseModalYOffset * 8;
    const int visibleRows = ComputeVisibleRows(entryCount, modalYOffsetPx);
    UpdateModalBounds(materials, visibleRows);

    if (entryCount > 0) {
        float target = static_cast<float>(sModal.cursor);

        // simple smooth approach
        sModal.carouselPos += (target - sModal.carouselPos) * 0.25f;

        // clamp
        if (sModal.carouselPos < 0.0f) {
            sModal.carouselPos = 0.0f;
        }

        if (sModal.carouselPos > static_cast<float>(entryCount - 1)) {
            sModal.carouselPos = static_cast<float>(entryCount - 1);
        }
    } else {
        sModal.carouselPos = 0.0f;
    }

    const bool hasHighlight = entryCount > 0 && sModal.cursor >= 0 && sModal.cursor < entryCount;
    const MaterialId prevHighlighted = sModal.highlightedMaterialId;
    sModal.highlightedMaterialId = hasHighlight ? materials[sModal.cursor].id : MaterialId::None;
    const bool highlightEnabled = hasHighlight && materials[sModal.cursor].enabled;
    sModal.previewMaterialId = (!sModal.isLocked && highlightEnabled) ? sModal.highlightedMaterialId : MaterialId::None;

    if (prevHighlighted != sModal.highlightedMaterialId) {
        Fuse::Log("[FuseDBG_UI] SelectedMaterialChange cursor=%d material=%d enabled=%d\n", sModal.cursor,
                  static_cast<int>(sModal.highlightedMaterialId), highlightEnabled ? 1 : 0);
    }

    if (pressed & BTN_A) {
        if (sModal.isLocked) {
            TriggerPrompt(FusePromptType::AlreadyFused, 60);
        } else if (sModal.previewMaterialId == MaterialId::None) {
            // Nothing selectable
        } else if (sModal.uiState != FuseUiState::Confirm) {
            SetUiState(FuseUiState::Confirm);
        } else {
            Fuse::FuseResult result = Fuse::FuseResult::InvalidMaterial;
            if (sModal.activeItem == FusePauseItem::Hammer) {
                result = Fuse::TryFuseHammer(sModal.previewMaterialId);
            } else if (sModal.activeItem == FusePauseItem::Boomerang) {
                result = Fuse::TryFuseBoomerang(sModal.previewMaterialId);
            } else if (sModal.activeItem == FusePauseItem::Sword) {
                result = Fuse::TryFuseSword(sModal.previewMaterialId);
            } else if (sModal.activeItem == FusePauseItem::HylianShield ||
                       sModal.activeItem == FusePauseItem::MirrorShield ||
                       sModal.activeItem == FusePauseItem::DekuShield) {
                const ShieldSlotKey key =
                    (sModal.activeItem == FusePauseItem::HylianShield)
                        ? ShieldSlotKey::Hylian
                        : ((sModal.activeItem == FusePauseItem::MirrorShield) ? ShieldSlotKey::Mirror
                                                                              : ShieldSlotKey::Deku);
                result = TryFuseShield(key, sModal.previewMaterialId);
            }
            const bool success = result == Fuse::FuseResult::Ok;

            Fuse::Log("[FuseDBG] UI:Confirm item=%s mat=%d result=%d\n",
                      PauseItemName(sModal.activeItem, context.hoveredSword),
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
    input->cur.button &= (u16) ~(BTN_DUP | BTN_DDOWN | BTN_DLEFT | BTN_DRIGHT | BTN_L | BTN_R | BTN_Z | BTN_CUP |
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
        GfxPrint_Printf(&debugPrinter, "equippedSword:%d hoveredSword:%d\n", context.equippedSword,
                        context.hoveredSword);
        GfxPrint_Printf(&debugPrinter, "isPauseOpen:%d isEquip:%d isSwordRow:%d isOwned:%d isEquippedSlot:%d\n",
                        context.isPauseOpen, context.isEquipmentPage, context.isSwordRow, context.isOwnedEquip,
                        context.isSwordAlreadyEquippedSlot);
        GfxPrint_Printf(&debugPrinter, "shouldShow:%d\n", context.shouldShowFusePrompt);

        OPA = GfxPrint_Close(&debugPrinter);
        GfxPrint_Destroy(&debugPrinter);
    }

    //Fuse::Log("[FuseMVP] FusePause_DrawPrompt called\n");

    if (!context.shouldShowFusePrompt) {
        return;
    }

    RestorePauseTextState(gfxCtx, &OPA);
    gDPSetPrimColor(OPA++, 0, 0, 255, 255, 255, 255);

    GfxPrint printer;
    GfxPrint_Init(&printer);
    GfxPrint_Open(&printer, OPA);
    GfxPrint_SetColor(&printer, 255, 255, 255, 255);

    const s32 promptXPx = 238;
    const s32 promptYPx = 196;

    GfxPrint_SetPosPx(&printer, promptXPx, promptYPx);

    GfxPrint_Printf(&printer, "LB: FUSE MENU");

    OPA = GfxPrint_Close(&printer);
    GfxPrint_Destroy(&printer);
}

void FusePause_DrawModal(PlayState* play, Gfx** polyOpaDisp, Gfx** polyXluDisp) {
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
    const bool isPauseOpen = context.isPauseOpen;

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

    if (!IsPausePageForItem(pauseCtx, sModal.activeItem)) {
        sModal.open = false;
        return;
    }

    const int currentFrame = play->state.frames;

    static int sLastRenderLogFrame = -1000;
    static int sLastRenderCursor = -1;
    static int sLastRenderScroll = -1;
    static int sLastRenderState = -1;
    static int sLastRenderLocked = -1;
    static int sLastRenderItem = -1;

    const int renderItem = static_cast<int>(sModal.activeItem);
    const int renderState = static_cast<int>(sModal.uiState);
    const int renderLocked = sModal.isLocked ? 1 : 0;
    const bool renderStateChanged = (renderItem != sLastRenderItem) || (sModal.cursor != sLastRenderCursor) ||
                                    (sModal.scroll != sLastRenderScroll) || (renderState != sLastRenderState) ||
                                    (renderLocked != sLastRenderLocked);

    if (renderStateChanged || (currentFrame - sLastRenderLogFrame) >= 30) {
        Fuse::Log("[FuseDBG_UI] RenderEntry item=%d cursor=%d scroll=%d state=%d locked=%d\n", renderItem,
                  sModal.cursor, sModal.scroll, renderState, renderLocked);
        sLastRenderLogFrame = currentFrame;
        sLastRenderCursor = sModal.cursor;
        sLastRenderScroll = sModal.scroll;
        sLastRenderState = renderState;
        sLastRenderLocked = renderLocked;
        sLastRenderItem = renderItem;
    }

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
        //Fuse::Log("[FuseUI] modal open, drawing\n");
    }

    std::vector<MaterialEntry> materials = BuildMaterialList();
    const int entryCount = static_cast<int>(materials.size());
    const s32 modalYOffsetPx = kFuseModalYOffset * 8;
    const int visibleRows = ComputeVisibleRows(entryCount, modalYOffsetPx);
    UpdateModalBounds(materials, visibleRows);
    const bool durabilityBarEnabled = IsDurabilityBarEnabled();
    const FuseWeaponView weaponView = WeaponViewForPauseItem(sModal.activeItem, play);

    SetScissorFullscreen(OPA);
    SetScissorFullscreen(XLU);
    // Cover bottom strip (tune Y if needed)
    DrawSolidRectOpa(gfxCtx, &OPA, 0, 200, SCREEN_WIDTH, SCREEN_HEIGHT - 200, 0, 0, 0, 200);

    Gfx_SetupDL_39Opa(gfxCtx);

    SetScissorFullscreen(OPA);

    DrawSolidRectOpa(gfxCtx, &OPA, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 0, 0, 0, 96);

    constexpr s32 border = 2;

    DrawSolidRectOpa(gfxCtx, &OPA, kPanelX, kPanelY, kPanelW + 1, kPanelH + 1, 25, 25, 25, 170);

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

    const bool locked = sModal.isLocked;
    const bool confirmMode = sModal.uiState == FuseUiState::Confirm;

    const s32 durabilityTextY = kDurabilityTextY + modalYOffsetPx;
    const s32 durabilityBarY = durabilityTextY + kInfoLineSpacing - 2;

    const s32 leftInnerX = leftCardX + kCardPaddingX;
    const s32 leftInnerY = leftCardY + kCardPaddingY;
    const s32 leftInnerW = leftCardW - (kCardPaddingX * 2);
    const s32 leftInnerH = leftCardH - (kCardPaddingY * 2);
    const s32 leftTextClipX = leftInnerX - 4;
    const s32 leftTextClipW = leftInnerW + 8;

    const s32 rightInnerY = rightCardY + kCardPaddingY;
    const s32 rightInnerH = rightCardH - (kCardPaddingY * 2);
    const s32 carouselX = kCarouselLeftBound;
    const s32 carouselW = kCarouselCardW;
    const s32 carouselY = rightInnerY + kCarouselInsetY;
    const s32 carouselH = std::max(0, rightInnerH - (kCarouselInsetY * 2));
    const s32 centerY = carouselY + (carouselH / 2);
    const s32 listClipY = rightInnerY;
    const s32 listClipH = rightInnerH;

    SetScissorRect(OPA, kCarouselLeftBound - 8, listClipY, kCarouselCardW + 16, listClipH);

    const int baseIndex = static_cast<int>(floorf(sModal.carouselPos));
    const int carouselHalfRange = kCarouselVisibleCards - 1;
    for (int idx = baseIndex - carouselHalfRange; idx <= baseIndex + carouselHalfRange; idx++) {
        if (idx < 0 || idx >= entryCount) {
            continue;
        }

        const float rel = static_cast<float>(idx) - sModal.carouselPos;

        const s32 cardX = carouselX;
        const s32 cardY = static_cast<s32>(centerY + rel * kCarouselStride - (kCarouselCardH / 2));
        const s32 cardW = carouselW;
        const s32 cardH = kCarouselCardH;
        const bool isSelected = (idx == sModal.cursor);
        const MaterialEntry& entry = materials[idx];
        DrawMaterialCard(gfxCtx, OPA, entry, cardX, cardY, cardW, cardH, isSelected, entry.enabled, locked,
                         confirmMode);
    }

    SetScissorFullscreen(OPA);

    if (durabilityBarEnabled && weaponView.isFused && weaponView.maxDurability > 0) {
        const int curDurability = std::clamp(weaponView.curDurability, 0, weaponView.maxDurability);
        const f32 ratio = static_cast<f32>(curDurability) / static_cast<f32>(weaponView.maxDurability);
        const s32 barWidth = kDurabilityBarWidth;
        const s32 innerBarWidth = std::max(barWidth - 2, 0);
        const s32 filled = std::clamp(static_cast<s32>(ratio * innerBarWidth), 0, innerBarWidth);
        const s32 barHeight = kDurabilityBarHeight;

        const s32 barX = leftCardX + kLeftCardInnerPadding;
        const s32 barY = durabilityBarY;
        const f32 percent = ratio * 100.0f;

        static int sLastDurabilityLogFrame = -1000;
        static int sLastDurabilityCur = -1;
        static int sLastDurabilityMax = -1;
        static int sLastDurabilityFilled = -1;
        static s32 sLastDurabilityBarX = -1;
        static s32 sLastDurabilityBarY = -1;
        static s32 sLastDurabilityBarW = -1;
        static s32 sLastDurabilityBarH = -1;

        const bool durabilityChanged = (curDurability != sLastDurabilityCur) ||
                                       (weaponView.maxDurability != sLastDurabilityMax) ||
                                       (filled != sLastDurabilityFilled) || (barX != sLastDurabilityBarX) ||
                                       (barY != sLastDurabilityBarY) || (barWidth != sLastDurabilityBarW) ||
                                       (barHeight != sLastDurabilityBarH);

        if (durabilityChanged || (currentFrame - sLastDurabilityLogFrame) >= 30) {
            Fuse::Log(
                "[FuseDBG_UI] DurabilityDraw pct=%.2f barX=%d barY=%d barW=%d barH=%d filled=%d cur=%d max=%d\n",
                percent, barX, barY, barWidth, barHeight, filled, curDurability, weaponView.maxDurability);
            sLastDurabilityLogFrame = currentFrame;
            sLastDurabilityCur = curDurability;
            sLastDurabilityMax = weaponView.maxDurability;
            sLastDurabilityFilled = filled;
            sLastDurabilityBarX = barX;
            sLastDurabilityBarY = barY;
            sLastDurabilityBarW = barWidth;
            sLastDurabilityBarH = barHeight;
        }

        SetScissorRect(OPA, leftTextClipX, leftInnerY, leftTextClipW, leftInnerH);
        DrawDurabilityBar(gfxCtx, &OPA, barX, barY, barWidth, barHeight, filled);
        SetScissorFullscreen(OPA);
    }

    gDPSetPrimColor(OPA++, 0, 0, 255, 255, 255, 255);

    {
        GfxPrint printer;
        GfxPrint_Init(&printer);
        GfxPrint_Open(&printer, OPA);
        GfxPrint_SetColor(&printer, 255, 255, 255, 255);

        GfxPrint_SetPosPx(&printer, kTitleX, kTitleY + modalYOffsetPx);
        GfxPrint_Printf(&printer, "Fuse");

        const s32 promptX = kFooterX;
        const s32 promptY = kFooterY + modalYOffsetPx;
        const s32 nextPromptLineY = promptY + kPromptLineSpacing;

        GfxPrint_SetPosPx(&printer, promptX, promptY);
        if (locked) {
            GfxPrint_Printf(&printer, "B: Back");
        } else if (confirmMode) {
            GfxPrint_Printf(&printer, "A: Confirm   B: Cancel");
        } else {
            GfxPrint_Printf(&printer, "A: Select   B: Back");
        }

        if (locked || (sModal.promptTimer > 0 && sModal.promptType == FusePromptType::AlreadyFused)) {
            GfxPrint_SetPosPx(&printer, promptX, nextPromptLineY);
            GfxPrint_SetColor(&printer, 255, 120, 120, 255);
            GfxPrint_Printf(&printer, "ITEM ALREADY FUSED");
            GfxPrint_SetColor(&printer, 255, 255, 255, 255);
        }

        OPA = GfxPrint_Close(&printer);
        GfxPrint_Destroy(&printer);
    }

    const char* selectedItemName = PauseItemName(sModal.activeItem, context.hoveredSword);


    const s32 leftSelectedY = kSelectedY + modalYOffsetPx;
    const s32 leftItemNameY = kItemNameY + modalYOffsetPx;
    const s32 leftDurabilityY = durabilityTextY;

    SetScissorRect(OPA, leftTextClipX, leftInnerY, leftTextClipW, leftInnerH);
    {
        GfxPrint printer;
        GfxPrint_Init(&printer);
        GfxPrint_Open(&printer, OPA);
        GfxPrint_SetColor(&printer, 255, 255, 255, 255);

        GfxPrint_SetPosPx(&printer, kLeftTextX, leftSelectedY);
        GfxPrint_Printf(&printer, "Selected:");

        GfxPrint_SetPosPx(&printer, kLeftTextX, leftItemNameY);
        GfxPrint_Printf(&printer, "%s", selectedItemName);

        GfxPrint_SetPosPx(&printer, kLeftTextX, leftDurabilityY);
        if (!weaponView.isFused) {
            GfxPrint_Printf(&printer, "Durability: --");
        } else {
            GfxPrint_Printf(&printer, "Durability: %d / %d", weaponView.curDurability, weaponView.maxDurability);
        }

        OPA = GfxPrint_Close(&printer);
        GfxPrint_Destroy(&printer);
    }

    SetScissorFullscreen(OPA);
}

} // extern "C"
