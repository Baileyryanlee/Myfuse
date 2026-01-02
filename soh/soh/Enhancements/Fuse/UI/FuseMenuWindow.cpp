#include "FuseMenuWindow.h"

#include "soh/Enhancements/Fuse/Fuse.h"

#include <imgui.h>
#include <algorithm>
#include <cstdio>
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

static int ClampOverride(int v) {
    return std::clamp(v, -1, 65535);
}

static void AdjustAttackDelta(MaterialId id, int delta) {
    const int cur = Fuse::GetMaterialAttackBonusDelta(id);
    Fuse::SetMaterialAttackBonusDelta(id, cur + delta);
    Fuse::SaveDebugOverrides();
}

static void AdjustDurabilityOverride(MaterialId id, int delta) {
    int cur = Fuse::GetMaterialDurabilityOverride(id);
    if (cur < 0) {
        cur = static_cast<int>(Fuse::GetMaterialBaseDurability(id));
    }

    cur = ClampOverride(cur + delta);
    Fuse::SetMaterialBaseDurabilityOverride(id, cur);
    Fuse::SaveDebugOverrides();
}

static void ResetMaterialOverrideUI(MaterialId id) {
    Fuse::ResetMaterialOverride(id);
    Fuse::SaveDebugOverrides();
}

static std::string MatNameWithCount(MaterialId m) {
    const char* baseName = MatName(m);
    const int count = Fuse::GetMaterialCount(m);

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s (x%d)", baseName ? baseName : "Unknown", count);
    return std::string(buf);
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
    // Material tuning
    // ------------------------------------------------------------------------
    ImGui::SeparatorText("Material Tuning");

    bool useDebugOverrides = Fuse::GetUseDebugOverrides();
    if (ImGui::Checkbox("Use Debug Overrides", &useDebugOverrides)) {
        Fuse::SetUseDebugOverrides(useDebugOverrides);
        Fuse::SaveDebugOverrides();
    }

    ImGui::SameLine();
    if (ImGui::Button("Reset All Overrides")) {
        Fuse::ResetAllMaterialOverrides();
        Fuse::SaveDebugOverrides();
    }

    size_t materialDefCount = 0;
    const MaterialDef* materialDefs = Fuse::GetMaterialDefs(&materialDefCount);

    ImGui::SeparatorText("Apply to All");
    if (ImGui::Button("Attack Delta -5")) {
        for (size_t i = 0; i < materialDefCount; i++) {
            if (materialDefs[i].id == MaterialId::None) {
                continue;
            }
            AdjustAttackDelta(materialDefs[i].id, -5);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Attack Delta -1")) {
        for (size_t i = 0; i < materialDefCount; i++) {
            if (materialDefs[i].id == MaterialId::None) {
                continue;
            }
            AdjustAttackDelta(materialDefs[i].id, -1);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Attack Delta +1")) {
        for (size_t i = 0; i < materialDefCount; i++) {
            if (materialDefs[i].id == MaterialId::None) {
                continue;
            }
            AdjustAttackDelta(materialDefs[i].id, 1);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Attack Delta +5")) {
        for (size_t i = 0; i < materialDefCount; i++) {
            if (materialDefs[i].id == MaterialId::None) {
                continue;
            }
            AdjustAttackDelta(materialDefs[i].id, 5);
        }
    }

    if (ImGui::Button("Durability Override -5")) {
        for (size_t i = 0; i < materialDefCount; i++) {
            if (materialDefs[i].id == MaterialId::None) {
                continue;
            }
            AdjustDurabilityOverride(materialDefs[i].id, -5);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Durability Override -1")) {
        for (size_t i = 0; i < materialDefCount; i++) {
            if (materialDefs[i].id == MaterialId::None) {
                continue;
            }
            AdjustDurabilityOverride(materialDefs[i].id, -1);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Durability Override +1")) {
        for (size_t i = 0; i < materialDefCount; i++) {
            if (materialDefs[i].id == MaterialId::None) {
                continue;
            }
            AdjustDurabilityOverride(materialDefs[i].id, 1);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Durability Override +5")) {
        for (size_t i = 0; i < materialDefCount; i++) {
            if (materialDefs[i].id == MaterialId::None) {
                continue;
            }
            AdjustDurabilityOverride(materialDefs[i].id, 5);
        }
    }

    if (ImGui::BeginTable("MaterialOverridesTable", 6,
                          ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Material");
        ImGui::TableSetupColumn("Attack Delta");
        ImGui::TableSetupColumn("Eff. Attack");
        ImGui::TableSetupColumn("Base Override");
        ImGui::TableSetupColumn("Eff. Base Dur");
        ImGui::TableSetupColumn("Controls", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (size_t materialIndex = 0; materialIndex < materialDefCount; materialIndex++) {
            const MaterialDef& def = materialDefs[materialIndex];
            if (def.id == MaterialId::None) {
                continue;
            }

            const int attackDelta = Fuse::GetMaterialAttackBonusDelta(def.id);
            const int attackEffective = Fuse::GetMaterialAttackBonus(def.id);
            const int durabilityOverride = Fuse::GetMaterialDurabilityOverride(def.id);
            const int durabilityEffective = Fuse::GetMaterialEffectiveBaseDurability(def.id);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s (id=%d)", def.name, static_cast<int>(def.id));

            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%d", attackDelta);

            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%d", attackEffective);

            ImGui::TableSetColumnIndex(3);
            if (durabilityOverride < 0) {
                ImGui::TextUnformatted("Default");
            } else {
                ImGui::Text("%d", durabilityOverride);
            }

            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%d", durabilityEffective);

            ImGui::TableSetColumnIndex(5);
            if (ImGui::SmallButton(("Atk -5##" + std::to_string(materialIndex)).c_str())) {
                AdjustAttackDelta(def.id, -5);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(("Atk -1##" + std::to_string(materialIndex)).c_str())) {
                AdjustAttackDelta(def.id, -1);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(("Atk +1##" + std::to_string(materialIndex)).c_str())) {
                AdjustAttackDelta(def.id, 1);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(("Atk +5##" + std::to_string(materialIndex)).c_str())) {
                AdjustAttackDelta(def.id, 5);
            }

            ImGui::SameLine();
            if (ImGui::SmallButton(("Dur -5##" + std::to_string(materialIndex)).c_str())) {
                AdjustDurabilityOverride(def.id, -5);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(("Dur -1##" + std::to_string(materialIndex)).c_str())) {
                AdjustDurabilityOverride(def.id, -1);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(("Dur +1##" + std::to_string(materialIndex)).c_str())) {
                AdjustDurabilityOverride(def.id, 1);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(("Dur +5##" + std::to_string(materialIndex)).c_str())) {
                AdjustDurabilityOverride(def.id, 5);
            }

            ImGui::SameLine();
            if (ImGui::SmallButton(("Reset##" + std::to_string(materialIndex)).c_str())) {
                ResetMaterialOverrideUI(def.id);
            }
        }

        ImGui::EndTable();
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
            std::string previewLabel =
                (current == MaterialId::None) ? std::string(MatName(current)) : MatNameWithCount(current);
            const char* preview = previewLabel.c_str();

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

                for (size_t materialIndex = 0; materialIndex < materialDefCount; materialIndex++) {
                    const MaterialDef& def = materialDefs[materialIndex];
                    if (def.id == MaterialId::None) {
                        continue;
                    }

                    const int qty = Fuse::GetMaterialCount(def.id);
                    const bool isSelected = (current == def.id);
                    const std::string label = MatNameWithCount(def.id);

                    if (qty <= 0) {
                        ImGui::BeginDisabled(true);
                    }
                    if (ImGui::Selectable(label.c_str(), isSelected)) {
                        newSelection = def.id;
                        changed = true;
                    }
                    if (qty <= 0) {
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
    // Materials inventory (v0: Rock plus Deku Nut adapter)
    // ------------------------------------------------------------------------
    ImGui::SeparatorText("Materials");

    if (ImGui::BeginTable("MaterialsTable", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Material", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Qty", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableHeadersRow();

        for (size_t materialIndex = 0; materialIndex < materialDefCount; materialIndex++) {
            const MaterialDef& def = materialDefs[materialIndex];
            if (def.id == MaterialId::None) {
                continue;
            }

            const int qty = Fuse::GetMaterialCount(def.id);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const std::string label = MatNameWithCount(def.id);
            ImGui::TextUnformatted(label.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%d", qty);
        }

        ImGui::EndTable();
    }

    static int rockDelta = 1;
    ImGui::InputInt("Rock delta", &rockDelta);
    if (ImGui::Button("Add Rock")) {
        Fuse::AddMaterial(MaterialId::Rock, std::max(rockDelta, 0));
    }

    ImGui::End();
}
