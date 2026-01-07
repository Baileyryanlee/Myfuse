#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "z64.h"
#include "soh/Enhancements/Fuse/FuseMaterials.h"

struct PlayState;
class SaveManager;

enum class SwordSlotKey { Kokiri = 0, Master = 1, Biggoron = 2 };

bool IsSwordEquipValue(int32_t equipValue);
SwordSlotKey SwordSlotKeyFromEquipValue(int32_t equipValue);
const char* SwordSlotName(SwordSlotKey key);

struct SwordFuseSlot {
    MaterialId materialId = MaterialId::None;
    int durabilityCur = 0;
    int durabilityMax = 0;

    void Clear();
    void ResetToUnfused();
};

// Centralized representation of the save data for the currently equipped sword.
// Invariants:
// * `isFused` determines whether the remaining fields are meaningful; when false, the sword is unfused and
//   `materialId` is treated as `MaterialId::None` and durability fields are zeroed.
// * Durability values are always clamped to be non-negative. `durabilityCur` is further clamped to the range
//   [0, durabilityMax] when a maximum is known (durabilityMax > 0).
// * `hasExplicitCur` mirrors the save field that tracks whether a current durability value was written; when
//   false, loading uses legacy or default behavior without altering durability semantics.
// * `legacyDurability` preserves the historical save slot that stored current durability to avoid changing
//   existing deserialization behavior.
struct FuseSwordSaveState {
    bool isFused = false;
    MaterialId materialId = MaterialId::None;
    int durabilityCur = 0;
    int durabilityMax = 0;
    bool hasExplicitCur = false;
    int legacyDurability = 0;
};

// Future save-persistent blob (versioned)
// NOTE: we won't wire this into save serialization yet.
struct FuseSaveData {
    uint32_t version = 1;

    // Per-item fused material ids later.
    MaterialId swordFuseMaterialId = MaterialId::None;

    // Durability (v0: only Sword+Rock)
    uint16_t swordFuseDurability = 0;    // current (0 = broken/none)
    uint16_t swordFuseMaxDurability = 0; // max

    std::array<SwordFuseSlot, 3> swordSlots{};

    SwordFuseSlot& GetSwordSlot(SwordSlotKey key);
    const SwordFuseSlot& GetSwordSlot(SwordSlotKey key) const;
    SwordFuseSlot& GetActiveSwordSlot(const PlayState* play);
};

struct FuseRuntimeState {
    bool enabled = true;
    bool swordFuseLoadedFromSave = false;

    // Useful for debugging/testing
    const char* lastEvent = "None";
};

namespace FusePersistence {

constexpr s16 kSwordMaterialIdNone = static_cast<s16>(MaterialId::None);
constexpr const char* kSwordSaveSectionName = "enhancements.fuse";
constexpr const char* kSwordMaterialKey = "matId";
constexpr const char* kSwordDurabilityKey = "curDurability";

// Vanilla materials read and consume directly from the vanilla inventory.
// Custom materials live in the custom inventory map saved under "enhancements.fuse.materials".
constexpr const char* kMaterialSaveSectionName = "enhancements.fuse.materials";
constexpr const char* kMaterialCountKey = "count";
constexpr const char* kMaterialArrayKey = "materials";
constexpr const char* kMaterialEntryIdKey = "id";
constexpr const char* kMaterialEntryQtyKey = "qty";

// Core helpers
FuseSwordSaveState ClearedSwordState();
FuseSwordSaveState BuildRuntimeSwordState();
FuseSwordSaveState ReadSwordStateFromContext();
void WriteSwordStateToContext(const FuseSwordSaveState& state);
void ApplySwordStateFromContext(const PlayState* play);

// SaveManager helpers
bool LoadSwordStateFromManager(SaveManager& manager, FuseSwordSaveState& outState, std::string* failReason);
void SaveSwordStateToManager(SaveManager& manager, const FuseSwordSaveState& state);
std::vector<std::pair<MaterialId, uint16_t>> LoadMaterialInventoryFromManager(SaveManager& manager);
void SaveMaterialInventoryToManager(
    SaveManager& manager, const std::vector<std::pair<MaterialId, uint16_t>>& inventoryEntries);

} // namespace FusePersistence
