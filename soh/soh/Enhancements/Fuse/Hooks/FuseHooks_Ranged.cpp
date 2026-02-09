#include "soh/Enhancements/Fuse/Hooks/FuseHooks_Ranged.h"
#include "overlays/actors/ovl_En_Fz/z_en_fz.h"

#include "soh/Enhancements/Fuse/Fuse.h"
#include "soh/Enhancements/Fuse/ShieldBashRules.h"

extern "C" {
#include "z64.h"
#include "src/overlays/actors/ovl_En_Arrow/z_en_arrow.h"
int EnArrow_SetLitByFire(EnArrow* thisx);
int EnArrow_SetFireDmgFlagsOnly(EnArrow* thisx);
}

void Fuse_GetRangedFuseStatus(RangedFuseSlot slot, int* outMaterialId, int* outDurabilityCur, int* outDurabilityMax);
void Fuse_GetRangedQueuedStatus(RangedFuseSlot slot, int* outMaterialId, int* outDurabilityCur, int* outDurabilityMax);
static constexpr float kBombableAssistRadius = 120.0f;

static const char* RangedSlotLabel(RangedFuseSlotId slot) {
    switch (slot) {
        case RANGED_FUSE_SLOT_ARROWS:
            return "Arrows";
        case RANGED_FUSE_SLOT_SLINGSHOT:
            return "Slingshot";
        case RANGED_FUSE_SLOT_HOOKSHOT:
            return "Hookshot";
        default:
            return "Unknown";
    }
}

static int RangedSlotItemId(RangedFuseSlotId slot) {
    switch (slot) {
        case RANGED_FUSE_SLOT_ARROWS:
            return ITEM_BOW;
        case RANGED_FUSE_SLOT_SLINGSHOT:
            return ITEM_SLINGSHOT;
        case RANGED_FUSE_SLOT_HOOKSHOT:
            return ITEM_HOOKSHOT;
        default:
            return ITEM_NONE;
    }
}

static FuseItemType RangedSlotItemType(RangedFuseSlotId slot) {
    switch (slot) {
        case RANGED_FUSE_SLOT_ARROWS:
            return FuseItemType::Arrows;
        case RANGED_FUSE_SLOT_SLINGSHOT:
            return FuseItemType::Slingshot;
        case RANGED_FUSE_SLOT_HOOKSHOT:
            return FuseItemType::Hookshot;
        default:
            return FuseItemType::Unknown;
    }
}

static const char* RangedSlotExplodeLabel(RangedFuseSlotId slot) {
    switch (slot) {
        case RANGED_FUSE_SLOT_ARROWS:
            return "arrow";
        case RANGED_FUSE_SLOT_SLINGSHOT:
            return "slingshot";
        case RANGED_FUSE_SLOT_HOOKSHOT:
            return "hookshot";
        default:
            return "unknown";
    }
}

static bool Fuse_ShouldSkipExplosionVictim(const Actor* victim) {
    return Fuse_IsExplosionImmuneVictim(victim);
}

static bool Fuse_ShouldTriggerExplosionOnActor(const Actor* actor) {
    if (!actor) {
        return false;
    }

    if (actor->category == ACTORCAT_PLAYER) {
        return false;
    }

    if (FuseBash_IsEnemyActor((Actor*)actor)) {
        return true;
    }

    if (actor->category == ACTORCAT_PROP || actor->category == ACTORCAT_BG) {
        return true;
    }

    return false;
}

static bool Fuse_RangedSlotHasBurn(RangedFuseSlot slot) {
    int materialIdRaw = static_cast<int>(MaterialId::None);
    int curDurability = 0;
    int maxDurability = 0;
    Fuse_GetRangedFuseStatus(slot, &materialIdRaw, &curDurability, &maxDurability);
    (void)maxDurability;

    if (materialIdRaw == static_cast<int>(MaterialId::None) || curDurability <= 0) {
        return false;
    }

    const MaterialDef* def = Fuse::GetMaterialDef(static_cast<MaterialId>(materialIdRaw));
    if (!def) {
        return false;
    }

    uint8_t burnLevel = 0;
    return HasModifier(def->modifiers, def->modifierCount, ModifierId::Burn, &burnLevel) && burnLevel > 0;
}

static void HandleRangedSurfaceHit(PlayState* play, RangedFuseSlot slot, const Vec3f* impactPos, const char* reason) {
    if (!play || !impactPos) {
        return;
    }

    int materialIdRaw = static_cast<int>(MaterialId::None);
    int curDurability = 0;
    int maxDurability = 0;
    Fuse_GetRangedFuseStatus(slot, &materialIdRaw, &curDurability, &maxDurability);

    Fuse::Log("[FuseDBG] RangedSurfaceHit slot=%s mat=%d dura=%d/%d pos=(%.2f %.2f %.2f) reason=%s\n",
              RangedSlotLabel(static_cast<RangedFuseSlotId>(slot)), materialIdRaw, curDurability, maxDurability,
              impactPos->x, impactPos->y, impactPos->z, reason ? reason : "None");

    if (materialIdRaw == static_cast<int>(MaterialId::None) || curDurability <= 0) {
        return;
    }

    const MaterialId materialId = static_cast<MaterialId>(materialIdRaw);
    const MaterialDef* def = Fuse::GetMaterialDef(materialId);
    if (!def) {
        return;
    }

    const uint8_t explosionLevel =
        Fuse::GetMaterialModifierLevel(materialId, RangedSlotItemType(static_cast<RangedFuseSlotId>(slot)),
                                       ModifierId::Explosion);
    if (explosionLevel > 0) {
        const Actor* bombable = Fuse_FindNearbyBombable(play, impactPos, kBombableAssistRadius);
        Vec3f explodePos = *impactPos;
        const char* kind = "bg";
        int bombableFlag = 0;
        s16 victimId = 0;
        if (bombable) {
            kind = "candidate";
            bombableFlag = 1;
            victimId = bombable->id;
            explodePos = Fuse_GetBombableAnchorPos(bombable, 25.0f);
            Fuse_AdjustExplosionPosForBombable(bombable, nullptr, &explodePos);
            Fuse::Log("[FuseDBG] ExplodeAssist: src=%s hit=(%.2f %.2f %.2f) bombable=0x%04X at=(%.2f %.2f %.2f) "
                      "r=%.2f\n",
                      RangedSlotExplodeLabel(static_cast<RangedFuseSlotId>(slot)), impactPos->x, impactPos->y,
                      impactPos->z, bombable->id, explodePos.x, explodePos.y, explodePos.z, kBombableAssistRadius);
        }
        FuseExplosionParams params = Fuse_GetExplosionParams(materialId, explosionLevel);
        params.hitFrames = 1;
        Fuse::Log("[FuseDBG] Explode: src=%s kind=%s pos=(%.2f %.2f %.2f) victim=0x%04X bombable=%d\n",
                  RangedSlotExplodeLabel(static_cast<RangedFuseSlotId>(slot)), kind, explodePos.x, explodePos.y,
                  explodePos.z, victimId, bombableFlag);
        Fuse_TriggerExplosion(play, explodePos, FuseExplosionSelfMode::DamagePlayer, params,
                              RangedSlotLabel(static_cast<RangedFuseSlotId>(slot)));
    }

}

extern "C" void Fuse_OnRangedHitActor(PlayState* play, RangedFuseSlotId slot, Actor* victim,
                                      const Vec3f* impactPos) {
    if (!play || !victim) {
        return;
    }

    if (!Fuse_ShouldTriggerExplosionOnActor(victim)) {
        return;
    }

    int materialIdRaw = static_cast<int>(MaterialId::None);
    int curDurability = 0;
    int maxDurability = 0;
    Fuse_GetRangedFuseStatus(static_cast<RangedFuseSlot>(slot), &materialIdRaw, &curDurability, &maxDurability);
    (void)maxDurability;

    Fuse::Log("[FuseDBG] RangedHit slot=%s mat=%d victim=%p id=0x%04X cat=%d\n", RangedSlotLabel(slot), materialIdRaw,
              (void*)victim, victim->id, victim->category);

    if (materialIdRaw == static_cast<int>(MaterialId::None) || curDurability <= 0) {
        return;
    }

    const MaterialId materialId = static_cast<MaterialId>(materialIdRaw);
    const MaterialDef* def = Fuse::GetMaterialDef(materialId);
    if (!def) {
        return;
    }

    if (slot != RANGED_FUSE_SLOT_HOOKSHOT) {
        const int bonus = Fuse::GetMaterialAttackBonus(materialId);
        if (bonus > 0) {
            const int base = victim->colChkInfo.damage;
            const int prevDamage = victim->colChkInfo.damage;
            victim->colChkInfo.damage = bonus;
            Actor_ApplyDamage(victim);
            victim->colChkInfo.damage = prevDamage;
            Fuse::Log("[FuseDBG] AtkBonusApply: src=ranged slot=%s mat=%d bonus=%d base=%d hpAfter=%d\n",
                      RangedSlotLabel(slot), materialIdRaw, bonus, base, victim->colChkInfo.health);
        }
    }

    const uint8_t explosionLevel =
        Fuse::GetMaterialModifierLevel(materialId, RangedSlotItemType(slot), ModifierId::Explosion);
    if (explosionLevel > 0) {
        const Vec3f* explodePos = impactPos ? impactPos : &victim->focus.pos;
        const Vec3f& loggedPos = explodePos ? *explodePos : victim->world.pos;
        FuseExplosionParams params = Fuse_GetExplosionParams(materialId, explosionLevel);
        params.hitFrames = 1;
        if (Fuse_ShouldSkipExplosionVictim(victim)) {
            Fuse::Log("[FuseDBG] ExplodeSkip: src=%s victim=ACTOR_BOSS_DODONGO\n", RangedSlotLabel(slot));
        } else if (FuseBash_IsEnemyActor(victim) || Fuse_IsBombableActorId(victim->id)) {
            const int bombable = Fuse_IsBombableActorId(victim->id) ? 1 : 0;
            if (bombable) {
                Vec3f adjustedPos = Fuse_GetBombableAnchorPos(victim, 25.0f);
                Fuse_AdjustExplosionPosForBombable(victim, nullptr, &adjustedPos);
                Fuse::Log("[FuseDBG] Explode: src=%s kind=actor pos=(%.2f %.2f %.2f) victim=0x%04X bombable=%d\n",
                          RangedSlotExplodeLabel(slot), adjustedPos.x, adjustedPos.y, adjustedPos.z,
                          victim->id, bombable);
                Fuse_TriggerExplosion(play, adjustedPos, FuseExplosionSelfMode::DamagePlayer,
                                      params, RangedSlotLabel(slot));
            } else {
                Vec3f adjustedPos = loggedPos;
                Fuse::Log("[FuseDBG] Explode: src=%s kind=actor pos=(%.2f %.2f %.2f) victim=0x%04X bombable=%d\n",
                          RangedSlotExplodeLabel(slot), adjustedPos.x, adjustedPos.y, adjustedPos.z,
                          victim->id, bombable);
                Fuse_TriggerExplosion(play, adjustedPos, FuseExplosionSelfMode::DamagePlayer,
                                      params, RangedSlotLabel(slot));
            }
        }
    }

    Player* player = GET_PLAYER(play);
    if (Fuse::TryFreezeShatter(play, victim, player ? &player->actor : nullptr, "ranged")) {
        Fuse::MarkRangedHitResolved(static_cast<RangedFuseSlot>(slot), "FreezeShatter");
        Fuse::ClearActiveRangedFuse(static_cast<RangedFuseSlot>(slot), "FreezeShatter");
        return;
    }

    uint8_t stunLevel = 0;
    if (HasModifier(def->modifiers, def->modifierCount, ModifierId::Stun, &stunLevel) && stunLevel > 0) {
        const Vec3f* stunPos = impactPos ? impactPos : &victim->focus.pos;
        const Vec3f& loggedPos = stunPos ? *stunPos : victim->world.pos;
        Fuse_TriggerDekuNutAtPos(play, loggedPos, RangedSlotItemId(slot));
    }

    if (Fuse::IsFuseFrozen(victim) || victim->freezeTimer > 0) {
        Fuse::TryFreezeShatter(play, victim, player ? &player->actor : nullptr, "ranged");
        Fuse::MarkRangedHitResolved(static_cast<RangedFuseSlot>(slot), "FreezeShatter");
        Fuse::ClearActiveRangedFuse(static_cast<RangedFuseSlot>(slot), "FreezeShatter");
        return;
    }

    uint8_t freezeLevel = 0;
    if (HasModifier(def->modifiers, def->modifierCount, ModifierId::Freeze, &freezeLevel) && freezeLevel > 0) {
        Fuse::QueueSwordFreeze(play, victim, freezeLevel, "ranged", RangedSlotLabel(slot), materialId);
    }

    uint8_t burnLevel = 0;
    if (HasModifier(def->modifiers, def->modifierCount, ModifierId::Burn, &burnLevel) && burnLevel > 0) {
        Fuse::ApplyBurn(play, victim, burnLevel, materialId, "ranged", RangedSlotLabel(slot));
    }

    Fuse::MarkRangedHitResolved(static_cast<RangedFuseSlot>(slot), "HitSuccess");
    Fuse::ClearActiveRangedFuse(static_cast<RangedFuseSlot>(slot), "HitSuccess");
}

static void LogRangedKnockbackStatus(const char* itemLabel, RangedFuseSlot slot, const char* eventLabel) {
    int materialId = static_cast<int>(MaterialId::None);
    int curDurability = 0;
    int maxDurability = 0;
    Fuse_GetRangedFuseStatus(slot, &materialId, &curDurability, &maxDurability);

    if (materialId == static_cast<int>(MaterialId::None) || curDurability <= 0) {
        return;
    }

    uint8_t level = 0;
    const MaterialDef* def = Fuse::GetMaterialDef(static_cast<MaterialId>(materialId));
    if (def) {
        HasModifier(def->modifiers, def->modifierCount, ModifierId::Knockback, &level);
    }

    Fuse::Log("[FuseDBG] RangedKnockback: event=%s item=%s mat=%d lvl=%u dura=%d/%d\n", eventLabel ? eventLabel : "hit",
              itemLabel ? itemLabel : "unknown", materialId, static_cast<unsigned int>(level), curDurability,
              maxDurability);

    if (level > 0) {
        // TODO: Apply knockback on enemy hit once a projectile-hit hook exposes the victim actor.
        Fuse::Log("[FuseDBG] RangedKnockbackTODO: event=%s item=%s mat=%d lvl=%u dura=%d/%d note=no-victim\n",
                  eventLabel ? eventLabel : "hit", itemLabel ? itemLabel : "unknown", materialId,
                  static_cast<unsigned int>(level), curDurability, maxDurability);
    }
}

extern "C" void FuseHooks_OnArrowProjectileFired(PlayState* play, int32_t isSeed) {
    (void)play;
    if (isSeed) {
        int queuedMat = static_cast<int>(MaterialId::None);
        int queuedCur = 0;
        int queuedMax = 0;
        Fuse_GetRangedQueuedStatus(RangedFuseSlot::Slingshot, &queuedMat, &queuedCur, &queuedMax);
        (void)queuedMax;
        if (queuedMat == static_cast<int>(MaterialId::None) || queuedCur <= 0) {
            Fuse::ClearActiveRangedFuse(RangedFuseSlot::Slingshot, "NoQueuedOnFire");
            return;
        }

        Fuse::CommitQueuedRangedFuse(RangedFuseSlot::Slingshot, "ArrowProjectileFired");
        LogRangedKnockbackStatus("slingshot", RangedFuseSlot::Slingshot, "fired");
        return;
    }
    Fuse::CommitQueuedRangedFuse(RangedFuseSlot::Arrows, "ArrowProjectileFired");
    LogRangedKnockbackStatus("arrows", RangedFuseSlot::Arrows, "fired");
}

extern "C" void FuseHooks_OnArrowProjectileSpawned(PlayState* play, Actor* projectile, int32_t isSeed) {
    (void)play;
    if (!projectile || projectile->id != ACTOR_EN_ARROW) {
        return;
    }

    const RangedFuseSlot slot = isSeed ? RangedFuseSlot::Slingshot : RangedFuseSlot::Arrows;
    int materialIdRaw = static_cast<int>(MaterialId::None);
    int curDurability = 0;
    int maxDurability = 0;
    Fuse_GetRangedFuseStatus(slot, &materialIdRaw, &curDurability, &maxDurability);

    if (materialIdRaw == static_cast<int>(MaterialId::None) || curDurability <= 0) {
        return;
    }

    const MaterialId materialId = static_cast<MaterialId>(materialIdRaw);
    const MaterialDef* def = Fuse::GetMaterialDef(materialId);
    if (!def) {
        return;
    }

    uint8_t burnLevel = 0;
    if (!HasModifier(def->modifiers, def->modifierCount, ModifierId::Burn, &burnLevel) || burnLevel == 0) {
        return;
    }

    if (slot == RangedFuseSlot::Arrows) {
        if (EnArrow_SetLitByFire(reinterpret_cast<EnArrow*>(projectile))) {
            Fuse::Log("[FuseDBG] BurnLitArrow: slot=%s proj=0x%04X mat=%d\n",
                      RangedSlotLabel(static_cast<RangedFuseSlotId>(slot)), projectile->id, materialIdRaw);
        }
        return;
    }

    if (slot == RangedFuseSlot::Slingshot) {
        if (EnArrow_SetFireDmgFlagsOnly(reinterpret_cast<EnArrow*>(projectile))) {
            const EnArrow* arrow = reinterpret_cast<EnArrow*>(projectile);
            Fuse::Log("[FuseDBG] BurnLitSeed: slot=%s proj=0x%04X mat=%d params=%d\n",
                      RangedSlotLabel(static_cast<RangedFuseSlotId>(slot)), projectile->id, materialIdRaw,
                      arrow->actor.params);
        }
    }
}

extern "C" void FuseHooks_OnRangedProjectileHit(PlayState* play, Actor* projectile, Actor* victim, Vec3f* impactPos,
                                                int32_t isSeed) {
    const RangedFuseSlotId slot = isSeed ? RANGED_FUSE_SLOT_SLINGSHOT : RANGED_FUSE_SLOT_ARROWS;
    const RangedFuseSlot fuseSlot = isSeed ? RangedFuseSlot::Slingshot : RangedFuseSlot::Arrows;

    if (victim && victim->category == ACTORCAT_ENEMY && projectile && projectile->id == ACTOR_EN_ARROW) {
        const EnArrow* arrow = reinterpret_cast<const EnArrow*>(projectile);
        if (((arrow->collider.info.toucher.dmgFlags & DMG_ARROW_FIRE) != 0) || (arrow->actor.params == ARROW_NORMAL_LIT)) {
            const int hp = victim->colChkInfo.health;
            const int dmg = victim->colChkInfo.damage;
            int hpEst = hp - dmg;
            if (hpEst < 0) {
                hpEst = 0;
            }
            // TEMP: TODO remove after measurement.
            Fuse::Log("[FuseDBG] ArrowHealthProbe: slot=%s projParams=%d dmgFlags=0x%08X victim=0x%04X hp=%d dmg=%d hpEst=%d\n",
                      RangedSlotLabel(slot), arrow->actor.params, arrow->collider.info.toucher.dmgFlags, victim->id,
                      hp, dmg, hpEst);
        }
    }

    Fuse::TryMarkRangedProjectileAsFire(fuseSlot, projectile, victim, "actor");
    Fuse_OnRangedHitActor(play, slot, victim, impactPos);
    Fuse::OnRangedProjectileHitFinalize(fuseSlot, "ProjectileHit");
}

extern "C" void FuseHooks_OnRangedProjectileHitSurface(PlayState* play, Actor* projectile, Vec3f* impactPos,
                                                       int32_t isSeed) {
    if (!play || !impactPos) {
        return;
    }

    const RangedFuseSlot slot = isSeed ? RangedFuseSlot::Slingshot : RangedFuseSlot::Arrows;
    Fuse::CommitQueuedRangedFuse(slot, "ProjectileSurfaceHit");
    Fuse::TryMarkRangedProjectileAsFire(slot, projectile, nullptr, "bg");
    HandleRangedSurfaceHit(play, slot, impactPos, "ProjectileSurfaceHit");
    Fuse::MarkRangedHitResolved(slot, "ProjectileSurfaceHit");
    Fuse::OnRangedProjectileHitFinalize(slot, "ProjectileSurfaceHit");
}

extern "C" void FuseHooks_OnHookshotShotStarted(PlayState* play) {
    (void)play;
    Fuse::OnHookshotShotStarted("HookshotShotStarted");
}

extern "C" void FuseHooks_OnHookshotEnemyHit(PlayState* play, Actor* victim, Vec3f* impactPos) {
    Fuse::CommitQueuedRangedFuse(RangedFuseSlot::Hookshot, "HookshotEnemyHit");
    LogRangedKnockbackStatus("hookshot", RangedFuseSlot::Hookshot, "enemy-hit");
    Fuse_OnRangedHitActor(play, RANGED_FUSE_SLOT_HOOKSHOT, victim, impactPos);
    Fuse::OnRangedProjectileHitFinalize(RangedFuseSlot::Hookshot, "HookshotEnemyHit");
}

extern "C" void FuseHooks_OnHookshotSurfaceHit(PlayState* play, Vec3f* impactPos) {
    if (!play || !impactPos) {
        return;
    }

    Fuse::CommitQueuedRangedFuse(RangedFuseSlot::Hookshot, "HookshotSurfaceHit");
    HandleRangedSurfaceHit(play, RangedFuseSlot::Hookshot, impactPos, "HookshotSurfaceHit");
    Fuse::MarkRangedHitResolved(RangedFuseSlot::Hookshot, "HookshotSurfaceHit");
    Fuse::OnRangedProjectileHitFinalize(RangedFuseSlot::Hookshot, "HookshotSurfaceHit");
}

extern "C" void FuseHooks_OnHookshotLatched(PlayState* play) {
    (void)play;
    Fuse::CommitQueuedRangedFuse(RangedFuseSlot::Hookshot, "HookshotLatched");
    LogRangedKnockbackStatus("hookshot", RangedFuseSlot::Hookshot, "latch");
    Fuse::OnRangedProjectileHitFinalize(RangedFuseSlot::Hookshot, "HookshotLatched");
}

extern "C" void FuseHooks_OnHookshotRetracted(PlayState* play) {
    (void)play;
    Fuse::OnHookshotRetractedOrKilled("HookshotRetracted");
}

extern "C" void FuseHooks_OnHookshotKilled(PlayState* play) {
    (void)play;
    Fuse::OnHookshotRetractedOrKilled("HookshotKilled");
}
