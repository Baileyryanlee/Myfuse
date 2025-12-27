#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"

#include "soh/Enhancements/Fuse/Fuse.h"
#include "soh/Enhancements/Fuse/UI/FuseMenuWindow.h"
#include "soh/SaveManager.h"

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
static constexpr const char* kFuseWeaponSectionName = "FuseWeapon";
static constexpr s16 kFuseSwordMaterialIdNone = -1;

static void SaveFuseWeaponSection(SaveContext* saveContext, int /*sectionID*/, bool /*fullSave*/) {
    const s16 materialId = saveContext->ship.fuseSwordMaterialId;
    const u16 curDurability = saveContext->ship.fuseSwordCurrentDurability;

    spdlog::info("[FuseDBG] Saving fuse matId={} cur={} max={}", materialId, curDurability,
                 saveContext->ship.fuseSwordMaxDur);

    SaveManager::Instance->SaveStruct("", [&]() {
        SaveManager::Instance->SaveData("matId", materialId);
        SaveManager::Instance->SaveData("curDurability", curDurability);
    });
}

static void LoadFuseWeaponSection() {
    s16 materialId = kFuseSwordMaterialIdNone;
    u16 curDurability = 0;

    SaveManager::Instance->LoadStruct("", [&]() {
        SaveManager::Instance->LoadData("matId", materialId, static_cast<s16>(kFuseSwordMaterialIdNone));
        SaveManager::Instance->LoadData("curDurability", curDurability, static_cast<u16>(0));
    });

    const bool hasFuseData = materialId != kFuseSwordMaterialIdNone;
    gSaveContext.ship.fuseSwordMaterialId = materialId;
    gSaveContext.ship.fuseSwordCurrentDurability = curDurability;
    gSaveContext.ship.fuseSwordCurDurabilityPresent = hasFuseData;
    gSaveContext.ship.fuseSwordCurDur = 0;
    gSaveContext.ship.fuseSwordMaxDur = 0;

    spdlog::info("[FuseDBG] Loaded fuse save data matId={} cur={} hasSavedCur={}", materialId, curDurability,
                 hasFuseData);
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
