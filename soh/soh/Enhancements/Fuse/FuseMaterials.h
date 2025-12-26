#pragma once

#include <cstdint>

enum class MaterialId : uint16_t {
    None = 0,
    Rock = 1,
};

struct MaterialDef {
    MaterialId id;
    const char* name;
    uint16_t baseMaxDurability;
    // TEMP: data-driven toggle to preserve current hammer behavior.
    bool hammerizeSword;
};

namespace FuseMaterials {
const MaterialDef* GetMaterialDef(MaterialId id);
}

