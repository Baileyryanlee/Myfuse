#include "soh/Enhancements/Fuse/Hooks/FuseHooks_Ranged.h"

#include "soh/Enhancements/Fuse/Fuse.h"
#include "soh/Enhancements/Fuse/ShieldBashRules.h"

extern "C" {
#include "z64.h"
}

void Fuse_GetRangedFuseStatus(RangedFuseSlot slot, int* outMaterialId, int* outDurabilityCur, int* outDurabilityMax);

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

void Fuse_OnRangedHitActor(PlayState* play, RangedFuseSlotId slot, Actor* victim) {
    if (!play || !victim) {
        return;
    }

    if (!FuseBash_IsEnemyActor(victim)) {
        return;
    }

    int materialIdRaw = static_cast<int>(MaterialId::None);
    int curDurability = 0;
    int maxDurability = 0;
    Fuse_GetRangedFuseStatus(static_cast<RangedFuseSlot>(slot), &materialIdRaw, &curDurability, &maxDurability);
    (void)maxDurability;

    Fuse::Log("[FuseDBG] RangedHit slot=%s mat=%d victim=%p id=0x%04X\n", RangedSlotLabel(slot), materialIdRaw,
              (void*)victim, victim->id);

    if (materialIdRaw == static_cast<int>(MaterialId::None) || curDurability <= 0) {
        return;
    }

    const MaterialId materialId = static_cast<MaterialId>(materialIdRaw);
    const MaterialDef* def = Fuse::GetMaterialDef(materialId);
    if (!def) {
        return;
    }

    uint8_t stunLevel = 0;
    if (HasModifier(def->modifiers, def->modifierCount, ModifierId::Stun, &stunLevel) && stunLevel > 0) {
        Fuse_TriggerDekuNutAtPos(play, victim->world.pos, RangedSlotItemId(slot));
    }

    uint8_t freezeLevel = 0;
    if (HasModifier(def->modifiers, def->modifierCount, ModifierId::Freeze, &freezeLevel) && freezeLevel > 0) {
        Fuse::EnqueueFreezeRequest(play, victim, freezeLevel, "ranged", RangedSlotLabel(slot), materialId);
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

void FuseHooks_OnArrowProjectileFired(PlayState* play, int32_t isSeed) {
    (void)play;
    if (isSeed) {
        Fuse::CommitQueuedRangedFuse(RangedFuseSlot::Slingshot, "ArrowProjectileFired");
        LogRangedKnockbackStatus("slingshot", RangedFuseSlot::Slingshot, "fired");
        return;
    }
    Fuse::CommitQueuedRangedFuse(RangedFuseSlot::Arrows, "ArrowProjectileFired");
    LogRangedKnockbackStatus("arrows", RangedFuseSlot::Arrows, "fired");
}

void FuseHooks_OnRangedProjectileHit(PlayState* play, Actor* victim, int32_t isSeed) {
    if (isSeed) {
        Fuse_OnRangedHitActor(play, RANGED_FUSE_SLOT_SLINGSHOT, victim);
        Fuse::OnRangedProjectileHitFinalize(RangedFuseSlot::Slingshot, "ProjectileHit");
        return;
    }

    Fuse_OnRangedHitActor(play, RANGED_FUSE_SLOT_ARROWS, victim);
    Fuse::OnRangedProjectileHitFinalize(RangedFuseSlot::Arrows, "ProjectileHit");
}

void FuseHooks_OnHookshotShotStarted(PlayState* play) {
    (void)play;
    Fuse::OnHookshotShotStarted("HookshotShotStarted");
}

void FuseHooks_OnHookshotEnemyHit(PlayState* play, Actor* victim) {
    Fuse::CommitQueuedRangedFuse(RangedFuseSlot::Hookshot, "HookshotEnemyHit");
    LogRangedKnockbackStatus("hookshot", RangedFuseSlot::Hookshot, "enemy-hit");
    Fuse_OnRangedHitActor(play, RANGED_FUSE_SLOT_HOOKSHOT, victim);
    Fuse::OnRangedProjectileHitFinalize(RangedFuseSlot::Hookshot, "HookshotEnemyHit");
}

void FuseHooks_OnHookshotLatched(PlayState* play) {
    (void)play;
    Fuse::CommitQueuedRangedFuse(RangedFuseSlot::Hookshot, "HookshotLatched");
    LogRangedKnockbackStatus("hookshot", RangedFuseSlot::Hookshot, "latch");
    Fuse::OnRangedProjectileHitFinalize(RangedFuseSlot::Hookshot, "HookshotLatched");
}

void FuseHooks_OnHookshotRetracted(PlayState* play) {
    (void)play;
    Fuse::OnHookshotRetractedOrKilled("HookshotRetracted");
}

void FuseHooks_OnHookshotKilled(PlayState* play) {
    (void)play;
    Fuse::OnHookshotRetractedOrKilled("HookshotKilled");
}
