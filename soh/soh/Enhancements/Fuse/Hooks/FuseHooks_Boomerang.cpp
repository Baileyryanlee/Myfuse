#include "global.h"

#include "FuseHooks_Boomerang.h"
#include "soh/Enhancements/Fuse/Fuse.h"
#include "soh/Enhancements/Fuse/ShieldBashRules.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace {
constexpr int kBoomerangBgCooldownFrames = 12;
constexpr float kBombableAssistRadius = 120.0f;
static std::unordered_map<const EnBoom*, int> sBoomerangBgLastExplodeFrame;

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
        uint8_t stunLevel = 0;
        uint8_t freezeLevel = 0;
        uint8_t explosionLevel = 0;
        if (def) {
            HasModifier(def->modifiers, def->modifierCount, ModifierId::Knockback, &knockbackLevel);
            HasModifier(def->modifiers, def->modifierCount, ModifierId::Stun, &stunLevel);
            HasModifier(def->modifiers, def->modifierCount, ModifierId::Freeze, &freezeLevel);
            explosionLevel =
                Fuse::GetMaterialModifierLevel(materialId, FuseItemType::Boomerang, ModifierId::Explosion);
        }

        Fuse::Log("[FuseDBG] BoomerangHit: item=boomerang mat=%d lvl=%u victim=%p dura=%d/%d event=hit\n",
                  static_cast<int>(materialId), static_cast<unsigned int>(knockbackLevel), (void*)victim,
                  Fuse::GetBoomerangFuseDurability(), Fuse::GetBoomerangFuseMaxDurability());

        Player* player = GET_PLAYER(play);
        if (def && explosionLevel > 0) {
            Fuse::Log("[FuseDBG] ExplosionProc: item=Boomerang mat=%d lvl=%u victim=%p dura=%d/%d\n",
                      static_cast<int>(materialId), static_cast<unsigned int>(explosionLevel), (void*)victim,
                      Fuse::GetBoomerangFuseDurability(), Fuse::GetBoomerangFuseMaxDurability());
            if (Fuse_IsExplosionImmuneVictim(victim)) {
                Fuse::Log("[FuseDBG] ExplodeSkip: src=Boomerang victim=ACTOR_BOSS_DODONGO\n");
            } else if (FuseBash_IsEnemyActor(victim) || Fuse_IsBombableActorId(victim->id)) {
                const int bombable = Fuse_IsBombableActorId(victim->id) ? 1 : 0;
                Fuse::Log("[FuseDBG] Explode: src=boom kind=actor pos=(%.2f %.2f %.2f) victim=0x%04X bombable=%d\n",
                          victim->world.pos.x, victim->world.pos.y, victim->world.pos.z, victim->id, bombable);
                Fuse_TriggerExplosion(play, victim->world.pos, FuseExplosionSelfMode::DamagePlayer,
                                      Fuse_GetExplosionParams(materialId, explosionLevel), "Boomerang");
            }
        }
        if (Fuse::TryFreezeShatter(play, victim, player ? &player->actor : nullptr, "boomerang")) {
            Fuse::DamageBoomerangFuseDurability(play, 1, "Boomerang hit");
            return;
        }

        if (def && knockbackLevel > 0) {
            ApplyBoomerangKnockback(play, victim, knockbackLevel, materialId, Fuse::GetBoomerangFuseDurability(),
                                    Fuse::GetBoomerangFuseMaxDurability());
        }

        if (def && stunLevel > 0 && FuseBash_IsEnemyActor(victim)) {
            Fuse_EnqueuePendingStun(victim, stunLevel, materialId, ITEM_BOOMERANG);
        }

        if (Fuse::IsFuseFrozen(victim) || victim->freezeTimer > 0) {
            Fuse::TryFreezeShatter(play, victim, player ? &player->actor : nullptr, "boomerang");
            Fuse::DamageBoomerangFuseDurability(play, 1, "Boomerang hit");
            return;
        }

        if (def && freezeLevel > 0) {
            Fuse::QueueSwordFreeze(play, victim, freezeLevel, "boomerang", "Boomerang", materialId);
        }

    }

    Fuse::DamageBoomerangFuseDurability(play, 1, "Boomerang hit");
}

extern "C" void FuseHooks_OnBoomerangHitSurface(EnBoom* boom, PlayState* play, const Vec3f* hitPos) {
    if (!boom || !play || !hitPos) {
        return;
    }

    if (!Fuse::IsBoomerangFused()) {
        return;
    }

    const MaterialId materialId = Fuse::GetBoomerangMaterial();
    const MaterialDef* def = Fuse::GetMaterialDef(materialId);
    if (!def) {
        return;
    }

    const uint8_t explosionLevel =
        Fuse::GetMaterialModifierLevel(materialId, FuseItemType::Boomerang, ModifierId::Explosion);
    if (explosionLevel == 0) {
        return;
    }

    const int curFrame = play->gameplayFrames;
    const auto lastIt = sBoomerangBgLastExplodeFrame.find(boom);
    if (lastIt != sBoomerangBgLastExplodeFrame.end() && curFrame >= 0 &&
        (curFrame - lastIt->second) < kBoomerangBgCooldownFrames) {
        return;
    }

    sBoomerangBgLastExplodeFrame[boom] = curFrame;

    const Actor* bombable = Fuse_FindNearbyBombable(play, hitPos, kBombableAssistRadius);
    Vec3f explodePos = *hitPos;
    const char* kind = "bg";
    int bombableFlag = 0;
    s16 victimId = 0;
    if (bombable) {
        explodePos = bombable->world.pos;
        kind = "assist";
        bombableFlag = 1;
        victimId = bombable->id;
        Fuse::Log("[FuseDBG] ExplodeAssist: src=boom hit=(%.2f %.2f %.2f) bombable=0x%04X at=(%.2f %.2f %.2f) "
                  "r=%.2f\n",
                  hitPos->x, hitPos->y, hitPos->z, bombable->id, explodePos.x, explodePos.y, explodePos.z,
                  kBombableAssistRadius);
    }

    Fuse::Log("[FuseDBG] Explode: src=boom kind=%s pos=(%.2f %.2f %.2f) victim=0x%04X bombable=%d\n", kind,
              explodePos.x, explodePos.y, explodePos.z, victimId, bombableFlag);
    Fuse_TriggerExplosion(play, explodePos, FuseExplosionSelfMode::DamagePlayer,
                          Fuse_GetExplosionParams(materialId, explosionLevel), "BoomerangBG");
}
