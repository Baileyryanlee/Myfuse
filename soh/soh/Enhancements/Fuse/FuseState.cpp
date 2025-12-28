#include "FuseState.h"

#include "Fuse.h"
#include "soh/SaveManager.h"

#include <algorithm>
#include <limits>

extern "C" {
#include "z64.h"
}

#ifndef FUSE_DEBUG_LOGS
#define FUSE_DEBUG_LOGS 0
#endif

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

void DebugLogState(const char* prefix, const FuseSwordSaveState& state) {
#if FUSE_DEBUG_LOGS
    const int material = state.isFused ? static_cast<int>(state.materialId) : FusePersistence::kSwordMaterialIdNone;
    const int durabilityCur = state.isFused ? state.durabilityCur : 0;
    const int durabilityMax = state.isFused ? state.durabilityMax : 0;
    Fuse::Log("[FuseDBG] %s: item=sword mat=%d dura=%d/%d\n", prefix, material, durabilityCur, durabilityMax);
#else
    (void)prefix;
    (void)state;
#endif
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
    DebugLogState("Save", state);
    return state;
}

FuseSwordSaveState ReadSwordStateFromContext() {
    FuseSwordSaveState state{};
    state.isFused = gSaveContext.ship.fuseSwordMaterialId != kSwordMaterialIdNone;
    state.materialId = static_cast<MaterialId>(std::max<s16>(gSaveContext.ship.fuseSwordMaterialId, 0));
    state.durabilityMax = gSaveContext.ship.fuseSwordMaxDur;
    state.hasExplicitCur = gSaveContext.ship.fuseSwordCurDurabilityPresent;
    state.durabilityCur = state.hasExplicitCur ? gSaveContext.ship.fuseSwordCurrentDurability
                                               : gSaveContext.ship.fuseSwordCurDur;
    state.legacyDurability = gSaveContext.ship.fuseSwordCurDur;

    NormalizeState(state);
    DebugLogState("Load", state);
    return state;
}

void WriteSwordStateToContext(const FuseSwordSaveState& state) {
    if (!state.isFused) {
        gSaveContext.ship.fuseSwordMaterialId = kSwordMaterialIdNone;
        gSaveContext.ship.fuseSwordCurDur = 0;
        gSaveContext.ship.fuseSwordMaxDur = 0;
        gSaveContext.ship.fuseSwordCurrentDurability = 0;
        gSaveContext.ship.fuseSwordCurDurabilityPresent = false;
        DebugLogState("Save", state);
        return;
    }

    gSaveContext.ship.fuseSwordMaterialId = static_cast<s16>(state.materialId);
    gSaveContext.ship.fuseSwordCurDur = static_cast<s16>(std::clamp(
        state.legacyDurability, 0, static_cast<int>(std::numeric_limits<int16_t>::max())));
    gSaveContext.ship.fuseSwordMaxDur = static_cast<s16>(std::clamp(
        state.durabilityMax, 0, static_cast<int>(std::numeric_limits<int16_t>::max())));
    gSaveContext.ship.fuseSwordCurrentDurability = static_cast<uint16_t>(std::clamp(
        state.durabilityCur, 0, static_cast<int>(std::numeric_limits<uint16_t>::max())));
    gSaveContext.ship.fuseSwordCurDurabilityPresent = state.hasExplicitCur;

    DebugLogState("Save", state);
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

    state.isFused = materialId != kSwordMaterialIdNone;
    state.materialId = static_cast<MaterialId>(std::max(materialId, 0));
    state.hasExplicitCur = curDurability != -999;
    state.durabilityCur = state.hasExplicitCur ? curDurability : 0;
    state.legacyDurability = 0;

    NormalizeState(state);
    DebugLogState("Load", state);
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

    DebugLogState("Save", normalizedState);
}

} // namespace FusePersistence
