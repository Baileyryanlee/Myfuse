#include "global.h"

#include "FuseHooks_Boomerang.h"
#include "soh/Enhancements/Fuse/Fuse.h"
#include "soh/Enhancements/Fuse/ShieldBashRules.h"

#include <algorithm>
#include <cmath>

namespace {
void ApplyBoomerangKnockback(PlayState* play, Actor* victim, uint8_t level, MaterialId materialId, int curDurability,
                             int maxDurability) {
    if (!play || !victim || level == 0) {
        return;
    }

    if (!FuseBash_IsEnemyActor(victim)) {
        return;
    }

    Player* player = GET_PLAYER(play);
    if (!player) {
        return;
    }

    Vec3f dir = { victim->world.pos.x - player->actor.world.pos.x, 0.0f,
                  victim->world.pos.z - player->actor.world.pos.z };
    float distSq = (dir.x * dir.x) + (dir.z * dir.z);
    if (distSq < 0.0001f) {
        dir.x = 0.0f;
        dir.z = 1.0f;
    } else {
        const float invLen = 1.0f / sqrtf(distSq);
        dir.x *= invLen;
        dir.z *= invLen;
    }

    constexpr float kBaseKnockbackSpeed = 5.0f;
    const float speed = kBaseKnockbackSpeed * (1.0f + (0.25f * (level - 1)));

    victim->velocity.x = dir.x * speed;
    victim->velocity.z = dir.z * speed;
    victim->speedXZ = std::max(victim->speedXZ, speed);

    victim->world.rot.y = Math_Atan2S(dir.x, dir.z);
    victim->shape.rot.y = victim->world.rot.y;

    (void)materialId;
    (void)curDurability;
    (void)maxDurability;
}
} // namespace

extern "C" void FuseHooks_OnBoomerangHitActor(PlayState* play, Actor* victim) {
    if (!play || !victim) {
        return;
    }

    if (Fuse::IsBoomerangFused()) {
        const MaterialId materialId = Fuse::GetBoomerangMaterial();
        const MaterialDef* def = Fuse::GetMaterialDef(materialId);
        uint8_t knockbackLevel = 0;
        if (def) {
            HasModifier(def->modifiers, def->modifierCount, ModifierId::Knockback, &knockbackLevel);
        }

        Fuse::Log("[FuseDBG] BoomerangHit: item=boomerang mat=%d lvl=%u victim=%p dura=%d/%d event=hit\n",
                  static_cast<int>(materialId), static_cast<unsigned int>(knockbackLevel), (void*)victim,
                  Fuse::GetBoomerangFuseDurability(), Fuse::GetBoomerangFuseMaxDurability());

        if (def && knockbackLevel > 0) {
            ApplyBoomerangKnockback(play, victim, knockbackLevel, materialId, Fuse::GetBoomerangFuseDurability(),
                                    Fuse::GetBoomerangFuseMaxDurability());
        }
    }

    Fuse::DamageBoomerangFuseDurability(play, 1, "Boomerang hit");
}
