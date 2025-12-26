#include "FuseMenuWindow.h"

#include "soh/Enhancements/Fuse/Fuse.h"

#include <imgui.h>
#include <algorithm>
#include <string>

// ----------------------------------------------------------------------------
// UI enums
// ----------------------------------------------------------------------------
enum class FuseItem : int {
    KokiriSword = 0,
    MasterSword,
    BiggoronSword,

    MegatonHammer,
    Boomerang,
    FairyBow,
    Hookshot,

    Arrows,

    DekuShield,
    HylianShield,
    MirrorShield,

    COUNT
};

static const char* ItemName(FuseItem i) {
    switch (i) {
        case FuseItem::KokiriSword:
            return "Kokiri Sword";
        case FuseItem::MasterSword:
            return "Master Sword";
        case FuseItem::BiggoronSword:
            return "Biggoron Sword";

        case FuseItem::MegatonHammer:
            return "Megaton Hammer";
        case FuseItem::Boomerang:
            return "Boomerang";
        case FuseItem::FairyBow:
            return "Fairy Bow";
        case FuseItem::Hookshot:
            return "Hookshot / Longshot";

        case FuseItem::Arrows:
            return "Arrows";

        case FuseItem::DekuShield:
            return "Deku Shield";
        case FuseItem::HylianShield:
            return "Hylian Shield";
        case FuseItem::MirrorShield:
            return "Mirror Shield";
        default:
            return "Unknown";
    }
}

static bool IsSword(FuseItem i) {
    return i == FuseItem::KokiriSword || i == FuseItem::MasterSword || i == FuseItem::BiggoronSword;
}

static const char* MatName(MaterialId m) {
    const MaterialDef* def = Fuse::GetMaterialDef(m);
    return def ? def->name : "Unknown";
}

static const char* ResultName(Fuse::FuseResult r) {
    switch (r) {
        case Fuse::FuseResult::Ok:
            return "Ok";
        case Fuse::FuseResult::NotEnoughMaterial:
            return "NotEnoughMaterial";
        case Fuse::FuseResult::InvalidMaterial:
            return "InvalidMaterial";
        case Fuse::FuseResult::AlreadyFused:
            return "AlreadyFused";
        case Fuse::FuseResult::NotAllowed:
            return "NotAllowed";
        default:
            return "Unknown";
    }
}

// In v0, only swords are implemented in backend. Everything else is display-only for now.
static bool ItemSupportedNow(FuseItem item) {
    return IsSword(item);
}

// Return currently selected material for an item, based on current backend state (v0).
static MaterialId GetCurrentMatForItem(FuseItem item) {
    if (IsSword(item)) {
        return Fuse::IsSwordFused() ? Fuse::GetSwordMaterial() : MaterialId::None;
    }
    return MaterialId::None;
}

// Apply selection for an item (v0 implementation).
static void ApplyMatForItem(FuseItem item, MaterialId mat) {
    if (!ItemSupportedNow(item)) {
        Fuse::SetLastEvent("That item isn't implemented yet");
        return;
    }

    // v0 swords share a single fuse state
    Fuse::FuseResult result = Fuse::FuseResult::InvalidMaterial;

    if (mat == MaterialId::None) {
        result = Fuse::TryUnfuseSword();
    } else {
        result = Fuse::TryFuseSword(mat);
    }

    static std::string s;
    s = std::string("Fuse result for ") + ItemName(item) + ": " + ResultName(result);
    Fuse::SetLastEvent(s.c_str());
}

void FuseMenuWindow::InitElement() {
    // Nothing needed yet
}

void FuseMenuWindow::DrawElement() {
    ImGui::SetNextWindowSize(ImVec2(720, 520), ImGuiCond_FirstUseEver);

    bool windowOpen = true;
    if (!ImGui::Begin("Fuse Debug Menu", &windowOpen, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    if (!windowOpen) {
        this->Hide();
        ImGui::End();
        return;
    }

    // ------------------------------------------------------------------------
    // Header / status
    // ------------------------------------------------------------------------
    ImGui::SeparatorText("Status");
    ImGui::Text("Fuse: %s", Fuse::IsEnabled() ? "Enabled" : "Disabled");
    ImGui::Text("Last: %s", Fuse::GetLastEvent());

    // v0 durability display (sword only)
    const bool swordFused = Fuse::IsSwordFused();
    const MaterialId swordMat = Fuse::GetSwordMaterial();
    ImGui::Text("Sword Fuse Material: %s", MatName(swordFused ? swordMat : MaterialId::None));
    if (swordFused) {
        ImGui::Text("Sword Fuse Durability: %d / %d", Fuse::GetSwordFuseDurability(),
                    Fuse::GetSwordFuseMaxDurability());
        if (ImGui::SmallButton("Damage Sword Fuse (-1)")) {
            Fuse::DamageSwordFuseDurability(nullptr, 1, "debug");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear Sword Fuse")) {
            Fuse::TryUnfuseSword();
            Fuse::SetLastEvent("Sword fuse cleared (debug)");
        }
    } else {
        ImGui::TextUnformatted("Sword Fuse Durability: -");
    }

    ImGui::Spacing();
    ImGui::Separator();

    // ------------------------------------------------------------------------
    // Fusable items list with dropdowns
    // ------------------------------------------------------------------------
    ImGui::SeparatorText("Fuse-capable Items");

    // Table layout looks much cleaner than ad-hoc SameLine calls.
    if (ImGui::BeginTable("FuseItemsTable", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Fused Material", ImGuiTableColumnFlags_WidthFixed, 220.0f);
        ImGui::TableHeadersRow();

        const bool rockOwned = Fuse::HasMaterial(MaterialId::Rock);
        const bool iceOwned = Fuse::HasMaterial(MaterialId::Ice);

        for (int i = 0; i < (int)FuseItem::COUNT; i++) {
            FuseItem item = (FuseItem)i;

            ImGui::TableNextRow();

            // Item name
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(ItemName(item));

            // Dropdown
            ImGui::TableSetColumnIndex(1);

            const bool supported = ItemSupportedNow(item);

            if (!supported) {
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.35f);
            }

            // Current selection derived from backend (v0: swords share one)
            MaterialId current = GetCurrentMatForItem(item);

            // Unique ID per row so combos don't collide
            std::string comboId = std::string("##mat_") + std::to_string(i);

            // We’ll build combo options: None always selectable.
            const char* preview = MatName(current);

            bool changed = false;
            MaterialId newSelection = current;

            if (ImGui::BeginCombo(comboId.c_str(), preview)) {
                // None option
                {
                    bool isSelected = (current == MaterialId::None);
                    if (ImGui::Selectable(MatName(MaterialId::None), isSelected)) {
                        newSelection = MaterialId::None;
                        changed = true;
                    }
                    if (isSelected)
                        ImGui::SetItemDefaultFocus();
                }

                // Rock option (only selectable if owned)
                {
                    bool isSelected = (current == MaterialId::Rock);

                    if (!rockOwned) {
                        ImGui::BeginDisabled(true);
                    }
                    if (ImGui::Selectable(MatName(MaterialId::Rock), isSelected)) {
                        newSelection = MaterialId::Rock;
                        changed = true;
                    }
                    if (!rockOwned) {
                        ImGui::EndDisabled();
                    }
                }

                // Ice option (only selectable if owned)
                {
                    bool isSelected = (current == MaterialId::Ice);

                    if (!iceOwned) {
                        ImGui::BeginDisabled(true);
                    }
                    if (ImGui::Selectable(MatName(MaterialId::Ice), isSelected)) {
                        newSelection = MaterialId::Ice;
                        changed = true;
                    }
                    if (!iceOwned) {
                        ImGui::EndDisabled();
                    }
                }

                ImGui::EndCombo();
            }

            if (!supported) {
                ImGui::PopStyleVar();
            }

            // Apply only if the item is supported (v0: swords)
            if (changed) {
                if (!supported) {
                    Fuse::SetLastEvent("That item isn't implemented yet");
                } else {
                    ApplyMatForItem(item, newSelection);
                }
            }
        }

        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Separator();

    // ------------------------------------------------------------------------
    // Materials inventory (v0: Rock only)
    // ------------------------------------------------------------------------
    ImGui::SeparatorText("Materials");

    // v0 is binary; later you'll switch this to a count array.
    const int rockQty = Fuse::GetMaterialCount(MaterialId::Rock);
    const int iceQty = Fuse::GetMaterialCount(MaterialId::Ice);

    if (ImGui::BeginTable("MaterialsTable", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Material", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Qty", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableHeadersRow();

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("ROCK");
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%d", rockQty);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Ice");
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%d", iceQty);

        ImGui::EndTable();
    }

    static int rockDelta = 1;
    ImGui::InputInt("Rock delta", &rockDelta);
    if (ImGui::Button("Add Rock")) {
        Fuse::AddMaterial(MaterialId::Rock, std::max(rockDelta, 0));
    }

    ImGui::SameLine();
    static int iceDelta = 1;
    ImGui::InputInt("Ice delta", &iceDelta);
    if (ImGui::Button("Add Ice")) {
        Fuse::AddMaterial(MaterialId::Ice, std::max(iceDelta, 0));
    }

    ImGui::End();
}
