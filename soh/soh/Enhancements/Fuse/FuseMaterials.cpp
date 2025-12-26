#include "soh/Enhancements/Fuse/FuseMaterials.h"

namespace {

constexpr MaterialDef kMaterialDefs[] = {
    { MaterialId::None, "None", 0, false },
    { MaterialId::Rock, "ROCK", 20, true },
};

} // namespace

const MaterialDef* FuseMaterials::GetMaterialDef(MaterialId id) {
    for (const auto& def : kMaterialDefs) {
        if (def.id == id) {
            return &def;
        }
    }
    return nullptr;
}

