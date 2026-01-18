#include "Fuse.h"
#include "FuseMaterials.h"
#include "FuseState.h"
#include "soh/Enhancements/Fuse/Hooks/FuseHooks_Objects.h"
#include "soh/Enhancements/Fuse/ShieldBashRules.h"
#include "soh/SaveManager.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

extern "C" {
#include "z64.h"
#include "variables.h"
#include "macros.h"
#include "functions.h"
}
extern "C" PlayState* gPlayState;
using Fuse::MaterialDebugOverride;

// -----------------------------------------------------------------------------
// Module-local state
// -----------------------------------------------------------------------------
static FuseSaveData gFuseSave; // persistent-ready (not serialized yet)
static FuseRuntimeState gFuseRuntime;
static bool sSwordSlotsLoadedFromSaveManager = false;
static bool sHammerSlotLoadedFromSaveManager = false;
static FuseSlot sLoadedHammerSlot;
static std::unordered_map<MaterialId, uint16_t> sMaterialInventory;
static bool sMaterialInventoryInitialized = false;
static constexpr size_t kSwordFreezeQueueCount = 2;
static std::unordered_map<MaterialId, MaterialDebugOverride> sMaterialDebugOverrides;
static bool sUseDebugOverrides = false;
static std::unordered_map<Actor*, s16> sFuseFrozenTimers;
static std::unordered_map<Actor*, int> sFreezeAppliedFrame;
static std::unordered_map<Actor*, int> sShatterFrame;
static std::unordered_map<Actor*, Vec3f> sFuseFrozenPos;

struct SwordFreezeRequest {
    Actor* victim = nullptr;
    uint8_t level = 0;
};

static std::array<std::vector<SwordFreezeRequest>, kSwordFreezeQueueCount> sSwordFreezeQueues;
static std::array<std::unordered_set<Actor*>, kSwordFreezeQueueCount> sSwordFreezeVictims;
static std::array<int, kSwordFreezeQueueCount> sSwordFreezeQueueFrames = { -1, -1 };

static bool IsFuseFrozen(Actor* actor) {
    if (actor == nullptr) {
        return false;
    }

    auto it = sFuseFrozenTimers.find(actor);
    return it != sFuseFrozenTimers.end() && it->second > 0;
}

static void ClearFuseFreeze(Actor* actor) {
    if (!actor) {
        return;
    }

    sFuseFrozenTimers.erase(actor);
    sFreezeAppliedFrame.erase(actor);
    sShatterFrame.erase(actor);
    sFuseFrozenPos.erase(actor);
    actor->colorFilterTimer = 0;
}

static void ResetSavedSwordFuseFields() {
    FusePersistence::WriteSwordStateToContext(FusePersistence::ClearedSwordState());
}

// -----------------------------------------------------------------------------
// Modifier helpers (module-local)
// -----------------------------------------------------------------------------
namespace {

int GetDekuNutAmmoCount() {
    return AMMO(ITEM_NUT);
}

bool ConsumeDekuNutAmmo(int amount) {
    if (amount <= 0) {
        return true;
    }

    const int cur = GetDekuNutAmmoCount();
    Fuse::Log("[FuseMVP] Consume DekuNut: cur=%d amount=%d\n", cur, amount);

    if (cur < amount) {
        return false;
    }

    const int newCount = std::max(0, cur - amount);
    const int delta = newCount - cur;

    if (delta != 0) {
        Inventory_ChangeAmmo(ITEM_NUT, delta);
    }

    Fuse::Log("[FuseMVP] Consume DekuNut: new=%d\n", newCount);
    return true;
}

Actor* SpawnDekuNutFlash(PlayState* play, const Vec3f& pos) {
    if (!play) {
        return nullptr;
    }

    iREG(50) = -1;
    return Actor_Spawn(&play->actorCtx, play, ACTOR_EN_M_FIRE1, pos.x, pos.y, pos.z, 0, 0, 0, 0, true);
}

void ApplyDekuNutStunVanilla(PlayState* play, Player* player, Actor* victim, uint8_t level) {
    (void)player;

    if (!play || !victim || level == 0) {
        return;
    }

    Vec3f spawnPos = victim->world.pos;
    Fuse::Log("[FuseMVP] DekuNut stun: using vanilla nut effect frame=%d victim=%p\n", play->gameplayFrames,
              (void*)victim);

    Actor* flashActor = SpawnDekuNutFlash(play, spawnPos);

    if (flashActor) {
        Fuse::Log("[FuseMVP] DekuNut stun: spawned actor id=0x%04X ptr=%p\n", flashActor->id, (void*)flashActor);
        SoundSource_PlaySfxAtFixedWorldPos(play, &spawnPos, 20, NA_SE_IT_DEKU);
        EffectSsStone1_Spawn(play, &spawnPos, 0);
    } else {
        Fuse::Log("[FuseMVP] DekuNut stun: spawn failed\n");
    }
}

bool IsVanillaMaterial(MaterialId id) {
    return id == MaterialId::DekuNut;
}

bool IsCustomMaterial(MaterialId id) {
    return id != MaterialId::None && !IsVanillaMaterial(id);
}

bool IsSupportedCustomMaterial(MaterialId id) {
    return IsCustomMaterial(id) && Fuse::GetMaterialDef(id) != nullptr;
}

bool IsDefaultOverride(const MaterialDebugOverride& override) {
    return override.attackBonusDelta == 0 && override.baseDurabilityOverride == -1;
}

MaterialDebugOverride& EnsureMaterialOverride(MaterialId id) {
    return sMaterialDebugOverrides[id];
}

void EnsureMaterialInventoryInitialized() {
    if (!sMaterialInventoryInitialized) {
        sMaterialInventory.clear();
        sMaterialInventoryInitialized = true;
    }
}

uint16_t GetStoredMaterialCount(MaterialId id) {
    EnsureMaterialInventoryInitialized();
    auto it = sMaterialInventory.find(id);
    return (it != sMaterialInventory.end()) ? it->second : 0;
}

void SetStoredMaterialCount(MaterialId id, int amount) {
    if (!IsCustomMaterial(id)) {
        return;
    }

    EnsureMaterialInventoryInitialized();
    const uint16_t clamped = static_cast<uint16_t>(std::clamp(amount, 0, 65535));
    sMaterialInventory[id] = clamped;
}

std::vector<std::pair<MaterialId, uint16_t>> BuildCustomMaterialInventorySnapshot() {
    EnsureMaterialInventoryInitialized();
    std::vector<std::pair<MaterialId, uint16_t>> entries;

    for (const auto& kvp : sMaterialInventory) {
        if (IsCustomMaterial(kvp.first) && kvp.second > 0) {
            entries.push_back({ kvp.first, kvp.second });
        }
    }

    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        return static_cast<uint16_t>(a.first) < static_cast<uint16_t>(b.first);
    });

    return entries;
}

void ApplyIceArrowFreeze(PlayState* play, Actor* victim, uint8_t level) {
    if (!victim || level == 0) {
        return;
    }

    constexpr s16 kBaseFreezeDuration = 40;
    const s16 duration = static_cast<s16>(kBaseFreezeDuration * level);
    constexpr s16 kIceColorFlagBlue = 0;        // Default flag yields the blue ice arrow tint (see z64actor.h)
    constexpr s16 kNeutralColorIntensity = 180; // Softer tint to look more snow/white than deep blue

    // Apply the same immobilization and visual feedback that Ice Arrows use
    sFuseFrozenTimers[victim] = std::max<s16>(sFuseFrozenTimers[victim], duration);
    Actor_SetColorFilter(victim, kIceColorFlagBlue, kNeutralColorIntensity, 0, duration);

    if (play != nullptr) {
        sFreezeAppliedFrame[victim] = play->gameplayFrames;
        constexpr s16 kPrim = 150;
        constexpr s16 kEnvPrim = 250;
        constexpr s16 kEnvSecondary = 235;
        constexpr s16 kEnvTertiary = 245;
        const float scale = 1.0f + (0.25f * (level - 1));

        Vec3f center = victim->focus.pos;
        const bool hasFocus = !(center.x == 0.0f && center.y == 0.0f && center.z == 0.0f);

        if (!hasFocus) {
            center = victim->world.pos;
            center.y += 40.0f;
        }

        const int shardCount = Rand_S16Offset(3, 3);

        for (int i = 0; i < shardCount; i++) {
            Vec3f spawnPos = center;
            spawnPos.x += Rand_CenteredFloat(20.0f);
            spawnPos.y += Rand_CenteredFloat(10.0f);
            spawnPos.z += Rand_CenteredFloat(20.0f);

            EffectSsEnIce_SpawnFlyingVec3f(play, victim, &spawnPos, kPrim, kPrim, kPrim, kEnvPrim, kEnvSecondary,
                                           kEnvTertiary, 255, scale);
        }
    }

    Fuse::Log("[FuseDBG] FreezeApply: victim=%p duration=%d mat=FrozenShard\n", (void*)victim, duration);
}

void ApplyFuseKnockback(PlayState* play, Player* player, Actor* victim, uint8_t level, const char* itemLabel,
                        MaterialId materialId, int curDurability, int maxDurability, const char* eventLabel) {
    if (!play || !player || !victim || level == 0) {
        return;
    }

    if (victim->id == ACTOR_EN_SKB) {
        Fuse::Log("[FuseDBG] knockback_skip_blacklist: event=%s item=%s victim=%p id=0x%04X\n",
                  eventLabel ? eventLabel : "hit", itemLabel ? itemLabel : "unknown", (void*)victim, victim->id);
        return;
    }

    if (!FuseBash_IsEnemyActor(victim)) {
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

    Fuse::Log("[FuseDBG] Knockback: event=%s item=%s mat=%d lvl=%u victim=%p dura=%d/%d v=(%.2f,%.2f,%.2f)\n",
              eventLabel ? eventLabel : "hit", itemLabel ? itemLabel : "unknown", static_cast<int>(materialId),
              static_cast<unsigned int>(level), (void*)victim, curDurability, maxDurability, victim->velocity.x,
              victim->velocity.y, victim->velocity.z);
}

const char* RangedSlotName(RangedFuseSlot slot) {
    switch (slot) {
        case RangedFuseSlot::Arrows:
            return "Arrows";
        case RangedFuseSlot::Slingshot:
            return "Slingshot";
        case RangedFuseSlot::Hookshot:
            return "Hookshot";
        default:
            return "Unknown";
    }
}

FuseSlot& GetRangedFuseSlot(RangedFuseSlot slot) {
    switch (slot) {
        case RangedFuseSlot::Arrows:
            return gFuseRuntime.GetActiveArrowsSlot(nullptr);
        case RangedFuseSlot::Slingshot:
            return gFuseRuntime.GetActiveSlingshotSlot(nullptr);
        case RangedFuseSlot::Hookshot:
            return gFuseRuntime.GetActiveHookshotSlot(nullptr);
    }
    return gFuseRuntime.GetActiveArrowsSlot(nullptr);
}

void ApplyRangedFuseSlotMaterial(RangedFuseSlot slot, MaterialId mat) {
    if (mat == MaterialId::None) {
        GetRangedFuseSlot(slot).ResetToUnfused();
        return;
    }

    switch (slot) {
        case RangedFuseSlot::Arrows:
            Fuse::FuseArrowsWithMaterial(mat, Fuse::GetMaterialEffectiveBaseDurability(mat));
            return;
        case RangedFuseSlot::Slingshot:
            Fuse::FuseSlingshotWithMaterial(mat, Fuse::GetMaterialEffectiveBaseDurability(mat));
            return;
        case RangedFuseSlot::Hookshot:
            Fuse::FuseHookshotWithMaterial(mat, Fuse::GetMaterialEffectiveBaseDurability(mat));
            return;
    }
}

int GetGameplayFrame() {
    return gPlayState ? gPlayState->gameplayFrames : -1;
}

void LogRangedEvent(const char* tag, RangedFuseSlot slot, MaterialId mat, const char* reason) {
    const int matId = static_cast<int>(mat);
    const int count = (mat != MaterialId::None) ? Fuse::GetMaterialCount(mat) : -1;
    Fuse::Log("[FuseDBG] %s: slot=%s mat=%d count=%d reason=%s\n", tag, RangedSlotName(slot), matId, count,
              reason ? reason : "None");
}

bool IsPlayerAimingRangedSlot(PlayState* play, RangedFuseSlot* outSlot) {
    if (!play) {
        return false;
    }

    Player* player = GET_PLAYER(play);
    if (!player) {
        return false;
    }

    if ((player->stateFlags1 & PLAYER_STATE1_READY_TO_FIRE) == 0) {
        return false;
    }

    switch (player->heldItemAction) {
        case PLAYER_IA_BOW:
        case PLAYER_IA_BOW_FIRE:
        case PLAYER_IA_BOW_ICE:
        case PLAYER_IA_BOW_LIGHT:
        case PLAYER_IA_BOW_0C:
        case PLAYER_IA_BOW_0D:
        case PLAYER_IA_BOW_0E:
            if (outSlot) {
                *outSlot = RangedFuseSlot::Arrows;
            }
            return true;
        case PLAYER_IA_SLINGSHOT:
            if (outSlot) {
                *outSlot = RangedFuseSlot::Slingshot;
            }
            return true;
        case PLAYER_IA_HOOKSHOT:
        case PLAYER_IA_LONGSHOT:
            if (outSlot) {
                *outSlot = RangedFuseSlot::Hookshot;
            }
            return true;
        default:
            break;
    }

    return false;
}

bool HeldItemActionToSlot(int32_t heldAction, RangedFuseSlot* outSlot) {
    switch (heldAction) {
        case PLAYER_IA_BOW:
        case PLAYER_IA_BOW_FIRE:
        case PLAYER_IA_BOW_ICE:
        case PLAYER_IA_BOW_LIGHT:
        case PLAYER_IA_BOW_0C:
        case PLAYER_IA_BOW_0D:
        case PLAYER_IA_BOW_0E:
            if (outSlot) {
                *outSlot = RangedFuseSlot::Arrows;
            }
            return true;
        case PLAYER_IA_SLINGSHOT:
            if (outSlot) {
                *outSlot = RangedFuseSlot::Slingshot;
            }
            return true;
        case PLAYER_IA_HOOKSHOT:
        case PLAYER_IA_LONGSHOT:
            if (outSlot) {
                *outSlot = RangedFuseSlot::Hookshot;
            }
            return true;
        default:
            return false;
    }
}

static bool IsActorAliveInPlay(PlayState* play, Actor* target) {
    if (!play || !target) {
        return false;
    }

    for (int i = 0; i < ACTORCAT_MAX; ++i) {
        Actor* a = play->actorCtx.actorLists[i].head;
        while (a != nullptr) {
            if (a == target) {
                return true;
            }
            a = a->next;
        }
    }

    return false;
}

static void TickFuseFrozenTimers(PlayState* play) {
    for (auto it = sFuseFrozenTimers.begin(); it != sFuseFrozenTimers.end();) {
        Actor* actor = it->first;
        s16& timer = it->second;

        if (!IsActorAliveInPlay(play, actor)) {
            sFreezeAppliedFrame.erase(actor);
            sShatterFrame.erase(actor);
            sFuseFrozenPos.erase(actor);
            it = sFuseFrozenTimers.erase(it);
            continue;
        }

        if (timer > 0) {
            timer--;
        }

        if (timer <= 0) {
            ++it;
            ClearFuseFreeze(actor);
        } else {
            auto shatterIt = sShatterFrame.find(actor);
            if (play != nullptr && shatterIt != sShatterFrame.end() && shatterIt->second == play->gameplayFrames) {
                ++it;
                continue;
            }

            actor->velocity.x = 0.0f;
            actor->velocity.y = 0.0f;
            actor->velocity.z = 0.0f;
            actor->speedXZ = 0.0f;
            actor->gravity = 0.0f;

            auto posIt = sFuseFrozenPos.find(actor);
            if (posIt == sFuseFrozenPos.end()) {
                sFuseFrozenPos[actor] = actor->world.pos;
            } else {
                actor->world.pos = posIt->second;
            }
            ++it;
        }
    }
}

void ResetSwordFreezeQueueInternal() {
    for (size_t i = 0; i < kSwordFreezeQueueCount; i++) {
        sSwordFreezeQueues[i].clear();
        sSwordFreezeVictims[i].clear();
        sSwordFreezeQueueFrames[i] = -1;
    }
}

void EnqueueSwordFreezeRequest(PlayState* play, Actor* victim, uint8_t level) {
    if (!play || !victim || level == 0) {
        return;
    }

    const int curFrame = play->gameplayFrames;
    const size_t queueIndex = static_cast<size_t>(curFrame) % kSwordFreezeQueueCount;

    if (sSwordFreezeQueueFrames[queueIndex] != curFrame) {
        sSwordFreezeQueues[queueIndex].clear();
        sSwordFreezeVictims[queueIndex].clear();
        sSwordFreezeQueueFrames[queueIndex] = curFrame;
    }

    if (sSwordFreezeVictims[queueIndex].count(victim) > 0) {
        return;
    }

    sSwordFreezeVictims[queueIndex].insert(victim);
    sSwordFreezeQueues[queueIndex].push_back({ victim, level });
}

} // namespace

// -----------------------------------------------------------------------------
// Logging
// -----------------------------------------------------------------------------
void Fuse::Log(const char* fmt, ...) {
    char buf[1024];

    va_list args;
    va_start(args, fmt);
#ifdef _WIN32
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, args);
#else
    vsnprintf(buf, sizeof(buf), fmt, args);
#endif
    va_end(args);

#ifdef _WIN32
    OutputDebugStringA(buf);
#endif

    fputs(buf, stdout);
    fflush(stdout);
}

// -----------------------------------------------------------------------------
// Save synchronization helpers (equipped sword only)
// -----------------------------------------------------------------------------
void Fuse_ClearSavedSwordFuse(const PlayState* play) {
    (void)play;

    ResetSavedSwordFuseFields();
    Fuse::ClearSwordFuse();
    gFuseRuntime.swordFuseLoadedFromSave = false;
}

void Fuse_WriteSwordFuseToSave(const PlayState* play) {
    (void)play;

    const FuseSwordSaveState state = FusePersistence::BuildRuntimeSwordState();
    FusePersistence::WriteSwordStateToContext(state);
}

void Fuse_ApplySavedSwordFuse(const PlayState* play, s16 savedMaterial, s16 savedMaxDur, bool hasSavedCurDurability,
                              u16 savedCurDurability, s16 legacyCurDur) {
    (void)play;

    if (savedMaterial == FusePersistence::kSwordMaterialIdNone) {
        Fuse::ClearSwordFuse();
        return;
    }

    const MaterialId materialId = static_cast<MaterialId>(std::max<s16>(0, savedMaterial));
    const MaterialDef* def = Fuse::GetMaterialDef(materialId);

    if (!def) {
        Fuse_ClearSavedSwordFuse(play);
        return;
    }

    int maxDurability = savedMaxDur;
    if (maxDurability <= 0) {
        maxDurability = Fuse::GetMaterialEffectiveBaseDurability(materialId);
    }

    if (maxDurability <= 0) {
        Fuse_ClearSavedSwordFuse(play);
        return;
    }

    uint16_t targetCur = 0;
    if (hasSavedCurDurability) {
        targetCur = static_cast<uint16_t>(std::clamp<int>(savedCurDurability, 0, maxDurability));
    } else if (legacyCurDur > 0) {
        targetCur = static_cast<uint16_t>(std::clamp<int>(legacyCurDur, 0, maxDurability));
    } else {
        targetCur = static_cast<uint16_t>(maxDurability);
    }

    if (targetCur == 0) {
        Fuse_ClearSavedSwordFuse(play);
        return;
    }

    Fuse::FuseSwordWithMaterial(materialId, static_cast<uint16_t>(maxDurability), false, false);
    Fuse::SetSwordFuseDurability(targetCur);
    gFuseRuntime.swordFuseLoadedFromSave = true;
    Fuse::SetLastEvent("Sword fuse restored from save");

    Fuse::Log("[FuseMVP] Sword fused with material=%d (durability %u/%u)\n", static_cast<int>(materialId),
              static_cast<unsigned int>(Fuse::GetSwordFuseDurability()),
              static_cast<unsigned int>(Fuse::GetSwordFuseMaxDurability()));
    Fuse::Log("[FuseDBG] Applied saved fuse matId=%d cur=%d max=%d\n", static_cast<int>(materialId), targetCur,
              Fuse::GetSwordFuseMaxDurability());
}

// -----------------------------------------------------------------------------
// Debug helpers
// -----------------------------------------------------------------------------
const char* Fuse::GetLastEvent() {
    return gFuseRuntime.lastEvent;
}

void Fuse::SetLastEvent(const char* msg) {
    gFuseRuntime.lastEvent = msg ? msg : "None";
}

// -----------------------------------------------------------------------------
// Core state API
// -----------------------------------------------------------------------------
bool Fuse::IsEnabled() {
    return gFuseRuntime.enabled;
}

void Fuse::SetEnabled(bool enabled) {
    gFuseRuntime.enabled = enabled;
}

const MaterialDef* Fuse::GetMaterialDef(MaterialId id) {
    return FuseMaterials::GetMaterialDef(id);
}

const MaterialDef* Fuse::GetMaterialDefs(size_t* count) {
    return FuseMaterials::GetMaterialDefs(count);
}

uint16_t Fuse::GetMaterialBaseDurability(MaterialId id) {
    const MaterialDef* def = Fuse::GetMaterialDef(id);
    return def ? def->baseMaxDurability : 0;
}

extern "C" bool Fuse_ShieldHasNegateKnockback(PlayState* play, int* outMaterialId, int* outDurabilityCur,
                                              int* outDurabilityMax, uint8_t* outLevel) {
    if (outMaterialId) {
        *outMaterialId = static_cast<int>(MaterialId::None);
    }
    if (outDurabilityCur) {
        *outDurabilityCur = 0;
    }
    if (outDurabilityMax) {
        *outDurabilityMax = 0;
    }
    if (outLevel) {
        *outLevel = 0;
    }

    if (!play) {
        return false;
    }

    const FuseSlot& slot = gFuseSave.GetActiveShieldSlot(play);
    if (slot.materialId == MaterialId::None || slot.durabilityCur <= 0) {
        return false;
    }

    if (outMaterialId) {
        *outMaterialId = static_cast<int>(slot.materialId);
    }
    if (outDurabilityCur) {
        *outDurabilityCur = slot.durabilityCur;
    }
    if (outDurabilityMax) {
        *outDurabilityMax = slot.durabilityMax;
    }

    const MaterialDef* def = Fuse::GetMaterialDef(slot.materialId);
    if (!def) {
        return false;
    }

    uint8_t level = 0;
    if (!HasModifier(def->modifiers, def->modifierCount, ModifierId::NegateKnockback, &level) || level == 0) {
        return false;
    }

    if (outLevel) {
        *outLevel = level;
    }

    return true;
}

extern "C" void Fuse_ShieldGuardDrain(PlayState* play) {
    if (!play) {
        return;
    }

    const int32_t equipValue = (static_cast<int32_t>(gSaveContext.equips.equipment & gEquipMasks[EQUIP_TYPE_SHIELD]) >>
                                gEquipShifts[EQUIP_TYPE_SHIELD]);
    if (!IsShieldEquipValue(equipValue)) {
        return;
    }

    const ShieldSlotKey key = ShieldSlotKeyFromEquipValue(equipValue);
    FuseSlot& slot = gFuseSave.GetShieldSlot(key);
    if (slot.materialId == MaterialId::None || slot.durabilityCur <= 0) {
        return;
    }

    const int materialId = static_cast<int>(slot.materialId);
    const int maxDurability = slot.durabilityMax;
    const int newCur = std::max(0, slot.durabilityCur - 1);
    slot.durabilityCur = newCur;

    if (newCur <= 0) {
        slot.ResetToUnfused();
    }

    Fuse::Log("[FuseDBG] shield_guard_drain: shield=%s mat=%d dura=%d/%d\n", ShieldSlotName(key), materialId, newCur,
              maxDurability);
}

void Fuse_GetRangedFuseStatus(RangedFuseSlot slot, int* outMaterialId, int* outDurabilityCur, int* outDurabilityMax) {
    if (outMaterialId) {
        *outMaterialId = static_cast<int>(MaterialId::None);
    }
    if (outDurabilityCur) {
        *outDurabilityCur = 0;
    }
    if (outDurabilityMax) {
        *outDurabilityMax = 0;
    }

    const FuseSlot* active = nullptr;
    switch (slot) {
        case RangedFuseSlot::Arrows:
            active = &gFuseRuntime.GetActiveArrowsSlot(nullptr);
            break;
        case RangedFuseSlot::Slingshot:
            active = &gFuseRuntime.GetActiveSlingshotSlot(nullptr);
            break;
        case RangedFuseSlot::Hookshot:
            active = &gFuseRuntime.GetActiveHookshotSlot(nullptr);
            break;
        default:
            break;
    }

    if (!active) {
        return;
    }

    if (outMaterialId) {
        *outMaterialId = static_cast<int>(active->materialId);
    }
    if (outDurabilityCur) {
        *outDurabilityCur = active->durabilityCur;
    }
    if (outDurabilityMax) {
        *outDurabilityMax = active->durabilityMax;
    }
}

static int GetMaterialBaseAttackBonus(MaterialId id) {
    // Attack bonus defaults are currently implicit (zero) until defined in MaterialDef.
    switch (id) {
        case MaterialId::Rock:
            return 1;
        case MaterialId::None:
        case MaterialId::DekuNut:
        case MaterialId::FrozenShard:
        default:
            return 0;
    }
}

int Fuse::GetMaterialAttackBonus(MaterialId id) {
    const int base = GetMaterialBaseAttackBonus(id);
    if (!sUseDebugOverrides) {
        return base;
    }

    return base + Fuse::GetMaterialAttackBonusDelta(id);
}

int Fuse::GetMaterialDurabilityOverride(MaterialId id) {
    auto it = sMaterialDebugOverrides.find(id);
    if (it == sMaterialDebugOverrides.end()) {
        return -1;
    }

    return it->second.baseDurabilityOverride;
}

int Fuse::GetMaterialEffectiveBaseDurability(MaterialId id) {
    const int base = static_cast<int>(Fuse::GetMaterialBaseDurability(id));
    if (!sUseDebugOverrides) {
        return base;
    }

    const int overrideValue = Fuse::GetMaterialDurabilityOverride(id);
    return overrideValue >= 0 ? overrideValue : base;
}

int Fuse::GetMaterialAttackBonusDelta(MaterialId id) {
    auto it = sMaterialDebugOverrides.find(id);
    if (it == sMaterialDebugOverrides.end()) {
        return 0;
    }

    return it->second.attackBonusDelta;
}

void Fuse::SetMaterialAttackBonusDelta(MaterialId id, int v) {
    MaterialDebugOverride& entry = EnsureMaterialOverride(id);
    entry.attackBonusDelta = v;

    if (IsDefaultOverride(entry)) {
        sMaterialDebugOverrides.erase(id);
    }
}

void Fuse::SetMaterialBaseDurabilityOverride(MaterialId id, int v) {
    MaterialDebugOverride& entry = EnsureMaterialOverride(id);
    entry.baseDurabilityOverride = v;

    if (IsDefaultOverride(entry)) {
        sMaterialDebugOverrides.erase(id);
    }
}

void Fuse::ResetMaterialOverride(MaterialId id) {
    sMaterialDebugOverrides.erase(id);
}

void Fuse::ResetAllMaterialOverrides() {
    sMaterialDebugOverrides.clear();
}

void Fuse::SetUseDebugOverrides(bool enabled) {
    sUseDebugOverrides = enabled;
}

bool Fuse::GetUseDebugOverrides() {
    return sUseDebugOverrides;
}

void Fuse::LoadDebugOverrides() {
    sMaterialDebugOverrides.clear();
    int enabledInt = 0;

    SaveManager::Instance->LoadStruct("enhancements.fuse.debugOverrides", [&]() {
        SaveManager::Instance->LoadData("enabled", enabledInt, 0);

        SaveManager::Instance->LoadStruct("materials", [&]() {
            size_t defCount = 0;
            const MaterialDef* defs = Fuse::GetMaterialDefs(&defCount);

            for (size_t i = 0; i < defCount; i++) {
                const MaterialId id = defs[i].id;
                const std::string key = std::to_string(static_cast<int>(id));

                SaveManager::Instance->LoadStruct(key, [&]() {
                    int attackDelta = 0;
                    int durabilityOverride = -1;
                    SaveManager::Instance->LoadData("attackBonusDelta", attackDelta, 0);
                    SaveManager::Instance->LoadData("baseDurabilityOverride", durabilityOverride, -1);

                    if (attackDelta != 0 || durabilityOverride != -1) {
                        MaterialDebugOverride entry{};
                        entry.attackBonusDelta = attackDelta;
                        entry.baseDurabilityOverride = durabilityOverride;
                        sMaterialDebugOverrides[id] = entry;

                        Fuse::Log("[FuseDBG] OverrideLoad: mat=%d atkDelta=%d duraOvr=%d\n", static_cast<int>(id),
                                  attackDelta, durabilityOverride);
                    }
                });
            }
        });
    });

    sUseDebugOverrides = enabledInt != 0;
    Fuse::Log("[FuseDBG] OverrideLoad: enabled=%d\n", sUseDebugOverrides ? 1 : 0);
}

void Fuse::SaveDebugOverrides() {
    SaveManager::Instance->SaveStruct("enhancements.fuse.debugOverrides", [&]() {
        SaveManager::Instance->SaveData("enabled", sUseDebugOverrides ? 1 : 0);

        SaveManager::Instance->SaveStruct("materials", [&]() {
            for (const auto& kvp : sMaterialDebugOverrides) {
                if (IsDefaultOverride(kvp.second)) {
                    continue;
                }

                const std::string key = std::to_string(static_cast<int>(kvp.first));
                SaveManager::Instance->SaveStruct(key, [&]() {
                    if (kvp.second.attackBonusDelta != 0) {
                        SaveManager::Instance->SaveData("attackBonusDelta", kvp.second.attackBonusDelta);
                    }
                    if (kvp.second.baseDurabilityOverride != -1) {
                        SaveManager::Instance->SaveData("baseDurabilityOverride", kvp.second.baseDurabilityOverride);
                    }
                });

                Fuse::Log("[FuseDBG] OverrideSave: mat=%d atkDelta=%d duraOvr=%d\n", static_cast<int>(kvp.first),
                          kvp.second.attackBonusDelta, kvp.second.baseDurabilityOverride);
            }
        });
    });

    Fuse::Log("[FuseDBG] OverrideSave: enabled=%d\n", sUseDebugOverrides ? 1 : 0);
}

uint8_t Fuse::GetSwordModifierLevel(ModifierId id) {
    if (!Fuse::IsSwordFused()) {
        return 0;
    }

    const MaterialDef* def = Fuse::GetMaterialDef(Fuse::GetSwordMaterial());
    if (!def) {
        return 0;
    }

    uint8_t level = 0;
    return HasModifier(def->modifiers, def->modifierCount, id, &level) ? level : 0;
}

bool Fuse::SwordHasModifier(ModifierId id) {
    return Fuse::GetSwordModifierLevel(id) > 0;
}

int Fuse::GetMaterialCount(MaterialId id) {
    if (IsVanillaMaterial(id)) {
        return GetDekuNutAmmoCount();
    }

    if (!IsCustomMaterial(id)) {
        return 0;
    }

    return GetStoredMaterialCount(id);
}

void Fuse::SetMaterialCount(MaterialId id, int amount) {
    if (IsVanillaMaterial(id)) {
        return;
    }

    SetStoredMaterialCount(id, amount);
}

bool Fuse::HasMaterial(MaterialId id, int amount) {
    if (amount <= 0) {
        return true;
    }
    return GetMaterialCount(id) >= amount;
}

void Fuse::AddMaterial(MaterialId id, int amount) {
    if (amount <= 0) {
        return;
    }

    if (IsVanillaMaterial(id)) {
        return;
    }

    const int newCount = std::clamp<int>(GetStoredMaterialCount(id) + amount, 0, 65535);
    SetStoredMaterialCount(id, newCount);
}

bool Fuse::ConsumeMaterial(MaterialId id, int amount) {
    if (amount <= 0) {
        return true;
    }

    if (!HasMaterial(id, amount)) {
        return false;
    }

    if (IsVanillaMaterial(id)) {
        return ConsumeDekuNutAmmo(amount);
    }

    const int newCount = std::max(0, GetStoredMaterialCount(id) - amount);
    SetStoredMaterialCount(id, newCount);
    return true;
}

bool Fuse::HasRockMaterial() {
    return HasMaterial(MaterialId::Rock);
}

int Fuse::GetRockCount() {
    return GetMaterialCount(MaterialId::Rock);
}

std::vector<std::pair<MaterialId, uint16_t>> Fuse::GetCustomMaterialInventory() {
    return BuildCustomMaterialInventorySnapshot();
}

void Fuse::ApplyCustomMaterialInventory(const std::vector<std::pair<MaterialId, uint16_t>>& entries) {
    ClearMaterialInventory();

    for (const auto& entry : entries) {
        if (entry.second == 0) {
            continue;
        }

        if (!IsSupportedCustomMaterial(entry.first)) {
            continue;
        }

        SetStoredMaterialCount(entry.first, entry.second);
    }
}

void Fuse::ClearMaterialInventory() {
    sMaterialInventory.clear();
    sMaterialInventoryInitialized = true;
}

bool Fuse::IsSwordFused() {
    const SwordFuseSlot& slot = gFuseSave.GetActiveSwordSlot(nullptr);
    return slot.materialId != MaterialId::None && slot.durabilityCur > 0;
}

bool Fuse::IsBoomerangFused() {
    const FuseSlot& slot = gFuseSave.GetActiveBoomerangSlot(nullptr);
    return slot.materialId != MaterialId::None && slot.durabilityCur > 0;
}

bool Fuse::IsHammerFused() {
    const FuseSlot& slot = gFuseRuntime.GetActiveHammerSlot(nullptr);
    return slot.materialId != MaterialId::None && slot.durabilityCur > 0;
}

bool Fuse::IsArrowsFused() {
    const FuseSlot& slot = gFuseRuntime.GetActiveArrowsSlot(nullptr);
    return slot.materialId != MaterialId::None && slot.durabilityCur > 0;
}

bool Fuse::IsSlingshotFused() {
    const FuseSlot& slot = gFuseRuntime.GetActiveSlingshotSlot(nullptr);
    return slot.materialId != MaterialId::None && slot.durabilityCur > 0;
}

bool Fuse::IsHookshotFused() {
    const FuseSlot& slot = gFuseRuntime.GetActiveHookshotSlot(nullptr);
    return slot.materialId != MaterialId::None && slot.durabilityCur > 0;
}

MaterialId Fuse::GetSwordMaterial() {
    const SwordFuseSlot& slot = gFuseSave.GetActiveSwordSlot(nullptr);
    return slot.materialId;
}

MaterialId Fuse::GetBoomerangMaterial() {
    const FuseSlot& slot = gFuseSave.GetActiveBoomerangSlot(nullptr);
    return slot.materialId;
}

MaterialId Fuse::GetHammerMaterial() {
    const FuseSlot& slot = gFuseRuntime.GetActiveHammerSlot(nullptr);
    return slot.materialId;
}

MaterialId Fuse::GetArrowsMaterial() {
    const FuseSlot& slot = gFuseRuntime.GetActiveArrowsSlot(nullptr);
    return slot.materialId;
}

MaterialId Fuse::GetSlingshotMaterial() {
    const FuseSlot& slot = gFuseRuntime.GetActiveSlingshotSlot(nullptr);
    return slot.materialId;
}

MaterialId Fuse::GetHookshotMaterial() {
    const FuseSlot& slot = gFuseRuntime.GetActiveHookshotSlot(nullptr);
    return slot.materialId;
}

// -----------------------------------------------------------------------------
// Durability (v0: only Sword+Rock)
// -----------------------------------------------------------------------------
int Fuse::GetSwordFuseDurability() {
    const SwordFuseSlot& slot = gFuseSave.GetActiveSwordSlot(nullptr);
    return slot.durabilityCur;
}

int Fuse::GetSwordFuseMaxDurability() {
    const SwordFuseSlot& slot = gFuseSave.GetActiveSwordSlot(nullptr);
    return slot.durabilityMax;
}

int Fuse::GetBoomerangFuseDurability() {
    const FuseSlot& slot = gFuseSave.GetActiveBoomerangSlot(nullptr);
    return slot.durabilityCur;
}

int Fuse::GetBoomerangFuseMaxDurability() {
    const FuseSlot& slot = gFuseSave.GetActiveBoomerangSlot(nullptr);
    return slot.durabilityMax;
}

int Fuse::GetHammerFuseDurability() {
    const FuseSlot& slot = gFuseRuntime.GetActiveHammerSlot(nullptr);
    return slot.durabilityCur;
}

int Fuse::GetHammerFuseMaxDurability() {
    const FuseSlot& slot = gFuseRuntime.GetActiveHammerSlot(nullptr);
    return slot.durabilityMax;
}

std::array<SwordFuseSlot, FusePersistence::kSwordSlotCount> Fuse::GetSwordSlots() {
    return gFuseSave.swordSlots;
}

FuseSlot Fuse::GetActiveSwordSlot() {
    return gFuseSave.GetActiveSwordSlot(nullptr);
}

FuseSlot Fuse::GetActiveBoomerangSlot() {
    return gFuseSave.GetActiveBoomerangSlot(nullptr);
}

FuseSlot Fuse::GetActiveHammerSlot() {
    return gFuseRuntime.GetActiveHammerSlot(nullptr);
}

void Fuse::ApplyLoadedSwordSlots(const std::array<SwordFuseSlot, FusePersistence::kSwordSlotCount>& slots) {
    gFuseSave.swordSlots = slots;
    gFuseSave.version = FusePersistence::kFuseSaveVersion;
    sSwordSlotsLoadedFromSaveManager = true;
    const SwordFuseSlot& slot = gFuseSave.GetActiveSwordSlot(nullptr);
    gFuseRuntime.swordFuseLoadedFromSave = slot.materialId != MaterialId::None;
}

bool Fuse::HasLoadedSwordSlots() {
    return sSwordSlotsLoadedFromSaveManager;
}

FuseSlot Fuse::GetBoomerangSlot() {
    return gFuseSave.GetBoomerangSlot();
}

void Fuse::ApplyLoadedBoomerangSlot(const FuseSlot& slot) {
    gFuseSave.boomerangSlot = slot;
}

FuseSlot Fuse::GetHammerSlot() {
    return gFuseRuntime.GetHammerSlot();
}

void Fuse::ApplyLoadedHammerSlot(const FuseSlot& slot) {
    gFuseRuntime.hammerSlot = slot;
    sLoadedHammerSlot = slot;
    sHammerSlotLoadedFromSaveManager = true;
    Fuse::Log("[FuseSave] ApplyHammer mat=%d dur=%d/%d\n", static_cast<int>(slot.materialId), slot.durabilityCur,
              slot.durabilityMax);
}

bool Fuse::HasLoadedHammerSlot() {
    return sHammerSlotLoadedFromSaveManager;
}

FuseSlot Fuse::GetLoadedHammerSlot() {
    return sLoadedHammerSlot;
}

FuseWeaponView Fuse_GetEquippedSwordView(const PlayState* play) {
    (void)play;

    FuseWeaponView out{};
    out.isFused = false;
    out.curDurability = 0;
    out.maxDurability = 0;
    out.materialId = MaterialId::None;

    if (Fuse::IsSwordFused()) {
        out.isFused = true;
        out.curDurability = Fuse::GetSwordFuseDurability();
        out.maxDurability = Fuse::GetSwordFuseMaxDurability();
        out.materialId = Fuse::GetSwordMaterial();
    }

    return out;
}

void Fuse::SetSwordFuseDurability(int v) {
    v = std::clamp(v, 0, 65535);
    SwordFuseSlot& slot = gFuseSave.GetActiveSwordSlot(nullptr);
    slot.durabilityCur = v;
}

void Fuse::SetSwordFuseMaxDurability(int v) {
    v = std::clamp(v, 0, 65535);
    SwordFuseSlot& slot = gFuseSave.GetActiveSwordSlot(nullptr);
    slot.durabilityMax = v;
}

void Fuse::SetBoomerangFuseDurability(int v) {
    v = std::clamp(v, 0, 65535);
    FuseSlot& slot = gFuseSave.GetActiveBoomerangSlot(nullptr);
    slot.durabilityCur = v;
}

void Fuse::SetBoomerangFuseMaxDurability(int v) {
    v = std::clamp(v, 0, 65535);
    FuseSlot& slot = gFuseSave.GetActiveBoomerangSlot(nullptr);
    slot.durabilityMax = v;
}

void Fuse::SetHammerFuseDurability(int v) {
    v = std::clamp(v, 0, 65535);
    FuseSlot& slot = gFuseRuntime.GetActiveHammerSlot(nullptr);
    slot.durabilityCur = v;
}

void Fuse::SetHammerFuseMaxDurability(int v) {
    v = std::clamp(v, 0, 65535);
    FuseSlot& slot = gFuseRuntime.GetActiveHammerSlot(nullptr);
    slot.durabilityMax = v;
}

void Fuse::ClearSwordFuse() {
    SwordFuseSlot& slot = gFuseSave.GetActiveSwordSlot(nullptr);
    slot.ResetToUnfused();
    gFuseRuntime.swordFuseLoadedFromSave = false;
}

void Fuse::ClearBoomerangFuse() {
    FuseSlot& slot = gFuseSave.GetActiveBoomerangSlot(nullptr);
    slot.ResetToUnfused();
}

void Fuse::ClearHammerFuse() {
    FuseSlot& slot = gFuseRuntime.GetActiveHammerSlot(nullptr);
    slot.ResetToUnfused();
}

void Fuse::ClearArrowsFuse() {
    ClearQueuedRangedFuse_NoRefund(RangedFuseSlot::Arrows, "ClearArrowsFuse");
    ApplyRangedFuseSlotMaterial(RangedFuseSlot::Arrows, MaterialId::None);
}

void Fuse::ClearSlingshotFuse() {
    ClearQueuedRangedFuse_NoRefund(RangedFuseSlot::Slingshot, "ClearSlingshotFuse");
    ApplyRangedFuseSlotMaterial(RangedFuseSlot::Slingshot, MaterialId::None);
}

void Fuse::ClearHookshotFuse() {
    ClearQueuedRangedFuse_NoRefund(RangedFuseSlot::Hookshot, "ClearHookshotFuse");
    ApplyRangedFuseSlotMaterial(RangedFuseSlot::Hookshot, MaterialId::None);
}

void Fuse::FuseSwordWithMaterial(MaterialId id, uint16_t maxDurability, bool initializeCurrentDurability,
                                 bool logDurability) {
    SwordFuseSlot& slot = gFuseSave.GetActiveSwordSlot(nullptr);
    slot.materialId = id;
    slot.durabilityMax = maxDurability;

    const bool shouldInitialize = initializeCurrentDurability && !gFuseRuntime.swordFuseLoadedFromSave;

    if (shouldInitialize) {
        slot.durabilityCur = maxDurability;
    } else {
        slot.durabilityCur = std::clamp<int>(slot.durabilityCur, 0, maxDurability);
    }

    gFuseRuntime.swordFuseLoadedFromSave = false;

    const MaterialDef* def = Fuse::GetMaterialDef(id);
    if (def) {
        Fuse::SetLastEvent(def->name);
    } else {
        Fuse::SetLastEvent("Sword fused with material");
    }

    if (logDurability) {
        Fuse::Log("[FuseMVP] Sword fused with material=%d (durability %u/%u)\n", static_cast<int>(id),
                  static_cast<unsigned int>(slot.durabilityCur), static_cast<unsigned int>(maxDurability));
    }
}

void Fuse::FuseBoomerangWithMaterial(MaterialId id, uint16_t maxDurability, bool initializeCurrentDurability,
                                     bool logDurability) {
    FuseSlot& slot = gFuseSave.GetActiveBoomerangSlot(nullptr);
    slot.materialId = id;
    slot.durabilityMax = maxDurability;

    if (initializeCurrentDurability) {
        slot.durabilityCur = maxDurability;
    } else {
        slot.durabilityCur = std::clamp<int>(slot.durabilityCur, 0, maxDurability);
    }

    const MaterialDef* def = Fuse::GetMaterialDef(id);
    if (def) {
        Fuse::SetLastEvent(def->name);
    } else {
        Fuse::SetLastEvent("Boomerang fused with material");
    }

    if (logDurability) {
        Fuse::Log("[FuseMVP] Boomerang fused with material=%d (durability %u/%u)\n", static_cast<int>(id),
                  static_cast<unsigned int>(slot.durabilityCur), static_cast<unsigned int>(maxDurability));
    }
}

void Fuse::FuseHammerWithMaterial(MaterialId id, uint16_t maxDurability, bool initializeCurrentDurability,
                                  bool logDurability) {
    FuseSlot& slot = gFuseRuntime.GetActiveHammerSlot(nullptr);
    slot.materialId = id;
    slot.durabilityMax = maxDurability;

    if (initializeCurrentDurability) {
        slot.durabilityCur = maxDurability;
    } else {
        slot.durabilityCur = std::clamp<int>(slot.durabilityCur, 0, maxDurability);
    }

    const MaterialDef* def = Fuse::GetMaterialDef(id);
    if (def) {
        Fuse::SetLastEvent(def->name);
    } else {
        Fuse::SetLastEvent("Hammer fused with material");
    }

    if (logDurability) {
        Fuse::Log("[FuseMVP] Hammer fused with material=%d (durability %u/%u)\n", static_cast<int>(id),
                  static_cast<unsigned int>(slot.durabilityCur), static_cast<unsigned int>(maxDurability));
    }
}

void Fuse::FuseArrowsWithMaterial(MaterialId id, uint16_t maxDurability, bool initializeCurrentDurability,
                                  bool logDurability) {
    FuseSlot& slot = gFuseRuntime.GetActiveArrowsSlot(nullptr);
    slot.materialId = id;
    slot.durabilityMax = maxDurability;

    if (initializeCurrentDurability) {
        slot.durabilityCur = maxDurability;
    } else {
        slot.durabilityCur = std::clamp<int>(slot.durabilityCur, 0, maxDurability);
    }

    const MaterialDef* def = Fuse::GetMaterialDef(id);
    if (def) {
        Fuse::SetLastEvent(def->name);
    } else {
        Fuse::SetLastEvent("Arrows fused with material");
    }

    if (logDurability) {
        Fuse::Log("[FuseMVP] Arrows fused with material=%d (durability %u/%u)\n", static_cast<int>(id),
                  static_cast<unsigned int>(slot.durabilityCur), static_cast<unsigned int>(maxDurability));
    }
}

void Fuse::FuseSlingshotWithMaterial(MaterialId id, uint16_t maxDurability, bool initializeCurrentDurability,
                                     bool logDurability) {
    FuseSlot& slot = gFuseRuntime.GetActiveSlingshotSlot(nullptr);
    slot.materialId = id;
    slot.durabilityMax = maxDurability;

    if (initializeCurrentDurability) {
        slot.durabilityCur = maxDurability;
    } else {
        slot.durabilityCur = std::clamp<int>(slot.durabilityCur, 0, maxDurability);
    }

    const MaterialDef* def = Fuse::GetMaterialDef(id);
    if (def) {
        Fuse::SetLastEvent(def->name);
    } else {
        Fuse::SetLastEvent("Slingshot fused with material");
    }

    if (logDurability) {
        Fuse::Log("[FuseMVP] Slingshot fused with material=%d (durability %u/%u)\n", static_cast<int>(id),
                  static_cast<unsigned int>(slot.durabilityCur), static_cast<unsigned int>(maxDurability));
    }
}

void Fuse::FuseHookshotWithMaterial(MaterialId id, uint16_t maxDurability, bool initializeCurrentDurability,
                                    bool logDurability) {
    FuseSlot& slot = gFuseRuntime.GetActiveHookshotSlot(nullptr);
    slot.materialId = id;
    slot.durabilityMax = maxDurability;

    if (initializeCurrentDurability) {
        slot.durabilityCur = maxDurability;
    } else {
        slot.durabilityCur = std::clamp<int>(slot.durabilityCur, 0, maxDurability);
    }

    const MaterialDef* def = Fuse::GetMaterialDef(id);
    if (def) {
        Fuse::SetLastEvent(def->name);
    } else {
        Fuse::SetLastEvent("Hookshot fused with material");
    }

    if (logDurability) {
        Fuse::Log("[FuseMVP] Hookshot fused with material=%d (durability %u/%u)\n", static_cast<int>(id),
                  static_cast<unsigned int>(slot.durabilityCur), static_cast<unsigned int>(maxDurability));
    }
}

Fuse::FuseResult Fuse::TryFuseSword(MaterialId id) {
    if (id == MaterialId::None) {
        return FuseResult::NotAllowed;
    }

    if (Fuse::IsSwordFused()) {
        return FuseResult::AlreadyFused;
    }

    if (!Fuse::HasMaterial(id, 1)) {
        return FuseResult::NotEnoughMaterial;
    }

    const MaterialDef* def = Fuse::GetMaterialDef(id);
    if (!def) {
        return FuseResult::InvalidMaterial;
    }

    const int preConsumeCount = (id == MaterialId::DekuNut) ? Fuse::GetMaterialCount(id) : -1;

    if (!Fuse::ConsumeMaterial(id, 1)) {
        return FuseResult::NotEnoughMaterial;
    }

    Fuse::FuseSwordWithMaterial(id, Fuse::GetMaterialEffectiveBaseDurability(id));

    if (id == MaterialId::DekuNut) {
        const int postConsumeCount = Fuse::GetMaterialCount(id);
        Fuse::Log("[FuseMVP] TryFuseSword(DekuNut): before=%d after=%d\n", preConsumeCount, postConsumeCount);
    }

    return FuseResult::Ok;
}

Fuse::FuseResult Fuse::TryFuseBoomerang(MaterialId id) {
    if (id == MaterialId::None) {
        return FuseResult::NotAllowed;
    }

    if (Fuse::IsBoomerangFused()) {
        return FuseResult::AlreadyFused;
    }

    if (!Fuse::HasMaterial(id, 1)) {
        return FuseResult::NotEnoughMaterial;
    }

    const MaterialDef* def = Fuse::GetMaterialDef(id);
    if (!def) {
        return FuseResult::InvalidMaterial;
    }

    if (!Fuse::ConsumeMaterial(id, 1)) {
        return FuseResult::NotEnoughMaterial;
    }

    Fuse::FuseBoomerangWithMaterial(id, Fuse::GetMaterialEffectiveBaseDurability(id));

    return FuseResult::Ok;
}

Fuse::FuseResult Fuse::TryFuseHammer(MaterialId id) {
    if (id == MaterialId::None) {
        return FuseResult::NotAllowed;
    }

    if (Fuse::IsHammerFused()) {
        return FuseResult::AlreadyFused;
    }

    if (!Fuse::HasMaterial(id, 1)) {
        return FuseResult::NotEnoughMaterial;
    }

    const MaterialDef* def = Fuse::GetMaterialDef(id);
    if (!def) {
        return FuseResult::InvalidMaterial;
    }

    if (!Fuse::ConsumeMaterial(id, 1)) {
        return FuseResult::NotEnoughMaterial;
    }

    Fuse::FuseHammerWithMaterial(id, Fuse::GetMaterialEffectiveBaseDurability(id));

    return FuseResult::Ok;
}

Fuse::FuseResult Fuse::TryFuseArrows(MaterialId id) {
    return Fuse::TryQueueRangedFuse(RangedFuseSlot::Arrows, id, "TryFuseArrows");
}

Fuse::FuseResult Fuse::TryFuseSlingshot(MaterialId id) {
    return Fuse::TryQueueRangedFuse(RangedFuseSlot::Slingshot, id, "TryFuseSlingshot");
}

Fuse::FuseResult Fuse::TryFuseHookshot(MaterialId id) {
    return Fuse::TryQueueRangedFuse(RangedFuseSlot::Hookshot, id, "TryFuseHookshot");
}

Fuse::FuseResult Fuse::TryUnfuseSword() {
    if (!Fuse::IsSwordFused()) {
        return FuseResult::Ok;
    }

    Fuse_ClearSavedSwordFuse(nullptr);
    return FuseResult::Ok;
}

Fuse::FuseResult Fuse::TryUnfuseBoomerang() {
    if (!Fuse::IsBoomerangFused()) {
        return FuseResult::Ok;
    }

    Fuse::ClearBoomerangFuse();
    return FuseResult::Ok;
}

Fuse::FuseResult Fuse::TryUnfuseHammer() {
    if (!Fuse::IsHammerFused()) {
        return FuseResult::Ok;
    }

    Fuse::ClearHammerFuse();
    return FuseResult::Ok;
}

Fuse::FuseResult Fuse::TryUnfuseArrows() {
    if (!Fuse::IsArrowsFused()) {
        return FuseResult::Ok;
    }

    Fuse::ClearArrowsFuse();
    return FuseResult::Ok;
}

Fuse::FuseResult Fuse::TryUnfuseSlingshot() {
    if (!Fuse::IsSlingshotFused()) {
        return FuseResult::Ok;
    }

    Fuse::ClearSlingshotFuse();
    return FuseResult::Ok;
}

Fuse::FuseResult Fuse::TryUnfuseHookshot() {
    if (!Fuse::IsHookshotFused()) {
        return FuseResult::Ok;
    }

    Fuse::ClearHookshotFuse();
    return FuseResult::Ok;
}

Fuse::FuseResult Fuse::TryQueueRangedFuse(RangedFuseSlot slot, MaterialId mat, const char* reason) {
    if (!Fuse::IsEnabled()) {
        return FuseResult::NotAllowed;
    }

    if (mat == MaterialId::None) {
        return FuseResult::NotAllowed;
    }

    if (!Fuse::GetMaterialDef(mat)) {
        return FuseResult::InvalidMaterial;
    }

    RangedQueuedFuse& queued = gFuseRuntime.GetRangedQueuedSlot(slot);
    if (queued.inFlight) {
        LogRangedEvent("RangedQueueFail", slot, mat, "inFlight");
        return FuseResult::NotAllowed;
    }

    const int currentFrame = GetGameplayFrame();
    const bool hasPendingSwap =
        queued.pendingRefundMaterial != MaterialId::None && queued.pendingRefundFrame == currentFrame;
    const MaterialId pendingMat = hasPendingSwap ? queued.pendingRefundMaterial : MaterialId::None;
    if (hasPendingSwap) {
        queued.pendingRefundMaterial = MaterialId::None;
        queued.pendingRefundFrame = -1;
    }

    if (queued.materialId != MaterialId::None) {
        Fuse::CancelQueuedRangedFuse_Refund(slot, "QueueReplace");
    }

    if (!Fuse::HasMaterial(mat, 1) || !Fuse::ConsumeMaterial(mat, 1)) {
        if (hasPendingSwap && pendingMat != MaterialId::None) {
            queued.materialId = pendingMat;
            queued.inFlight = false;
            queued.hadSuccess = false;
            ApplyRangedFuseSlotMaterial(slot, pendingMat);
        }

        LogRangedEvent("RangedQueueFail", slot, mat, reason);
        return FuseResult::NotEnoughMaterial;
    }

    if (hasPendingSwap && pendingMat != MaterialId::None) {
        Fuse::AddMaterial(pendingMat, 1);
        LogRangedEvent("RangedCancel", slot, pendingMat, "SwapRefund");
    }

    queued.materialId = mat;
    queued.inFlight = false;
    queued.hadSuccess = false;
    queued.pendingRefundMaterial = MaterialId::None;
    queued.pendingRefundFrame = -1;
    ApplyRangedFuseSlotMaterial(slot, mat);
    LogRangedEvent("RangedQueue", slot, mat, reason);
    return FuseResult::Ok;
}

void Fuse::ClearQueuedRangedFuse_NoRefund(RangedFuseSlot slot, const char* reason) {
    RangedQueuedFuse& queued = gFuseRuntime.GetRangedQueuedSlot(slot);
    if (queued.materialId == MaterialId::None) {
        return;
    }

    queued.pendingRefundMaterial = queued.materialId;
    queued.pendingRefundFrame = GetGameplayFrame();
    queued.materialId = MaterialId::None;
    queued.inFlight = false;
    queued.hadSuccess = false;

    LogRangedEvent("RangedClear", slot, queued.pendingRefundMaterial, reason);
}

void Fuse::CommitQueuedRangedFuse(RangedFuseSlot slot, const char* reason) {
    RangedQueuedFuse& queued = gFuseRuntime.GetRangedQueuedSlot(slot);
    if (queued.materialId == MaterialId::None) {
        return;
    }

    const MaterialId mat = queued.materialId;
    queued.materialId = MaterialId::None;
    queued.inFlight = false;
    queued.hadSuccess = true;
    queued.pendingRefundMaterial = MaterialId::None;
    queued.pendingRefundFrame = -1;
    ApplyRangedFuseSlotMaterial(slot, MaterialId::None);
    LogRangedEvent("RangedCommit", slot, mat, reason);
}

void Fuse::CancelQueuedRangedFuse_Refund(RangedFuseSlot slot, const char* reason) {
    RangedQueuedFuse& queued = gFuseRuntime.GetRangedQueuedSlot(slot);
    if (queued.materialId == MaterialId::None) {
        return;
    }

    const MaterialId mat = queued.materialId;
    Fuse::AddMaterial(mat, 1);
    queued.materialId = MaterialId::None;
    queued.inFlight = false;
    queued.hadSuccess = false;
    queued.pendingRefundMaterial = MaterialId::None;
    queued.pendingRefundFrame = -1;
    ApplyRangedFuseSlotMaterial(slot, MaterialId::None);
    LogRangedEvent("RangedCancel", slot, mat, reason);
}

void Fuse::OnHookshotShotStarted(const char* reason) {
    RangedQueuedFuse& queued = gFuseRuntime.GetRangedQueuedSlot(RangedFuseSlot::Hookshot);
    queued.inFlight = true;
    queued.hadSuccess = false;
    (void)reason;
}

void Fuse::OnHookshotRetractedOrKilled(const char* reason) {
    RangedQueuedFuse& queued = gFuseRuntime.GetRangedQueuedSlot(RangedFuseSlot::Hookshot);
    if (!queued.inFlight) {
        queued.hadSuccess = false;
        return;
    }

    queued.inFlight = false;
    if (!queued.hadSuccess) {
        Fuse::CancelQueuedRangedFuse_Refund(RangedFuseSlot::Hookshot, reason);
        return;
    }

    queued.hadSuccess = false;
}

bool Fuse::HammerDrainedThisSwing() {
    return gFuseRuntime.hammerDrainedThisSwing;
}

bool Fuse::HammerHitActorThisSwing() {
    return gFuseRuntime.hammerHitActorThisSwing;
}

s16 Fuse::GetHammerSwingId() {
    return gFuseRuntime.hammerSwingId;
}

void Fuse::ResetHammerSwingTracking(s16 swingId) {
    gFuseRuntime.hammerDrainedThisSwing = false;
    gFuseRuntime.hammerHitActorThisSwing = false;
    gFuseRuntime.hammerSwingId = swingId;
}

void Fuse::SetHammerDrainedThisSwing(bool drained) {
    gFuseRuntime.hammerDrainedThisSwing = drained;
}

void Fuse::SetHammerHitActorThisSwing(bool hit) {
    gFuseRuntime.hammerHitActorThisSwing = hit;
}

void Fuse::IncrementHammerSwingId() {
    if (gFuseRuntime.hammerSwingId == std::numeric_limits<s16>::max()) {
        gFuseRuntime.hammerSwingId = 0;
    } else {
        gFuseRuntime.hammerSwingId++;
    }
}

bool Fuse::DamageSwordFuseDurability(PlayState* play, int amount, const char* reason) {
    amount = std::max(amount, 0);

    if (!Fuse::IsSwordFused()) {
        return false;
    }

    int cur = GetSwordFuseDurability();
    cur = std::max(0, cur - amount);
    SetSwordFuseDurability(cur);

    if (cur == 0) {
        Fuse_ClearSavedSwordFuse(play);
        const int frame = play ? play->gameplayFrames : -1;
        Log("[FuseMVP] Sword fuse broke at frame=%d; clearing fuse and reverting to vanilla (reason=%s)\n", frame,
            reason ? reason : "unknown");
        OnSwordFuseBroken(play);
        return true;
    }

    return false;
}

bool Fuse::DamageBoomerangFuseDurability(PlayState* play, int amount, const char* reason) {
    amount = std::max(amount, 0);

    if (!Fuse::IsBoomerangFused()) {
        return false;
    }

    int cur = GetBoomerangFuseDurability();
    cur = std::max(0, cur - amount);
    SetBoomerangFuseDurability(cur);

    if (cur == 0) {
        const int frame = play ? play->gameplayFrames : -1;
        Log("[FuseMVP] Boomerang fuse broke at frame=%d; clearing fuse (reason=%s)\n", frame,
            reason ? reason : "unknown");
        OnBoomerangFuseBroken(play);
        return true;
    }

    return false;
}

bool Fuse::DamageHammerFuseDurability(PlayState* play, int amount, const char* reason) {
    amount = std::max(amount, 0);

    if (!Fuse::IsHammerFused()) {
        return false;
    }

    int cur = GetHammerFuseDurability();
    cur = std::max(0, cur - amount);
    SetHammerFuseDurability(cur);

    if (cur == 0) {
        const int frame = play ? play->gameplayFrames : -1;
        Log("[FuseMVP] Hammer fuse broke at frame=%d; clearing fuse (reason=%s)\n", frame, reason ? reason : "unknown");
        OnHammerFuseBroken(play);
        return true;
    }

    return false;
}

void Fuse::OnSwordFuseBroken(PlayState* play) {
    SetLastEvent("Sword fuse broke");
    FuseHooks::RestoreSwordHitboxVanillaNow(play);
}

void Fuse::OnBoomerangFuseBroken(PlayState* play) {
    (void)play;
    SetLastEvent("Boomerang fuse broke");
    ClearBoomerangFuse();
}

void Fuse::OnHammerFuseBroken(PlayState* play) {
    (void)play;
    SetLastEvent("Hammer fuse broke");
    ClearHammerFuse();
}

// -----------------------------------------------------------------------------
// MVP: Award ROCK to inventory
// -----------------------------------------------------------------------------
void Fuse::AwardRockMaterial() {
    Fuse::AddMaterial(MaterialId::Rock, 1);

    const int count = Fuse::GetMaterialCount(MaterialId::Rock);
    Fuse::SetLastEvent("Acquired ROCK");
    Fuse::Log("[FuseMVP] Acquired material: ROCK (count=%d)\n", count);
}

// -----------------------------------------------------------------------------
// Hooks entrypoints
// -----------------------------------------------------------------------------
void Fuse::OnLoadGame(int32_t /*fileNum*/) {
    // Reset runtime state
    gFuseRuntime = FuseRuntimeState{};
    gFuseRuntime.enabled = true;
    gFuseRuntime.lastEvent = "Loaded";
    if (sHammerSlotLoadedFromSaveManager) {
        gFuseRuntime.hammerSlot = sLoadedHammerSlot;
    }

    ResetSwordFreezeQueueInternal();
    sFuseFrozenTimers.clear();
    sFreezeAppliedFrame.clear();
    sShatterFrame.clear();
    sFuseFrozenPos.clear();

    EnsureMaterialInventoryInitialized();

    if (!sSwordSlotsLoadedFromSaveManager) {
        gFuseSave = FuseSaveData{};
        FusePersistence::ApplySwordStateFromContext(nullptr);
    } else {
        const SwordFuseSlot& slot = gFuseSave.GetActiveSwordSlot(nullptr);
        gFuseRuntime.swordFuseLoadedFromSave = slot.materialId != MaterialId::None;
    }

    Fuse::Log("[FuseMVP] Save loaded -> Fuse ACTIVE (always enabled)\n");
    Fuse::Log("[FuseMVP] MVP: Throw a liftable rock until it BREAKS to acquire ROCK.\n");
}

static void UpdateRangedFuseLifecycle(PlayState* play) {
    const int currentFrame = GetGameplayFrame();
    for (size_t i = 0; i < gFuseRuntime.rangedQueuedSlots.size(); ++i) {
        RangedQueuedFuse& queued = gFuseRuntime.rangedQueuedSlots[i];
        if (queued.pendingRefundMaterial != MaterialId::None && queued.pendingRefundFrame != currentFrame) {
            queued.pendingRefundMaterial = MaterialId::None;
            queued.pendingRefundFrame = -1;
        }
    }

    if (!play || !Fuse::IsEnabled()) {
        return;
    }

    Player* player = GET_PLAYER(play);
    const int32_t heldAction = player ? player->heldItemAction : 0;
    RangedFuseSlot aimingSlot = RangedFuseSlot::Arrows;
    const bool aiming = IsPlayerAimingRangedSlot(play, &aimingSlot);

    if (gFuseRuntime.lastHeldItemAction != heldAction) {
        RangedFuseSlot previousSlot = RangedFuseSlot::Arrows;
        if (HeldItemActionToSlot(gFuseRuntime.lastHeldItemAction, &previousSlot)) {
            RangedQueuedFuse& queued = gFuseRuntime.GetRangedQueuedSlot(previousSlot);
            if (queued.materialId != MaterialId::None &&
                !(previousSlot == RangedFuseSlot::Hookshot && queued.inFlight)) {
                if (!aiming || aimingSlot != previousSlot) {
                    Fuse::CancelQueuedRangedFuse_Refund(previousSlot, "HeldItemSwitch");
                }
            }
        }
    }

    for (size_t i = 0; i < gFuseRuntime.rangedQueuedSlots.size(); ++i) {
        const RangedFuseSlot slot = static_cast<RangedFuseSlot>(i);
        RangedQueuedFuse& queued = gFuseRuntime.rangedQueuedSlots[i];
        if (queued.materialId == MaterialId::None) {
            continue;
        }

        if (slot == RangedFuseSlot::Hookshot && queued.inFlight) {
            continue;
        }

        if (!aiming || aimingSlot != slot) {
            Fuse::CancelQueuedRangedFuse_Refund(slot, "AimExitOrSwitch");
        }
    }

    gFuseRuntime.lastHeldItemAction = heldAction;
}

void Fuse::OnGameFrameUpdate(PlayState* play) {
    TickFuseFrozenTimers(play);
    UpdateRangedFuseLifecycle(play);
}

void Fuse::ProcessDeferredSwordFreezes(PlayState* play) {
    if (!play) {
        return;
    }

    if (!Fuse::IsEnabled()) {
        ResetSwordFreezeQueueInternal();
        return;
    }

    const int curFrame = play->gameplayFrames;
    if (curFrame < 0) {
        return;
    }

    const size_t applyIndex = static_cast<size_t>((curFrame + kSwordFreezeQueueCount - 1) % kSwordFreezeQueueCount);
    const int queuedFrame = sSwordFreezeQueueFrames[applyIndex];

    if (queuedFrame == -1 || queuedFrame >= curFrame) {
        return;
    }

    for (const auto& request : sSwordFreezeQueues[applyIndex]) {
        ApplyIceArrowFreeze(play, request.victim, request.level);
    }

    sSwordFreezeQueues[applyIndex].clear();
    sSwordFreezeVictims[applyIndex].clear();
    sSwordFreezeQueueFrames[applyIndex] = -1;
}

void Fuse::ResetSwordFreezeQueue() {
    ResetSwordFreezeQueueInternal();
}

static void ApplyMeleeHitMaterialEffects(PlayState* play, Actor* victim, MaterialId materialId) {
    if (!victim) {
        return;
    }

    const MaterialDef* def = Fuse::GetMaterialDef(materialId);
    if (!def) {
        return;
    }

    uint8_t stunLevel = 0;
    if (HasModifier(def->modifiers, def->modifierCount, ModifierId::Stun, &stunLevel) && stunLevel > 0) {
        ApplyDekuNutStunVanilla(play, GET_PLAYER(play), victim, stunLevel);
    }

    const int frame = play ? play->gameplayFrames : -1;
    const bool wasFuseFrozen = IsFuseFrozen(victim);
    const auto applyIt = sFreezeAppliedFrame.find(victim);
    const bool freezeAppliedThisFrame = (applyIt != sFreezeAppliedFrame.end() && applyIt->second == frame);
    const bool shouldShatter = wasFuseFrozen && !freezeAppliedThisFrame;
    if (shouldShatter) {
        ClearFuseFreeze(victim);
        Actor_ApplyDamage(victim);
        Fuse::Log("[FuseDBG] FreezeShatter: victim=%p doubleDamage=1\n", (void*)victim);

        if (play != nullptr) {
            Player* player = GET_PLAYER(play);
            if (player != nullptr) {
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

                constexpr float kShatterKnockbackSpeed = 14.0f;
                constexpr float kShatterUpwardBoost = 1.5f;

                victim->velocity.x = dir.x * kShatterKnockbackSpeed;
                victim->velocity.z = dir.z * kShatterKnockbackSpeed;

                victim->velocity.y = std::max(victim->velocity.y, kShatterUpwardBoost);

                victim->speedXZ = std::max(victim->speedXZ, kShatterKnockbackSpeed);
                victim->world.rot.y = Math_Atan2S(dir.x, dir.z);
                victim->shape.rot.y = victim->world.rot.y;

                Fuse::Log("[FuseDBG] FreezeShatterKnockback: victim=%p v=(%.2f,%.2f,%.2f)\n", (void*)victim,
                          victim->velocity.x, victim->velocity.y, victim->velocity.z);
            }
        }
        if (frame >= 0) {
            sShatterFrame[victim] = frame;
        }
    }

    uint8_t freezeLevel = 0;
    const bool shatteredThisHit =
        (frame >= 0 && sShatterFrame.find(victim) != sShatterFrame.end() && sShatterFrame[victim] == frame);
    if (!shatteredThisHit && !wasFuseFrozen &&
        HasModifier(def->modifiers, def->modifierCount, ModifierId::Freeze, &freezeLevel) && freezeLevel > 0) {
        EnqueueSwordFreezeRequest(play, victim, freezeLevel);
    }
}

void Fuse::OnSwordMeleeHit(PlayState* play, Actor* victim) {
    if (!Fuse::IsSwordFused()) {
        return;
    }

    const MaterialId materialId = Fuse::GetSwordMaterial();
    const MaterialDef* def = Fuse::GetMaterialDef(materialId);
    if (def) {
        uint8_t knockbackLevel = 0;
        if (HasModifier(def->modifiers, def->modifierCount, ModifierId::Knockback, &knockbackLevel) &&
            knockbackLevel > 0) {
            Player* player = GET_PLAYER(play);
            ApplyFuseKnockback(play, player, victim, knockbackLevel, "Sword", materialId,
                               Fuse::GetSwordFuseDurability(), Fuse::GetSwordFuseMaxDurability(), "hit");
        }
    }

    ApplyMeleeHitMaterialEffects(play, victim, Fuse::GetSwordMaterial());
}

void Fuse::OnHammerMeleeHit(PlayState* play, Actor* victim) {
    if (!Fuse::IsHammerFused()) {
        return;
    }

    ApplyMeleeHitMaterialEffects(play, victim, Fuse::GetHammerMaterial());
}
