#pragma once
#include <cstdint>

#include "z64.h"
#include "soh/Enhancements/Fuse/FuseMaterials.h"

struct PlayState;
class SaveManager;

// Centralized representation of the save data for the currently equipped sword.
// Invariants:
// * `isFused` determines whether the remaining fields are meaningful; when false, the sword is unfused and
//   `materialId` is treated as `MaterialId::None`.
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

    // v0 material storage: ROCK count (was bool)
    uint16_t rockCount = 0;

    // Per-item fused material ids later.
    MaterialId swordFuseMaterialId = MaterialId::None;

    // Durability (v0: only Sword+Rock)
    uint16_t swordFuseDurability = 0;    // current (0 = broken/none)
    uint16_t swordFuseMaxDurability = 0; // max
};

struct FuseRuntimeState {
    bool enabled = true;
    bool swordFuseLoadedFromSave = false;

    // Useful for debugging/testing
    const char* lastEvent = "None";
};

namespace FusePersistence {

constexpr s16 kSwordMaterialIdNone = -1;
constexpr const char* kSwordSaveSectionName = "enhancements.fuse";
constexpr const char* kSwordMaterialKey = "matId";
constexpr const char* kSwordDurabilityKey = "curDurability";

// Core helpers
FuseSwordSaveState ClearedSwordState();
FuseSwordSaveState BuildRuntimeSwordState();
FuseSwordSaveState ReadSwordStateFromContext();
void WriteSwordStateToContext(const FuseSwordSaveState& state);
void ApplySwordStateFromContext(const PlayState* play);

// SaveManager helpers
FuseSwordSaveState LoadSwordStateFromManager(SaveManager& manager);
void SaveSwordStateToManager(SaveManager& manager, const FuseSwordSaveState& state);

} // namespace FusePersistence
