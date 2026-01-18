#include "soh/Enhancements/Fuse/FuseMaterials.h"
#include "soh/Enhancements/Fuse/FuseModifiers.h"

namespace {

static const ModifierSpec kRockMods[] = {
    { ModifierId::Hammerize, 1 },
    { ModifierId::Knockback, 1 },
    { ModifierId::PoundUp, 1 },
    { ModifierId::NegateKnockback, 1 },
};

static const ModifierSpec kDekuNutMods[] = {
    { ModifierId::Stun, 1 },
    { ModifierId::MegaStun, 1 },
};

static const ModifierSpec kFrozenShardMods[] = {
    { ModifierId::Freeze, 1 },
};

constexpr MaterialDef kMaterialDefs[] = {
    { MaterialId::None, "None", 0, nullptr, 0 },
    { MaterialId::Rock, "ROCK", 10, kRockMods, 4 },
    { MaterialId::DekuNut, "Deku Nut", 5, kDekuNutMods, 2 },
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

const MaterialDef* FuseMaterials::GetMaterialDefs(size_t* count) {
    if (count != nullptr) {
        *count = sizeof(kMaterialDefs) / sizeof(kMaterialDefs[0]);
    }
    return kMaterialDefs;
}
