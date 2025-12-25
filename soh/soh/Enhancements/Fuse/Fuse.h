#pragma once
#include <cstdint>

struct PlayState;
struct Player;
struct Actor;

namespace Fuse {

// Call once on load / init and every frame.
void OnLoadGame(int32_t fileNum);
void OnGameFrameUpdate(PlayState* play);

// Core state
bool IsEnabled();
void SetEnabled(bool enabled);

// MVP material: ROCK
bool HasRockMaterial(); // now means rockCount > 0
int GetRockCount();     // NEW
bool IsSwordFusedWithRock();

// MVP: award rock and (optionally) auto-fuse to sword (runtime for now)
void AwardRockMaterial();

// Durability (v0: only Sword+Rock)
int GetSwordFuseDurability();
int GetSwordFuseMaxDurability();
void SetSwordFuseDurability(int v);
void SetSwordFuseMaxDurability(int v);
bool DamageSwordFuseDurability(int amount);
void ClearSwordFuse();

// Debug/testing (safe no-op in shipping later)
const char* GetLastEvent();
void SetLastEvent(const char* msg);

// Simple logger (goes to VS Output on Windows)
void Log(const char* fmt, ...);

} // namespace Fuse
