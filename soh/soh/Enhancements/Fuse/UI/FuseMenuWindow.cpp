#include "FuseMenuWindow.h"

#include "soh/Enhancements/Fuse/Fuse.h"

#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>

// ----------------------------------------------------------------------------
// UI enums
// ----------------------------------------------------------------------------
enum class FuseItem : int {
    KokiriSword = 0,
    MasterSword,
    BiggoronSword,

    MegatonHammer,
    Boomerang,
    SlingshotAmmo,
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
        case FuseItem::SlingshotAmmo:
            return "Slingshot Bullets";
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

static bool IsShield(FuseItem i) {
    return i == FuseItem::DekuShield || i == FuseItem::HylianShield || i == FuseItem::MirrorShield;
}

static ShieldSlotKey ShieldKeyForItem(FuseItem item) {
    switch (item) {
        case FuseItem::HylianShield:
            return ShieldSlotKey::Hylian;
        case FuseItem::MirrorShield:
            return ShieldSlotKey::Mirror;
        case FuseItem::DekuShield:
        default:
            return ShieldSlotKey::Deku;
    }
}

static const char* ShieldDebugName(FuseItem item) {
    switch (item) {
        case FuseItem::HylianShield:
            return "shield_hylian";
        case FuseItem::MirrorShield:
            return "shield_mirror";
        case FuseItem::DekuShield:
        default:
            return "shield_deku";
    }
}

static size_t ShieldSlotIndex(ShieldSlotKey key) {
    return FusePersistence::kShieldSlotOffset + static_cast<size_t>(key);
}

static FuseSlot GetShieldSlotForItem(FuseItem item) {
    const ShieldSlotKey key = ShieldKeyForItem(item);
    const std::array<SwordFuseSlot, FusePersistence::kSwordSlotCount> slots = Fuse::GetSwordSlots();
    return slots[ShieldSlotIndex(key)];
}

static void ApplyShieldSlot(ShieldSlotKey key, const FuseSlot& slot) {
    std::array<SwordFuseSlot, FusePersistence::kSwordSlotCount> slots = Fuse::GetSwordSlots();
    slots[ShieldSlotIndex(key)] = slot;
    Fuse::ApplyLoadedSwordSlots(slots);
}

static bool IsShieldSlotFused(const FuseSlot& slot) {
    return slot.materialId != MaterialId::None && slot.durabilityCur > 0 && slot.durabilityMax > 0;
}

static void LogShieldSlotView(FuseItem item, const FuseSlot& slot) {
    Fuse::Log("[FuseDBG] DebugSlotView: item=%s material=%d dura=%d/%d\n", ShieldDebugName(item),
              static_cast<int>(slot.materialId), slot.durabilityCur, slot.durabilityMax);
}

static void LogShieldAction(const char* action, FuseItem item, const FuseSlot& slot) {
    Fuse::Log("[FuseDBG] DebugAction: action=%s item=%s material=%d dura=%d/%d\n", action, ShieldDebugName(item),
              static_cast<int>(slot.materialId), slot.durabilityCur, slot.durabilityMax);
}

static const char* MatName(MaterialId m) {
    const MaterialDef* def = Fuse::GetMaterialDef(m);
    return def ? def->name : "Unknown";
}

static int ClampStepperValue(int v) {
    return std::clamp(v, 0, 999);
}

static void AdjustAttackDelta(MaterialId id, int delta) {
    const int cur = Fuse::GetMaterialAttackBonusDelta(id);
    const int next = ClampStepperValue(cur + delta);
    Fuse::SetMaterialAttackBonusDelta(id, next);
    Fuse::SaveDebugOverrides();
}

static void AdjustDurabilityOverride(MaterialId id, int delta) {
    int cur = Fuse::GetMaterialDurabilityOverride(id);
    if (cur < 0) {
        cur = static_cast<int>(Fuse::GetMaterialBaseDurability(id));
    }

    cur = ClampStepperValue(cur + delta);
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
    return IsSword(item) || item == FuseItem::Boomerang || item == FuseItem::MegatonHammer || IsShield(item);
}

// Return currently selected material for an item, based on current backend state (v0).
static MaterialId GetCurrentMatForItem(FuseItem item) {
    if (IsSword(item)) {
        return Fuse::IsSwordFused() ? Fuse::GetSwordMaterial() : MaterialId::None;
    }
    if (item == FuseItem::Boomerang) {
        return Fuse::IsBoomerangFused() ? Fuse::GetBoomerangMaterial() : MaterialId::None;
    }
    if (item == FuseItem::MegatonHammer) {
        return Fuse::IsHammerFused() ? Fuse::GetHammerMaterial() : MaterialId::None;
    }
    if (IsShield(item)) {
        const FuseSlot slot = GetShieldSlotForItem(item);
        return IsShieldSlotFused(slot) ? slot.materialId : MaterialId::None;
    }
    return MaterialId::None;
}

static Fuse::FuseResult TryFuseShieldSlot(ShieldSlotKey key, MaterialId id) {
    std::array<SwordFuseSlot, FusePersistence::kSwordSlotCount> slots = Fuse::GetSwordSlots();
    FuseSlot& slot = slots[ShieldSlotIndex(key)];

    if (IsShieldSlotFused(slot)) {
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
    slot.durabilityMax = maxDurability;
    slot.durabilityCur = maxDurability;
    Fuse::ApplyLoadedSwordSlots(slots);
    return Fuse::FuseResult::Ok;
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
        if (IsSword(item)) {
            result = Fuse::TryUnfuseSword();
        } else if (item == FuseItem::Boomerang) {
            result = Fuse::TryUnfuseBoomerang();
        } else if (item == FuseItem::MegatonHammer) {
            result = Fuse::TryUnfuseHammer();
        } else if (IsShield(item)) {
            FuseSlot slot{};
            slot.ResetToUnfused();
            ApplyShieldSlot(ShieldKeyForItem(item), slot);
            result = Fuse::FuseResult::Ok;
            LogShieldAction("clear", item, slot);
        }
    } else {
        if (IsSword(item)) {
            result = Fuse::TryFuseSword(mat);
        } else if (item == FuseItem::Boomerang) {
            result = Fuse::TryFuseBoomerang(mat);
        } else if (item == FuseItem::MegatonHammer) {
            result = Fuse::TryFuseHammer(mat);
        } else if (IsShield(item)) {
            const ShieldSlotKey key = ShieldKeyForItem(item);
            result = TryFuseShieldSlot(key, mat);
            const FuseSlot slot = GetShieldSlotForItem(item);
            LogShieldAction("fuse", item, slot);
        }
    }

    static std::string s;
    s = std::string("Fuse result for ") + ItemName(item) + ": " + ResultName(result);
    Fuse::SetLastEvent(s.c_str());
}

static bool DrawIntStepper(const char* id, int* value, const char* label = nullptr) {
    bool changed = false;
    int next = *value;

    ImGui::PushID(id);
    if (label) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine();
    }

    if (ImGui::SmallButton("-")) {
        next -= 1;
        changed = true;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 3.5f);
    if (ImGui::InputInt("##value", &next, 0, 0, ImGuiInputTextFlags_CharsDecimal)) {
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("+")) {
        next += 1;
        changed = true;
    }

    if (changed) {
        *value = ClampStepperValue(next);
    }
    ImGui::PopID();
    return changed;
}

static bool IsItemFused(FuseItem item) {
    if (IsSword(item)) {
        return Fuse::IsSwordFused();
    }
    if (item == FuseItem::Boomerang) {
        return Fuse::IsBoomerangFused();
    }
    if (item == FuseItem::MegatonHammer) {
        return Fuse::IsHammerFused();
    }
    if (IsShield(item)) {
        return IsShieldSlotFused(GetShieldSlotForItem(item));
    }
    return false;
}

static void DamageItemFuse(FuseItem item) {
    if (IsSword(item)) {
        Fuse::DamageSwordFuseDurability(nullptr, 1, "debug");
    } else if (item == FuseItem::Boomerang) {
        Fuse::DamageBoomerangFuseDurability(nullptr, 1, "debug");
    } else if (item == FuseItem::MegatonHammer) {
        Fuse::DamageHammerFuseDurability(nullptr, 1, "debug");
    } else if (IsShield(item)) {
        FuseSlot slot = GetShieldSlotForItem(item);
        slot.durabilityCur = std::max(0, slot.durabilityCur - 1);
        if (slot.durabilityCur <= 0) {
            slot.ResetToUnfused();
        }
        ApplyShieldSlot(ShieldKeyForItem(item), slot);
        LogShieldAction("damage", item, slot);
    } else {
        Fuse::SetLastEvent("That item isn't implemented yet");
    }
}

static void ClearItemFuse(FuseItem item) {
    if (IsSword(item)) {
        Fuse::TryUnfuseSword();
    } else if (item == FuseItem::Boomerang) {
        Fuse::TryUnfuseBoomerang();
    } else if (item == FuseItem::MegatonHammer) {
        Fuse::TryUnfuseHammer();
    } else if (IsShield(item)) {
        FuseSlot slot = GetShieldSlotForItem(item);
        slot.ResetToUnfused();
        ApplyShieldSlot(ShieldKeyForItem(item), slot);
        LogShieldAction("clear", item, slot);
    } else {
        Fuse::SetLastEvent("That item isn't implemented yet");
        return;
    }

    static std::string s;
    s = std::string(ItemName(item)) + " fuse cleared (debug)";
    Fuse::SetLastEvent(s.c_str());
}

static std::pair<int, int> GetItemDurability(FuseItem item) {
    if (IsSword(item)) {
        return { Fuse::GetSwordFuseDurability(), Fuse::GetSwordFuseMaxDurability() };
    }
    if (item == FuseItem::Boomerang) {
        return { Fuse::GetBoomerangFuseDurability(), Fuse::GetBoomerangFuseMaxDurability() };
    }
    if (item == FuseItem::MegatonHammer) {
        return { Fuse::GetHammerFuseDurability(), Fuse::GetHammerFuseMaxDurability() };
    }
    if (IsShield(item)) {
        const FuseSlot slot = GetShieldSlotForItem(item);
        return { slot.durabilityCur, slot.durabilityMax };
    }
    return { 0, 0 };
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

    size_t materialDefCount = 0;
    const MaterialDef* materialDefs = Fuse::GetMaterialDefs(&materialDefCount);

    if (ImGui::BeginTabBar("FuseDebugTabs")) {
        if (ImGui::BeginTabItem("Fuse")) {
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
            if (ImGui::BeginTable("FuseItemsTable", 3, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Fused Material", ImGuiTableColumnFlags_WidthFixed, 220.0f);
                ImGui::TableSetupColumn("Controls", ImGuiTableColumnFlags_WidthFixed, 220.0f);
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
                    if (IsShield(item)) {
                        LogShieldSlotView(item, GetShieldSlotForItem(item));
                    }

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

                    ImGui::TableSetColumnIndex(2);
                    const auto durability = GetItemDurability(item);
                    if (IsItemFused(item)) {
                        ImGui::Text("Durability: %d / %d", durability.first, durability.second);
                    } else {
                        ImGui::TextUnformatted("Durability: -");
                    }
                    const bool isFused = IsItemFused(item);
                    ImGui::BeginDisabled(!supported || !isFused);
                    std::string damageLabel = std::string("Damage ") + ItemName(item) + " Fuse (-1)";
                    if (ImGui::SmallButton(damageLabel.c_str())) {
                        DamageItemFuse(item);
                    }
                    ImGui::SameLine();
                    std::string clearLabel = std::string("Clear ") + ItemName(item) + " Fuse";
                    if (ImGui::SmallButton(clearLabel.c_str())) {
                        ClearItemFuse(item);
                    }
                    ImGui::EndDisabled();
                }

                ImGui::EndTable();
            }

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Material Tuning")) {
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
                                  ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_SizingStretchProp)) {
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
                    int attackDeltaDisplay = ClampStepperValue(attackDelta);
                    if (DrawIntStepper(("AtkDelta##" + std::to_string(materialIndex)).c_str(), &attackDeltaDisplay)) {
                        if (attackDeltaDisplay != attackDelta) {
                            Fuse::SetMaterialAttackBonusDelta(def.id, attackDeltaDisplay);
                            Fuse::SaveDebugOverrides();
                        }
                    }

                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%d", attackEffective);

                    ImGui::TableSetColumnIndex(3);
                    int durabilityDisplay = durabilityOverride;
                    if (durabilityDisplay < 0) {
                        durabilityDisplay = static_cast<int>(Fuse::GetMaterialBaseDurability(def.id));
                    }
                    durabilityDisplay = ClampStepperValue(durabilityDisplay);
                    if (DrawIntStepper(("DurOverride##" + std::to_string(materialIndex)).c_str(), &durabilityDisplay)) {
                        Fuse::SetMaterialBaseDurabilityOverride(def.id, durabilityDisplay);
                        Fuse::SaveDebugOverrides();
                    }

                    ImGui::TableSetColumnIndex(4);
                    ImGui::Text("%d", durabilityEffective);

                    ImGui::TableSetColumnIndex(5);
                    if (ImGui::SmallButton(("Reset##" + std::to_string(materialIndex)).c_str())) {
                        ResetMaterialOverrideUI(def.id);
                    }
                }

                ImGui::EndTable();
            }

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Inventory")) {
            ImGui::SeparatorText("Materials");
            if (ImGui::BeginTable("FuseInventoryTable", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Material", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Qty", ImGuiTableColumnFlags_WidthFixed, 140.0f);
                ImGui::TableHeadersRow();

                const MaterialId inventoryMaterials[] = { MaterialId::Rock, MaterialId::FrozenShard };
                const size_t inventoryMaterialCount = sizeof(inventoryMaterials) / sizeof(inventoryMaterials[0]);
                for (size_t index = 0; index < inventoryMaterialCount; index++) {
                    const MaterialId id = inventoryMaterials[index];
                    const int qty = Fuse::GetMaterialCount(id);
                    int qtyDisplay = ClampStepperValue(qty);

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(MatName(id));
                    ImGui::TableSetColumnIndex(1);
                    if (DrawIntStepper(("InvQty##" + std::to_string(index)).c_str(), &qtyDisplay)) {
                        if (qtyDisplay != qty) {
                            Fuse::SetMaterialCount(id, qtyDisplay);
                        }
                    }
                }

                ImGui::EndTable();
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}
