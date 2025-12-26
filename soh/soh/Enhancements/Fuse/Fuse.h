#pragma once
#include <cstdint>

#include "soh/Enhancements/Fuse/FuseMaterials.h"
#include "soh/Enhancements/Fuse/FuseModifiers.h"

struct PlayState;
struct Player;
struct Actor;

namespace Fuse {

// Call once on load / init and every frame.
void OnLoadGame(int32_t fileNum);
void OnGameFrameUpdate(PlayState* play);
void OnSwordMeleeHit(PlayState* play, Actor* victim);

// Core state
bool IsEnabled();
void SetEnabled(bool enabled);

// Materials registry and helpers
const MaterialDef* GetMaterialDef(MaterialId id);
uint16_t GetMaterialBaseDurability(MaterialId id);
uint8_t GetSwordModifierLevel(ModifierId id);
bool SwordHasModifier(ModifierId id);

// Materials inventory API (v0: ROCK only)
int GetMaterialCount(MaterialId id);
bool HasMaterial(MaterialId id, int amount = 1);
void AddMaterial(MaterialId id, int amount);
bool ConsumeMaterial(MaterialId id, int amount);
// Back-compat helpers for the MVP rock-only flow
bool HasRockMaterial(); // now means rockCount > 0
int GetRockCount();     // NEW
bool IsSwordFused();
MaterialId GetSwordMaterial();
void FuseSwordWithMaterial(MaterialId id, uint16_t maxDurability);
enum class FuseResult { Ok, NotEnoughMaterial, InvalidMaterial, AlreadyFused, NotAllowed };
FuseResult TryFuseSword(MaterialId id);
FuseResult TryUnfuseSword();

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

// Debug/testing (safe no-op in shipping later)
const char* GetLastEvent();
void SetLastEvent(const char* msg);

// Simple logger (goes to VS Output on Windows)
void Log(const char* fmt, ...);

} // namespace Fuse
