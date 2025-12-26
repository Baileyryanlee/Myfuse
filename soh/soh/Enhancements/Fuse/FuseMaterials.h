#pragma once

#include <cstddef>
#include <cstdint>

#include "soh/Enhancements/Fuse/FuseModifiers.h"

enum class MaterialId : uint16_t {
    None = 0,
    Rock = 1,
};

struct MaterialDef {
    MaterialId id;
    const char* name;
    uint16_t baseMaxDurability;
    const ModifierSpec* modifiers;
    size_t modifierCount;
};

namespace FuseMaterials {
const MaterialDef* GetMaterialDef(MaterialId id);
}

