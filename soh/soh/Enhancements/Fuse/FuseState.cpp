#include "FuseState.h"

#include "Fuse.h"
#include "soh/SaveManager.h"

#include <cstdint>
#include <algorithm>
#include <limits>
#include <vector>

extern "C" {
#include "z64.h"
#include "variables.h"
}

namespace {

void NormalizeState(FuseSwordSaveState& state) {
    state.durabilityMax = std::max(state.durabilityMax, 0);
    state.durabilityCur = std::max(state.durabilityCur, 0);
    state.legacyDurability = std::max(state.legacyDurability, 0);

    if (state.durabilityMax > 0) {
        state.durabilityCur = std::clamp(state.durabilityCur, 0, state.durabilityMax);
    }

    if (!state.isFused) {
        state.materialId = MaterialId::None;
        state.durabilityCur = 0;
        state.durabilityMax = 0;
        state.hasExplicitCur = false;
        state.legacyDurability = 0;
    }
}

void NormalizeSlot(SwordFuseSlot& slot) {
    slot.durabilityCur = std::max(slot.durabilityCur, 0);
    slot.durabilityMax = std::max(slot.durabilityMax, 0);

    if (slot.materialId == MaterialId::None || slot.durabilityMax <= 0 || slot.durabilityCur <= 0) {
        slot.ResetToUnfused();
        return;
    }

    slot.durabilityCur = std::clamp(slot.durabilityCur, 0, slot.durabilityMax);
}

bool IsNoneMaterialId(int matId) {
    constexpr int kMaterialIdMin = static_cast<int>(MaterialId::None);
    constexpr int kMaterialIdMax = static_cast<int>(MaterialId::DekuNut);

    if (matId == kMaterialIdMin || matId == -1) {
        return true;
    }

    return matId < kMaterialIdMin || matId > kMaterialIdMax;
}

void LogSlotPersistenceEvent(const char* prefix, SwordSlotKey slotKey, const SwordFuseSlot& slot) {
    const int material = static_cast<int>(slot.materialId);
    const int durabilityCur = slot.durabilityCur;
    const int durabilityMax = slot.durabilityMax;
    Fuse::Log("[FuseDBG] %s: slot=%s material=%d dura=%d/%d\n", prefix, SwordSlotName(slotKey), material, durabilityCur,
              durabilityMax);
}

SwordFuseSlot BuildSlotFromLegacy(int materialId, int curDurability, bool hasCurDurability) {
    SwordFuseSlot slot{};

    if (IsNoneMaterialId(materialId)) {
        return slot;
    }

    const MaterialId material = static_cast<MaterialId>(std::max(0, materialId));
    const MaterialDef* def = Fuse::GetMaterialDef(material);
    if (!def) {
        return slot;
    }

    int maxDurability = Fuse::GetMaterialEffectiveBaseDurability(material);
    if (maxDurability <= 0) {
        return slot;
    }

    int targetCur = 0;
    if (hasCurDurability) {
        targetCur = curDurability;
    } else {
        targetCur = maxDurability;
    }

    slot.materialId = material;
    slot.durabilityMax = maxDurability;
    slot.durabilityCur = std::clamp(targetCur, 0, maxDurability);

    NormalizeSlot(slot);
    return slot;
}

} // namespace

bool IsSwordEquipValue(int32_t equipValue) {
    switch (equipValue) {
        case EQUIP_VALUE_SWORD_KOKIRI:
        case EQUIP_VALUE_SWORD_MASTER:
        case EQUIP_VALUE_SWORD_BIGGORON:
            return true;
        default:
            return false;
    }
}

SwordSlotKey SwordSlotKeyFromEquipValue(int32_t equipValue) {
    switch (equipValue) {
        case EQUIP_VALUE_SWORD_MASTER:
            return SwordSlotKey::Master;
        case EQUIP_VALUE_SWORD_BIGGORON:
            return SwordSlotKey::Biggoron;
        case EQUIP_VALUE_SWORD_KOKIRI:
        default:
            return SwordSlotKey::Kokiri;
    }
}

const char* SwordSlotName(SwordSlotKey key) {
    switch (key) {
        case SwordSlotKey::Kokiri:
            return "Kokiri";
        case SwordSlotKey::Master:
            return "Master";
        case SwordSlotKey::Biggoron:
            return "Biggoron";
        default:
            return "Unknown";
    }
}

void SwordFuseSlot::Clear() {
    ResetToUnfused();
}

void SwordFuseSlot::ResetToUnfused() {
    materialId = MaterialId::None;
    durabilityCur = 0;
    durabilityMax = 0;
}

SwordFuseSlot& FuseSaveData::GetSwordSlot(SwordSlotKey key) {
    return swordSlots[static_cast<size_t>(key)];
}

const SwordFuseSlot& FuseSaveData::GetSwordSlot(SwordSlotKey key) const {
    return swordSlots[static_cast<size_t>(key)];
}

SwordFuseSlot& FuseSaveData::GetActiveSwordSlot([[maybe_unused]] const PlayState* play) {
    const int32_t equipValue =
        (static_cast<int32_t>(gSaveContext.equips.equipment & gEquipMasks[EQUIP_TYPE_SWORD]) >>
         gEquipShifts[EQUIP_TYPE_SWORD]);
    const SwordSlotKey key = IsSwordEquipValue(equipValue) ? SwordSlotKeyFromEquipValue(equipValue)
                                                           : SwordSlotKey::Kokiri;
    return this->swordSlots[static_cast<size_t>(key)];
}

FuseSlot& FuseRuntimeState::GetBoomerangSlot() {
    return boomerangSlot;
}

const FuseSlot& FuseRuntimeState::GetBoomerangSlot() const {
    return boomerangSlot;
}

FuseSlot& FuseRuntimeState::GetActiveBoomerangSlot([[maybe_unused]] const PlayState* play) {
    return boomerangSlot;
}

namespace FusePersistence {

FuseSwordSaveState ClearedSwordState() {
    FuseSwordSaveState state{};
    state.isFused = false;
    state.materialId = MaterialId::None;
    state.durabilityCur = 0;
    state.durabilityMax = 0;
    state.hasExplicitCur = false;
    state.legacyDurability = 0;
    return state;
}

FuseSwordSaveState BuildRuntimeSwordState() {
    FuseSwordSaveState state{};
    state.isFused = Fuse::IsSwordFused();

    if (state.isFused) {
        state.materialId = Fuse::GetSwordMaterial();
        state.durabilityCur = Fuse::GetSwordFuseDurability();
        state.durabilityMax = Fuse::GetSwordFuseMaxDurability();
        state.hasExplicitCur = true;
        state.legacyDurability = state.durabilityCur;
    }

    NormalizeState(state);
    return state;
}

FuseSwordSaveState ReadSwordStateFromContext() {
    FuseSwordSaveState state{};
    const s16 rawMat = gSaveContext.ship.fuseSwordMaterialId;
    state.isFused = !IsNoneMaterialId(rawMat);
    state.materialId = state.isFused ? static_cast<MaterialId>(rawMat) : MaterialId::None;
    state.durabilityMax = gSaveContext.ship.fuseSwordMaxDur;
    state.hasExplicitCur = gSaveContext.ship.fuseSwordCurDurabilityPresent;
    state.durabilityCur =
        state.hasExplicitCur ? gSaveContext.ship.fuseSwordCurrentDurability : gSaveContext.ship.fuseSwordCurDur;
    state.legacyDurability = gSaveContext.ship.fuseSwordCurDur;

    NormalizeState(state);
    return state;
}

void WriteSwordStateToContext(const FuseSwordSaveState& state) {
    FuseSwordSaveState normalizedState = state;
    NormalizeState(normalizedState);

    if (!normalizedState.isFused) {
        gSaveContext.ship.fuseSwordMaterialId = kSwordMaterialIdNone;
        gSaveContext.ship.fuseSwordCurDur = 0;
        gSaveContext.ship.fuseSwordMaxDur = 0;
        gSaveContext.ship.fuseSwordCurrentDurability = 0;
        gSaveContext.ship.fuseSwordCurDurabilityPresent = false;
        return;
    }

    gSaveContext.ship.fuseSwordMaterialId = static_cast<s16>(normalizedState.materialId);
    gSaveContext.ship.fuseSwordCurDur = static_cast<s16>(
        std::clamp(normalizedState.legacyDurability, 0, static_cast<int>(std::numeric_limits<int16_t>::max())));
    gSaveContext.ship.fuseSwordMaxDur = static_cast<s16>(
        std::clamp(normalizedState.durabilityMax, 0, static_cast<int>(std::numeric_limits<int16_t>::max())));
    gSaveContext.ship.fuseSwordCurrentDurability = static_cast<uint16_t>(
        std::clamp(normalizedState.durabilityCur, 0, static_cast<int>(std::numeric_limits<uint16_t>::max())));
    gSaveContext.ship.fuseSwordCurDurabilityPresent = normalizedState.hasExplicitCur;
}

void ApplySwordStateFromContext(const PlayState* play) {
    FuseSwordSaveState state = ReadSwordStateFromContext();

    if (!state.isFused) {
        Fuse_ClearSavedSwordFuse(play);
        return;
    }

    Fuse_ApplySavedSwordFuse(play, static_cast<s16>(state.materialId), static_cast<s16>(state.durabilityMax),
                             state.hasExplicitCur, static_cast<u16>(state.durabilityCur),
                             static_cast<s16>(state.legacyDurability));
}

FuseSwordSlotsSaveState LoadSwordSlotsFromManager(SaveManager& manager) {
    FuseSwordSlotsSaveState state{};
    int32_t version = 0;
    int legacyMaterialId = kSwordMaterialIdNone;
    constexpr int kLegacyDurabilityMissing = -999;
    int legacyCurDurability = kLegacyDurabilityMissing;

    manager.LoadStruct(kSwordSaveSectionName, [&]() {
        manager.LoadData(kSwordSaveVersionKey, version, 0);
        if (version >= static_cast<int32_t>(kSwordSlotsSaveVersion)) {
            manager.LoadArray(kSwordSlotsKey, kSwordSlotCount, [&](size_t i) {
                int32_t materialId = static_cast<int32_t>(MaterialId::None);
                int32_t durabilityCur = 0;
                int32_t durabilityMax = 0;

                manager.LoadStruct("", [&]() {
                    manager.LoadData(kSwordSlotMaterialKey, materialId, static_cast<int32_t>(MaterialId::None));
                    manager.LoadData(kSwordSlotDurabilityCurKey, durabilityCur, 0);
                    manager.LoadData(kSwordSlotDurabilityMaxKey, durabilityMax, 0);
                });

                SwordFuseSlot slot{};
                slot.materialId = static_cast<MaterialId>(materialId);
                slot.durabilityCur = durabilityCur;
                slot.durabilityMax = durabilityMax;
                NormalizeSlot(slot);
                state.swordSlots[i] = slot;
            });
            if (version >= static_cast<int32_t>(kSwordSaveVersion)) {
                manager.LoadStruct(kBoomerangSlotKey, [&]() {
                    int32_t materialId = static_cast<int32_t>(MaterialId::None);
                    int32_t durabilityCur = 0;
                    int32_t durabilityMax = 0;

                    manager.LoadData(kBoomerangSlotMaterialKey, materialId, static_cast<int32_t>(MaterialId::None));
                    manager.LoadData(kBoomerangSlotDurabilityCurKey, durabilityCur, 0);
                    manager.LoadData(kBoomerangSlotDurabilityMaxKey, durabilityMax, 0);

                    SwordFuseSlot slot{};
                    slot.materialId = static_cast<MaterialId>(materialId);
                    slot.durabilityCur = durabilityCur;
                    slot.durabilityMax = durabilityMax;
                    NormalizeSlot(slot);
                    state.boomerangSlot = slot;
                    state.boomerangSlotLoaded = true;
                });
            }
        } else {
            manager.LoadData(kSwordMaterialKey, legacyMaterialId, static_cast<int>(kSwordMaterialIdNone));
            manager.LoadData(kSwordDurabilityKey, legacyCurDurability, kLegacyDurabilityMissing);
        }
    });

    if (version >= static_cast<int32_t>(kSwordSlotsSaveVersion)) {
        state.version = static_cast<uint32_t>(version);
        for (size_t i = 0; i < kSwordSlotCount; ++i) {
            LogSlotPersistenceEvent("Load", static_cast<SwordSlotKey>(i), state.swordSlots[i]);
        }
        if (!state.boomerangSlotLoaded && version < static_cast<int32_t>(kSwordSaveVersion)) {
            const bool anySwordFused = std::any_of(state.swordSlots.begin(), state.swordSlots.end(),
                                                   [](const SwordFuseSlot& slot) {
                                                       return slot.materialId != MaterialId::None;
                                                   });
            if (!anySwordFused) {
                const FuseSwordSaveState legacyState = ReadSwordStateFromContext();
                if (legacyState.isFused) {
                    SwordFuseSlot slot{};
                    slot.materialId = legacyState.materialId;
                    slot.durabilityCur = legacyState.durabilityCur;
                    slot.durabilityMax = legacyState.durabilityMax;
                    NormalizeSlot(slot);
                    if (slot.materialId != MaterialId::None) {
                        state.boomerangSlot = slot;
                        state.boomerangSlotLoaded = true;
                    }
                }
            }
        }
        return state;
    }

    state.migratedFromLegacy = true;
    const bool hasCurDurability = legacyCurDurability != kLegacyDurabilityMissing;
    SwordFuseSlot migratedSlot = BuildSlotFromLegacy(legacyMaterialId, legacyCurDurability, hasCurDurability);

    if (migratedSlot.materialId != MaterialId::None) {
        const int32_t equipValue =
            (static_cast<int32_t>(gSaveContext.equips.equipment & gEquipMasks[EQUIP_TYPE_SWORD]) >>
             gEquipShifts[EQUIP_TYPE_SWORD]);
        const SwordSlotKey targetSlot =
            IsSwordEquipValue(equipValue) ? SwordSlotKeyFromEquipValue(equipValue) : SwordSlotKey::Kokiri;
        state.swordSlots[static_cast<size_t>(targetSlot)] = migratedSlot;
    }

    for (size_t i = 0; i < kSwordSlotCount; ++i) {
        LogSlotPersistenceEvent("LoadLegacy", static_cast<SwordSlotKey>(i), state.swordSlots[i]);
    }

    return state;
}

void SaveSwordSlotsToManager(SaveManager& manager, const std::array<SwordFuseSlot, kSwordSlotCount>& slots,
                             const FuseSlot& boomerangSlot) {
    manager.SaveStruct(kSwordSaveSectionName, [&]() {
        manager.SaveData(kSwordSaveVersionKey, static_cast<int32_t>(kSwordSaveVersion));
        manager.SaveArray(kSwordSlotsKey, kSwordSlotCount, [&](size_t i) {
            SwordFuseSlot slot = slots[i];
            NormalizeSlot(slot);

            const int32_t materialId = static_cast<int32_t>(slot.materialId);
            const int32_t durabilityCur = slot.durabilityCur;
            const int32_t durabilityMax = slot.durabilityMax;

            manager.SaveStruct("", [&]() {
                manager.SaveData(kSwordSlotMaterialKey, materialId);
                manager.SaveData(kSwordSlotDurabilityCurKey, durabilityCur);
                manager.SaveData(kSwordSlotDurabilityMaxKey, durabilityMax);
            });

            LogSlotPersistenceEvent("Save", static_cast<SwordSlotKey>(i), slot);
        });
        SwordFuseSlot slot = boomerangSlot;
        NormalizeSlot(slot);
        manager.SaveStruct(kBoomerangSlotKey, [&]() {
            manager.SaveData(kBoomerangSlotMaterialKey, static_cast<int32_t>(slot.materialId));
            manager.SaveData(kBoomerangSlotDurabilityCurKey, slot.durabilityCur);
            manager.SaveData(kBoomerangSlotDurabilityMaxKey, slot.durabilityMax);
        });
    });
}

std::vector<std::pair<MaterialId, uint16_t>> LoadMaterialInventoryFromManager(SaveManager& manager) {
    uint32_t entryCount = 0;
    std::vector<std::pair<MaterialId, uint16_t>> entries;

    manager.LoadStruct(kMaterialSaveSectionName, [&]() {
        manager.LoadData(kMaterialCountKey, entryCount, 0u);
        manager.LoadArray(kMaterialArrayKey, entryCount, [&](size_t /*i*/) {
            int32_t idRaw = static_cast<int32_t>(MaterialId::None);
            int32_t qty = 0;

            manager.LoadStruct("", [&]() {
                manager.LoadData(kMaterialEntryIdKey, idRaw, static_cast<int32_t>(MaterialId::None));
                manager.LoadData(kMaterialEntryQtyKey, qty, 0);
            });

            if (qty > 0) {
                const uint16_t clampedQty = static_cast<uint16_t>(std::clamp(qty, 0, 65535));
                entries.push_back({ static_cast<MaterialId>(idRaw), clampedQty });
                Fuse::Log("[FuseDBG] MatLoad: mat=%u qty=%u\n", static_cast<unsigned int>(idRaw),
                          static_cast<unsigned int>(clampedQty));
            }
        });
    });

    return entries;
}

void SaveMaterialInventoryToManager(SaveManager& manager,
                                    const std::vector<std::pair<MaterialId, uint16_t>>& inventoryEntries) {
    const uint32_t entryCount = static_cast<uint32_t>(inventoryEntries.size());

    manager.SaveStruct(kMaterialSaveSectionName, [&]() {
        manager.SaveData(kMaterialCountKey, entryCount);
        manager.SaveArray(kMaterialArrayKey, entryCount, [&](size_t i) {
            const auto& entry = inventoryEntries[i];
            const int32_t idSaved = static_cast<int32_t>(entry.first);
            const int32_t qtySaved = static_cast<int32_t>(entry.second);

            manager.SaveStruct("", [&]() {
                manager.SaveData(kMaterialEntryIdKey, idSaved);
                manager.SaveData(kMaterialEntryQtyKey, qtySaved);
            });

            Fuse::Log("[FuseDBG] MatSave: mat=%u qty=%u\n", static_cast<unsigned int>(entry.first),
                      static_cast<unsigned int>(entry.second));
        });
    });
}

} // namespace FusePersistence
