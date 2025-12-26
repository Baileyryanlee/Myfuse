#include "Fuse.h"
#include "FuseMaterials.h"
#include "FuseState.h"
#include "soh/Enhancements/Fuse/Hooks/FuseHooks_Objects.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#endif

extern "C" {
#include "z64.h"
#include "variables.h"
#include "macros.h"
#include "functions.h"
}

// -----------------------------------------------------------------------------
// Module-local state
// -----------------------------------------------------------------------------
static FuseSaveData gFuseSave; // persistent-ready (not serialized yet)
static FuseRuntimeState gFuseRuntime;

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

void ApplyStunToVictim(PlayState* play, Actor* victim, uint8_t level) {
    (void)play;

    if (!victim || level == 0) {
        return;
    }

    // Placeholder effect: leverage existing freeze timer until a dedicated helper is surfaced.
    const int16_t desiredDuration = (int16_t)std::clamp<int>(20 * level, 1, 0x7FFF);
    if (victim->freezeTimer < desiredDuration) {
        victim->freezeTimer = desiredDuration;
    }

    Fuse::Log("[FuseMVP] Stun applied to victim=%p level=%u duration=%d\n", (void*)victim, (unsigned)level,
              (int)desiredDuration);
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

uint16_t Fuse::GetMaterialBaseDurability(MaterialId id) {
    const MaterialDef* def = Fuse::GetMaterialDef(id);
    return def ? def->baseMaxDurability : 0;
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
    switch (id) {
        case MaterialId::Rock:
            return gFuseSave.rockCount;
        case MaterialId::DekuNut:
            return GetDekuNutAmmoCount();
        default:
            return 0;
    }
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

    switch (id) {
        case MaterialId::Rock: {
            const int newCount = std::clamp<int>(gFuseSave.rockCount + amount, 0, 65535);
            gFuseSave.rockCount = (uint16_t)newCount;
            break;
        }
        default:
            break;
    }
}

bool Fuse::ConsumeMaterial(MaterialId id, int amount) {
    if (amount <= 0) {
        return true;
    }

    if (!HasMaterial(id, amount)) {
        return false;
    }

    switch (id) {
        case MaterialId::Rock:
            gFuseSave.rockCount = (uint16_t)(gFuseSave.rockCount - amount);
            return true;
        case MaterialId::DekuNut:
            return ConsumeDekuNutAmmo(amount);
        default:
            break;
    }

    return false;
}

bool Fuse::HasRockMaterial() {
    return HasMaterial(MaterialId::Rock);
}

int Fuse::GetRockCount() {
    return GetMaterialCount(MaterialId::Rock);
}

bool Fuse::IsSwordFused() {
    return gFuseSave.swordFuseMaterialId != MaterialId::None && gFuseSave.swordFuseDurability > 0;
}

MaterialId Fuse::GetSwordMaterial() {
    return gFuseSave.swordFuseMaterialId;
}

// -----------------------------------------------------------------------------
// Durability (v0: only Sword+Rock)
// -----------------------------------------------------------------------------
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
    gFuseSave.swordFuseMaterialId = MaterialId::None;
    gFuseSave.swordFuseDurability = 0;
    gFuseSave.swordFuseMaxDurability = 0;
}

void Fuse::FuseSwordWithMaterial(MaterialId id, uint16_t maxDurability) {
    gFuseSave.swordFuseMaterialId = id;
    gFuseSave.swordFuseMaxDurability = maxDurability;
    gFuseSave.swordFuseDurability = maxDurability;

    const MaterialDef* def = Fuse::GetMaterialDef(id);
    if (def) {
        Fuse::SetLastEvent(def->name);
    } else {
        Fuse::SetLastEvent("Sword fused with material");
    }

    Fuse::Log("[FuseMVP] Sword fused with material=%d (durability %u)\n", static_cast<int>(id),
              (unsigned int)maxDurability);
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

    Fuse::FuseSwordWithMaterial(id, Fuse::GetMaterialBaseDurability(id));

    if (id == MaterialId::DekuNut) {
        const int postConsumeCount = Fuse::GetMaterialCount(id);
        Fuse::Log("[FuseMVP] TryFuseSword(DekuNut): before=%d after=%d\n", preConsumeCount, postConsumeCount);
    }

    return FuseResult::Ok;
}

Fuse::FuseResult Fuse::TryUnfuseSword() {
    if (!Fuse::IsSwordFused()) {
        return FuseResult::Ok;
    }

    Fuse::ClearSwordFuse();
    return FuseResult::Ok;
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
        ClearSwordFuse();
        const int frame = play ? play->gameplayFrames : -1;
        Log("[FuseMVP] Sword fuse broke at frame=%d; clearing fuse and reverting to vanilla (reason=%s)\n", frame,
            reason ? reason : "unknown");
        OnSwordFuseBroken(play);
        return true;
    }

    return false;
}

void Fuse::OnSwordFuseBroken(PlayState* play) {
    SetLastEvent("Sword fuse broke");
    FuseHooks::RestoreSwordHitboxVanillaNow(play);
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

    // For now, reset save-state too (until we implement persistence)
    // NOTE: When we add serialization, remove this reset.
    gFuseSave = FuseSaveData{};

    Fuse::Log("[FuseMVP] Save loaded -> Fuse ACTIVE (always enabled)\n");
    Fuse::Log("[FuseMVP] MVP: Throw a liftable rock until it BREAKS to acquire ROCK.\n");
}

void Fuse::OnGameFrameUpdate(PlayState* /*play*/) {
    // No per-frame work needed yet
}

void Fuse::OnSwordMeleeHit(PlayState* play, Actor* victim) {
    if (!Fuse::IsSwordFused() || !victim) {
        return;
    }

    const MaterialDef* def = Fuse::GetMaterialDef(Fuse::GetSwordMaterial());
    if (!def) {
        return;
    }

    uint8_t stunLevel = 0;
    if (HasModifier(def->modifiers, def->modifierCount, ModifierId::Stun, &stunLevel) && stunLevel > 0) {
        ApplyStunToVictim(play, victim, stunLevel);
    }
}
