#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"

#include "soh/Enhancements/Fuse/Fuse.h"
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
static bool gFuseEnableSaveWrite = false;
static constexpr const char* kFuseWeaponSectionName = "enhancements.fuse";
static constexpr s16 kFuseSwordMaterialIdNone = -1;

static void SaveFuseWeaponSection(SaveContext* saveContext, int /*sectionID*/, bool /*fullSave*/) {
    (void)saveContext;

    if (!gFuseEnableSaveWrite) {
        return;
    }

    const int materialId = Fuse::IsSwordFused() ? static_cast<int>(Fuse::GetSwordMaterial())
                                                : static_cast<int>(kFuseSwordMaterialIdNone);
    const int curDurability = Fuse::IsSwordFused() ? Fuse::GetSwordFuseDurability() : 0;

    spdlog::info("[FuseDBG] SaveWrite begin: matId={} cur={}", materialId, curDurability);

    SaveManager::Instance->SaveStruct(kFuseWeaponSectionName, [&]() {
        SaveManager::Instance->SaveData("matId", materialId);
        SaveManager::Instance->SaveData("curDurability", curDurability);
    });

    spdlog::info("[FuseDBG] SaveWrite end: wrote section {}", kFuseWeaponSectionName);
}

static void LoadFuseWeaponSection() {
    int materialId = kFuseSwordMaterialIdNone;
    int curDurability = 0;

    SaveManager::Instance->LoadStruct(kFuseWeaponSectionName, [&]() {
        SaveManager::Instance->LoadData("matId", materialId, static_cast<int>(kFuseSwordMaterialIdNone));
        SaveManager::Instance->LoadData("curDurability", curDurability, 0);
    });

    const bool hasFuseData = materialId != kFuseSwordMaterialIdNone;
    gSaveContext.ship.fuseSwordMaterialId = static_cast<s16>(materialId);
    gSaveContext.ship.fuseSwordCurrentDurability = static_cast<u16>(std::max(curDurability, 0));
    gSaveContext.ship.fuseSwordCurDurabilityPresent = hasFuseData;
    gSaveContext.ship.fuseSwordCurDur = 0;
    gSaveContext.ship.fuseSwordMaxDur = 0;

    spdlog::info("[FuseDBG] SaveRead: hasSection={} matId={} cur={}", hasFuseData ? 1 : 0, materialId,
                 curDurability);
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
    SaveManager::Instance->AddSaveFunction(kFuseWeaponSectionName, 1, SaveFuseWeaponSection, true, SECTION_PARENT_NONE);
    SaveManager::Instance->AddLoadFunction(kFuseWeaponSectionName, 1, LoadFuseWeaponSection);

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
