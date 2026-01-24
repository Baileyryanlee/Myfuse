#pragma once

#include <cstddef>
#include <cstdint>

enum class ModifierId : uint16_t {
    Hammerize = 1,
    Stun = 2,
    MegaStun = 3,
    Freeze = 4,
    Knockback = 5,
    PoundUp = 6,
    NegateKnockback = 7,
    RangeUp = 8,
    WideRange = 9,
    Explosion = 10,
};

struct ModifierSpec {
    ModifierId id;
    uint8_t level; // 1–3
};

// Helper API:
bool HasModifier(const ModifierSpec* mods, size_t count, ModifierId id, uint8_t* outLevel = nullptr);
