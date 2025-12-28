#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"
#include "soh/cvar_prefixes.h"
#include "soh/OTRGlobals.h"

#include "soh/Enhancements/Fuse/Fuse.h"
#include "soh/Enhancements/Fuse/FuseState.h"
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
    FusePersistence::SaveSwordStateToManager(*SaveManager::Instance, state);
}

static void LoadFuseWeaponSection() {
    const FuseSwordSaveState state = FusePersistence::LoadSwordStateFromManager(*SaveManager::Instance);
    FusePersistence::WriteSwordStateToContext(state);
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
