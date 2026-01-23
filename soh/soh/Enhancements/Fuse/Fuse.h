#pragma once
#include <array>
#include <cstdint>
#include <vector>

#include "z64.h"
#include "soh/Enhancements/Fuse/FuseMaterials.h"
#include "soh/Enhancements/Fuse/FuseModifiers.h"
#include "soh/Enhancements/Fuse/FuseState.h"

struct PlayState;
struct Player;
struct Actor;

struct FuseWeaponView {
    bool isFused = false;
    int curDurability = 0;
    int maxDurability = 0;
    MaterialId materialId = MaterialId::None;
};

void Fuse_ApplySavedSwordFuse(const PlayState* play, s16 savedMaterialId, s16 savedMaxDurability,
                              bool hasSavedCurDurability, u16 savedCurDurability, s16 legacyCurDurability);
void Fuse_WriteSwordFuseToSave(const PlayState* play);
void Fuse_ClearSavedSwordFuse(const PlayState* play);
void Fuse_EnqueuePendingStun(Actor* victim, uint8_t level, MaterialId materialId, int itemId);
void Fuse_TriggerDekuNutAtPos(PlayState* play, const Vec3f& pos, int srcItemId);
void Fuse_TriggerMegaStun(PlayState* play, Player* player, MaterialId materialId, int itemId);

FuseWeaponView Fuse_GetEquippedSwordView(const PlayState* play);

extern "C" int32_t Fuse_GetPlayerMeleeHammerizeLevel(PlayState* play);
extern "C" bool Fuse_IsActorFuseFrozen(Actor* actor);

namespace Fuse {

struct MaterialDebugOverride {
    int attackBonusDelta = 0;
    int baseDurabilityOverride = -1;
};

// Call once on load / init and every frame.
void OnLoadGame(int32_t fileNum);
void OnGameFrameUpdate(PlayState* play);
void OnSwordMeleeHit(PlayState* play, Actor* victim, int baseWeaponDamage);
void OnHammerMeleeHit(PlayState* play, Actor* victim, int baseWeaponDamage);
void ProcessPendingStuns(PlayState* play);
void ProcessDeferredSwordFreezes(PlayState* play);
void ResetSwordFreezeQueue();
void QueueSwordFreeze(PlayState* play, Actor* victim, uint8_t level, const char* srcLabel, const char* slotLabel,
                      MaterialId materialId);
bool IsFuseFrozen(Actor* actor);
bool TryFreezeShatter(PlayState* play, Actor* victim, Actor* attacker, const char* srcLabel);
bool TryFreezeShatterWithDamage(PlayState* play, Actor* victim, Actor* attacker, int baseWeaponDamage,
                                MaterialId materialId, int itemId, const char* srcLabel);

// Core state
bool IsEnabled();
void SetEnabled(bool enabled);

// Materials registry and helpers
const MaterialDef* GetMaterialDef(MaterialId id);
const MaterialDef* GetMaterialDefs(size_t* count);
uint16_t GetMaterialBaseDurability(MaterialId id);
int GetMaterialAttackBonus(MaterialId id);
int GetMaterialDurabilityOverride(MaterialId id);
int GetMaterialEffectiveBaseDurability(MaterialId id);
int GetMaterialAttackBonusDelta(MaterialId id);
void SetMaterialAttackBonusDelta(MaterialId id, int v);
void SetMaterialBaseDurabilityOverride(MaterialId id, int v);
void ResetMaterialOverride(MaterialId id);
void ResetAllMaterialOverrides();
void SetUseDebugOverrides(bool enabled);
bool GetUseDebugOverrides();
void LoadDebugOverrides();
void SaveDebugOverrides();
uint8_t GetSwordModifierLevel(ModifierId id);
bool SwordHasModifier(ModifierId id);

// Materials inventory API (v0: ROCK + Deku Nut adapter)
int GetMaterialCount(MaterialId id);
void SetMaterialCount(MaterialId id, int amount);
bool HasMaterial(MaterialId id, int amount = 1);
void AddMaterial(MaterialId id, int amount);
bool ConsumeMaterial(MaterialId id, int amount);
// Back-compat helpers for the MVP rock-only flow
bool HasRockMaterial(); // now means rockCount > 0
int GetRockCount();     // NEW
std::vector<std::pair<MaterialId, uint16_t>> GetCustomMaterialInventory();
void ApplyCustomMaterialInventory(const std::vector<std::pair<MaterialId, uint16_t>>& entries);
void ClearMaterialInventory();
bool IsSwordFused();
MaterialId GetSwordMaterial();
void FuseSwordWithMaterial(MaterialId id, uint16_t maxDurability, bool initializeCurrentDurability = true,
                           bool logDurability = true);
enum class FuseResult { Ok, NotEnoughMaterial, InvalidMaterial, AlreadyFused, NotAllowed };
FuseResult TryFuseSword(MaterialId id);
FuseResult TryUnfuseSword();
bool IsBoomerangFused();
MaterialId GetBoomerangMaterial();
void FuseBoomerangWithMaterial(MaterialId id, uint16_t maxDurability, bool initializeCurrentDurability = true,
                               bool logDurability = true);
FuseResult TryFuseBoomerang(MaterialId id);
FuseResult TryUnfuseBoomerang();
bool IsHammerFused();
MaterialId GetHammerMaterial();
void FuseHammerWithMaterial(MaterialId id, uint16_t maxDurability, bool initializeCurrentDurability = true,
                            bool logDurability = true);
FuseResult TryFuseHammer(MaterialId id);
FuseResult TryUnfuseHammer();
bool IsArrowsFused();
MaterialId GetArrowsMaterial();
void FuseArrowsWithMaterial(MaterialId id, uint16_t maxDurability, bool initializeCurrentDurability = true,
                            bool logDurability = true);
FuseResult TryFuseArrows(MaterialId id);
FuseResult TryUnfuseArrows();
bool IsSlingshotFused();
MaterialId GetSlingshotMaterial();
void FuseSlingshotWithMaterial(MaterialId id, uint16_t maxDurability, bool initializeCurrentDurability = true,
                               bool logDurability = true);
FuseResult TryFuseSlingshot(MaterialId id);
FuseResult TryUnfuseSlingshot();
bool IsHookshotFused();
MaterialId GetHookshotMaterial();
void FuseHookshotWithMaterial(MaterialId id, uint16_t maxDurability, bool initializeCurrentDurability = true,
                              bool logDurability = true);
FuseResult TryFuseHookshot(MaterialId id);
FuseResult TryUnfuseHookshot();
bool HammerDrainedThisSwing();
bool HammerHitActorThisSwing();
s16 GetHammerSwingId();
void ResetHammerSwingTracking(s16 swingId);
void SetHammerDrainedThisSwing(bool drained);
void SetHammerHitActorThisSwing(bool hit);
void IncrementHammerSwingId();

// MVP: award rock and (optionally) auto-fuse to sword (runtime for now)
void AwardRockMaterial();

// Durability (v0: only Sword+Rock)
int GetSwordFuseDurability();
int GetSwordFuseMaxDurability();
void SetSwordFuseDurability(int v);
void SetSwordFuseMaxDurability(int v);
bool DamageSwordFuseDurability(PlayState* play, int amount, const char* reason);
void ClearSwordFuse();
void OnSwordFuseBroken(PlayState* play);
int GetBoomerangFuseDurability();
int GetBoomerangFuseMaxDurability();
void SetBoomerangFuseDurability(int v);
void SetBoomerangFuseMaxDurability(int v);
bool DamageBoomerangFuseDurability(PlayState* play, int amount, const char* reason);
void ClearBoomerangFuse();
void OnBoomerangFuseBroken(PlayState* play);
int GetHammerFuseDurability();
int GetHammerFuseMaxDurability();
void SetHammerFuseDurability(int v);
void SetHammerFuseMaxDurability(int v);
bool DamageHammerFuseDurability(PlayState* play, int amount, const char* reason);
void ClearHammerFuse();
void OnHammerFuseBroken(PlayState* play);
void ClearArrowsFuse();
void ClearSlingshotFuse();
void ClearHookshotFuse();
FuseResult TryQueueRangedFuse(RangedFuseSlot slot, MaterialId mat, const char* reason);
void ClearQueuedRangedFuse_NoRefund(RangedFuseSlot slot, const char* reason);
void CommitQueuedRangedFuse(RangedFuseSlot slot, const char* reason);
void CancelQueuedRangedFuse_Refund(RangedFuseSlot slot, const char* reason);
void ClearActiveRangedFuse(RangedFuseSlot slot, const char* reason);
void MarkRangedHitResolved(RangedFuseSlot slot, const char* reason);
void OnRangedProjectileHitFinalize(RangedFuseSlot slot, const char* reason);
void OnHookshotShotStarted(const char* reason);
void OnHookshotRetractedOrKilled(const char* reason);

// Debug/testing (safe no-op in shipping later)
const char* GetLastEvent();
void SetLastEvent(const char* msg);

std::array<SwordFuseSlot, FusePersistence::kSwordSlotCount> GetSwordSlots();
void ApplyLoadedSwordSlots(const std::array<SwordFuseSlot, FusePersistence::kSwordSlotCount>& slots);
bool HasLoadedSwordSlots();
FuseSlot GetActiveSwordSlot();
FuseSlot GetActiveBoomerangSlot();
FuseSlot GetActiveHammerSlot();
FuseSlot GetBoomerangSlot();
void ApplyLoadedBoomerangSlot(const FuseSlot& slot);
FuseSlot GetHammerSlot();
void ApplyLoadedHammerSlot(const FuseSlot& slot);
bool HasLoadedHammerSlot();
FuseSlot GetLoadedHammerSlot();

// Simple logger (goes to VS Output on Windows)
void Log(const char* fmt, ...);

} // namespace Fuse
