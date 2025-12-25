#include "Fuse.h"
#include "FuseState.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#endif

extern "C" {
#include "z64.h"
#include "variables.h"
extern PlayState* gPlayState;
}

// -----------------------------------------------------------------------------
// Module-local state
// -----------------------------------------------------------------------------
static FuseSaveData gFuseSave; // persistent-ready (not serialized yet)
static FuseRuntimeState gFuseRuntime;

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

bool Fuse::HasRockMaterial() {
    return gFuseSave.rockCount > 0;
}

int Fuse::GetRockCount() {
    return (int)gFuseSave.rockCount;
}

bool Fuse::IsSwordFusedWithRock() {
    return gFuseSave.swordFusedWithRock;
}

// -----------------------------------------------------------------------------
// Durability (v0: only Sword+Rock)
// -----------------------------------------------------------------------------
static int GetBaseSwordRockDurability() {
    // Placeholder value; tweak later when you finalize balancing.
    return 20;
}

int Fuse::GetSwordFuseDurability() {
    return (int)gFuseSave.swordFuseDurability;
}

int Fuse::GetSwordFuseMaxDurability() {
    return (int)gFuseSave.swordFuseMaxDurability;
}

void Fuse::SetSwordFuseDurability(int v) {
    v = std::clamp(v, 0, 65535);
    gFuseSave.swordFuseDurability = (uint16_t)v;
}

void Fuse::SetSwordFuseMaxDurability(int v) {
    v = std::clamp(v, 0, 65535);
    gFuseSave.swordFuseMaxDurability = (uint16_t)v;
}

void Fuse::ClearSwordFuse() {
    gFuseSave.swordFusedWithRock = false;
    gFuseSave.swordFuseDurability = 0;
    gFuseSave.swordFuseMaxDurability = 0;
}

bool Fuse::DamageSwordFuseDurability(int amount, const char* reason) {
    amount = std::max(amount, 0);

    const int frame = gPlayState ? gPlayState->gameplayFrames : -1;
    const int before = GetSwordFuseDurability();
    int after = before;
    const char* tag = reason ? reason : "unspecified";

    if (!gFuseSave.swordFusedWithRock) {
        Log("[FuseMVP] DamageSwordFuseDurability frame=%d amount=%d durability=%d->%d reason=%s (ignored: not fused)\n",
            frame, amount, before, after, tag);
        return false;
    }

    after = std::max(0, before - amount);
    SetSwordFuseDurability(after);

    const bool broke = (after == 0);

    Log("[FuseMVP] DamageSwordFuseDurability frame=%d amount=%d durability=%d->%d reason=%s%s\n", frame, amount, before,
        after, tag, broke ? " (broke)" : "");

    if (broke) {
        ClearSwordFuse();
        SetLastEvent("Sword fuse broke (durability 0)");
        Log("[FuseMVP] Sword fuse broke (durability 0)\n");
        return true;
    }

    return false;
}

// -----------------------------------------------------------------------------
// MVP: Award ROCK and auto-fuse to sword
// -----------------------------------------------------------------------------
void Fuse::AwardRockMaterial() {
    // Always add one ROCK
    gFuseSave.rockCount++;

    Fuse::SetLastEvent("Acquired ROCK");
    Fuse::Log("[FuseMVP] Acquired material: ROCK (count=%d)\n", (int)gFuseSave.rockCount);

    // Auto-fuse to sword if not already fused
    if (!gFuseSave.swordFusedWithRock) {
        gFuseSave.swordFusedWithRock = true;

        // Initialize durability when fuse is created
        const int base = GetBaseSwordRockDurability();
        SetSwordFuseMaxDurability(base);
        SetSwordFuseDurability(base);

        Fuse::SetLastEvent("Sword fused with ROCK");
        Fuse::Log("[FuseMVP] Sword fused with ROCK (durability %d)\n", base);
    }
}

// -----------------------------------------------------------------------------
// Hooks entrypoints
// -----------------------------------------------------------------------------
void Fuse::OnLoadGame(int32_t /*fileNum*/) {
    // Reset runtime state
    gFuseRuntime = FuseRuntimeState{};
    gFuseRuntime.enabled = true;
    gFuseRuntime.lastEvent = "Loaded";

    // For now, reset save-state too (until we implement persistence)
    // NOTE: When we add serialization, remove this reset.
    gFuseSave = FuseSaveData{};

    Fuse::Log("[FuseMVP] Save loaded -> Fuse ACTIVE (always enabled)\n");
    Fuse::Log("[FuseMVP] MVP: Throw a liftable rock until it BREAKS to acquire ROCK.\n");
}

void Fuse::OnGameFrameUpdate(PlayState* /*play*/) {
    // No per-frame work needed yet
}
