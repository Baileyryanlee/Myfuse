#include "soh/Enhancements/Fuse/Hooks/FuseHooks_Ranged.h"

#include "soh/Enhancements/Fuse/Fuse.h"

extern "C" {
#include "z64.h"
}

void Fuse_GetRangedFuseStatus(RangedFuseSlot slot, int* outMaterialId, int* outDurabilityCur, int* outDurabilityMax);

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

void FuseHooks_OnHookshotShotStarted(PlayState* play) {
    (void)play;
    Fuse::OnHookshotShotStarted("HookshotShotStarted");
}

void FuseHooks_OnHookshotEnemyHit(PlayState* play) {
    (void)play;
    Fuse::CommitQueuedRangedFuse(RangedFuseSlot::Hookshot, "HookshotEnemyHit");
    LogRangedKnockbackStatus("hookshot", RangedFuseSlot::Hookshot, "enemy-hit");
}

void FuseHooks_OnHookshotLatched(PlayState* play) {
    (void)play;
    Fuse::CommitQueuedRangedFuse(RangedFuseSlot::Hookshot, "HookshotLatched");
    LogRangedKnockbackStatus("hookshot", RangedFuseSlot::Hookshot, "latch");
}

void FuseHooks_OnHookshotRetracted(PlayState* play) {
    (void)play;
    Fuse::OnHookshotRetractedOrKilled("HookshotRetracted");
}

void FuseHooks_OnHookshotKilled(PlayState* play) {
    (void)play;
    Fuse::OnHookshotRetractedOrKilled("HookshotKilled");
}
