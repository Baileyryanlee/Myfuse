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

static const ModifierSpec kStickMods[] = {
    { ModifierId::RangeUp, 3 },
    { ModifierId::WideRange, 3 },
};

static const ModifierSpec kBombMods[] = {
    { ModifierId::Explosion, 1 },
};

static const ModifierSpec kKeeseEyeMods[] = {
    { ModifierId::Seek, 1 },
};

static const ModifierSpec kFireJellyMods[] = {
    { ModifierId::Burn, 1 },
};

static const ModifierSpec kFireKeeseEyeMods[] = {
    { ModifierId::Seek, 1 },
    { ModifierId::Burn, 1 },
};

static const ModifierSpec kBeamosHeadMods[] = {
    { ModifierId::Beam, 1 },
};

constexpr MaterialDef kMaterialDefs[] = {
    { MaterialId::None, "None", 0, 0, nullptr, 0 },
    { MaterialId::Rock, "ROCK", 1, 10, kRockMods, 4 },
    { MaterialId::DekuNut, "Deku Nut", 0, 5, kDekuNutMods, 2 },
    { MaterialId::Stick, "Stick", 2, 3, kStickMods, 2 },
    { MaterialId::FrozenShard, "Frozen Shard", 0, 8, kFrozenShardMods, 1 },
    { MaterialId::Bomb, "Bomb", 1, 1, kBombMods, 1 },
    { MaterialId::KeeseEye, "Keese Eye", 0, 4, kKeeseEyeMods, 1 },
    { MaterialId::FireJelly, "Fire Jelly", 0, 10, kFireJellyMods, 1 },
    { MaterialId::FireKeeseEye, "Fire Keese Eye", 0, 1, kFireKeeseEyeMods, 2 },
    { MaterialId::BeamosHead, "Beamos Head", 4, 100, kBeamosHeadMods, 1 },
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
