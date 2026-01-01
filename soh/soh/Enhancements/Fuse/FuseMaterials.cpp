#include "soh/Enhancements/Fuse/FuseMaterials.h"
#include "soh/Enhancements/Fuse/FuseModifiers.h"

namespace {

static const ModifierSpec kRockMods[] = {
    { ModifierId::Hammerize, 1 },
};

static const ModifierSpec kDekuNutMods[] = {
    { ModifierId::Stun, 1 },
};

static const ModifierSpec kFrozenShardMods[] = {
    { ModifierId::Freeze, 1 },
};

constexpr MaterialDef kMaterialDefs[] = {
    { MaterialId::None, "None", 0, nullptr, 0 },
    { MaterialId::Rock, "ROCK", 20, kRockMods, 1 },
    { MaterialId::DekuNut, "Deku Nut", 5, kDekuNutMods, 1 },
    { MaterialId::FrozenShard, "Frozen Shard", 8, kFrozenShardMods, 1 },
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

