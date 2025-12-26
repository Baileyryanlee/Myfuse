#pragma once
#include <cstdint>

#include "soh/Enhancements/Fuse/FuseMaterials.h"

// Future save-persistent blob (versioned)
// NOTE: we won't wire this into save serialization yet.
struct FuseSaveData {
    uint32_t version = 1;

    // v0 material storage: ROCK count (was bool)
    uint16_t rockCount = 0;
    uint16_t iceCount = 0;

    // Per-item fused material ids later.
    MaterialId swordFuseMaterialId = MaterialId::None;

    // Durability (v0: only Sword+Rock)
    uint16_t swordFuseDurability = 0;    // current (0 = broken/none)
    uint16_t swordFuseMaxDurability = 0; // max
};

struct FuseRuntimeState {
    bool enabled = true;

    // Useful for debugging/testing
    const char* lastEvent = "None";
};
