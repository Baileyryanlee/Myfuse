#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"
#include "soh/cvar_prefixes.h"
#include "soh/OTRGlobals.h"

#include "soh/Enhancements/Fuse/Fuse.h"
#include "soh/Enhancements/Fuse/FuseState.h"
#include "soh/Enhancements/Fuse/UI/FuseMenuWindow.h"
#include "soh/SaveManager.h"

#include <algorithm>
#include <array>
#include <memory>
#include <string>

#include <spdlog/spdlog.h>

extern "C" {
#include "z64.h"
#include "variables.h"
extern PlayState* gPlayState;
}

// Forward declarations from hooks cpp
namespace FuseHooks {
void OnLoadGame_ResetObjects();
void OnFrame_Objects_Pre(PlayState* play);
void OnFrame_Objects_Post(PlayState* play);
void OnPlayerUpdate(PlayState* play);
} // namespace FuseHooks

static std::shared_ptr<FuseMenuWindow> sFuseMenuWindow;
static bool sFuseMenuShown = false;
static constexpr const char* kFuseSaveWriteCVar = CVAR_ENHANCEMENT("Fuse.SaveWrite");
static constexpr const char* kFuseDebugOverrideSectionName = "enhancements.fuse.debugOverrides";

static std::array<SwordFuseSlot, 3> SnapshotSwordSlots() {
    return { Fuse::GetSwordSlot(SwordSlotKey::Kokiri), Fuse::GetSwordSlot(SwordSlotKey::Master),
             Fuse::GetSwordSlot(SwordSlotKey::Biggoron) };
}

static SwordSlotKey GetEquippedSwordSlotKey() {
    const int32_t equipValue =
        (static_cast<int32_t>(gSaveContext.equips.equipment & gEquipMasks[EQUIP_TYPE_SWORD]) >>
         gEquipShifts[EQUIP_TYPE_SWORD]);
    return IsSwordEquipValue(equipValue) ? SwordSlotKeyFromEquipValue(equipValue) : SwordSlotKey::Kokiri;
}

static std::array<SwordFuseSlot, 3> BuildSlotsWithReadState(const FuseSwordSaveState& state) {
    auto slots = SnapshotSwordSlots();
    SwordFuseSlot& slot = slots[static_cast<size_t>(GetEquippedSwordSlotKey())];
    if (state.isFused) {
        slot.materialId = state.materialId;
        slot.durabilityCur = state.durabilityCur;
        slot.durabilityMax = state.durabilityMax;
    } else {
        slot.ResetToUnfused();
    }
    return slots;
}

static void LogSwordSlots(const char* action, const std::array<SwordFuseSlot, 3>& slots, uint32_t version) {
    Fuse::Log("[FuseSave] %s ver=%u slots:\n", action, version);
    const auto logSlot = [&](const char* label, const SwordFuseSlot& slot) {
        Fuse::Log("    %s: mat=%d dur=%d/%d\n", label, static_cast<int>(slot.materialId), slot.durabilityCur,
                  slot.durabilityMax);
    };
    logSlot("K", slots[static_cast<size_t>(SwordSlotKey::Kokiri)]);
    logSlot("M", slots[static_cast<size_t>(SwordSlotKey::Master)]);
    logSlot("B", slots[static_cast<size_t>(SwordSlotKey::Biggoron)]);
}

static void SaveFuseWeaponSection(SaveContext* saveContext, int /*sectionID*/, bool /*fullSave*/) {
    (void)saveContext;

    const bool enableSaveWrite = CVarGetInteger(kFuseSaveWriteCVar, 1) != 0;

    static bool sLoggedSaveWriteState = false;
    if (!sLoggedSaveWriteState) {
        spdlog::info("[FuseDBG] Fuse.SaveWrite cvar={} enabled={}", kFuseSaveWriteCVar, enableSaveWrite ? 1 : 0);
        sLoggedSaveWriteState = true;
    }

    if (!enableSaveWrite) {
        return;
    }

    const FuseSwordSaveState state = FusePersistence::BuildRuntimeSwordState();
    LogSwordSlots("Write", SnapshotSwordSlots(), Fuse::GetSaveDataVersion());
    FusePersistence::SaveSwordStateToManager(*SaveManager::Instance, state);
}

static void SaveFuseMaterialsSection(SaveContext* /*saveContext*/, int /*sectionID*/, bool /*fullSave*/) {
    const bool enableSaveWrite = CVarGetInteger(kFuseSaveWriteCVar, 1) != 0;

    if (!enableSaveWrite) {
        return;
    }

    const auto entries = Fuse::GetCustomMaterialInventory();
    FusePersistence::SaveMaterialInventoryToManager(*SaveManager::Instance, entries);
}

static void SaveFuseDebugOverridesSection(SaveContext* /*saveContext*/, int /*sectionID*/, bool /*fullSave*/) {
    const bool enableSaveWrite = CVarGetInteger(kFuseSaveWriteCVar, 1) != 0;

    if (!enableSaveWrite) {
        return;
    }

    Fuse::SaveDebugOverrides();
}

static void LoadFuseWeaponSection() {
    FuseSwordSaveState state{};
    std::string failReason;
    if (!FusePersistence::LoadSwordStateFromManager(*SaveManager::Instance, state, &failReason)) {
        Fuse::Log("[FuseSave] Read FAIL reason=%s\n", failReason.empty() ? "unknown" : failReason.c_str());
        return;
    }

    FusePersistence::WriteSwordStateToContext(state);
    LogSwordSlots("Read OK", BuildSlotsWithReadState(state), Fuse::GetSaveDataVersion());
}

static void LoadFuseMaterialsSection() {
    const auto entries = FusePersistence::LoadMaterialInventoryFromManager(*SaveManager::Instance);
    Fuse::ApplyCustomMaterialInventory(entries);
}

static void LoadFuseDebugOverridesSection() {
    Fuse::LoadDebugOverrides();
}

static bool IsInGameplay() {
    return gPlayState != nullptr;
}

static void EnsureFuseMenuWindow() {
    if (!sFuseMenuWindow) {
        sFuseMenuWindow = std::make_shared<FuseMenuWindow>(CVAR_WINDOW("FuseMenu"), "Fuse Debug Menu");
        Ship::Context::GetInstance()->GetWindow()->GetGui()->AddGuiWindow(sFuseMenuWindow);

        // Start hidden
        sFuseMenuWindow->Hide();
        sFuseMenuShown = false;
    }
}

static void RegisterFuseMod() {
    SaveManager::Instance->AddSaveFunction(FusePersistence::kSwordSaveSectionName, 1, SaveFuseWeaponSection, true,
                                           SECTION_PARENT_NONE);
    SaveManager::Instance->AddLoadFunction(FusePersistence::kSwordSaveSectionName, 1, LoadFuseWeaponSection);
    SaveManager::Instance->AddSaveFunction(FusePersistence::kMaterialSaveSectionName, 1, SaveFuseMaterialsSection,
                                           true, SECTION_PARENT_NONE);
    SaveManager::Instance->AddLoadFunction(FusePersistence::kMaterialSaveSectionName, 1, LoadFuseMaterialsSection);
    SaveManager::Instance->AddSaveFunction(kFuseDebugOverrideSectionName, 1, SaveFuseDebugOverridesSection, true,
                                           SECTION_PARENT_NONE);
    SaveManager::Instance->AddLoadFunction(kFuseDebugOverrideSectionName, 1, LoadFuseDebugOverridesSection);

    COND_HOOK(OnLoadGame, true, [](int32_t fileNum) {
        Fuse::OnLoadGame(fileNum);
        FuseHooks::OnLoadGame_ResetObjects();
    });

    // Create/register window once
    EnsureFuseMenuWindow();

    COND_HOOK(OnGameFrameUpdate, true, []() {
        if (!IsInGameplay()) {
            return;
        }

        EnsureFuseMenuWindow();

        PlayState* play = gPlayState;
        Fuse::OnGameFrameUpdate(play);
        // Pre-collision: enables hammer flags for En_Ishi breaking
        FuseHooks::OnFrame_Objects_Pre(play);

        // ... everything else in the frame happens ...

        // Post-collision hook kept for compatibility (durability handled in AT collision hook)
        FuseHooks::OnFrame_Objects_Post(play);

        // L + DpadDown toggles menu for now
        Input* input = &gPlayState->state.input[0];
        const bool lHeld = (input->cur.button & BTN_L) != 0;
        const bool dDownPressed = (input->press.button & BTN_DDOWN) != 0;

        if (lHeld && dDownPressed) {
            sFuseMenuShown = !sFuseMenuShown;

            if (sFuseMenuShown) {
                sFuseMenuWindow->Show();
                Fuse::SetLastEvent("Fuse Menu opened");
            } else {
                sFuseMenuWindow->Hide();
                Fuse::SetLastEvent("Fuse Menu closed");
            }
        }
    });

    COND_HOOK(OnPlayerUpdate, true, []() {
        if (!IsInGameplay()) {
            return;
        }

        FuseHooks::OnPlayerUpdate(gPlayState);
    });
}

static RegisterShipInitFunc initFuseMod(RegisterFuseMod);
