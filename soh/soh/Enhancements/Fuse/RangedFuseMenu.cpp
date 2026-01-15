#include "soh/Enhancements/Fuse/RangedFuseMenu.h"

#include "global.h"
#include "functions.h"
#include "soh/Enhancements/Fuse/Fuse.h"
#include "soh/OTRGlobals.h"

#include <libultraship/controller/controldeck/ControlDeck.h>
#include <libultraship/libultraship.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {
constexpr int kNavRepeatFrames = 10;
constexpr int kReopenCooldownFrames = 18;
constexpr int kStickThreshold = 30;
constexpr int kVisibleRows = 7;
constexpr float kTextScale = 0.85f;
constexpr int kMenuX = 24;
constexpr int kMenuY = 56;
constexpr int kBaseRowHeight = 14;
constexpr int kBaseListWidth = 160;
constexpr int kRowHeight = static_cast<int>((kBaseRowHeight * kTextScale) + 0.5f);
constexpr int kPanelPadding = 6;
constexpr int kListWidth = static_cast<int>((kBaseListWidth * kTextScale) + 0.5f);
constexpr bool kEnableTimeSlowdown = false;
constexpr float kTimeSlowdownFactor = 0.35f;

enum class RangedWeaponType {
    None,
    Bow,
    Slingshot,
    Hookshot,
};

enum class RangedFuseSlotId {
    Arrows = 0,
    Slingshot = 1,
    Hookshot = 2,
};

struct MaterialEntry {
    MaterialId id = MaterialId::None;
    const MaterialDef* def = nullptr;
    int quantity = 0;
};

struct RangedFuseMenuState {
    bool isOpen = false;
    int selectedIndex = 0;
    int scrollOffset = 0;
    float reopenCooldownTimer = 0.0f;
    float navRepeatTimer = 0.0f;
    RangedWeaponType weapon = RangedWeaponType::None;
    RangedFuseSlotId slot = RangedFuseSlotId::Arrows;
    std::vector<MaterialEntry> entries;
};

RangedFuseMenuState sMenu;

bool IsFuseMenuHeld() {
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

    return (pads[0].button & BTN_CUSTOM_FUSE_MENU) != 0;
}

const char* WeaponName(RangedWeaponType weapon) {
    switch (weapon) {
        case RangedWeaponType::Bow:
            return "bow";
        case RangedWeaponType::Slingshot:
            return "slingshot";
        case RangedWeaponType::Hookshot:
            return "hookshot";
        default:
            return "none";
    }
}

int SlotIdValue(RangedFuseSlotId slot) {
    return static_cast<int>(slot);
}

bool IsPlayerAimingRangedWeapon(PlayState* play, RangedWeaponType* outWeapon, RangedFuseSlotId* outSlot) {
    if (!play) {
        return false;
    }

    Player* player = GET_PLAYER(play);
    if (!player) {
        return false;
    }

    const bool aiming = (player->stateFlags1 & (PLAYER_STATE1_READY_TO_FIRE | PLAYER_STATE1_FIRST_PERSON)) != 0;
    if (!aiming) {
        return false;
    }

    switch (player->heldItemAction) {
        case PLAYER_IA_BOW:
        case PLAYER_IA_BOW_FIRE:
        case PLAYER_IA_BOW_ICE:
        case PLAYER_IA_BOW_LIGHT:
        case PLAYER_IA_BOW_0C:
        case PLAYER_IA_BOW_0D:
        case PLAYER_IA_BOW_0E:
            if (outWeapon) {
                *outWeapon = RangedWeaponType::Bow;
            }
            if (outSlot) {
                *outSlot = RangedFuseSlotId::Arrows;
            }
            return true;
        case PLAYER_IA_SLINGSHOT:
            if (outWeapon) {
                *outWeapon = RangedWeaponType::Slingshot;
            }
            if (outSlot) {
                *outSlot = RangedFuseSlotId::Slingshot;
            }
            return true;
        case PLAYER_IA_HOOKSHOT:
        case PLAYER_IA_LONGSHOT:
            if (outWeapon) {
                *outWeapon = RangedWeaponType::Hookshot;
            }
            if (outSlot) {
                *outSlot = RangedFuseSlotId::Hookshot;
            }
            return true;
        default:
            break;
    }

    return false;
}

MaterialId GetSlotMaterial(RangedFuseSlotId slot) {
    switch (slot) {
        case RangedFuseSlotId::Arrows:
            return Fuse::GetArrowsMaterial();
        case RangedFuseSlotId::Slingshot:
            return Fuse::GetSlingshotMaterial();
        case RangedFuseSlotId::Hookshot:
            return Fuse::GetHookshotMaterial();
        default:
            return MaterialId::None;
    }
}

void ClearSlot(RangedFuseSlotId slot) {
    switch (slot) {
        case RangedFuseSlotId::Arrows:
            Fuse::ClearArrowsFuse();
            return;
        case RangedFuseSlotId::Slingshot:
            Fuse::ClearSlingshotFuse();
            return;
        case RangedFuseSlotId::Hookshot:
            Fuse::ClearHookshotFuse();
            return;
    }
}

Fuse::FuseResult TryFuseSlot(RangedFuseSlotId slot, MaterialId id) {
    switch (slot) {
        case RangedFuseSlotId::Arrows:
            return Fuse::TryFuseArrows(id);
        case RangedFuseSlotId::Slingshot:
            return Fuse::TryFuseSlingshot(id);
        case RangedFuseSlotId::Hookshot:
            return Fuse::TryFuseHookshot(id);
    }
    return Fuse::FuseResult::InvalidMaterial;
}

void BuildEntries(RangedFuseMenuState& state) {
    state.entries.clear();
    state.entries.push_back({ MaterialId::None, nullptr, 0 });

    size_t count = 0;
    const MaterialDef* defs = Fuse::GetMaterialDefs(&count);
    for (size_t i = 0; i < count; ++i) {
        const MaterialDef& def = defs[i];
        if (def.id == MaterialId::None) {
            continue;
        }
        state.entries.push_back({ def.id, &def, Fuse::GetMaterialCount(def.id) });
    }

    if (state.selectedIndex >= static_cast<int>(state.entries.size())) {
        state.selectedIndex = static_cast<int>(state.entries.size()) - 1;
    }
    if (state.selectedIndex < 0) {
        state.selectedIndex = 0;
    }
}

int FindEntryIndexForMaterial(const std::vector<MaterialEntry>& entries, MaterialId id) {
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].id == id) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

void ApplySlowdown(bool active) {
    static bool sSlowdownActive = false;
    if (active == sSlowdownActive) {
        return;
    }

    if (!kEnableTimeSlowdown) {
        // TODO: Wire time slowdown to a safe engine time scale hook if one becomes available.
        (void)kTimeSlowdownFactor;
        sSlowdownActive = active;
        return;
    }

    sSlowdownActive = active;
    if (active) {
        (void)kTimeSlowdownFactor;
    }
}

void OpenMenu(RangedWeaponType weapon, RangedFuseSlotId slot) {
    sMenu.isOpen = true;
    sMenu.weapon = weapon;
    sMenu.slot = slot;
    sMenu.navRepeatTimer = 0.0f;
    sMenu.scrollOffset = 0;
    BuildEntries(sMenu);
    sMenu.selectedIndex = FindEntryIndexForMaterial(sMenu.entries, GetSlotMaterial(slot));

    Fuse::Log("[FuseDBG] RangedFuseMenu: Open weapon=%s slot=%d\n", WeaponName(weapon), SlotIdValue(slot));

    ApplySlowdown(true);
}

void CloseMenu() {
    if (!sMenu.isOpen) {
        return;
    }

    sMenu.isOpen = false;
    sMenu.reopenCooldownTimer = kReopenCooldownFrames;
    sMenu.navRepeatTimer = 0.0f;
    sMenu.entries.clear();

    Fuse::Log("[FuseDBG] RangedFuseMenu: Close\n");

    ApplySlowdown(false);
}

void CommitSelection() {
    if (sMenu.entries.empty()) {
        return;
    }

    const MaterialId chosen = sMenu.entries[sMenu.selectedIndex].id;
    const MaterialId current = GetSlotMaterial(sMenu.slot);

    if (chosen == MaterialId::None) {
        if (current != MaterialId::None) {
            ClearSlot(sMenu.slot);
        }
    } else if (chosen != current) {
        if (current != MaterialId::None) {
            ClearSlot(sMenu.slot);
        }
        TryFuseSlot(sMenu.slot, chosen);
    }

    if (chosen == MaterialId::None) {
        Fuse::Log("[FuseDBG] RangedFuseMenu: Select weapon=%s slot=%d material=NONE\n", WeaponName(sMenu.weapon),
                  SlotIdValue(sMenu.slot));
    } else {
        Fuse::Log("[FuseDBG] RangedFuseMenu: Select weapon=%s slot=%d material=%d\n", WeaponName(sMenu.weapon),
                  SlotIdValue(sMenu.slot), static_cast<int>(chosen));
    }
}

void UpdateCooldowns() {
    if (sMenu.reopenCooldownTimer > 0.0f) {
        sMenu.reopenCooldownTimer = std::max(0.0f, sMenu.reopenCooldownTimer - 1.0f);
    }
    if (sMenu.navRepeatTimer > 0.0f) {
        sMenu.navRepeatTimer = std::max(0.0f, sMenu.navRepeatTimer - 1.0f);
    }
}

void AdjustScroll() {
    const int totalEntries = static_cast<int>(sMenu.entries.size());
    const int availableHeight = std::max(0, SCREEN_HEIGHT - kMenuY - kPanelPadding);
    const int maxVisibleRows = std::max(1, availableHeight / kRowHeight);
    const int visible = std::min({ kVisibleRows, totalEntries, maxVisibleRows });
    const int maxScroll = std::max(0, totalEntries - visible);

    if (sMenu.selectedIndex < sMenu.scrollOffset) {
        sMenu.scrollOffset = sMenu.selectedIndex;
    } else if (sMenu.selectedIndex >= sMenu.scrollOffset + visible) {
        sMenu.scrollOffset = sMenu.selectedIndex - visible + 1;
    }

    sMenu.scrollOffset = std::clamp(sMenu.scrollOffset, 0, maxScroll);
}

void HandleNavigation(Input* input) {
    if (!input) {
        return;
    }

    int move = 0;
    if (input->press.button & BTN_DUP) {
        move = -1;
    } else if (input->press.button & BTN_DDOWN) {
        move = 1;
    } else if (sMenu.navRepeatTimer <= 0.0f) {
        if (input->cur.stick_y >= kStickThreshold) {
            move = -1;
        } else if (input->cur.stick_y <= -kStickThreshold) {
            move = 1;
        }
    }

    if (move != 0) {
        const int totalEntries = static_cast<int>(sMenu.entries.size());
        sMenu.selectedIndex = std::clamp(sMenu.selectedIndex + move, 0, std::max(0, totalEntries - 1));
        sMenu.navRepeatTimer = static_cast<float>(kNavRepeatFrames);
        AdjustScroll();
    }

    input->press.button &= ~(BTN_DUP | BTN_DDOWN);
    input->cur.button &= ~(BTN_DUP | BTN_DDOWN);
}

void DrawSolidRectOpa(GraphicsContext* gfxCtx, Gfx** gfxp, s32 x, s32 y, s32 w, s32 h, u8 r, u8 g, u8 b, u8 a) {
    if (gfxCtx == nullptr || gfxp == nullptr || *gfxp == nullptr || w <= 0 || h <= 0) {
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

    gSPVertex((*gfxp)++, (uintptr_t)vtx, 4, 0);
    gSP2Triangles((*gfxp)++, 0, 1, 2, 0, 0, 2, 3, 0);
}

void RestoreOverlayTextState(GraphicsContext* gfxCtx, Gfx** gfxp) {
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

} // namespace

namespace RangedFuseMenu {

void Update(PlayState* play) {
    if (!play || !Fuse::IsEnabled()) {
        return;
    }

    UpdateCooldowns();

    const bool fuseHeld = IsFuseMenuHeld();
    RangedWeaponType weapon = RangedWeaponType::None;
    RangedFuseSlotId slot = RangedFuseSlotId::Arrows;
    const bool aiming = IsPlayerAimingRangedWeapon(play, &weapon, &slot);

    if (!sMenu.isOpen) {
        if (aiming && fuseHeld && sMenu.reopenCooldownTimer <= 0.0f) {
            OpenMenu(weapon, slot);
        }
        return;
    }

    if (!aiming) {
        CloseMenu();
        return;
    }

    if (!fuseHeld) {
        CommitSelection();
        CloseMenu();
        return;
    }

    BuildEntries(sMenu);

    Input* input = &play->state.input[0];
    HandleNavigation(input);
}

void Draw(PlayState* play) {
    if (!play || !sMenu.isOpen || sMenu.entries.empty()) {
        return;
    }

    GraphicsContext* gfxCtx = play->state.gfxCtx;
    Gfx* opa = gfxCtx->polyOpa.p;

    Gfx_SetupDL_39Opa(gfxCtx);

    const int totalEntries = static_cast<int>(sMenu.entries.size());
    const int availableHeight = std::max(0, SCREEN_HEIGHT - kMenuY - kPanelPadding);
    const int maxVisibleRows = std::max(1, availableHeight / kRowHeight);
    const int visible = std::min({ kVisibleRows, totalEntries, maxVisibleRows });
    const int panelX = kMenuX - kPanelPadding;
    const int panelY = kMenuY - kPanelPadding;
    const int panelH = (visible * kRowHeight) + (kPanelPadding * 2);
    const int panelW = kListWidth + (kPanelPadding * 2);

    DrawSolidRectOpa(gfxCtx, &opa, panelX, panelY, panelW, panelH, 10, 10, 10, 180);

    const int highlightIndex = sMenu.selectedIndex - sMenu.scrollOffset;
    if (highlightIndex >= 0 && highlightIndex < visible) {
        const int highlightY = kMenuY + (highlightIndex * kRowHeight);
        DrawSolidRectOpa(gfxCtx, &opa, kMenuX - 2, highlightY - 2, kListWidth + 4, kRowHeight, 20, 20, 20, 220);
    }

    RestoreOverlayTextState(gfxCtx, &opa);

    GfxPrint printer;
    GfxPrint_Init(&printer);
    GfxPrint_Open(&printer, opa);

    for (int row = 0; row < visible; ++row) {
        const int entryIndex = sMenu.scrollOffset + row;
        if (entryIndex >= totalEntries) {
            break;
        }

        const MaterialEntry& entry = sMenu.entries[entryIndex];
        const bool selected = entryIndex == sMenu.selectedIndex;

        if (selected) {
            GfxPrint_SetColor(&printer, 255, 255, 80, 255);
        } else {
            GfxPrint_SetColor(&printer, 220, 220, 220, 255);
        }

        const int textY = kMenuY + (row * kRowHeight);
        GfxPrint_SetPosPx(&printer, kMenuX, textY);

        if (entry.id == MaterialId::None) {
            GfxPrint_Printf(&printer, "NONE");
        } else {
            const char* name = entry.def ? entry.def->name : "Unknown";
            GfxPrint_Printf(&printer, "%s x%d", name, entry.quantity);
        }
    }

    opa = GfxPrint_Close(&printer);
    GfxPrint_Destroy(&printer);

    gfxCtx->polyOpa.p = opa;
}

} // namespace RangedFuseMenu
