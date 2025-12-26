#include "soh/Enhancements/Fuse/FuseMaterials.h"
#include "soh/Enhancements/Fuse/FuseModifiers.h"

namespace {

static const ModifierSpec kRockMods[] = {
    { ModifierId::Hammerize, 1 },
};

constexpr MaterialDef kMaterialDefs[] = {
    { MaterialId::None, "None", 0, nullptr, 0 },
    { MaterialId::Rock, "ROCK", 20, kRockMods, 1 },
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

