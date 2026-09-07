#pragma once

#include <libultraship/libultraship.h>
#include <libultraship/controller/controldeck/ControlDeck.h>
#include "soh/OTRGlobals.h"

namespace FuseInput {
inline bool IsMenuHeld() {
    auto* context = Ship::Context::GetRawInstance();
    if (context == nullptr) {
        return false;
    }
    auto deck = std::dynamic_pointer_cast<LUS::ControlDeck>(context->GetControlDeck());
    if (!deck || deck->GetPads() == nullptr) {
        return false;
    }
    auto controller = deck->GetControllerByPort(0);
    if (!controller) {
        return false;
    }
    auto button = controller->GetButton(BTN_CUSTOM_FUSE_MENU);
    // Preserve the original L default without sharing mapping objects between buttons.
    const auto mask = button && !button->GetAllButtonMappings().empty() ? BTN_CUSTOM_FUSE_MENU : BTN_L;
    return (deck->GetPads()[0].button & mask) != 0;
}
} // namespace FuseInput
