#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"
#include "soh/cvar_prefixes.h"
#include "soh/OTRGlobals.h"

#include "soh/Enhancements/Fuse/Fuse.h"
#include "soh/Enhancements/Fuse/FuseState.h"
#include "soh/Enhancements/Fuse/Hooks/FuseHooks_Objects.h"
#include "soh/Enhancements/Fuse/RangedFuseMenu.h"
#include "soh/Enhancements/Fuse/UI/FuseMenuWindow.h"
#include "soh/SaveManager.h"

#include <algorithm>
#include <memory>

#include <spdlog/spdlog.h>

extern "C" {
#include "z64.h"
#include "variables.h"
extern PlayState* gPlayState;
}

static std::shared_ptr<FuseMenuWindow> sFuseMenuWindow;
static bool sFuseMenuShown = false;
static constexpr const char* kFuseSaveWriteCVar = CVAR_ENHANCEMENT("Fuse.SaveWrite");
static constexpr const char* kFuseDebugOverrideSectionName = "enhancements.fuse.debugOverrides";

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

    const auto slots = Fuse::GetSwordSlots();
    const FuseSlot boomerangSlot = Fuse::GetBoomerangSlot();
    const FuseSlot hammerSlot = Fuse::GetHammerSlot();
    FusePersistence::SaveFuseStateToManager(*SaveManager::Instance, slots, boomerangSlot, hammerSlot);
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
    const FusePersistence::FuseSwordSlotsSaveState state =
        FusePersistence::LoadFuseStateFromManager(*SaveManager::Instance);
    Fuse::ApplyLoadedSwordSlots(state.swordSlots);
    Fuse::ApplyLoadedBoomerangSlot(state.boomerangSlot);
    if (state.hammerSlotLoaded) {
        Fuse::ApplyLoadedHammerSlot(state.hammerSlot);
    }
    if (state.migratedFromLegacy) {
        FusePersistence::WriteSwordStateToContext(FusePersistence::ClearedSwordState());
    }
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
        FuseHooks::OnLoadGame_RestoreObjects();
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
        RangedFuseMenu::Update(play);
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

    COND_HOOK(OnPlayDrawEnd, true, []() {
        if (!IsInGameplay()) {
            return;
        }

        RangedFuseMenu::Draw(gPlayState);
    });
}

static RegisterShipInitFunc initFuseMod(RegisterFuseMod);
