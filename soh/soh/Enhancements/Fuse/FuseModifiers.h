#pragma once

#include <cstddef>
#include <cstdint>

enum class ModifierId : uint16_t {
    Hammerize = 1,
    Stun = 2,
    Freeze = 3,
    Knockback = 4,
    PoundUp = 5,
    NegateKnockback = 6,
    // future: Range, Freeze, Explode, Stun, Seek, etc.
};

struct ModifierSpec {
    ModifierId id;
    uint8_t level; // 1–3
};

// Helper API:
bool HasModifier(const ModifierSpec* mods, size_t count, ModifierId id, uint8_t* outLevel = nullptr);
