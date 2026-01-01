#include "Fuse.h"
#include "FuseMaterials.h"
#include "FuseState.h"
#include "soh/Enhancements/Fuse/Hooks/FuseHooks_Objects.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <limits>
#include <unordered_map>
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

// -----------------------------------------------------------------------------
// Module-local state
// -----------------------------------------------------------------------------
static FuseSaveData gFuseSave; // persistent-ready (not serialized yet)
static FuseRuntimeState gFuseRuntime;
static std::unordered_map<MaterialId, uint16_t> sMaterialInventory;
static bool sMaterialInventoryInitialized = false;

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
        maxDurability = Fuse::GetMaterialBaseDurability(materialId);
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
    gFuseRuntime.swordFuseLoadedFromSave = false;
}

void Fuse::FuseSwordWithMaterial(MaterialId id, uint16_t maxDurability, bool initializeCurrentDurability,
                                 bool logDurability) {
    gFuseSave.swordFuseMaterialId = id;
    gFuseSave.swordFuseMaxDurability = maxDurability;

    const bool shouldInitialize = initializeCurrentDurability && !gFuseRuntime.swordFuseLoadedFromSave;

    if (shouldInitialize) {
        gFuseSave.swordFuseDurability = maxDurability;
    } else {
        gFuseSave.swordFuseDurability = std::clamp<uint16_t>(gFuseSave.swordFuseDurability, 0, maxDurability);
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
                  (unsigned int)gFuseSave.swordFuseDurability, (unsigned int)maxDurability);
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

    Fuse_ClearSavedSwordFuse(nullptr);
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
        Fuse_ClearSavedSwordFuse(play);
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

    EnsureMaterialInventoryInitialized();

    gFuseSave = FuseSaveData{};
    FusePersistence::ApplySwordStateFromContext(nullptr);

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
        ApplyDekuNutStunVanilla(play, GET_PLAYER(play), victim, stunLevel);
    }
}
