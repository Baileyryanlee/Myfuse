#include "soh/Enhancements/Fuse/UI/FusePauseBridge.h"

#include "global.h"
#include <libultraship/libultra/gbi.h>
#include "functions.h"
#include "soh/Enhancements/Fuse/Fuse.h"
#include "soh/Enhancements/Fuse/FuseInput.h"
#include "soh/OTRGlobals.h"
#include <libultraship/controller/controldeck/ControlDeck.h>
#include <libultraship/libultraship.h>
#include "soh/cvar_prefixes.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
static bool IsFuseMenuPressed() {
    const bool down = FuseInput::IsMenuHeld();
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
constexpr s32 kCarouselLeftBound = 156;

constexpr s32 kCarouselCardH = 44;
constexpr s32 kCarouselGap = 10;
constexpr s32 kCarouselStride = kCarouselCardH + kCarouselGap;

constexpr s32 kCarouselVisibleCards = 3;
constexpr s32 kRightCardInnerPad = 6;
constexpr s32 kSpriteBoxSize = 44;
constexpr [[maybe_unused]] s32 kQtyBoxW = 24;
constexpr s32 kAttackBoxW = 24;
constexpr s32 kAttackBoxH = 14;
constexpr s32 kModifierIconBoxSize = 6;
constexpr s32 kModifierIconBoxGap = 2;
constexpr s32 kModifierIconRowCount = 6;
constexpr s32 kCardNameTopPad = 2;
constexpr s32 kCardRowGap = 3;
constexpr s32 kCardQtyRowExtraPad = 1;
constexpr s32 kCardModifierBandPad = 6;
constexpr s32 kCardVanillaIconSize = 24;
constexpr s32 kOrderedGlyphBasePx = 16;

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
constexpr const char* kPauseCardNameScaleCVar = CVAR_DEVELOPER_TOOLS("Fuse.UiPauseCardNameScale");
constexpr const char* kPauseCardQtyScaleCVar = CVAR_DEVELOPER_TOOLS("Fuse.UiPauseCardQtyScale");
constexpr const char* kPauseFooterPromptScaleCVar = CVAR_DEVELOPER_TOOLS("Fuse.UiPauseFooterPromptScale");
constexpr const char* kPauseFooterStatusScaleCVar = CVAR_DEVELOPER_TOOLS("Fuse.UiPauseFooterStatusScale");
constexpr const char* kPauseUseOrderedFontCVar = CVAR_DEVELOPER_TOOLS("Fuse.UiPauseUseOrderedFont");
constexpr const char* kPauseOrderedTightenCVar = CVAR_DEVELOPER_TOOLS("Fuse.Pause.OrderedTighten");

static Font sFuseOrderedFont;
static bool sFuseOrderedFontLoaded = false;
static s16 sFuseOrderedGlyphForByte[256];
static constexpr u8 kFuseMsgNewline = 0x01;
static constexpr u8 kFuseMsgEnd = 0x02;

void FuseUi_EnsureOrderedFontLoaded() {
    if (sFuseOrderedFontLoaded) {
        return;
    }

    Font_LoadOrderedFont(&sFuseOrderedFont);

    std::fill(std::begin(sFuseOrderedGlyphForByte), std::end(sFuseOrderedGlyphForByte), static_cast<s16>(-1));
    int glyph = 0;
    for (int i = 0;; i++) {
        const u8 ch = static_cast<u8>(sFuseOrderedFont.msgBuf[i]);
        if (ch == 0 || ch == kFuseMsgEnd) {
            break;
        }
        if (ch == kFuseMsgNewline) {
            continue;
        }
        if (glyph >= 0x8B) {
            break;
        }
        sFuseOrderedGlyphForByte[ch] = static_cast<s16>(glyph);
        glyph++;
    }

    if (sFuseOrderedGlyphForByte[static_cast<u8>('?')] < 0) {
        sFuseOrderedGlyphForByte[static_cast<u8>('?')] = 0;
    }
    if (sFuseOrderedGlyphForByte[static_cast<u8>(' ')] < 0) {
        sFuseOrderedGlyphForByte[static_cast<u8>(' ')] = 0;
    }

    sFuseOrderedFontLoaded = true;
}

constexpr int kFontGlyphW = 16;
constexpr int kFontGlyphH = 16;
constexpr int kFontGlyphBytes = FONT_CHAR_TEX_SIZE;

void FuseUi_DrawOrderedGlyph(GraphicsContext* gfxCtx, Gfx*& opa, int x, int y, int w, int h, int glyphIndex, u8 r, u8 g,
                             u8 b, u8 a) {
    (void)gfxCtx;

    if (w <= 0 || h <= 0) {
        return;
    }

    const int clampedGlyph = std::clamp(glyphIndex, 0, 0x8A);
    void* tex = (void*)(sFuseOrderedFont.fontBuf + (clampedGlyph * kFontGlyphBytes));

    gDPPipeSync(opa++);
    gDPSetTextureLUT(opa++, G_TT_NONE);
    gDPSetPrimColor(opa++, 0, 0, r, g, b, a);
    gDPSetCombineMode(opa++, G_CC_MODULATEIDECALA_PRIM, G_CC_MODULATEIDECALA_PRIM);
    gDPSetRenderMode(opa++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPLoadTextureBlock_4b(opa++, tex, G_IM_FMT_I, kFontGlyphW, kFontGlyphH, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                           G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    const s32 dstW = std::max(1, w);
    const s32 dstH = std::max(1, h);

    // 16.10 fixed-point step:
    // How many texture subpixels to advance per screen pixel.
    const s32 ds = (kFontGlyphW << 10) / dstW;
    const s32 dt = (kFontGlyphH << 10) / dstH;

    gSPTextureRectangle(opa++, (x << 2), (y << 2), ((x + dstW) << 2), ((y + dstH) << 2), G_TX_RENDERTILE, 0, 0, ds,
                        dt);
}

void FuseUi_DrawOrderedTextTracked(GraphicsContext* gfxCtx, Gfx*& opa, int x, int y, float scale, const char* text,
                                   u8 r, u8 g, u8 b, u8 a, float trackingPx) {
    if (text == nullptr) {
        return;
    }

    FuseUi_EnsureOrderedFontLoaded();

    const int w = std::max(1, static_cast<int>(std::lround(kFontGlyphW * scale)));
    const int h = std::max(1, static_cast<int>(std::lround(kFontGlyphH * scale)));
    const float clampedTracking = std::max(-2.0f, trackingPx);

    float penX = static_cast<float>(x);
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p != '\0'; ++p) {
        const u8 ch = *p;
        int gi = -1;
        if (sFuseOrderedFontLoaded) {
            gi = sFuseOrderedGlyphForByte[ch];
        }
        if (gi < 0) {
            gi = sFuseOrderedGlyphForByte[static_cast<u8>('?')];
        }
        if (gi < 0) {
            gi = sFuseOrderedGlyphForByte[static_cast<u8>(' ')];
        }
        if (gi < 0) {
            gi = 0;
        }

        FuseUi_DrawOrderedGlyph(gfxCtx, opa, static_cast<int>(std::lround(penX)), y, w, h, gi, r, g, b, a);
        penX += static_cast<float>(w) + clampedTracking;
    }
}

void FuseUi_DrawOrderedText(GraphicsContext* gfxCtx, Gfx*& opa, int x, int y, float scale, const char* text, u8 r, u8 g,
                            u8 b, u8 a) {
    FuseUi_DrawOrderedTextTracked(gfxCtx, opa, x, y, scale, text, r, g, b, a, 0.0f);
}

void FuseUi_DrawOrderedTextWithColor(GraphicsContext* gfxCtx, Gfx*& opa, s32 x, s32 y, float scale, u8 r, u8 g, u8 b,
                                     u8 a, const char* text) {
    gDPSetPrimColor(opa++, 0, 0, r, g, b, a);
    FuseUi_DrawOrderedText(gfxCtx, opa, x, y, scale, text, r, g, b, a);
}

void FuseUi_DrawOrderedTextTrackedWithColor(GraphicsContext* gfxCtx, Gfx*& opa, s32 x, s32 y, float scale, u8 r, u8 g,
                                            u8 b, u8 a, const char* text, float trackingPx) {
    gDPSetPrimColor(opa++, 0, 0, r, g, b, a);
    FuseUi_DrawOrderedTextTracked(gfxCtx, opa, x, y, scale, text, r, g, b, a, trackingPx);
}

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

bool FusePause_TryGetVanillaItemIconForMaterial(MaterialId mat, ItemID* outItemId) {
    if (outItemId == nullptr) {
        return false;
    }

    switch (mat) {
        case MaterialId::DekuNut:
            *outItemId = ITEM_NUT;
            return true;
        case MaterialId::Stick:
            *outItemId = ITEM_STICK;
            return true;
        case MaterialId::Bomb:
            *outItemId = ITEM_BOMB;
            return true;
        default:
            return false;
    }
}

void FusePause_DrawVanillaItemIcon(GraphicsContext* gfxCtx, Gfx*& opa, ItemID itemId, s32 x, s32 y, s32 w, s32 h) {
    if (gfxCtx == nullptr || w <= 0 || h <= 0) {
        return;
    }

    const s32 srcW = 32;
    const s32 srcH = 32;
    void* texture = gItemIcons[itemId];
    if (texture == nullptr) {
        return;
    }

    const s32 ds = (srcW << 10) / w;
    const s32 dt = (srcH << 10) / h;

    gDPPipeSync(opa++);
    Gfx_SetupDL_42Opa(gfxCtx);
    gDPSetTextureLUT(opa++, G_TT_NONE);
    gDPSetPrimColor(opa++, 0, 0, 255, 255, 255, 255);
    gDPSetCombineMode(opa++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
    gDPLoadTextureBlock(opa++, texture, G_IM_FMT_RGBA, G_IM_SIZ_32b, srcW, srcH, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    gSPTextureRectangle(opa++, (x << 2), (y << 2), ((x + w) << 2), ((y + h) << 2), G_TX_RENDERTILE, 0, 0, ds, dt);
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

float ReadScaleFloat(const char* cvarName, float defaultValue) {
    return CVarGetFloat(cvarName, defaultValue);
}

s32 TextWidthMonoPx(const char* text, s32 glyphPx) {
    if (text == nullptr) {
        return 0;
    }

    return static_cast<s32>(std::strlen(text)) * glyphPx;
}

std::string TruncateToPxEllipsis(const char* text, s32 maxPx, s32 glyphPx) {
    if (text == nullptr || maxPx <= 0) {
        return "";
    }

    const s32 maxChars = maxPx / std::max(1, glyphPx);
    const std::string src(text);

    if (maxChars <= 0) {
        return "";
    }

    if (static_cast<s32>(src.size()) <= maxChars) {
        return src;
    }

    if (maxChars <= 3) {
        return "...";
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

    ItemID vanillaIconItemId = ITEM_NONE;
    const bool hasVanillaIcon = FusePause_TryGetVanillaItemIconForMaterial(entry.id, &vanillaIconItemId);

    char qtyText[16];
    std::snprintf(qtyText, sizeof(qtyText), "x%d", entry.quantity);

    char atkText[16];
    if (entry.def != nullptr) {
        std::snprintf(atkText, sizeof(atkText), "%d", entry.def->attackBonus);
    } else {
        std::snprintf(atkText, sizeof(atkText), "--");
    }

    const float nameScale = ReadScaleFloat(kPauseCardNameScaleCVar, 1.0f);
    const float qtyScale = ReadScaleFloat(kPauseCardQtyScaleCVar, 0.70f);

    const s32 nameY = cardY + kRightCardInnerPad + kCardNameTopPad;
    const s32 nameH = std::max(1, static_cast<s32>(std::lround(static_cast<float>(kOrderedGlyphBasePx) * nameScale)));
    const s32 qtyY = nameY + nameH + kCardRowGap + kCardQtyRowExtraPad;

    const s32 scaledNameGlyphPx =
        std::max(1, static_cast<s32>(std::lround(static_cast<float>(kOrderedGlyphBasePx) * nameScale)));
    const s32 nameMaxPx = std::max(0, cardW - (textX - cardX) - kRightCardInnerPad);
    const std::string materialName =
        TruncateToPxEllipsis(entry.def ? entry.def->name : "Unknown", nameMaxPx, scaledNameGlyphPx);

    const s32 qtyX = textX;

    const bool useOrderedFont = CVarGetInteger(kPauseUseOrderedFontCVar, 1) != 0;
    const float orderedTighten = std::clamp(CVarGetFloat(kPauseOrderedTightenCVar, 0.75f), 0.0f, 2.0f);
    const float trackingPx = -orderedTighten;
    const u8 textR = 255;
    const u8 textG = 255;
    const u8 textB = selected ? 0 : 255;
    const u8 textA = 255;

    const s32 atkX = cardX + cardW - kRightCardInnerPad - kAttackBoxW;

    if (hasVanillaIcon) {
        const s32 iconX = spriteX + ((kSpriteBoxSize - kCardVanillaIconSize) / 2);
        const s32 iconY = spriteY + ((kSpriteBoxSize - kCardVanillaIconSize) / 2);
        FusePause_DrawVanillaItemIcon(gfxCtx, opa, vanillaIconItemId, iconX, iconY, kCardVanillaIconSize,
                                      kCardVanillaIconSize);
    }

    const s32 qtyGlyphPx =
        std::max(1, static_cast<s32>(std::lround(static_cast<float>(kOrderedGlyphBasePx) * qtyScale)));
    const s32 atkTextPx = static_cast<s32>(std::strlen(atkText)) * qtyGlyphPx;
    const s32 atkTextX = atkX + ((kAttackBoxW - atkTextPx) / 2);

    if (useOrderedFont) {
        RestorePauseTextState(gfxCtx, &opa);
        FuseUi_DrawOrderedTextTracked(gfxCtx, opa, textX, nameY, nameScale, materialName.c_str(), textR, textG,
                                      textB, textA, trackingPx);
        FuseUi_DrawOrderedText(gfxCtx, opa, qtyX, qtyY, qtyScale, qtyText, textR, textG, textB, textA);
        FuseUi_DrawOrderedText(gfxCtx, opa, atkTextX, qtyY, qtyScale, atkText, textR, textG, textB, textA);
    } else {
        RestorePauseTextState(gfxCtx, &opa);

        GfxPrint printer;
        GfxPrint_Init(&printer);
        GfxPrint_Open(&printer, opa);
        GfxPrint_SetColor(&printer, textR, textG, textB, textA);

        GfxPrint_SetPosPx(&printer, textX, nameY);
        GfxPrint_Printf(&printer, "%s", materialName.c_str());

        GfxPrint_SetPosPx(&printer, qtyX, qtyY);
        GfxPrint_Printf(&printer, "%s", qtyText);

        GfxPrint_SetPosPx(&printer, atkTextX, qtyY);
        GfxPrint_Printf(&printer, "%s", atkText);

        opa = GfxPrint_Close(&printer);
        GfxPrint_Destroy(&printer);
    }

    const s32 iconY = cardY + cardH - kCardModifierBandPad - kModifierIconBoxSize;
    for (s32 i = 0; i < kModifierIconRowCount; i++) {
        const s32 iconX = textX + i * (kModifierIconBoxSize + kModifierIconBoxGap);
        DrawSolidRectOpa(gfxCtx, &opa, iconX, iconY, kModifierIconBoxSize, kModifierIconBoxSize, 24, 24, 24, 220);
    }
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
    const s32 carouselRightBound = kPanelX + kPanelW - 10;
    const s32 carouselW = std::max(0, carouselRightBound - kCarouselLeftBound);
    const s32 carouselY = rightInnerY + kCarouselInsetY;
    const s32 carouselH = std::max(0, rightInnerH - (kCarouselInsetY * 2));
    const s32 centerY = carouselY + (carouselH / 2);
    const s32 listClipY = rightInnerY;
    const s32 listClipH = rightInnerH;

    SetScissorRect(OPA, kCarouselLeftBound - 8, listClipY, carouselW + 16, listClipH);

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

        OPA = GfxPrint_Close(&printer);
        GfxPrint_Destroy(&printer);
    }

    {
        const float promptScale = ReadScaleFloat(kPauseFooterPromptScaleCVar, 0.90f);
        const float statusScale = ReadScaleFloat(kPauseFooterStatusScaleCVar, 0.85f);
        const s32 promptX = kFooterX;
        const s32 promptY = kFooterY + modalYOffsetPx;
        const s32 nextPromptLineY = promptY + kPromptLineSpacing;

        const char* promptText = "A: Select   B: Back";
        if (locked) {
            promptText = "B: Back";
        } else if (confirmMode) {
            promptText = "A: Confirm   B: Cancel";
        }

        const float orderedTighten = std::clamp(CVarGetFloat(kPauseOrderedTightenCVar, 0.75f), 0.0f, 2.0f);
        const float trackingPx = -orderedTighten;
        FuseUi_DrawOrderedTextTrackedWithColor(gfxCtx, OPA, promptX, promptY, promptScale, 255, 255, 255, 255,
                                               promptText, trackingPx);

        if (locked || (sModal.promptTimer > 0 && sModal.promptType == FusePromptType::AlreadyFused)) {
            FuseUi_DrawOrderedTextTrackedWithColor(gfxCtx, OPA, promptX, nextPromptLineY, statusScale, 255, 120, 120,
                                                   255, "ITEM ALREADY FUSED", trackingPx);
        }
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
