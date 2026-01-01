#include "FuseState.h"

#include "Fuse.h"
#include "soh/SaveManager.h"

#include <algorithm>
#include <limits>

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

bool IsNoneMaterialId(int matId) {
    constexpr int kMaterialIdMin = static_cast<int>(MaterialId::None);
    constexpr int kMaterialIdMax = static_cast<int>(MaterialId::DekuNut);

    if (matId == kMaterialIdMin || matId == -1) {
        return true;
    }

    return matId < kMaterialIdMin || matId > kMaterialIdMax;
}

int GetEquippedSwordItemId() {
    return gSaveContext.equips.buttonItems[0];
}

void LogPersistenceEvent(const char* prefix, const FuseSwordSaveState& state) {
    const int material = state.isFused ? static_cast<int>(state.materialId) : FusePersistence::kSwordMaterialIdNone;
    const int durabilityCur = state.isFused ? state.durabilityCur : 0;
    const int durabilityMax = state.isFused ? state.durabilityMax : 0;
    const int swordItemId = GetEquippedSwordItemId();

    Fuse::Log("[FuseDBG] %s: sword=%d material=%d dura=%d/%d\n", prefix, swordItemId, material, durabilityCur,
              durabilityMax);
}

} // namespace

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
    state.durabilityCur = state.hasExplicitCur ? gSaveContext.ship.fuseSwordCurrentDurability
                                               : gSaveContext.ship.fuseSwordCurDur;
    state.legacyDurability = gSaveContext.ship.fuseSwordCurDur;

    NormalizeState(state);
    LogPersistenceEvent("Load", state);
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
        LogPersistenceEvent("Save", normalizedState);
        return;
    }

    gSaveContext.ship.fuseSwordMaterialId = static_cast<s16>(normalizedState.materialId);
    gSaveContext.ship.fuseSwordCurDur = static_cast<s16>(std::clamp(
        normalizedState.legacyDurability, 0, static_cast<int>(std::numeric_limits<int16_t>::max())));
    gSaveContext.ship.fuseSwordMaxDur = static_cast<s16>(std::clamp(
        normalizedState.durabilityMax, 0, static_cast<int>(std::numeric_limits<int16_t>::max())));
    gSaveContext.ship.fuseSwordCurrentDurability = static_cast<uint16_t>(std::clamp(
        normalizedState.durabilityCur, 0, static_cast<int>(std::numeric_limits<uint16_t>::max())));
    gSaveContext.ship.fuseSwordCurDurabilityPresent = normalizedState.hasExplicitCur;

    LogPersistenceEvent("Save", normalizedState);
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

FuseSwordSaveState LoadSwordStateFromManager(SaveManager& manager) {
    FuseSwordSaveState state = ClearedSwordState();
    int materialId = kSwordMaterialIdNone;
    int curDurability = -999;

    manager.LoadStruct(kSwordSaveSectionName, [&]() {
        manager.LoadData(kSwordMaterialKey, materialId, static_cast<int>(kSwordMaterialIdNone));
        manager.LoadData(kSwordDurabilityKey, curDurability, -999);
    });

    state.isFused = !IsNoneMaterialId(materialId);
    state.materialId = state.isFused ? static_cast<MaterialId>(materialId) : MaterialId::None;
    state.hasExplicitCur = curDurability != -999;
    state.durabilityCur = state.hasExplicitCur ? curDurability : 0;
    state.legacyDurability = 0;

    NormalizeState(state);
    LogPersistenceEvent("Load", state);
    return state;
}

void SaveSwordStateToManager(SaveManager& manager, const FuseSwordSaveState& state) {
    const FuseSwordSaveState normalizedState = [&]() {
        FuseSwordSaveState copy = state;
        NormalizeState(copy);
        return copy;
    }();

    const int materialId = normalizedState.isFused ? static_cast<int>(normalizedState.materialId) : kSwordMaterialIdNone;
    const int durabilityCur = normalizedState.isFused ? normalizedState.durabilityCur : 0;

    manager.SaveStruct(kSwordSaveSectionName, [&]() {
        manager.SaveData(kSwordMaterialKey, materialId);
        manager.SaveData(kSwordDurabilityKey, durabilityCur);
    });

    LogPersistenceEvent("Save", normalizedState);
}

} // namespace FusePersistence
