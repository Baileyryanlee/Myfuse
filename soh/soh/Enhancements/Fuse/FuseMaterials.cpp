#include "soh/Enhancements/Fuse/FuseMaterials.h"

namespace {

constexpr MaterialDef kMaterialDefs[] = {
    { MaterialId::None, "None", 0, false },
    { MaterialId::Rock, "ROCK", 20, true },
};

} // namespace

const MaterialDef* Fuse::GetMaterialDef(MaterialId id) {
    for (const auto& def : kMaterialDefs) {
        if (def.id == id) {
            return &def;
        }
    }
    return nullptr;
}

uint16_t Fuse::GetMaterialBaseDurability(MaterialId id) {
    const MaterialDef* def = GetMaterialDef(id);
    if (!def) {
        return 0;
    }
    return def->baseMaxDurability;
}

