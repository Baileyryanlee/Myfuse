#include "Fuse.h"
#include "FuseCBridge.h"
#include "FuseMaterials.h"
#include "FuseState.h"
#include "soh/Enhancements/Fuse/Hooks/FuseHooks_Objects.h"
#include "soh/Enhancements/Fuse/ShieldBashRules.h"
#include "soh/SaveManager.h"
#include "libultraship/bridge/consolevariablebridge.h"

#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <array>
#include <cmath>
#include <cstdint>
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
#include "z64actor.h"
#include "variables.h"
#include "macros.h"
#include "functions.h"
}

#include "src/overlays/actors/ovl_En_Dekubaba/z_en_dekubaba.h"
#include "src/overlays/actors/ovl_En_Bom/z_en_bom.h"
#include "src/overlays/actors/ovl_En_Tite/z_en_tite.h"
#include "src/overlays/actors/ovl_En_Zf/z_en_zf.h"
#include "src/overlays/actors/ovl_En_Arrow/z_en_arrow.h"
#include "src/overlays/actors/ovl_En_Fuse_Beam/z_en_fuse_beam.h"
extern "C" PlayState* gPlayState;
using Fuse::MaterialDebugOverride;

// -----------------------------------------------------------------------------
// Module-local state
// -----------------------------------------------------------------------------

struct FuseSeekState {
    int acquireDelayFramesRemaining = 0;
    bool hasAcquired = false;
    bool isSeekingActive = false;
    Actor* targetActor = nullptr;
    bool loggedNoTarget = false;
    bool loggedStop = false;
    bool loggedSteer = false;
    Vec3f prevPos{};
    bool hasPrevPos = false;
    int ticksSinceAcquire = 0;
};

struct FuseBurnState {
    int endFrame = -1;
    int nextTickFrame = -1;
    int ticksRemaining = 0;
    int tickDamage = 0;
    bool burnVfxActive = false;
    s16 burnVfxColorFlag = 0;
    s16 burnVfxIntensity = 0;
    s16 burnVfxXlu = 0;
    s16 burnVfxDuration = 0;
    u16 burnVfxParams = 0;
};

struct FuseShieldBeamState {
    bool active = false;
    Vec3f start{};
    Vec3f end{};
    int nextDamageFrame = -1;
    int nextDrainFrame = -1;
    int boostUntilFrame = -1;
    s16 sweepYaw = 0;
    bool sweepInitialized = false;
    bool turretModeActive = false;
};

struct FuseSwordBeamState {
    bool active = false;
    bool swingActive = false;
    bool swingConsumedDrain = false;
    int swordItemId = ITEM_NONE;
    std::unordered_set<Actor*> hitVictims;
    Vec3f start{};
    Vec3f end{};
};

static FuseSaveData gFuseSave; // persistent-ready (not serialized yet)
static FuseRuntimeState gFuseRuntime;
static bool sSwordSlotsLoadedFromSaveManager = false;
static bool sHammerSlotLoadedFromSaveManager = false;
static FuseSlot sLoadedHammerSlot;
static std::array<RangedFuseState, 3> gRangedQueued;
static std::array<RangedFuseState, 3> gRangedActive;
static constexpr std::array<RangedFuseSlot, 3> kRangedSlots = { RangedFuseSlot::Arrows, RangedFuseSlot::Slingshot,
                                                                RangedFuseSlot::Hookshot };
static std::unordered_map<MaterialId, uint16_t> sMaterialInventory;
static bool sMaterialInventoryInitialized = false;
static constexpr size_t kSwordFreezeQueueCount = 2;
static constexpr s16 kFreezeDurationFramesBase = 120;
static constexpr float kFreezeShatterDamageMult = 1.5f;
static constexpr float kFreezeShatterKnockbackSpeed = 18.0f;
static constexpr float kFreezeShatterKnockbackYBoost = 3.0f;
// TODO: revert burn duration to 60 frames after validation.
static constexpr int kBurnDurationFrames = 120;
static constexpr int kBurnTickIntervalFrames = 30;
static constexpr int kBurnDefaultTicks = 3;
static constexpr int kBurnRangedTicks = 2;
static constexpr int kBurnTickDamage = 2;
static constexpr s16 kBurnVfxColorFlag = 0x4000;
static constexpr s16 kBurnVfxIntensity = 200;
static constexpr s16 kBurnVfxXlu = 0;
static constexpr s16 kBurnVfxDurationFrames = 30;
static constexpr float kBeamRange = 1600.0f;
static constexpr int kBeamTickIntervalFrames = 15;
static constexpr int kBeamDamagePerTick = 2;
static constexpr float kBeamMinForwardDot = 0.85f;
static constexpr float kBeamWidthNormal = 1.0f;
static constexpr float kBeamWidthBoosted = 2.0f;
static constexpr float kBeamDamageRadiusNormal = 55.0f;
static constexpr float kBeamDamageRadiusBoosted = 110.0f;
static constexpr int kShieldBeamBoostDurationFrames = 30;
static constexpr int kShieldBeamBoostExtraDrain = 5;
static constexpr int kShieldBeamBoostDamageMult = 3;
static constexpr float kShieldBeamBoostWidthMult = 2.0f;
static constexpr float kBeamShieldWidthMin = 0.10f;
static constexpr float kBeamShieldWidthMax = 3.00f;
static constexpr float kShieldBeamChildHylianCrouchBaseBackOffset = 20.0f;
static constexpr int kSwordBeamDurabilityDrainPerSwing = 4;
static constexpr int kFuseDbgLogIntervalFrames = 20;
static constexpr int kShatterImpulseFrames = 5;
static constexpr float kShatterImpulseStep = 3.5f;
static constexpr float kShatterImpulseY = 0.0f;
static std::unordered_map<MaterialId, MaterialDebugOverride> sMaterialDebugOverrides;
static bool sUseDebugOverrides = false;
static std::unordered_map<Actor*, s16> sFuseFrozenTimers;
static std::unordered_map<Actor*, int> sFreezeAppliedFrame;
static std::unordered_map<Actor*, int32_t> sFreezeShatterFrame;
static Actor* sFreezeShatterDamageVictim = nullptr;
static int32_t sFreezeShatterDamageFrame = -1;
static std::unordered_map<Actor*, int32_t> sFreezeLastShatterFrame;
static std::unordered_map<Actor*, int32_t> sFreezeNoReapplyUntilFrame;
static std::unordered_map<Actor*, int> sShatterImpulseUntilFrame;
static std::unordered_map<Actor*, Vec3f> sShatterImpulseDir;
static std::unordered_map<Actor*, s16> sShatterImpulseYaw;
static std::unordered_set<Actor*> sShatterImpulseFlipped;
static std::unordered_map<Actor*, float> sFuseFrozenOrigGravity;
static std::unordered_map<Actor*, FuseBurnState> sBurnStates;
static std::unordered_set<Actor*> sHpOverrideApplied;
static constexpr int32_t kFreezeNoReapplyFrames = 12;
static std::unordered_map<Actor*, Vec3f> sFuseFrozenPos;
static std::unordered_map<Actor*, bool> sFuseFrozenPinned;
static std::unordered_map<uintptr_t, Vec3f> sProjPrevPos;
static std::unordered_map<Actor*, FuseSeekState> sSeekStates;
static FuseShieldBeamState sShieldBeamState;
static Actor* sShieldBeamActor = nullptr;
static FuseSwordBeamState sSwordBeamState;
static Actor* sSwordBeamActor = nullptr;
static int gLastSwordBgExplodeFrame = -999;
static int gLastSwordActorExplodeFrame = -999999;
static uintptr_t sCachedBeamosVmSeg06 = 0;
static int sCachedBeamosVmFrame = -1;

extern "C" void Fuse_SetCachedBeamosVmSeg06(uintptr_t seg06, int frame) {
    if (seg06 != 0) {
        sCachedBeamosVmSeg06 = seg06;
        sCachedBeamosVmFrame = frame;
    }
}

extern "C" uintptr_t Fuse_GetCachedBeamosVmSeg06(void) {
    return sCachedBeamosVmSeg06;
}

extern "C" int Fuse_GetCachedBeamosVmSeg06Frame(void) {
    return sCachedBeamosVmFrame;
}

static inline bool Fuse_LogDbgEnabled() {
    return CVarGetInteger("gFuseLogDbg", 0) != 0;
}

static inline bool Fuse_LogMvpEnabled() {
    return CVarGetInteger("gFuseLogMvp", 0) != 0;
}

#define FUSE_LOG_DBG(...)           \
    do {                            \
        if (Fuse_LogDbgEnabled()) { \
            Fuse::Log(__VA_ARGS__); \
        }                           \
    } while (0)

#define FUSE_LOG_MVP(...)           \
    do {                            \
        if (Fuse_LogMvpEnabled()) { \
            Fuse::Log(__VA_ARGS__); \
        }                           \
    } while (0)

static inline bool Fuse_SeekDebugEnabled() {
    return CVarGetInteger("gFuseSeekDebug", 0) != 0;
}

static void Fuse_RegisterSeekCVars() {
    static bool registered = false;
    if (registered) {
        return;
    }
    registered = true;

    // Console syntax reminder: use `set <name> <value>` (no '=' sign).
    // Aggressive turning is safe thanks to behind-gate checks; extra turn scaling
    // normalizes behavior for slower slingshot projectiles versus the bow.
    CVarRegisterFloat("gFuseSeekRadius", 1500.0f);
    CVarRegisterFloat("gFuseSeekDotMin", 0.60f);
    CVarRegisterFloat("gFuseSeekMaxTurnDeg", 16.0f);
    CVarRegisterFloat("gFuseSeekTurnScaleFar", 1.9f);
    CVarRegisterInteger("gFuseSeekAcquireDelay", 2);
    CVarRegisterInteger("gFuseSeekDebug", 0);
    CVarRegisterInteger("gFuseSeekDisableStop", 0);
    CVarRegisterInteger("gFuseSeekStopGraceTicks", 2);
}

static void Fuse_RegisterShieldBeamCVars() {
    static bool registered = false;
    if (registered) {
        return;
    }
    registered = true;

    CVarRegisterFloat("gFuseBeamShieldAdultOffsetX", 6.0f);
    CVarRegisterFloat("gFuseBeamShieldAdultOffsetY", 10.0f);
    CVarRegisterFloat("gFuseBeamShieldAdultOffsetZ", 24.0f);
    CVarRegisterFloat("gFuseBeamShieldChildOffsetX", 4.0f);
    CVarRegisterFloat("gFuseBeamShieldChildOffsetY", 8.0f);
    CVarRegisterFloat("gFuseBeamShieldChildOffsetZ", 20.0f);
    CVarRegisterFloat("gFuseBeamShieldChildHylianCrouchOffsetX", 3.5f);
    CVarRegisterFloat("gFuseBeamShieldChildHylianCrouchOffsetY", 7.0f);
    CVarRegisterFloat("gFuseBeamShieldChildHylianCrouchOffsetZ", 18.0f);
    CVarRegisterFloat("gFuseBeamShieldChildHylianCrouchLeanOffsetX", 2.0f);
    CVarRegisterFloat("gFuseBeamShieldChildHylianCrouchLeanOffsetZ", 2.0f);
    CVarRegisterFloat("gFuseBeamShieldChildHylianCrouchSweepSpeedDeg", 1.2f);
    CVarRegisterFloat("gFuseBeamShieldScaleX", 0.35f);
    CVarRegisterInteger("gFuseBeamShieldZTargetPitchDown", 0);
    CVarRegisterInteger("gFuseBeamShieldDebug", 0);
}

static void Fuse_RegisterSwordBeamCVars() {
    static bool registered = false;
    if (registered) {
        return;
    }
    registered = true;

    CVarRegisterFloat("gFuseBeamSwordOffsetX", 0.0f);
    CVarRegisterFloat("gFuseBeamSwordOffsetY", 8.0f);
    CVarRegisterFloat("gFuseBeamSwordOffsetZ", 12.0f);
    CVarRegisterFloat("gFuseBeamSwordRange", kBeamRange);
    CVarRegisterFloat("gFuseBeamSwordScaleX", kBeamWidthNormal);
}

static inline float Fuse_Vec3fLength(const Vec3f& value) {
    return sqrtf(value.x * value.x + value.y * value.y + value.z * value.z);
}

static inline Vec3f Fuse_Vec3fNormalize(const Vec3f& value, float* outLength = nullptr) {
    const float length = Fuse_Vec3fLength(value);
    if (outLength) {
        *outLength = length;
    }
    if (length <= 0.0001f) {
        return Vec3f{ 0.0f, 0.0f, 0.0f };
    }
    const float inv = 1.0f / length;
    return Vec3f{ value.x * inv, value.y * inv, value.z * inv };
}

static inline float Fuse_Vec3fDot(const Vec3f& a, const Vec3f& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static inline Vec3f Fuse_GetArrowEffectiveDir(Actor* proj, float* outSpeed) {
    const s16 yaw = proj->world.rot.y;
    const float sinYaw = Math_SinS(yaw);
    const float cosYaw = Math_CosS(yaw);
    Vec3f effVel{ sinYaw * proj->speedXZ, proj->velocity.y, cosYaw * proj->speedXZ };
    return Fuse_Vec3fNormalize(effVel, outSpeed);
}

static inline void Fuse_ApplyArrowSteer(Actor* proj, const Vec3f& newDir, float speed) {
    const float horiz = sqrtf((newDir.x * newDir.x) + (newDir.z * newDir.z));
    const float newSpeedXZ = speed * horiz;
    const float newVelY = speed * newDir.y;
    const s16 yawS = Math_Atan2S(newDir.z, newDir.x);
    s16 pitchS = 0;
    if (horiz > 0.0001f) {
        pitchS = Math_Atan2S(-newDir.y, horiz);
    }

    proj->world.rot.y = yawS;
    proj->shape.rot.y = yawS;
    proj->world.rot.x = pitchS;
    proj->shape.rot.x = pitchS;
    proj->speedXZ = newSpeedXZ;
    proj->velocity.y = newVelY;
    proj->velocity.x = Math_SinS(yawS) * newSpeedXZ;
    proj->velocity.z = Math_CosS(yawS) * newSpeedXZ;
}

static inline u16 Fuse_MakeColorFilterParams(s16 colorFlag, s16 colorIntensityMax, s16 xluFlag, s16 duration) {
    return static_cast<u16>(colorFlag | xluFlag | ((colorIntensityMax & 0xF8) << 5) | (duration & 0xFF));
}

bool Fuse_IsBombableActorId(s16 id) {
    switch (id) {
        case ACTOR_BG_BREAKWALL:
        case ACTOR_BG_HIDAN_KOWARERUKABE:
        case ACTOR_BG_BOMBWALL:
        case ACTOR_OBJ_BOMBIWA:
        case ACTOR_BG_JYA_BOMBIWA:
        case ACTOR_BG_MIZU_BWALL:
            return true;
        default:
            return false;
    }
}

bool Fuse_IsExplosionImmuneVictim(const Actor* victim) {
    return victim && victim->id == ACTOR_BOSS_DODONGO;
}

static bool EnFirefly_IsFireVariant(const Actor* victim) {
    if (!victim || victim->id != ACTOR_EN_FIREFLY) {
        return false;
    }

    // ovl_En_Firefly uses params low 15 bits as KeeseType after stripping 0x8000 lens bit in Init.
    // KeeseType: 0 = KEESE_FIRE_FLY, 1 = KEESE_FIRE_PERCH (both should be burn-immune).
    constexpr s16 kKeeseTypeMask = 0x7FFF;
    constexpr s16 kKeeseFirePerchType = 1;
    const s16 fireflyType = static_cast<s16>(victim->params & kKeeseTypeMask);
    return fireflyType <= kKeeseFirePerchType;
}

static bool EnBb_IsRedVariant(const Actor* victim) {
    if (!victim || victim->id != ACTOR_EN_BB) {
        return false;
    }

    // ovl_En_Bb EnBbType: ENBB_RED is -2 in params after init-time decoding/sign-extension.
    constexpr s16 kEnBbRedType = -2;
    return static_cast<s16>(victim->params) == kEnBbRedType;
}

static bool Fuse_IsBurnImmuneActor(const Actor* victim) {
    if (!victim) {
        return false;
    }

    switch (victim->id) {
        case ACTOR_EN_BW:        // Torch Slug
        case ACTOR_EN_FD:        // Flare Dancer
        case ACTOR_EN_AM:        // Armos
        case ACTOR_EN_VM:        // Beamos
        case ACTOR_EN_DODONGO:   // Dodongo
        case ACTOR_EN_DODOJR:    // Baby Dodongo
        case ACTOR_BOSS_DODONGO: // King Dodongo
        case ACTOR_BOSS_FD:      // Volvagia (Flying)
        case ACTOR_BOSS_FD2:     // Volvagia (Hole Form)
            return true;

        case ACTOR_EN_FIREFLY:
            return EnFirefly_IsFireVariant(victim);

        case ACTOR_EN_BB:
            return EnBb_IsRedVariant(victim);

        default:
            return false;
    }
}

Actor* Fuse_FindNearbyBombable(PlayState* play, const Vec3f* pos, float radius) {
    if (!play || !pos || radius <= 0.0f) {
        return nullptr;
    }

    const float radiusSq = radius * radius;
    Actor* nearest = nullptr;
    float nearestDistSq = radiusSq;

    for (int i = 0; i < ACTORCAT_MAX; ++i) {
        Actor* actor = play->actorCtx.actorLists[i].head;
        while (actor != nullptr) {
            if (Fuse_IsBombableActorId(actor->id)) {
                const float dx = actor->world.pos.x - pos->x;
                const float dy = actor->world.pos.y - pos->y;
                const float dz = actor->world.pos.z - pos->z;
                const float distSq = (dx * dx) + (dy * dy) + (dz * dz);
                if (distSq <= nearestDistSq) {
                    nearestDistSq = distSq;
                    nearest = actor;
                }
            }
            actor = actor->next;
        }
    }

    return nearest;
}

extern "C" void Fuse_AdjustExplosionPosForBombable(const Actor* victim, const Actor* source, Vec3f* ioPos) {
    if (!victim || !ioPos || !Fuse_IsBombableActorId(victim->id)) {
        return;
    }

    if (Fuse_IsZeroishPos(*ioPos)) {
        *ioPos = Fuse_GetBombableAnchorPos(victim, 25.0f);
    }

    Vec3f from = *ioPos;
    Vec3f dir{ ioPos->x - victim->world.pos.x, 0.0f, ioPos->z - victim->world.pos.z };
    float distSq = (dir.x * dir.x) + (dir.z * dir.z);
    if (distSq < 0.0001f) {
        if (source) {
            dir.x = victim->world.pos.x - source->world.pos.x;
            dir.z = victim->world.pos.z - source->world.pos.z;
            distSq = (dir.x * dir.x) + (dir.z * dir.z);
        }
        if (distSq < 0.0001f) {
            dir.x = 0.0f;
            dir.z = 1.0f;
            distSq = 1.0f;
        }
    }

    const float invLen = 1.0f / sqrtf(distSq);
    dir.x *= invLen;
    dir.z *= invLen;

    ioPos->x += dir.x * 8.0f;
    ioPos->z += dir.z * 8.0f;
    ioPos->y += 6.0f;
    if (ioPos->y < victim->world.pos.y + 20.0f) {
        ioPos->y = victim->world.pos.y + 20.0f;
    }

    FUSE_LOG_DBG("[FuseDBG] ExplodeNudge: victim=0x%04X from=(%.2f %.2f %.2f) to=(%.2f %.2f %.2f)\n", victim->id,
                 from.x, from.y, from.z, ioPos->x, ioPos->y, ioPos->z);
}
static constexpr int kDekuStunInitialDelayFrames = 4;
static constexpr int kDekuStunRetryStepFrames = 2;
static constexpr int kDekuStunMaxAttempts = 8;
static constexpr int kDekuStunSwordIFrameFrames = 6;
static constexpr int kDekuStunCooldownFrames = 90;
struct SwordFreezeRequest {
    Actor* victim = nullptr;
    uint8_t level = 0;
};

struct PendingStunRequest {
    Actor* victim = nullptr;
    uint8_t level = 0;
    int applyNotBeforeFrame = -1;
    int attemptsRemaining = 0;
    int retryStepFrames = 0;
    MaterialId materialId = MaterialId::None;
    int itemId = 0;
};

static std::vector<PendingStunRequest> sPendingStunQueue;
static std::unordered_map<Actor*, size_t> sPendingStunIndex;
static std::unordered_map<Actor*, int> sDekuStunCooldownUntil;
static std::unordered_map<Actor*, int> sDekuLastSwordHitFrame;
static int sMegaStunCooldownUntil = -1;

static std::array<std::vector<SwordFreezeRequest>, kSwordFreezeQueueCount> sSwordFreezeQueues;
static std::array<std::unordered_set<Actor*>, kSwordFreezeQueueCount> sSwordFreezeVictims;
static std::array<int, kSwordFreezeQueueCount> sSwordFreezeQueueFrames = { -1, -1 };

static Vec3f Fuse_GetPosInFrontOfPlayer(PlayState* play, float forward, float up) {
    Vec3f pos{ 0.0f, 0.0f, 0.0f };
    if (!play) {
        return pos;
    }

    Player* player = GET_PLAYER(play);
    if (!player) {
        return pos;
    }

    pos = player->actor.world.pos;
    const s16 yaw = player->actor.shape.rot.y;
    pos.x += Math_SinS(yaw) * forward;
    pos.z += Math_CosS(yaw) * forward;
    pos.y += up;
    return pos;
}

static Vec3f GetPlayerImpactPos(Player* player, float forward, float up) {
    Vec3f pos{ 0.0f, 0.0f, 0.0f };
    if (!player) {
        return pos;
    }

    pos = player->actor.world.pos;
    const s16 yaw = player->actor.shape.rot.y;
    pos.x += Math_SinS(yaw) * forward;
    pos.z += Math_CosS(yaw) * forward;
    pos.y += up;
    return pos;
}

static float DistSqPointToSegmentXZ(const Vec3f* p, const Vec3f* a, const Vec3f* b, float* outT) {
    if (!p || !a || !b) {
        if (outT) {
            *outT = 0.0f;
        }
        return 0.0f;
    }

    const float abx = b->x - a->x;
    const float abz = b->z - a->z;
    const float apx = p->x - a->x;
    const float apz = p->z - a->z;
    const float lenSq = (abx * abx) + (abz * abz);
    float t = 0.0f;
    if (lenSq > 0.0001f) {
        t = (apx * abx + apz * abz) / lenSq;
        if (t < 0.0f) {
            t = 0.0f;
        } else if (t > 1.0f) {
            t = 1.0f;
        }
    }

    const float cx = a->x + (abx * t);
    const float cz = a->z + (abz * t);
    const float dx = p->x - cx;
    const float dz = p->z - cz;
    if (outT) {
        *outT = t;
    }
    return (dx * dx) + (dz * dz);
}

static bool IsPlayerSwingingSword(const Player* player) {
    return player && player->meleeWeaponState != 0;
}

static bool IsFuseFrozenInternal(Actor* actor) {
    if (actor == nullptr) {
        return false;
    }

    auto it = sFuseFrozenTimers.find(actor);
    return it != sFuseFrozenTimers.end() && it->second > 0;
}

static bool WasFreezeAppliedRecentlyInternal(Actor* actor, int frame, int windowFrames) {
    if (!actor || frame < 0) {
        return false;
    }

    const auto it = sFreezeAppliedFrame.find(actor);
    if (it == sFreezeAppliedFrame.end()) {
        return false;
    }

    const int dt = frame - it->second;
    return dt >= 0 && dt <= windowFrames;
}

static bool WasFreezeRecentlyShattered(PlayState* play, Actor* victim) {
    if (!play || !victim) {
        return false;
    }

    const auto it = sFreezeLastShatterFrame.find(victim);
    if (it == sFreezeLastShatterFrame.end()) {
        return false;
    }

    const int32_t dt = play->gameplayFrames - it->second;
    return dt >= 0 && dt <= 1;
}

static bool IsFreezeReapplyBlocked(PlayState* play, Actor* victim) {
    if (!play || !victim) {
        return false;
    }

    auto it = sFreezeNoReapplyUntilFrame.find(victim);
    if (it == sFreezeNoReapplyUntilFrame.end()) {
        return false;
    }

    if (static_cast<int64_t>(play->gameplayFrames) < it->second) {
        return true;
    }

    sFreezeNoReapplyUntilFrame.erase(it);
    return false;
}

static void RemoveDeferredFreezeRequestsFor(Actor* victim) {
    if (!victim) {
        return;
    }

    for (size_t i = 0; i < kSwordFreezeQueueCount; i++) {
        auto& queue = sSwordFreezeQueues[i];
        if (!queue.empty()) {
            const auto newEnd = std::remove_if(queue.begin(), queue.end(), [victim](const SwordFreezeRequest& request) {
                return request.victim == victim;
            });
            if (newEnd != queue.end()) {
                queue.erase(newEnd, queue.end());
            }
        }

        sSwordFreezeVictims[i].erase(victim);
    }
}

static bool IsActorFrozenInternal(Actor* actor) {
    return IsFuseFrozenInternal(actor) || (actor && actor->freezeTimer > 0);
}

bool Fuse::IsFuseFrozen(Actor* actor) {
    return IsFuseFrozenInternal(actor);
}

extern "C" bool Fuse_IsActorFuseFrozen(Actor* actor) {
    return IsFuseFrozenInternal(actor);
}

static void ClearFuseFreeze(Actor* actor) {
    if (!actor) {
        return;
    }

    auto gravIt = sFuseFrozenOrigGravity.find(actor);
    if (gravIt != sFuseFrozenOrigGravity.end()) {
        actor->gravity = gravIt->second;
        sFuseFrozenOrigGravity.erase(gravIt);
    }

    sFuseFrozenTimers.erase(actor);
    sFreezeAppliedFrame.erase(actor);
    sFreezeShatterFrame.erase(actor);
    sFuseFrozenPos.erase(actor);
    sFuseFrozenPinned.erase(actor);
    actor->colorFilterTimer = 0;
}

static bool IsActorAliveInPlay(PlayState* play, Actor* target);
static FuseItemType RangedSlotItemType(RangedFuseSlot slot);
static int GetMaterialEffectiveBaseDurabilityForItem(MaterialId id, FuseItemType itemType);
static void ClearSwordBeamRuntimeState();
static void TickShieldGuardBeam(PlayState* play);
static void BeginSwordBeamSwing(PlayState* play, Player* player, int32_t q0, int32_t q1);
static void TickSwordSwingBeam(PlayState* play, Player* player, int32_t q0, int32_t q1);
static void EndSwordBeamSwing(PlayState* play, Player* player, int32_t q0, int32_t q1);
static void LogSwordBeamDbg(const char* phase, PlayState* play, Player* player, int32_t q0, int32_t q1, bool eligible);
static void Fuse_DrawShieldBeam(PlayState* play, Gfx** polyOpaDisp, Gfx**);

void TickBurnTimers(PlayState* play);

void Fuse::TickStatusEffects(PlayState* play) {
    TickBurnTimers(play);
}

static void Fuse_BurnTryApplyVfx(Actor* victim, int frames) {
    if (!victim) {
        return;
    }

    const auto it = sBurnStates.find(victim);
    if (it == sBurnStates.end()) {
        return;
    }

    FuseBurnState& state = it->second;
    const s16 duration = static_cast<s16>(std::clamp(frames, 1, 255));
    const u16 desiredParams = Fuse_MakeColorFilterParams(kBurnVfxColorFlag, kBurnVfxIntensity, kBurnVfxXlu, duration);

    if (victim->colorFilterTimer > 0) {
        const bool matchesBurn = state.burnVfxActive && (victim->colorFilterParams == state.burnVfxParams);
        if (!matchesBurn) {
            FUSE_LOG_DBG("[FuseDBG] BurnVfxSkip: victim=%p reason=other-filter\n", (void*)victim);
            return;
        }
    }

    Actor_SetColorFilter(victim, kBurnVfxColorFlag, kBurnVfxIntensity, kBurnVfxXlu, duration);
    state.burnVfxActive = true;
    state.burnVfxColorFlag = kBurnVfxColorFlag;
    state.burnVfxIntensity = kBurnVfxIntensity;
    state.burnVfxXlu = kBurnVfxXlu;
    state.burnVfxDuration = duration;
    state.burnVfxParams = desiredParams;
}

static void TickBurnTimers(PlayState* play) {
    if (!play) {
        sBurnStates.clear();
        return;
    }

    const int curFrame = play->gameplayFrames;
    if (curFrame < 0) {
        return;
    }

    for (auto it = sBurnStates.begin(); it != sBurnStates.end();) {
        Actor* victim = it->first;
        FuseBurnState& state = it->second;

        if (!IsActorAliveInPlay(play, victim)) {
            it = sBurnStates.erase(it);
            continue;
        }

        if (curFrame >= state.endFrame || state.ticksRemaining <= 0) {
            it = sBurnStates.erase(it);
            continue;
        }

        if (curFrame >= state.nextTickFrame && state.ticksRemaining > 0) {
            const int prevDamage = victim->colChkInfo.damage;
            const int hpBefore = victim->colChkInfo.health;
            const int tickDamage = state.tickDamage > 0 ? state.tickDamage : kBurnTickDamage;
            victim->colChkInfo.damage = tickDamage;
            Actor_ApplyDamage(victim);
            victim->colChkInfo.damage = prevDamage;
            const int hpAfter = victim->colChkInfo.health;

            state.ticksRemaining--;
            state.nextTickFrame = curFrame + kBurnTickIntervalFrames;

            if (hpAfter == hpBefore && hpBefore > 0 && victim->category != ACTORCAT_BOSS) {
                const int adjustedHealth = std::max(0, hpBefore - tickDamage);
                victim->colChkInfo.health = static_cast<uint8_t>(adjustedHealth);
            }

            Fuse_BurnTryApplyVfx(victim, kBurnVfxDurationFrames);
            FUSE_LOG_DBG("[FuseDBG] BurnTick: victim=%p id=0x%04X dmg=%d ticksLeft=%d\n", (void*)victim, victim->id,
                         tickDamage, state.ticksRemaining);
        }

        ++it;
    }
}

bool Fuse::TryFreezeShatterWithDamage(PlayState* play, Actor* victim, Actor* attacker, int itemId,
                                      MaterialId materialId, int baseWeaponDamage, const char* srcLabel) {
    if (!victim) {
        return false;
    }

    const int frame = play ? play->gameplayFrames : -1;
    const bool freezeAppliedRecently = WasFreezeAppliedRecentlyInternal(victim, frame, 3);
    if (!IsActorFrozenInternal(victim) || freezeAppliedRecently) {
        if (freezeAppliedRecently) {
            int dt = -1;
            const auto it = sFreezeAppliedFrame.find(victim);
            if (it != sFreezeAppliedFrame.end()) {
                dt = frame - it->second;
            }
            FUSE_LOG_DBG("[FuseDBG] FreezeShatterSkip: reason=RecentlyApplied frame=%d victim=%p dt=%d\n", frame,
                         (void*)victim, dt);
        }
        return false;
    }

    RemoveDeferredFreezeRequestsFor(victim);
    ClearFuseFreeze(victim);
    FUSE_LOG_DBG("[FuseDBG] ShatterUnfreeze: victim=%p restored_grav=%.2f\n", (void*)victim, victim->gravity);
    if (play) {
        sFreezeLastShatterFrame[victim] = play->gameplayFrames;
        sFreezeNoReapplyUntilFrame[victim] = play->gameplayFrames + kFreezeNoReapplyFrames;
    }

    FUSE_LOG_DBG("[FuseDBG] ShatterKB pre: victim=%p vel=(%.2f,%.2f,%.2f) spd=%.2f grav=%.2f\n", (void*)victim,
                 victim->velocity.x, victim->velocity.y, victim->velocity.z, victim->speedXZ, victim->gravity);

    int materialAtk = 0;
    const MaterialDef* def = Fuse::GetMaterialDef(materialId);
    if (def) {
        materialAtk = Fuse::GetMaterialAttackBonus(materialId);
    }

    float damageMult = kFreezeShatterDamageMult;
    // TODO: if attacker has Flame/Burn modifier active, set damageMult = 2.0f.
    const int rawDamage = std::max(0, baseWeaponDamage) + std::max(0, materialAtk);
    const int finalDamage = static_cast<int>(lroundf(rawDamage * damageMult));
    if (finalDamage > 0) {
        const int hpBefore = victim->colChkInfo.health;
        victim->colChkInfo.damage = finalDamage;
        Actor_ApplyDamage(victim);
        const int hpAfter = victim->colChkInfo.health;

        if (hpAfter == hpBefore && hpBefore > 0 && victim->category != ACTORCAT_BOSS) {
            const int adjustedHealth = std::max(0, hpBefore - finalDamage);
            victim->colChkInfo.health = static_cast<uint8_t>(adjustedHealth);
        }
    }

    FUSE_LOG_DBG("[FuseDBG] ShatterKB postDamage: victim=%p vel=(%.2f,%.2f,%.2f) spd=%.2f grav=%.2f\n", (void*)victim,
                 victim->velocity.x, victim->velocity.y, victim->velocity.z, victim->speedXZ, victim->gravity);

    if (frame >= 0) {
        sFreezeShatterFrame[victim] = frame;
        sFreezeLastShatterFrame[victim] = frame;
        sFreezeShatterDamageVictim = victim;
        sFreezeShatterDamageFrame = frame;
    }

    FUSE_LOG_MVP("[FuseMVP] FreezeShatter: src=%s victim=%p item=%d mat=%d base=%d matAtk=%d mult=%.2f final=%d\n",
                 srcLabel ? srcLabel : "unknown", (void*)victim, itemId, static_cast<int>(materialId), baseWeaponDamage,
                 materialAtk, damageMult, finalDamage);

    Actor* sourceActor = nullptr;
    Player* player = (play != nullptr) ? GET_PLAYER(play) : nullptr;
    Actor* playerActor = (player != nullptr) ? &player->actor : nullptr;
    const bool usePlayerAsSource = (srcLabel != nullptr && (!strcmp(srcLabel, "sword") || !strcmp(srcLabel, "shield") ||
                                                            !strcmp(srcLabel, "shield_bash")));

    // Default: use player for melee/shield sources (most consistent with design intent).
    if (usePlayerAsSource && playerActor != nullptr) {
        sourceActor = playerActor;
    } else if (attacker != nullptr) {
        sourceActor = attacker;
    } else if (playerActor != nullptr) {
        sourceActor = playerActor;
    }

    Vec3f away = { 0.0f, 0.0f, 1.0f };
    FUSE_LOG_DBG("[FuseDBG] ShatterSrc: src=%s victim=%p attacker=%p source=%p usePlayer=%d\n",
                 srcLabel ? srcLabel : "unknown", (void*)victim, (void*)attacker, (void*)sourceActor,
                 usePlayerAsSource ? 1 : 0);
    if (sourceActor != nullptr) {
        away.x = victim->world.pos.x - sourceActor->world.pos.x;
        away.z = victim->world.pos.z - sourceActor->world.pos.z;
        float lenSq = (away.x * away.x) + (away.z * away.z);

        if (lenSq > 0.0001f) {
            float inv = 1.0f / sqrtf(lenSq);
            away.x *= inv;
            away.z *= inv;
        } else {
            away.x = 0.0f;
            away.z = 1.0f;
        }
    }

    Vec3f kbDir = away;
    Actor* distRef = sourceActor;
    if (distRef != nullptr && play != nullptr) {
        float dx0 = victim->world.pos.x - distRef->world.pos.x;
        float dz0 = victim->world.pos.z - distRef->world.pos.z;
        float dist0 = dx0 * dx0 + dz0 * dz0;

        float px1 = victim->world.pos.x + kbDir.x * kShatterImpulseStep;
        float pz1 = victim->world.pos.z + kbDir.z * kShatterImpulseStep;
        float dx1 = px1 - distRef->world.pos.x;
        float dz1 = pz1 - distRef->world.pos.z;
        float dist1 = dx1 * dx1 + dz1 * dz1;

        FUSE_LOG_DBG("[FuseDBG] ShatterDirCheck: src=%s victim=%p ref=%p dist0=%.2f dist1=%.2f dir=(%.2f,%.2f)\n",
                     srcLabel ? srcLabel : "unknown", (void*)victim, (void*)distRef, dist0, dist1, kbDir.x, kbDir.z);
    }

    kbDir.x = -kbDir.x;
    kbDir.z = -kbDir.z;
    s16 knockbackYaw = Math_Atan2S(kbDir.x, kbDir.z);

    if (play != nullptr) {
        sShatterImpulseDir[victim] = { kbDir.x, 0.0f, kbDir.z };
        sShatterImpulseUntilFrame[victim] = play->gameplayFrames + kShatterImpulseFrames;
        sShatterImpulseYaw[victim] = knockbackYaw;
        FUSE_LOG_DBG("[FuseDBG] ShatterImpulse start: victim=%p until=%d step=%.2f\n", (void*)victim,
                     sShatterImpulseUntilFrame[victim], kShatterImpulseStep);
    }

    victim->velocity.x = kbDir.x * kFreezeShatterKnockbackSpeed;
    victim->velocity.z = kbDir.z * kFreezeShatterKnockbackSpeed;
    victim->velocity.y = std::max(victim->velocity.y, kFreezeShatterKnockbackYBoost);
    victim->speedXZ = kFreezeShatterKnockbackSpeed;
    victim->world.rot.y = knockbackYaw;
    victim->shape.rot.y = victim->world.rot.y;

    FUSE_LOG_DBG(
        "[FuseDBG] ShatterKB applied: victim=%p source=%p flip=1 dir=(%.2f,%.2f) vel=(%.2f,%.2f,%.2f) spd=%.2f "
        "yaw=%d\n",
        (void*)victim, (void*)sourceActor, kbDir.x, kbDir.z, victim->velocity.x, victim->velocity.y, victim->velocity.z,
        victim->speedXZ, knockbackYaw);
    FUSE_LOG_MVP("[FuseMVP] FreezeShatterKB: src=%s victim=%p vel=(%.2f,%.2f,%.2f) spd=%.2f\n",
                 srcLabel ? srcLabel : "unknown", (void*)victim, victim->velocity.x, victim->velocity.y,
                 victim->velocity.z, victim->speedXZ);

    return true;
}

bool Fuse::TryFreezeShatter(PlayState* play, Actor* victim, Actor* attacker, const char* srcLabel) {
    const int baseWeaponDamage = victim ? std::max(0, static_cast<int>(victim->colChkInfo.damage)) : 0;
    return Fuse::TryFreezeShatterWithDamage(play, victim, attacker, 0, MaterialId::None, baseWeaponDamage, srcLabel);
}

bool Fuse::WasFreezeShatterDamageAppliedThisFrame(PlayState* play, Actor* victim) {
    if (!play || !victim) {
        return false;
    }

    return sFreezeShatterDamageVictim == victim && sFreezeShatterDamageFrame == play->gameplayFrames;
}

void Fuse::ApplyBurn(PlayState* play, Actor* victim, uint8_t level, MaterialId materialId, const char* srcLabel,
                     const char* slotLabel) {
    if (!play || !victim) {
        return;
    }

    if (!FuseBash_IsEnemyActor(victim)) {
        return;
    }

    const int curFrame = play->gameplayFrames;
    if (curFrame < 0) {
        return;
    }

    const int durationFrames = kBurnDurationFrames;
    const bool isRangedSource = srcLabel && strcmp(srcLabel, "ranged") == 0;
    const bool isRangedSlot = slotLabel && (strcmp(slotLabel, "Arrows") == 0 || strcmp(slotLabel, "Slingshot") == 0);
    const bool isRangedFire = isRangedSource && isRangedSlot;
    const int totalTicks = isRangedFire ? kBurnRangedTicks : kBurnDefaultTicks;
    const int tickDamage = kBurnTickDamage;
    const bool immune = Fuse_IsBurnImmuneActor(victim);
    if (isRangedFire) {
        FUSE_LOG_DBG("[FuseDBG] BurnApply: src=ranged slot=%s victim=%p id=0x%04X durFrames=%d ticks=%d tickDmg=%d "
                     "firstTick=vanillaFireBonus\n",
                     slotLabel ? slotLabel : "unknown", (void*)victim, victim->id, durationFrames, totalTicks,
                     tickDamage);
    } else {
        FUSE_LOG_DBG(
            "[FuseDBG] BurnApply: src=%s slot=%s victim=%p id=0x%04X durFrames=%d ticks=%d tickDmg=%d immune=%d\n",
            srcLabel ? srcLabel : "unknown", slotLabel ? slotLabel : "unknown", (void*)victim, victim->id,
            durationFrames, totalTicks, tickDamage, immune ? 1 : 0);
    }

    if (immune) {
        return;
    }

    (void)level;
    (void)materialId;

    auto stateIt = sBurnStates.find(victim);
    const bool hasState = stateIt != sBurnStates.end();
    const bool isActive = hasState && stateIt->second.ticksRemaining > 0 && curFrame < stateIt->second.endFrame;

    if (!isActive) {
        FuseBurnState state{};
        state.endFrame = curFrame + durationFrames;
        state.nextTickFrame = curFrame + kBurnTickIntervalFrames;
        state.ticksRemaining = totalTicks;
        state.tickDamage = tickDamage;
        sBurnStates[victim] = state;
        Fuse_BurnTryApplyVfx(victim, kBurnVfxDurationFrames);
        FUSE_LOG_DBG("[FuseDBG] BurnApplySet: victim=%p end=%d next=%d ticks=%d\n", (void*)victim, state.endFrame,
                     state.nextTickFrame, state.ticksRemaining);
        return;
    }

    FuseBurnState& state = stateIt->second;
    state.endFrame = curFrame + durationFrames;
    if (state.nextTickFrame < curFrame) {
        state.nextTickFrame = curFrame + kBurnTickIntervalFrames;
    }
    state.tickDamage = tickDamage;
    Fuse_BurnTryApplyVfx(victim, kBurnVfxDurationFrames);
    FUSE_LOG_DBG("[FuseDBG] BurnApplySet: victim=%p end=%d next=%d ticks=%d\n", (void*)victim, state.endFrame,
                 state.nextTickFrame, state.ticksRemaining);
}

static void ResetSavedSwordFuseFields() {
    FusePersistence::WriteSwordStateToContext(FusePersistence::ClearedSwordState());
}

extern "C" int32_t Fuse_GetPlayerMeleeHammerizeLevel(PlayState* play) {
    if (!Fuse::IsEnabled()) {
        return 0;
    }

    const SwordFuseSlot& slot = gFuseSave.GetActiveSwordSlot(play);
    if (slot.materialId == MaterialId::None || slot.durabilityCur <= 0) {
        return 0;
    }

    const MaterialDef* def = Fuse::GetMaterialDef(slot.materialId);
    if (!def) {
        return 0;
    }

    uint8_t level = 0;
    if (!HasModifier(def->modifiers, def->modifierCount, ModifierId::Hammerize, &level) || level == 0) {
        return 0;
    }

    return std::min<int32_t>(level, 2);
}

extern "C" float Fuse_GetSwordRangeUpScale(int32_t* outLevel) {
    int32_t level = 0;
    if (Fuse::IsEnabled()) {
        level = Fuse::GetSwordModifierLevel(ModifierId::RangeUp);
    }
    if (outLevel != nullptr) {
        *outLevel = level;
    }
    return Fuse::GetRangeUpScale(static_cast<uint8_t>(level));
}

extern "C" float Fuse_GetBoomerangWideRangeScale(int32_t* outLevel) {
    int32_t level = 0;
    if (Fuse::IsEnabled() && Fuse::IsBoomerangFused()) {
        level = Fuse::GetMaterialModifierLevel(Fuse::GetBoomerangMaterial(), FuseItemType::Boomerang,
                                               ModifierId::WideRange);
    }
    if (outLevel != nullptr) {
        *outLevel = level;
    }
    return Fuse::GetWideRangeScale(static_cast<uint8_t>(level));
}

extern "C" void Fuse_LogSwordRangeUp(int level, float scale) {
    FUSE_LOG_DBG("[FuseDBG] SwordRangeUp: level=%d scale=%.2f\n", level, static_cast<double>(scale));
}

extern "C" void Fuse_LogBoomerangWideRange(int level, float scale) {
    FUSE_LOG_DBG("[FuseDBG] BoomerangWideRange: level=%d scale=%.2f\n", level, static_cast<double>(scale));
}

// -----------------------------------------------------------------------------
// Modifier helpers (module-local)
// -----------------------------------------------------------------------------
namespace {

int GetDekuNutAmmoCount() {
    return AMMO(ITEM_NUT);
}

int GetDekuStickAmmoCount() {
    return AMMO(ITEM_STICK);
}

int GetBombAmmoCount() {
    return AMMO(ITEM_BOMB);
}

bool ConsumeDekuNutAmmo(int amount) {
    if (amount <= 0) {
        return true;
    }

    const int cur = GetDekuNutAmmoCount();
    FUSE_LOG_MVP("[FuseMVP] Consume DekuNut: cur=%d amount=%d\n", cur, amount);

    if (cur < amount) {
        return false;
    }

    const int newCount = std::max(0, cur - amount);
    const int delta = newCount - cur;

    if (delta != 0) {
        Inventory_ChangeAmmo(ITEM_NUT, delta);
    }

    FUSE_LOG_MVP("[FuseMVP] Consume DekuNut: new=%d\n", newCount);
    return true;
}

bool ConsumeDekuStickAmmo(int amount) {
    if (amount <= 0) {
        return true;
    }

    const int cur = GetDekuStickAmmoCount();
    if (cur < amount) {
        FUSE_LOG_DBG("[FuseDBG] Consume Stick FAILED: cur=%d need=%d\n", cur, amount);
        return false;
    }

    FUSE_LOG_MVP("[FuseMVP] Consume Stick: cur=%d amount=%d\n", cur, amount);

    const int newCount = std::max(0, cur - amount);
    const int delta = newCount - cur;

    if (delta != 0) {
        Inventory_ChangeAmmo(ITEM_STICK, delta);
    }

    return true;
}

bool ConsumeBombAmmo(int amount) {
    if (amount <= 0) {
        return true;
    }

    const int cur = GetBombAmmoCount();
    if (cur < amount) {
        FUSE_LOG_DBG("[FuseDBG] Consume Bomb FAILED: cur=%d need=%d\n", cur, amount);
        return false;
    }

    FUSE_LOG_MVP("[FuseMVP] Consume Bomb: cur=%d amount=%d\n", cur, amount);

    const int newCount = std::max(0, cur - amount);
    const int delta = newCount - cur;

    if (delta != 0) {
        Inventory_ChangeAmmo(ITEM_BOMB, delta);
    }

    return true;
}

void AddDekuNutAmmo(int amount) {
    if (amount <= 0) {
        return;
    }

    const int cur = GetDekuNutAmmoCount();
    const int newCount = std::max(0, cur + amount);
    const int delta = newCount - cur;

    if (delta != 0) {
        Inventory_ChangeAmmo(ITEM_NUT, delta);
    }
}

void AddDekuStickAmmo(int amount) {
    if (amount <= 0) {
        return;
    }

    const int cur = GetDekuStickAmmoCount();
    const int newCount = std::max(0, cur + amount);
    const int delta = newCount - cur;

    if (delta != 0) {
        Inventory_ChangeAmmo(ITEM_STICK, delta);
    }
}

void AddBombAmmo(int amount) {
    if (amount <= 0) {
        return;
    }

    const int cur = GetBombAmmoCount();
    const int newCount = std::max(0, cur + amount);
    const int delta = newCount - cur;

    if (delta != 0) {
        Inventory_ChangeAmmo(ITEM_BOMB, delta);
    }
}

void Fuse_AddMaterialOrAmmo(MaterialId mat, int amount) {
    if (amount <= 0 || mat == MaterialId::None) {
        return;
    }

    if (mat == MaterialId::DekuNut) {
        AddDekuNutAmmo(amount);
        return;
    }

    if (mat == MaterialId::Stick) {
        AddDekuStickAmmo(amount);
        return;
    }

    if (mat == MaterialId::Bomb) {
        AddBombAmmo(amount);
        return;
    }

    Fuse::AddMaterial(mat, amount);
}

constexpr s16 kVanillaDekuNutParams = 0;
constexpr float kVanillaDekuNutRadius = 200.0f;

const char* GetStunSourceLabel(int itemId);

Actor* SpawnVanillaDekuNutFlash(PlayState* play, const Vec3f& pos, int srcItemId) {
    if (!play) {
        return nullptr;
    }

    if (CVarGetInteger(CVAR_ENHANCEMENT("FuseDekuNutSpawn"), 1) == 0) {
        const char* srcLabel = GetStunSourceLabel(srcItemId);
        FUSE_LOG_DBG("[FuseDBG] DekuNutSpawnDisabled src=%s frame=%d\n", srcLabel, play->gameplayFrames);
        return nullptr;
    }

    Actor* flashActor = EnArrow_TriggerDekuNutEffect(play, &pos);
    if (flashActor != nullptr) {
        FUSE_LOG_DBG("[FuseDBG] DekuNutEffect: vanilla_call ok frame=%d src=%s\n", play->gameplayFrames,
                     GetStunSourceLabel(srcItemId));
    }
    return flashActor;
}

void ApplyDekuNutStunVanilla(PlayState* play, Player* player, Actor* victim, uint8_t level, int srcItemId) {
    (void)player;

    if (!play || !victim || level == 0) {
        return;
    }

    Vec3f spawnPos = victim->world.pos;
    FUSE_LOG_DBG("[FuseDBG] DekuNutVanilla: trigger frame=%d victim=%p params=%d radius=%.2f pos=(%.2f, %.2f, %.2f)\n",
                 play->gameplayFrames, (void*)victim, kVanillaDekuNutParams, kVanillaDekuNutRadius, spawnPos.x,
                 spawnPos.y, spawnPos.z);
    FUSE_LOG_MVP("[FuseMVP] DekuNut stun: using vanilla nut effect frame=%d victim=%p\n", play->gameplayFrames,
                 (void*)victim);

    Actor* flashActor = SpawnVanillaDekuNutFlash(play, spawnPos, srcItemId);

    if (flashActor) {
        FUSE_LOG_MVP("[FuseMVP] DekuNut stun: spawned actor id=0x%04X ptr=%p\n", flashActor->id, (void*)flashActor);
    } else {
        FUSE_LOG_MVP("[FuseMVP] DekuNut stun: spawn failed\n");
    }
}

void TriggerDekuNutAtPosInternal(PlayState* play, const Vec3f& pos, int srcItemId) {
    if (!play) {
        return;
    }

    (void)SpawnVanillaDekuNutFlash(play, pos, srcItemId);
}

const char* GetStunSourceLabel(int itemId) {
    switch (itemId) {
        case ITEM_SWORD_KOKIRI:
            return "kokiri_sword";
        case ITEM_SWORD_MASTER:
            return "master_sword";
        case ITEM_SWORD_BGS:
            return "biggoron_sword";
        case ITEM_SWORD_KNIFE:
            return "giant_knife";
        case ITEM_BOOMERANG:
            return "boomerang";
        case ITEM_BOW:
            return "arrows";
        case ITEM_SLINGSHOT:
            return "slingshot";
        case ITEM_HOOKSHOT:
            return "hookshot";
        case ITEM_LONGSHOT:
            return "longshot";
        case ITEM_HAMMER:
            return "hammer";
        case ITEM_SHIELD_DEKU:
            return "deku_shield";
        case ITEM_SHIELD_HYLIAN:
            return "hylian_shield";
        case ITEM_SHIELD_MIRROR:
            return "mirror_shield";
        default:
            return "unknown";
    }
}

bool IsVanillaMaterial(MaterialId id) {
    return id == MaterialId::DekuNut || id == MaterialId::Stick || id == MaterialId::Bomb;
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

    if (IsActorFrozenInternal(victim)) {
        if (victim->freezeTimer > 0 && !IsFuseFrozenInternal(victim)) {
            Fuse::TryFreezeShatter(play, victim, nullptr, "ice_arrow");
        }
        return;
    }

    if (IsFreezeReapplyBlocked(play, victim)) {
        FUSE_LOG_DBG("[FuseDBG] FreezeSkip: reason=NoReapplyWindow frame=%d victim=%p\n",
                     play ? play->gameplayFrames : -1, (void*)victim);
        return;
    }

    if (WasFreezeRecentlyShattered(play, victim)) {
        FUSE_LOG_DBG("[FuseDBG] FreezeSkip: reason=RecentlyShattered frame=%d victim=%p\n",
                     play ? play->gameplayFrames : -1, (void*)victim);
        return;
    }

    const s16 duration = static_cast<s16>(kFreezeDurationFramesBase * level);
    constexpr s16 kIceColorFlagBlue = 0;        // Default flag yields the blue ice arrow tint (see z64actor.h)
    constexpr s16 kNeutralColorIntensity = 180; // Softer tint to look more snow/white than deep blue

    if (sFuseFrozenOrigGravity.find(victim) == sFuseFrozenOrigGravity.end()) {
        sFuseFrozenOrigGravity[victim] = victim->gravity;
    }

    // Apply the same immobilization and visual feedback that Ice Arrows use
    sFuseFrozenTimers[victim] = std::max<s16>(sFuseFrozenTimers[victim], duration);
    Actor_SetColorFilter(victim, kIceColorFlagBlue, kNeutralColorIntensity, 0, duration);
    static constexpr uint16_t kBgGroundStanding = 0x0001;
    const bool isAirborne = (victim->bgCheckFlags & kBgGroundStanding) == 0;
    sFuseFrozenPinned[victim] = !isAirborne;
    if (isAirborne) {
        victim->velocity.x = 0.0f;
        victim->velocity.z = 0.0f;
        victim->speedXZ = 0.0f;
        victim->velocity.y = std::min(victim->velocity.y, 0.0f);
        sFuseFrozenPos.erase(victim);
    }

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

    FUSE_LOG_DBG("[FuseDBG] FreezeApply: victim=%p duration=%d mat=FrozenShard\n", (void*)victim, duration);
}

void ApplyFuseKnockback(PlayState* play, Player* player, Actor* victim, uint8_t level, const char* itemLabel,
                        MaterialId materialId, int curDurability, int maxDurability, const char* eventLabel) {
    if (!play || !player || !victim || level == 0) {
        return;
    }

    if (victim->id == ACTOR_EN_SKB) {
        FUSE_LOG_DBG("[FuseDBG] knockback_skip_blacklist: event=%s item=%s victim=%p id=0x%04X\n",
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

    FUSE_LOG_DBG("[FuseDBG] Knockback: event=%s item=%s mat=%d lvl=%u victim=%p dura=%d/%d v=(%.2f,%.2f,%.2f)\n",
                 eventLabel ? eventLabel : "hit", itemLabel ? itemLabel : "unknown", static_cast<int>(materialId),
                 static_cast<unsigned int>(level), (void*)victim, curDurability, maxDurability, victim->velocity.x,
                 victim->velocity.y, victim->velocity.z);
}

static inline int RangedSlotToIndex(RangedFuseSlot slot) {
    switch (slot) {
        case RangedFuseSlot::Arrows:
            return 0;
        case RangedFuseSlot::Slingshot:
            return 1;
        case RangedFuseSlot::Hookshot:
            return 2;
        default:
            return -1;
    }
}

static bool IsMaterialIdInRange(MaterialId id) {
    const int value = static_cast<int>(id);
    return value >= static_cast<int>(MaterialId::None) && value <= static_cast<int>(MaterialId::FireJelly);
}

#ifndef NDEBUG
static void DebugAssertRangedIndex(int idx) {
    static bool sRangedIndexAsserted = false;
    if (sRangedIndexAsserted) {
        return;
    }
    assert(idx >= 0 && idx < static_cast<int>(kRangedSlots.size()));
    sRangedIndexAsserted = true;
}

static void DebugAssertMaterialId(MaterialId materialId) {
    static bool sMaterialAsserted = false;
    if (sMaterialAsserted) {
        return;
    }
    assert(IsMaterialIdInRange(materialId));
    sMaterialAsserted = true;
}

static void DebugAssertDurabilityValues(int durabilityCur, int durabilityMax) {
    static bool sDurabilityAsserted = false;
    if (sDurabilityAsserted) {
        return;
    }
    assert(durabilityCur >= 0);
    assert(durabilityMax >= 0);
    sDurabilityAsserted = true;
}
#endif

static RangedFuseState& GetRangedQueued(RangedFuseSlot slot) {
    const int idx = RangedSlotToIndex(slot);
    if (idx < 0) {
        FUSE_LOG_DBG("[FuseDBG] RangedSlotInvalid slot=%d\n", static_cast<int>(slot));
        return gRangedQueued[0];
    }
#ifndef NDEBUG
    DebugAssertRangedIndex(idx);
#endif
    return gRangedQueued[static_cast<size_t>(idx)];
}

static RangedFuseState& GetRangedActive(RangedFuseSlot slot) {
    const int idx = RangedSlotToIndex(slot);
    if (idx < 0) {
        FUSE_LOG_DBG("[FuseDBG] RangedSlotInvalid slot=%d\n", static_cast<int>(slot));
        return gRangedActive[0];
    }
#ifndef NDEBUG
    DebugAssertRangedIndex(idx);
#endif
    return gRangedActive[static_cast<size_t>(idx)];
}

static bool Fuse_RangedHasBurnModifier(RangedFuseSlot slot, MaterialId* outMaterialId) {
    const RangedFuseState& active = GetRangedActive(slot);
    if (active.materialId == MaterialId::None || active.durabilityCur <= 0) {
        return false;
    }

    const MaterialDef* def = Fuse::GetMaterialDef(active.materialId);
    if (!def) {
        return false;
    }

    uint8_t burnLevel = 0;
    if (!HasModifier(def->modifiers, def->modifierCount, ModifierId::Burn, &burnLevel) || burnLevel == 0) {
        return false;
    }

    if (outMaterialId) {
        *outMaterialId = active.materialId;
    }

    return true;
}

static void Fuse_RangedMarkProjectileAsFire(Actor* projectile) {
    if (!projectile || projectile->id != ACTOR_EN_ARROW) {
        return;
    }

    EnArrow* arrow = reinterpret_cast<EnArrow*>(projectile);
    arrow->collider.info.toucher.dmgFlags = DMG_ARROW_FIRE;
}

bool Fuse_RangedSuppressLitArrowEnemyBonus(Actor* projectile) {
    if (!projectile || projectile->id != ACTOR_EN_ARROW) {
        return false;
    }

    EnArrow* arrow = reinterpret_cast<EnArrow*>(projectile);
    if (arrow->actor.params != ARROW_NORMAL_LIT) {
        return false;
    }

    if ((arrow->collider.info.toucher.dmgFlags & DMG_ARROW_FIRE) == 0) {
        return false;
    }

    arrow->collider.info.toucher.dmgFlags = DMG_ARROW_NORMAL;
    FUSE_LOG_DBG("[FuseDBG] BurnEnemyBonusSuppressed: proj=0x%04X\n", projectile->id);
    return true;
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

bool IsRangedActiveBusy(RangedFuseSlot slot) {
    const RangedFuseState& active = GetRangedActive(slot);
    return active.materialId != MaterialId::None && active.durabilityCur > 0;
}

void LogRangedBusy(RangedFuseSlot slot, const char* reason) {
    const RangedFuseState& active = GetRangedActive(slot);
    FUSE_LOG_DBG("[FuseDBG] RangedBusy slot=%s activeMat=%d reason=%s\n", RangedSlotName(slot),
                 static_cast<int>(active.materialId), reason ? reason : "None");
}

void ApplyRangedFuseSlotMaterial(RangedFuseSlot slot, MaterialId mat) {
    RangedFuseState& active = GetRangedActive(slot);

    if (mat == MaterialId::None) {
        active.ResetToUnfused();
        return;
    }

    switch (slot) {
        case RangedFuseSlot::Arrows:
            Fuse::FuseArrowsWithMaterial(mat, GetMaterialEffectiveBaseDurabilityForItem(mat, FuseItemType::Arrows));
            return;
        case RangedFuseSlot::Slingshot:
            Fuse::FuseSlingshotWithMaterial(mat,
                                            GetMaterialEffectiveBaseDurabilityForItem(mat, FuseItemType::Slingshot));
            return;
        case RangedFuseSlot::Hookshot:
            Fuse::FuseHookshotWithMaterial(mat, GetMaterialEffectiveBaseDurabilityForItem(mat, FuseItemType::Hookshot));
            return;
    }
}

int GetGameplayFrame() {
    return gPlayState ? gPlayState->gameplayFrames : -1;
}

void LogRangedEvent(const char* tag, RangedFuseSlot slot, MaterialId mat, const char* reason) {
    const int matId = static_cast<int>(mat);
    const int count = (mat != MaterialId::None) ? Fuse::GetMaterialCount(mat) : -1;
    FUSE_LOG_DBG("[FuseDBG] %s: slot=%s mat=%d count=%d reason=%s\n", tag, RangedSlotName(slot), matId, count,
                 reason ? reason : "None");
}

void LogRangedActiveEvent(const char* tag, RangedFuseSlot slot) {
    const RangedFuseState& active = GetRangedActive(slot);
    FUSE_LOG_DBG("[FuseDBG] %s slot=%s mat=%d dura=%d/%d\n", tag, RangedSlotName(slot),
                 static_cast<int>(active.materialId), active.durabilityCur, active.durabilityMax);
}

void LogRangedQueuedEvent(const char* tag, RangedFuseSlot slot) {
    const RangedFuseState& queued = GetRangedQueued(slot);
    FUSE_LOG_DBG("[FuseDBG] %s slot=%s mat=%d dura=%d/%d\n", tag, RangedSlotName(slot),
                 static_cast<int>(queued.materialId), queued.durabilityCur, queued.durabilityMax);
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

static void TickFuseFrozenTimers(PlayState* play) {
    for (auto it = sFuseFrozenTimers.begin(); it != sFuseFrozenTimers.end();) {
        Actor* actor = it->first;
        s16& timer = it->second;

        if (!IsActorAliveInPlay(play, actor)) {
            sFreezeAppliedFrame.erase(actor);
            sFreezeShatterFrame.erase(actor);
            sFreezeLastShatterFrame.erase(actor);
            sFreezeNoReapplyUntilFrame.erase(actor);
            sFuseFrozenOrigGravity.erase(actor);
            sFuseFrozenPos.erase(actor);
            sFuseFrozenPinned.erase(actor);
            it = sFuseFrozenTimers.erase(it);
            continue;
        }

        if (timer > 0) {
            timer--;
        }

        if (timer <= 0) {
            Actor* a = actor;
            it = sFuseFrozenTimers.erase(it);
            ClearFuseFreeze(a);
            if (play) {
                auto noReapplyIt = sFreezeNoReapplyUntilFrame.find(a);
                if (noReapplyIt != sFreezeNoReapplyUntilFrame.end() &&
                    static_cast<int64_t>(play->gameplayFrames) >= noReapplyIt->second) {
                    sFreezeNoReapplyUntilFrame.erase(noReapplyIt);
                }
            }
            continue;
        } else {
            auto shatterIt = sFreezeShatterFrame.find(actor);
            if (play != nullptr && shatterIt != sFreezeShatterFrame.end() &&
                shatterIt->second == play->gameplayFrames) {
                ++it;
                continue;
            }

            const auto pinnedIt = sFuseFrozenPinned.find(actor);
            const bool pinPosition = (pinnedIt == sFuseFrozenPinned.end()) ? true : pinnedIt->second;
            actor->velocity.x = 0.0f;
            actor->velocity.z = 0.0f;
            actor->speedXZ = 0.0f;

            if (pinPosition) {
                actor->velocity.y = 0.0f;
                actor->gravity = 0.0f;

                auto posIt = sFuseFrozenPos.find(actor);
                if (posIt == sFuseFrozenPos.end()) {
                    sFuseFrozenPos[actor] = actor->world.pos;
                } else {
                    actor->world.pos = posIt->second;
                }
            } else {
                actor->velocity.y = std::min(actor->velocity.y, 0.0f);
                sFuseFrozenPos.erase(actor);
            }
            ++it;
        }
    }
}

static void TickShatterImpulse(PlayState* play) {
    if (!play) {
        return;
    }

    for (auto it = sShatterImpulseUntilFrame.begin(); it != sShatterImpulseUntilFrame.end();) {
        Actor* actor = it->first;
        const int untilFrame = it->second;

        if (!IsActorAliveInPlay(play, actor) || static_cast<int64_t>(play->gameplayFrames) >= untilFrame) {
            sShatterImpulseDir.erase(actor);
            sShatterImpulseFlipped.erase(actor);
            sShatterImpulseYaw.erase(actor);
            it = sShatterImpulseUntilFrame.erase(it);
            continue;
        }

        const auto yawIt = sShatterImpulseYaw.find(actor);
        if (yawIt != sShatterImpulseYaw.end()) {
            actor->world.rot.y = yawIt->second;
            actor->shape.rot.y = actor->world.rot.y;
        }
        actor->speedXZ = std::max(actor->speedXZ, kFreezeShatterKnockbackSpeed);

        const auto dirIt = sShatterImpulseDir.find(actor);
        if (dirIt != sShatterImpulseDir.end()) {
            Vec3f dir = dirIt->second;
            /*
            if (playerActor) {
                const float dx0 = actor->world.pos.x - playerActor->world.pos.x;
                const float dz0 = actor->world.pos.z - playerActor->world.pos.z;
                const float dist0 = dx0 * dx0 + dz0 * dz0;
                const float px1 = actor->world.pos.x + dir.x * kShatterImpulseStep;
                const float pz1 = actor->world.pos.z + dir.z * kShatterImpulseStep;
                const float dx1 = px1 - playerActor->world.pos.x;
                const float dz1 = pz1 - playerActor->world.pos.z;
                const float dist1 = dx1 * dx1 + dz1 * dz1;
                if (dist1 < dist0) {
                    dir.x = -dir.x;
                    dir.z = -dir.z;
                    sShatterImpulseDir[actor] = dir;
                    if (sShatterImpulseFlipped.find(actor) == sShatterImpulseFlipped.end()) {
                        sShatterImpulseFlipped.insert(actor);
                        FUSE_LOG_DBG("[FuseDBG] ShatterImpulseFlip: victim=%p dist0=%.2f dist1=%.2f\n",
                                  static_cast<void*>(actor), dist0, dist1);
                    }
                }
            }
            */
            actor->world.pos.x += dir.x * kShatterImpulseStep;
            actor->world.pos.z += dir.z * kShatterImpulseStep;
            if (kShatterImpulseY != 0.0f) {
                actor->world.pos.y += kShatterImpulseY;
            }
        }

        ++it;
    }
}

static const char* GetEnemyHpOverrideKey(const Actor* actor) {
    if (!actor) {
        return nullptr;
    }

    switch (actor->id) {
        case ACTOR_EN_FIREFLY:
            return "gFuse.DebugEnemyHpOverride.Keese";
        case ACTOR_EN_DEKUBABA:
            if (actor->params == DEKUBABA_BIG) {
                return "gFuse.DebugEnemyHpOverride.BigDekuBaba";
            }
            return "gFuse.DebugEnemyHpOverride.DekuBaba";
        case ACTOR_EN_TITE:
            if (actor->params == TEKTITE_BLUE) {
                return "gFuse.DebugEnemyHpOverride.BlueTektite";
            }
            if (actor->params == TEKTITE_RED) {
                return "gFuse.DebugEnemyHpOverride.RedTektite";
            }
            return nullptr;
        case ACTOR_EN_ZF:
            if (actor->params == ENZF_TYPE_DINOLFOS) {
                return "gFuse.DebugEnemyHpOverride.Dinolfos";
            }
            return "gFuse.DebugEnemyHpOverride.Lizalfos";
        case ACTOR_EN_PEEHAT:
            return "gFuse.DebugEnemyHpOverride.Peahat";
        case ACTOR_EN_WF:
            return "gFuse.DebugEnemyHpOverride.Wolfos";
        case ACTOR_EN_TEST:
            return "gFuse.DebugEnemyHpOverride.Stalfos";
        default:
            return nullptr;
    }
}

static void TryApplyEnemyHpOverride(Actor* actor) {
    if (!actor) {
        return;
    }
    if (CVarGetInteger("gFuse.DebugEnemyHpOverride.Enable", 0) == 0) {
        return;
    }
    if (actor->category != ACTORCAT_ENEMY) {
        return;
    }

    const char* key = GetEnemyHpOverrideKey(actor);
    if (!key) {
        return;
    }

    const bool sticky = CVarGetInteger("gFuse.DebugEnemyHpOverride.Sticky", 0) != 0;
    if (sHpOverrideApplied.find(actor) != sHpOverrideApplied.end()) {
        return;
    }

    int overrideHp = CVarGetInteger(key, 0);
    if (overrideHp <= 0) {
        return;
    }

    overrideHp = std::clamp(overrideHp, 1, 255);
    const int before = static_cast<int>(actor->colChkInfo.health);
    actor->colChkInfo.health = static_cast<uint8_t>(overrideHp);
    const bool inserted = sHpOverrideApplied.insert(actor).second;
    if (before != overrideHp || (sticky && inserted)) {
        FUSE_LOG_DBG("[FuseDBG] EnemyHpOverride: id=%d actor=%p hp=%d->%d key=%s\n", static_cast<int>(actor->id),
                     static_cast<void*>(actor), before, overrideHp, key);
    }
}

static void CleanupEnemyHpOverrides(PlayState* play) {
    for (auto it = sHpOverrideApplied.begin(); it != sHpOverrideApplied.end();) {
        if (!IsActorAliveInPlay(play, *it)) {
            it = sHpOverrideApplied.erase(it);
        } else {
            ++it;
        }
    }
}

static void SpawnFuseExplosionEffects(PlayState* play, Actor* actor) {
    if (!play || !actor) {
        return;
    }

    Vec3f effVelocity = { 0.0f, 0.0f, 0.0f };
    Vec3f bomb2Accel = { 0.0f, 0.1f, 0.0f };
    Vec3f effAccel = { 0.0f, 0.0f, 0.0f };
    Vec3f effPos = actor->world.pos;

    effPos.y += 10.0f;
    EffectSsBomb2_SpawnLayered(play, &effPos, &effVelocity, &bomb2Accel, 100, (actor->shape.rot.z * 6) + 19);

    effPos.y = actor->floorHeight;
    if (actor->floorHeight > BGCHECK_Y_MIN) {
        EffectSsBlast_SpawnWhiteShockwave(play, &effPos, &effVelocity, &effAccel);
    }

    Audio_PlayActorSound2(actor, NA_SE_IT_BOMB_EXPLOSION);

    play->envCtx.adjLight1Color[0] = play->envCtx.adjLight1Color[1] = play->envCtx.adjLight1Color[2] = 250;
    play->envCtx.adjAmbientColor[0] = play->envCtx.adjAmbientColor[1] = play->envCtx.adjAmbientColor[2] = 250;

    Camera_AddQuake(&play->mainCamera, 2, 0xB, 8);
    actor->flags |= ACTOR_FLAG_DRAW_CULLING_DISABLED;
}

void ResetSwordFreezeQueueInternal() {
    for (size_t i = 0; i < kSwordFreezeQueueCount; i++) {
        sSwordFreezeQueues[i].clear();
        sSwordFreezeVictims[i].clear();
        sSwordFreezeQueueFrames[i] = -1;
    }
}

void ResetDekuStunQueueInternal() {
    sPendingStunQueue.clear();
    sPendingStunIndex.clear();
    sDekuStunCooldownUntil.clear();
    sDekuLastSwordHitFrame.clear();
    sMegaStunCooldownUntil = -1;
}

bool EnqueueSwordFreezeRequest(PlayState* play, Actor* victim, uint8_t level) {
    if (!play || !victim || level == 0) {
        return false;
    }

    const int curFrame = play->gameplayFrames;
    const size_t queueIndex = static_cast<size_t>(curFrame) % kSwordFreezeQueueCount;

    if (sSwordFreezeQueueFrames[queueIndex] != curFrame) {
        sSwordFreezeQueues[queueIndex].clear();
        sSwordFreezeVictims[queueIndex].clear();
        sSwordFreezeQueueFrames[queueIndex] = curFrame;
    }

    if (sSwordFreezeVictims[queueIndex].count(victim) > 0) {
        return false;
    }

    sSwordFreezeVictims[queueIndex].insert(victim);
    sSwordFreezeQueues[queueIndex].push_back({ victim, level });
    return true;
}

void QueueSwordFreezeInternal(PlayState* play, Actor* victim, uint8_t level, const char* srcLabel,
                              const char* slotLabel, MaterialId materialId) {
    if (!play || !victim || level == 0) {
        return;
    }

    if (!EnqueueSwordFreezeRequest(play, victim, level)) {
        return;
    }

    FUSE_LOG_DBG("[FuseDBG] FreezeApply: src=%s slot=%s mat=%d lvl=%u victim=%p\n", srcLabel ? srcLabel : "unknown",
                 slotLabel ? slotLabel : "unknown", static_cast<int>(materialId), static_cast<unsigned int>(level),
                 (void*)victim);
}

} // namespace

FuseExplosionParams Fuse_GetExplosionParams(MaterialId mat, int level) {
    (void)mat;
    (void)level;
    // Always treat fuse bomb explosions as explosive damage.
    FuseExplosionParams params{ 80.0f, 8, DMG_EXPLOSIVE, 10 };
    return params;
}

bool Fuse_TriggerExplosion(PlayState* play, const Vec3f& pos, FuseExplosionSelfMode selfMode,
                           FuseExplosionParams params, const char* srcLabel) {
    if (!play) {
        return false;
    }

    FUSE_LOG_DBG("[FuseDBG] Explosion: pos=(%.2f %.2f %.2f) radius=%.2f dmg=%d flags=0x%08X self=%d frames=%d src=%s\n",
                 pos.x, pos.y, pos.z, static_cast<double>(params.radius), params.damage, params.dmgFlags,
                 (selfMode == FuseExplosionSelfMode::DamagePlayer) ? 1 : 0, params.hitFrames,
                 srcLabel ? srcLabel : "unknown");

    // Root cause note: forcing params to BOMB_EXPLOSION here skips the vanilla BOMB_BODY countdown->explosion
    // transition in z_en_bom.c, so EnBom_Explode never runs (no effects/damage) and the actor can persist,
    // eventually exhausting the lit-bomb budget. Keep the actor as BOMB_BODY and let it transition naturally.
    Actor* explosionActor =
        Actor_Spawn(&play->actorCtx, play, ACTOR_EN_BOM, pos.x, pos.y, pos.z, 0, 0, 0, BOMB_BODY);
    FUSE_LOG_DBG("[FuseDBG] ExplodeSpawnAttempt pos=(%.2f %.2f %.2f) src=%s result=%p\n", pos.x, pos.y, pos.z,
                 srcLabel ? srcLabel : "unknown", (void*)explosionActor);
    if (!explosionActor) {
        return false;
    }

    EnBom* bomb = reinterpret_cast<EnBom*>(explosionActor);
    bomb->timer = 2;
    bomb->actor.shape.rot.z = 0;
    FUSE_LOG_DBG("[FuseDBG] ExplodeSpawnInit pos=(%.2f %.2f %.2f) params=%d timer=%d src=%s frame=%d\n",
                 bomb->actor.world.pos.x, bomb->actor.world.pos.y, bomb->actor.world.pos.z, bomb->actor.params,
                 bomb->timer, srcLabel ? srcLabel : "unknown", play->gameplayFrames);
    return true;
}

Vec3f Fuse_AdjustShieldExplosionPos(const Player* player, const Vec3f& impactPos) {
    constexpr float kShieldExplosionEpsilon = 12.0f;
    Vec3f outwardDir = { 0.0f, 0.0f, 0.0f };
    if (player) {
        outwardDir.x = impactPos.x - player->actor.world.pos.x;
        outwardDir.y = impactPos.y - player->actor.world.pos.y;
        outwardDir.z = impactPos.z - player->actor.world.pos.z;
    }

    const float distSq = (outwardDir.x * outwardDir.x) + (outwardDir.y * outwardDir.y) + (outwardDir.z * outwardDir.z);
    if (distSq < 0.0001f) {
        const s16 yaw = player ? player->actor.shape.rot.y : 0;
        outwardDir.x = Math_SinS(yaw);
        outwardDir.y = 0.0f;
        outwardDir.z = Math_CosS(yaw);
    } else {
        const float invLen = 1.0f / sqrtf(distSq);
        outwardDir.x *= invLen;
        outwardDir.y *= invLen;
        outwardDir.z *= invLen;
    }

    Vec3f adjusted = impactPos;
    adjusted.x += outwardDir.x * kShieldExplosionEpsilon;
    adjusted.y += outwardDir.y * kShieldExplosionEpsilon;
    adjusted.z += outwardDir.z * kShieldExplosionEpsilon;
    return adjusted;
}

void Fuse::QueueSwordFreeze(PlayState* play, Actor* victim, uint8_t level, const char* srcLabel, const char* slotLabel,
                            MaterialId materialId) {
    QueueSwordFreezeInternal(play, victim, level, srcLabel, slotLabel, materialId);
}

void Fuse_TriggerDekuNutAtPos(PlayState* play, const Vec3f& pos, int srcItemId) {
    if (!play) {
        return;
    }

    FUSE_LOG_DBG("[FuseDBG] DekuNutAtPos: trigger frame=%d src=%s item=%d pos=(%.2f, %.2f, %.2f)\n",
                 play->gameplayFrames, GetStunSourceLabel(srcItemId), srcItemId, pos.x, pos.y, pos.z);

    TriggerDekuNutAtPosInternal(play, pos, srcItemId);
}

void Fuse_EnqueuePendingStun(Actor* victim, uint8_t level, MaterialId materialId, int itemId) {
    if (!victim || level == 0) {
        return;
    }

    const char* srcLabel = GetStunSourceLabel(itemId);
    const int curFrame = GetGameplayFrame();
    auto cooldownIt = sDekuStunCooldownUntil.find(victim);
    if (curFrame >= 0 && cooldownIt != sDekuStunCooldownUntil.end() && curFrame < cooldownIt->second) {
        FUSE_LOG_DBG("[FuseDBG] dekunut_skip_cooldown victim=%p id=0x%04X until=%d\n", (void*)victim, victim->id,
                     cooldownIt->second);
        return;
    }
    const int applyNotBefore = (curFrame >= 0) ? curFrame + kDekuStunInitialDelayFrames : kDekuStunInitialDelayFrames;
    auto existing = sPendingStunIndex.find(victim);
    if (existing != sPendingStunIndex.end()) {
        PendingStunRequest& request = sPendingStunQueue[existing->second];
        request.level = level;
        request.applyNotBeforeFrame = applyNotBefore;
        request.attemptsRemaining = kDekuStunMaxAttempts;
        request.retryStepFrames = kDekuStunRetryStepFrames;
        request.materialId = materialId;
        request.itemId = itemId;
        FUSE_LOG_DBG("[FuseDBG] dekunut_enqueue victim=%p id=0x%04X src=%s notBefore=%d\n", (void*)victim, victim->id,
                     srcLabel, request.applyNotBeforeFrame);
        return;
    }

    PendingStunRequest request{};
    request.victim = victim;
    request.level = level;
    request.applyNotBeforeFrame = applyNotBefore;
    request.attemptsRemaining = kDekuStunMaxAttempts;
    request.retryStepFrames = kDekuStunRetryStepFrames;
    request.materialId = materialId;
    request.itemId = itemId;
    sPendingStunIndex[victim] = sPendingStunQueue.size();
    sPendingStunQueue.push_back(request);
    FUSE_LOG_DBG("[FuseDBG] dekunut_enqueue victim=%p id=0x%04X src=%s notBefore=%d\n", (void*)victim, victim->id,
                 srcLabel, request.applyNotBeforeFrame);
}

void Fuse_TriggerMegaStun(PlayState* play, Player* player, MaterialId materialId, int itemId) {
    if (!play || !player) {
        return;
    }

    const int curFrame = play->gameplayFrames;
    if (curFrame >= 0 && sMegaStunCooldownUntil >= 0 && curFrame < sMegaStunCooldownUntil) {
        return;
    }

    constexpr int kMegaStunCooldownFrames = 60;
    constexpr int kMegaStunCount = 6;
    constexpr float kMegaStunRadius = 160.0f;
    sMegaStunCooldownUntil = (curFrame >= 0) ? (curFrame + kMegaStunCooldownFrames) : kMegaStunCooldownFrames;

    const char* srcLabel = GetStunSourceLabel(itemId);
    FUSE_LOG_DBG("[FuseDBG] megastun_trigger src=%s count=%d\n", srcLabel, kMegaStunCount);

    Vec3f basePos = player->actor.world.pos;
    const s16 baseYaw = player->actor.shape.rot.y;
    const s16 angleStep = static_cast<s16>(0x10000 / kMegaStunCount);

    for (int i = 0; i < kMegaStunCount; ++i) {
        const s16 angle = baseYaw + (angleStep * i);
        Vec3f spawnPos = basePos;
        spawnPos.x += Math_SinS(angle) * kMegaStunRadius;
        spawnPos.z += Math_CosS(angle) * kMegaStunRadius;

        (void)SpawnVanillaDekuNutFlash(play, spawnPos, itemId);
    }

    (void)materialId;
}

extern "C" void Fuse_ShieldEnqueuePendingStun(Actor* victim, uint8_t level, int materialId, int itemId) {
    Fuse_EnqueuePendingStun(victim, level, static_cast<MaterialId>(materialId), itemId);
}

extern "C" void Fuse_ShieldTriggerMegaStun(PlayState* play, Player* player, int materialId, int itemId) {
    Fuse_TriggerMegaStun(play, player, static_cast<MaterialId>(materialId), itemId);
}

// -----------------------------------------------------------------------------
// Logging
// -----------------------------------------------------------------------------
static void Fuse_LogVPrintf(const char* fmt, va_list args) {
    char buf[1024];

#ifdef _WIN32
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, args);
#else
    vsnprintf(buf, sizeof(buf), fmt, args);
#endif

#ifdef _WIN32
    OutputDebugStringA(buf);
#endif

    fputs(buf, stdout);
    fflush(stdout);
}

void Fuse::Log(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    Fuse_LogVPrintf(fmt, args);
    va_end(args);
}

extern "C" void Fuse_DebugPrintf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    Fuse_LogVPrintf(fmt, args);
    va_end(args);
}

extern "C" s32 Fuse_IsEnabled(void) {
    return Fuse::IsEnabled();
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

    FUSE_LOG_MVP("[FuseMVP] Sword fused with material=%d (durability %u/%u)\n", static_cast<int>(materialId),
                 static_cast<unsigned int>(Fuse::GetSwordFuseDurability()),
                 static_cast<unsigned int>(Fuse::GetSwordFuseMaxDurability()));
    FUSE_LOG_DBG("[FuseDBG] Applied saved fuse matId=%d cur=%d max=%d\n", static_cast<int>(materialId), targetCur,
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

static bool Fuse_ShieldHasModifier(PlayState* play, ModifierId modifierId, int* outMaterialId, int* outDurabilityCur,
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
    if (!HasModifier(def->modifiers, def->modifierCount, modifierId, &level) || level == 0) {
        return false;
    }

    if (outLevel) {
        *outLevel = level;
    }

    return true;
}

static void Fuse_PruneSeekStates(PlayState* play) {
    if (!play || sSeekStates.empty()) {
        return;
    }

    for (auto it = sSeekStates.begin(); it != sSeekStates.end();) {
        if (!IsActorAliveInPlay(play, it->first)) {
            it = sSeekStates.erase(it);
        } else {
            ++it;
        }
    }
}

extern "C" bool Fuse_ShieldHasNegateKnockback(PlayState* play, int* outMaterialId, int* outDurabilityCur,
                                              int* outDurabilityMax, uint8_t* outLevel) {
    return Fuse_ShieldHasModifier(play, ModifierId::NegateKnockback, outMaterialId, outDurabilityCur, outDurabilityMax,
                                  outLevel);
}

extern "C" bool Fuse_ShieldHasStun(PlayState* play, int* outMaterialId, int* outDurabilityCur, int* outDurabilityMax,
                                   uint8_t* outLevel) {
    return Fuse_ShieldHasModifier(play, ModifierId::Stun, outMaterialId, outDurabilityCur, outDurabilityMax, outLevel);
}

extern "C" bool Fuse_ShieldHasFreeze(PlayState* play, int* outMaterialId, int* outDurabilityCur, int* outDurabilityMax,
                                     uint8_t* outLevel) {
    return Fuse_ShieldHasModifier(play, ModifierId::Freeze, outMaterialId, outDurabilityCur, outDurabilityMax,
                                  outLevel);
}

extern "C" bool Fuse_ShieldHasBurn(PlayState* play, int* outMaterialId, int* outDurabilityCur, int* outDurabilityMax,
                                   uint8_t* outLevel) {
    return Fuse_ShieldHasModifier(play, ModifierId::Burn, outMaterialId, outDurabilityCur, outDurabilityMax, outLevel);
}

extern "C" bool Fuse_ShieldHasMegaStun(PlayState* play, int* outMaterialId, int* outDurabilityCur,
                                       int* outDurabilityMax, uint8_t* outLevel) {
    return Fuse_ShieldHasModifier(play, ModifierId::MegaStun, outMaterialId, outDurabilityCur, outDurabilityMax,
                                  outLevel);
}

extern "C" s32 Fuse_ShieldHasExplosion(PlayState* play, s32* outMaterialId, s32* outDurabilityCur,
                                       s32* outDurabilityMax, u8* outLevel) {
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
        return 0;
    }

    const FuseSlot& slot = gFuseSave.GetActiveShieldSlot(play);
    if (slot.materialId == MaterialId::None || slot.durabilityCur <= 0) {
        return 0;
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

    const uint8_t level = Fuse::GetMaterialModifierLevel(slot.materialId, FuseItemType::Shield, ModifierId::Explosion);
    if (level == 0) {
        return 0;
    }

    if (outLevel) {
        *outLevel = level;
    }

    return 1;
}

extern "C" void Fuse_ShieldTriggerExplosion(PlayState* play, s32 shieldMaterialId, u8 level, const Vec3f* pos) {
    if (!play || !pos) {
        return;
    }

    const MaterialId materialId = static_cast<MaterialId>(shieldMaterialId);
    const FuseExplosionParams params = Fuse_GetExplosionParams(materialId, level);
    Player* player = GET_PLAYER(play);
    const Vec3f offsetPos = Fuse_AdjustShieldExplosionPos(player, *pos);
    const s16 yaw = GET_PLAYER(play) ? GET_PLAYER(play)->actor.shape.rot.y : 0;
    const uint32_t atFlags = AT_ON | AT_TYPE_ALL;
    const char* srcLabel = "Shield";

    FUSE_LOG_DBG("[FuseDBG] ShieldExplosionOffset: src=%s orig=(%.2f %.2f %.2f) offset=(%.2f %.2f %.2f) yaw=%d "
                 "atFlags=0x%08X dmg=%d radius=%.2f\n",
                 srcLabel, pos->x, pos->y, pos->z, offsetPos.x, offsetPos.y, offsetPos.z, yaw, atFlags, params.damage,
                 static_cast<double>(params.radius));

    Fuse_TriggerExplosion(play, offsetPos, FuseExplosionSelfMode::DamagePlayer, params, srcLabel);
}

extern "C" void Fuse_AddMaterialById(s32 materialId, s32 amount) {
    if (amount <= 0) {
        return;
    }

    Fuse::AddMaterial(static_cast<MaterialId>(materialId), amount);
}

extern "C" void Fuse_ShieldApplyFreeze(PlayState* play, Actor* victim, uint8_t level) {
    if (victim && IsActorFrozenInternal(victim)) {
        Fuse::TryFreezeShatter(play, victim, nullptr, "shield");
        return;
    }

    ApplyIceArrowFreeze(play, victim, level);
}

extern "C" void Fuse_ShieldApplyBurn(PlayState* play, Actor* victim, uint8_t level, int materialId) {
    if (!victim) {
        return;
    }

    Fuse::ApplyBurn(play, victim, level, static_cast<MaterialId>(materialId), "shield", "Shield");
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

    FUSE_LOG_DBG("[FuseDBG] shield_guard_drain: shield=%s mat=%d dura=%d/%d\n", ShieldSlotName(key), materialId, newCur,
                 maxDurability);
}

extern "C" int16_t Fuse_GetShieldBashDamage(int shieldItemId, int* outMaterialId, int* outHasBashMod,
                                            int* outMaterialAtk) {
    if (outMaterialId) {
        *outMaterialId = static_cast<int>(MaterialId::None);
    }
    if (outHasBashMod) {
        *outHasBashMod = 0;
    }
    if (outMaterialAtk) {
        *outMaterialAtk = 0;
    }

    const FuseSlot slot = Fuse::GetActiveShieldSlot();
    if (outMaterialId) {
        *outMaterialId = static_cast<int>(slot.materialId);
    }

    bool hasBashMod = false;
    int materialAtk = 0;
    const int16_t bashDamage = Fuse::GetShieldBashDamage(slot, shieldItemId, &hasBashMod, &materialAtk);

    if (outHasBashMod) {
        *outHasBashMod = hasBashMod ? 1 : 0;
    }
    if (outMaterialAtk) {
        *outMaterialAtk = materialAtk;
    }

    return bashDamage;
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

    const RangedFuseState& active = GetRangedActive(slot);
    if (outMaterialId) {
        *outMaterialId = static_cast<int>(active.materialId);
    }
    if (outDurabilityCur) {
        *outDurabilityCur = active.durabilityCur;
    }
    if (outDurabilityMax) {
        *outDurabilityMax = active.durabilityMax;
    }
}

void Fuse_GetRangedQueuedStatus(RangedFuseSlot slot, int* outMaterialId, int* outDurabilityCur, int* outDurabilityMax) {
    if (outMaterialId) {
        *outMaterialId = static_cast<int>(MaterialId::None);
    }
    if (outDurabilityCur) {
        *outDurabilityCur = 0;
    }
    if (outDurabilityMax) {
        *outDurabilityMax = 0;
    }

    const RangedFuseState& queued = GetRangedQueued(slot);
    if (outMaterialId) {
        *outMaterialId = static_cast<int>(queued.materialId);
    }
    if (outDurabilityCur) {
        *outDurabilityCur = queued.durabilityCur;
    }
    if (outDurabilityMax) {
        *outDurabilityMax = queued.durabilityMax;
    }
}

static int GetMaterialBaseAttackBonus(MaterialId id) {
    const MaterialDef* def = Fuse::GetMaterialDef(id);
    return def ? def->attackBonus : 0;
}

int Fuse::GetMaterialAttackBonus(MaterialId id) {
    const int base = GetMaterialBaseAttackBonus(id);
    if (!sUseDebugOverrides) {
        return base;
    }

    return base + Fuse::GetMaterialAttackBonusDelta(id);
}

int16_t Fuse::GetShieldBashDamage(const FuseSlot& slot, int shieldItemId, bool* outHasBashMod, int* outMaterialAtk) {
    if (outHasBashMod) {
        *outHasBashMod = false;
    }
    if (outMaterialAtk) {
        *outMaterialAtk = 0;
    }

    if (slot.materialId == MaterialId::None || slot.durabilityCur <= 0) {
        FUSE_LOG_DBG("[FuseDBG] BashDamage: shield=%d mat=%d atk=0 bash=0 mod=0\n", shieldItemId,
                     static_cast<int>(slot.materialId));
        return 0;
    }

    const MaterialDef* def = Fuse::GetMaterialDef(slot.materialId);
    if (!def) {
        FUSE_LOG_DBG("[FuseDBG] BashDamage: shield=%d mat=%d atk=0 bash=0 mod=0\n", shieldItemId,
                     static_cast<int>(slot.materialId));
        return 0;
    }

    const int materialAtk = Fuse::GetMaterialAttackBonus(slot.materialId);
    uint8_t bashLevel = 0;
    const bool hasBashMod =
        HasModifier(def->modifiers, def->modifierCount, ModifierId::BashAttack, &bashLevel) && bashLevel > 0;
    const int bashDamage = hasBashMod ? (materialAtk / 2) : 0;

    if (outHasBashMod) {
        *outHasBashMod = hasBashMod;
    }
    if (outMaterialAtk) {
        *outMaterialAtk = materialAtk;
    }

    FUSE_LOG_DBG("[FuseDBG] BashDamage: shield=%d mat=%d atk=%d bash=%d mod=%d\n", shieldItemId,
                 static_cast<int>(slot.materialId), materialAtk, bashDamage, hasBashMod ? 1 : 0);
    return static_cast<int16_t>(bashDamage);
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

static int GetMaterialEffectiveBaseDurabilityForItem(MaterialId id, FuseItemType itemType) {
    int effective = Fuse::GetMaterialEffectiveBaseDurability(id);
    if (id == MaterialId::BeamosHead && (itemType == FuseItemType::Arrows || itemType == FuseItemType::Slingshot)) {
        effective = 1;
    }
    return effective;
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

                        FUSE_LOG_DBG("[FuseDBG] OverrideLoad: mat=%d atkDelta=%d duraOvr=%d\n", static_cast<int>(id),
                                     attackDelta, durabilityOverride);
                    }
                });
            }
        });
    });

    sUseDebugOverrides = enabledInt != 0;
    FUSE_LOG_DBG("[FuseDBG] OverrideLoad: enabled=%d\n", sUseDebugOverrides ? 1 : 0);
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

                FUSE_LOG_DBG("[FuseDBG] OverrideSave: mat=%d atkDelta=%d duraOvr=%d\n", static_cast<int>(kvp.first),
                             kvp.second.attackBonusDelta, kvp.second.baseDurabilityOverride);
            }
        });
    });

    FUSE_LOG_DBG("[FuseDBG] OverrideSave: enabled=%d\n", sUseDebugOverrides ? 1 : 0);
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

static bool Fuse_ModifierAppliesForItem(ModifierId id, FuseItemType itemType) {
    switch (id) {
        case ModifierId::RangeUp:
            return itemType == FuseItemType::Sword;
        case ModifierId::WideRange:
            return itemType == FuseItemType::Boomerang;
        case ModifierId::Explosion:
            return itemType == FuseItemType::Sword || itemType == FuseItemType::Shield ||
                   itemType == FuseItemType::Boomerang || itemType == FuseItemType::Hammer ||
                   itemType == FuseItemType::Arrows || itemType == FuseItemType::Slingshot ||
                   itemType == FuseItemType::Hookshot;
        case ModifierId::Seek:
            return itemType == FuseItemType::Arrows || itemType == FuseItemType::Slingshot;
        case ModifierId::Burn:
            return itemType == FuseItemType::Sword || itemType == FuseItemType::Shield ||
                   itemType == FuseItemType::Boomerang || itemType == FuseItemType::Hammer ||
                   itemType == FuseItemType::Arrows || itemType == FuseItemType::Slingshot ||
                   itemType == FuseItemType::Hookshot;
        case ModifierId::Beam:
            return itemType == FuseItemType::Sword || itemType == FuseItemType::Shield ||
                   itemType == FuseItemType::Arrows || itemType == FuseItemType::Slingshot;
        default:
            return true;
    }
}

static bool Fuse_MaterialHasSeek(MaterialId materialId) {
    const MaterialDef* def = Fuse::GetMaterialDef(materialId);
    if (!def) {
        return false;
    }

    uint8_t level = 0;
    return HasModifier(def->modifiers, def->modifierCount, ModifierId::Seek, &level) && level > 0;
}

uint8_t Fuse::GetMaterialModifierLevel(MaterialId materialId, FuseItemType itemType, ModifierId id) {
    const MaterialDef* def = Fuse::GetMaterialDef(materialId);
    if (!def) {
        return 0;
    }

    if (!Fuse_ModifierAppliesForItem(id, itemType)) {
        return 0;
    }

    uint8_t level = 0;
    return HasModifier(def->modifiers, def->modifierCount, id, &level) ? level : 0;
}

float Fuse::GetRangeUpScale(uint8_t level) {
    switch (level) {
        case 1:
            return 1.10f;
        case 2:
            return 1.20f;
        case 3:
            return 1.30f;
        default:
            return 1.0f;
    }
}

float Fuse::GetWideRangeScale(uint8_t level) {
    switch (level) {
        case 1:
            return 2.0f;
        case 2:
            return 3.0f;
        case 3:
            return 4.0f;
        default:
            return 1.0f;
    }
}

int Fuse::GetMaterialCount(MaterialId id) {
    if (IsVanillaMaterial(id)) {
        if (id == MaterialId::DekuNut) {
            return GetDekuNutAmmoCount();
        }
        if (id == MaterialId::Stick) {
            return GetDekuStickAmmoCount();
        }
        if (id == MaterialId::Bomb) {
            return GetBombAmmoCount();
        }
        return 0;
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
        if (id == MaterialId::DekuNut) {
            return ConsumeDekuNutAmmo(amount);
        }
        if (id == MaterialId::Stick) {
            return ConsumeDekuStickAmmo(amount);
        }
        if (id == MaterialId::Bomb) {
            return ConsumeBombAmmo(amount);
        }
        return false;
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
    const RangedFuseState& slot = GetRangedQueued(RangedFuseSlot::Arrows);
    return slot.materialId != MaterialId::None && slot.durabilityCur > 0;
}

bool Fuse::IsSlingshotFused() {
    const RangedFuseState& slot = GetRangedQueued(RangedFuseSlot::Slingshot);
    return slot.materialId != MaterialId::None && slot.durabilityCur > 0;
}

bool Fuse::IsHookshotFused() {
    const RangedFuseState& slot = GetRangedQueued(RangedFuseSlot::Hookshot);
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
    const RangedFuseState& slot = GetRangedQueued(RangedFuseSlot::Arrows);
    return slot.materialId;
}

MaterialId Fuse::GetSlingshotMaterial() {
    const RangedFuseState& slot = GetRangedQueued(RangedFuseSlot::Slingshot);
    return slot.materialId;
}

MaterialId Fuse::GetHookshotMaterial() {
    const RangedFuseState& slot = GetRangedQueued(RangedFuseSlot::Hookshot);
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

FuseSlot Fuse::GetActiveShieldSlot() {
    return gFuseSave.GetActiveShieldSlot(nullptr);
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
}

void Fuse::ClearSlingshotFuse() {
    ClearQueuedRangedFuse_NoRefund(RangedFuseSlot::Slingshot, "ClearSlingshotFuse");
}

void Fuse::ClearHookshotFuse() {
    ClearQueuedRangedFuse_NoRefund(RangedFuseSlot::Hookshot, "ClearHookshotFuse");
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
        FUSE_LOG_MVP("[FuseMVP] Sword fused with material=%d (durability %u/%u)\n", static_cast<int>(id),
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
        FUSE_LOG_MVP("[FuseMVP] Boomerang fused with material=%d (durability %u/%u)\n", static_cast<int>(id),
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
        FUSE_LOG_MVP("[FuseMVP] Hammer fused with material=%d (durability %u/%u)\n", static_cast<int>(id),
                     static_cast<unsigned int>(slot.durabilityCur), static_cast<unsigned int>(maxDurability));
    }
}

void Fuse::FuseArrowsWithMaterial(MaterialId id, uint16_t maxDurability, bool initializeCurrentDurability,
                                  bool logDurability) {
    RangedFuseState& slot = GetRangedActive(RangedFuseSlot::Arrows);

#ifndef NDEBUG
    DebugAssertMaterialId(id);
#endif

    const int newCur = initializeCurrentDurability
                           ? static_cast<int>(maxDurability)
                           : std::clamp<int>(slot.durabilityCur, 0, static_cast<int>(maxDurability));
#ifndef NDEBUG
    DebugAssertDurabilityValues(newCur, maxDurability);
#endif

    slot.materialId = id;
    slot.durabilityMax = maxDurability;
    slot.durabilityCur = newCur;

    const MaterialDef* def = Fuse::GetMaterialDef(id);
    if (def) {
        Fuse::SetLastEvent(def->name);
    } else {
        Fuse::SetLastEvent("Arrows fused with material");
    }

    if (logDurability) {
        FUSE_LOG_MVP("[FuseMVP] Arrows fused with material=%d (durability %u/%u)\n", static_cast<int>(id),
                     static_cast<unsigned int>(slot.durabilityCur), static_cast<unsigned int>(maxDurability));
    }
}

void Fuse::FuseSlingshotWithMaterial(MaterialId id, uint16_t maxDurability, bool initializeCurrentDurability,
                                     bool logDurability) {
    RangedFuseState& slot = GetRangedActive(RangedFuseSlot::Slingshot);

#ifndef NDEBUG
    DebugAssertMaterialId(id);
#endif

    const int newCur = initializeCurrentDurability
                           ? static_cast<int>(maxDurability)
                           : std::clamp<int>(slot.durabilityCur, 0, static_cast<int>(maxDurability));
#ifndef NDEBUG
    DebugAssertDurabilityValues(newCur, maxDurability);
#endif

    slot.materialId = id;
    slot.durabilityMax = maxDurability;
    slot.durabilityCur = newCur;

    const MaterialDef* def = Fuse::GetMaterialDef(id);
    if (def) {
        Fuse::SetLastEvent(def->name);
    } else {
        Fuse::SetLastEvent("Slingshot fused with material");
    }

    if (logDurability) {
        FUSE_LOG_MVP("[FuseMVP] Slingshot fused with material=%d (durability %u/%u)\n", static_cast<int>(id),
                     static_cast<unsigned int>(slot.durabilityCur), static_cast<unsigned int>(maxDurability));
    }
}

void Fuse::FuseHookshotWithMaterial(MaterialId id, uint16_t maxDurability, bool initializeCurrentDurability,
                                    bool logDurability) {
    RangedFuseState& slot = GetRangedActive(RangedFuseSlot::Hookshot);

#ifndef NDEBUG
    DebugAssertMaterialId(id);
#endif

    const int newCur = initializeCurrentDurability
                           ? static_cast<int>(maxDurability)
                           : std::clamp<int>(slot.durabilityCur, 0, static_cast<int>(maxDurability));
#ifndef NDEBUG
    DebugAssertDurabilityValues(newCur, maxDurability);
#endif

    slot.materialId = id;
    slot.durabilityMax = maxDurability;
    slot.durabilityCur = newCur;

    const MaterialDef* def = Fuse::GetMaterialDef(id);
    if (def) {
        Fuse::SetLastEvent(def->name);
    } else {
        Fuse::SetLastEvent("Hookshot fused with material");
    }

    if (logDurability) {
        FUSE_LOG_MVP("[FuseMVP] Hookshot fused with material=%d (durability %u/%u)\n", static_cast<int>(id),
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

    if (Fuse_MaterialHasSeek(id)) {
        return FuseResult::NotAllowed;
    }

    const int preConsumeCount = (id == MaterialId::DekuNut) ? Fuse::GetMaterialCount(id) : -1;

    if (!Fuse::ConsumeMaterial(id, 1)) {
        return FuseResult::NotEnoughMaterial;
    }

    Fuse::FuseSwordWithMaterial(id, Fuse::GetMaterialEffectiveBaseDurability(id));

    const uint8_t rangeUpLevel = Fuse::GetMaterialModifierLevel(id, FuseItemType::Sword, ModifierId::RangeUp);
    if (rangeUpLevel > 0) {
        const char* matName = def ? def->name : "Unknown";
        FUSE_LOG_DBG("[FuseDBG] ApplyMods: item=Sword mat=%s rangeUp=%u\n", matName,
                     static_cast<unsigned int>(rangeUpLevel));
    }

    if (id == MaterialId::DekuNut) {
        const int postConsumeCount = Fuse::GetMaterialCount(id);
        FUSE_LOG_MVP("[FuseMVP] TryFuseSword(DekuNut): before=%d after=%d\n", preConsumeCount, postConsumeCount);
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

    if (Fuse_MaterialHasSeek(id)) {
        return FuseResult::NotAllowed;
    }

    if (!Fuse::ConsumeMaterial(id, 1)) {
        return FuseResult::NotEnoughMaterial;
    }

    Fuse::FuseBoomerangWithMaterial(id, Fuse::GetMaterialEffectiveBaseDurability(id));

    const uint8_t wideRangeLevel = Fuse::GetMaterialModifierLevel(id, FuseItemType::Boomerang, ModifierId::WideRange);
    if (wideRangeLevel > 0) {
        const char* matName = def ? def->name : "Unknown";
        FUSE_LOG_DBG("[FuseDBG] ApplyMods: item=Boomerang mat=%s wideRange=%u\n", matName,
                     static_cast<unsigned int>(wideRangeLevel));
    }

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

    if (Fuse_MaterialHasSeek(id)) {
        return FuseResult::NotAllowed;
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

    if (slot == RangedFuseSlot::Hookshot && Fuse_MaterialHasSeek(mat)) {
        return FuseResult::NotAllowed;
    }

    if (IsRangedActiveBusy(slot)) {
        LogRangedBusy(slot, "menu_swap_blocked");
        return FuseResult::NotAllowed;
    }

    RangedFuseState& state = GetRangedQueued(slot);
    if (state.inFlight) {
        LogRangedEvent("RangedQueueFail", slot, mat, "inFlight");
        return FuseResult::NotAllowed;
    }

    const int currentFrame = GetGameplayFrame();
    const bool hasPendingSwap =
        state.pendingRefundMaterial != MaterialId::None && state.pendingRefundFrame == currentFrame;
    const MaterialId pendingMat = hasPendingSwap ? state.pendingRefundMaterial : MaterialId::None;
    if (hasPendingSwap) {
        state.pendingRefundMaterial = MaterialId::None;
        state.pendingRefundFrame = -1;
    }

    if (state.materialId != MaterialId::None) {
        Fuse::CancelQueuedRangedFuse_Refund(slot, "QueueReplace");
    }

    if (!Fuse::HasMaterial(mat, 1) || !Fuse::ConsumeMaterial(mat, 1)) {
        if (hasPendingSwap && pendingMat != MaterialId::None) {
            state.materialId = pendingMat;
            state.durabilityMax = GetMaterialEffectiveBaseDurabilityForItem(pendingMat, RangedSlotItemType(slot));
            state.durabilityCur = state.durabilityMax;
            state.inFlight = false;
            state.hadSuccess = false;
            state.hitResolved = false;
        }
        LogRangedEvent("RangedQueueFail", slot, mat, reason);
        return FuseResult::NotEnoughMaterial;
    }

    if (hasPendingSwap && pendingMat != MaterialId::None) {
        const int before = Fuse::GetMaterialCount(pendingMat);
        Fuse_AddMaterialOrAmmo(pendingMat, 1);
        const int after = Fuse::GetMaterialCount(pendingMat);
        FUSE_LOG_DBG("[FuseDBG] Refund mat=%d amount=1 before=%d after=%d reason=%s\n", static_cast<int>(pendingMat),
                     before, after, "SwapRefund");
        FUSE_LOG_DBG("[FuseDBG] RangedRefundQueued slot=%s mat=%d amount=1 reason=%s\n", RangedSlotName(slot),
                     static_cast<int>(pendingMat), "SwapRefund");
    }

    state.materialId = mat;
    state.durabilityMax = GetMaterialEffectiveBaseDurabilityForItem(mat, RangedSlotItemType(slot));
    state.durabilityCur = state.durabilityMax;
    state.inFlight = false;
    state.hadSuccess = false;
    state.hitResolved = false;
    state.pendingRefundMaterial = MaterialId::None;
    state.pendingRefundFrame = -1;
    LogRangedEvent("RangedQueue", slot, mat, reason);
    LogRangedQueuedEvent("RangedQueueQueued", slot);
    return FuseResult::Ok;
}

void Fuse::ClearQueuedRangedFuse_NoRefund(RangedFuseSlot slot, const char* reason) {
    RangedFuseState& state = GetRangedQueued(slot);
    if (state.materialId == MaterialId::None) {
        return;
    }

    if (IsRangedActiveBusy(slot)) {
        LogRangedBusy(slot, "menu_swap_blocked");
        return;
    }

    const MaterialId mat = state.materialId;
    const int before = Fuse::GetMaterialCount(mat);
    Fuse_AddMaterialOrAmmo(mat, 1);
    const int after = Fuse::GetMaterialCount(mat);
    FUSE_LOG_DBG("[FuseDBG] Refund mat=%d amount=1 before=%d after=%d reason=%s\n", static_cast<int>(mat), before,
                 after, reason ? reason : "None");
    state.pendingRefundMaterial = MaterialId::None;
    state.pendingRefundFrame = -1;
    state.ResetToUnfused();
    state.inFlight = false;
    state.hadSuccess = false;

    LogRangedEvent("RangedClear", slot, mat, reason);
}

void Fuse::CommitQueuedRangedFuse(RangedFuseSlot slot, const char* reason) {
    RangedFuseState& state = GetRangedQueued(slot);
    if (state.materialId == MaterialId::None) {
        return;
    }

    const MaterialId mat = state.materialId;
    RangedFuseState& active = GetRangedActive(slot);
    active.materialId = state.materialId;
    active.durabilityCur = state.durabilityCur;
    active.durabilityMax = state.durabilityMax;
    state.ResetToUnfused();
    state.inFlight = false;
    state.hadSuccess = true;
    state.pendingRefundMaterial = MaterialId::None;
    state.pendingRefundFrame = -1;
    FUSE_LOG_DBG("[FuseDBG] RangedCommitActive slot=%s mat=%d dura=%d/%d\n", RangedSlotName(slot),
                 static_cast<int>(mat), active.durabilityCur, active.durabilityMax);
    LogRangedEvent("RangedCommit", slot, mat, reason);
}

void Fuse::CancelQueuedRangedFuse_Refund(RangedFuseSlot slot, const char* reason) {
    RangedFuseState& state = GetRangedQueued(slot);
    if (state.materialId == MaterialId::None) {
        return;
    }

    if (IsRangedActiveBusy(slot)) {
        LogRangedBusy(slot, "menu_swap_blocked");
        return;
    }

    const MaterialId mat = state.materialId;
    const int before = Fuse::GetMaterialCount(mat);
    Fuse_AddMaterialOrAmmo(mat, 1);
    const int after = Fuse::GetMaterialCount(mat);
    FUSE_LOG_DBG("[FuseDBG] Refund mat=%d amount=1 before=%d after=%d reason=%s\n", static_cast<int>(mat), before,
                 after, reason ? reason : "None");
    state.ResetToUnfused();
    state.inFlight = false;
    state.hadSuccess = false;
    state.pendingRefundMaterial = MaterialId::None;
    state.pendingRefundFrame = -1;
    FUSE_LOG_DBG("[FuseDBG] RangedRefundQueued slot=%s mat=%d amount=1 reason=%s\n", RangedSlotName(slot),
                 static_cast<int>(mat), reason ? reason : "None");
}

void Fuse::ClearActiveRangedFuse(RangedFuseSlot slot, const char* reason) {
    RangedFuseState& active = GetRangedActive(slot);
    if (active.materialId == MaterialId::None) {
        return;
    }

    const int materialId = static_cast<int>(active.materialId);
    active.ResetToUnfused();

    FUSE_LOG_DBG("[FuseDBG] RangedClearActive slot=%s mat=%d reason=%s\n", RangedSlotName(slot), materialId,
                 reason ? reason : "None");
}

void Fuse::MarkRangedHitResolved(RangedFuseSlot slot, const char* reason) {
    RangedFuseState& state = GetRangedQueued(slot);
    state.hitResolved = true;
    state.inFlight = false;
    state.pendingRefundMaterial = MaterialId::None;
    state.pendingRefundFrame = -1;
    (void)reason;
}

bool Fuse::TryMarkRangedProjectileAsFire(RangedFuseSlot slot, Actor* projectile, Actor* target, const char* hitKind) {
    if (!projectile) {
        return false;
    }

    MaterialId materialId = MaterialId::None;
    if (!Fuse_RangedHasBurnModifier(slot, &materialId)) {
        return false;
    }

    if (target && target->id == ACTOR_BG_ICE_SHELTER) {
        FUSE_LOG_DBG("[FuseDBG] BurnFireSkip: target=BG_ICE_SHELTER\n");
        return false;
    }

    if (target && FuseBash_IsEnemyActor(target)) {
        FUSE_LOG_DBG("[FuseDBG] BurnFireSkip: proj=0x%04X kind=%s target=enemy id=0x%04X mat=%d\n", projectile->id,
                     hitKind ? hitKind : "unknown", target->id, static_cast<int>(materialId));
        return false;
    }

    Fuse_RangedMarkProjectileAsFire(projectile);
    FUSE_LOG_DBG("[FuseDBG] BurnFireHit: proj=0x%04X kind=%s target=0x%04X mat=%d\n", projectile->id,
                 hitKind ? hitKind : "unknown", target ? target->id : 0, static_cast<int>(materialId));
    return true;
}

void Fuse::OnRangedProjectileHitFinalize(RangedFuseSlot slot, const char* reason) {
    RangedFuseState& active = GetRangedActive(slot);
    if (active.materialId == MaterialId::None || active.durabilityCur <= 0) {
        return;
    }

    const int materialId = static_cast<int>(active.materialId);
    const int maxDurability = active.durabilityMax;
    const int newCur = std::max(0, active.durabilityCur - 1);
    active.durabilityCur = newCur;
    FUSE_LOG_DBG("[FuseDBG] RangedHitActive slot=%s mat=%d dura=%d/%d\n", RangedSlotName(slot), materialId, newCur,
                 maxDurability);

    FUSE_LOG_DBG("[FuseDBG] RangedHitFinalize slot=%s mat=%d dura=%d/%d reason=%s\n", RangedSlotName(slot), materialId,
                 newCur, maxDurability, reason ? reason : "None");

    Fuse::ClearActiveRangedFuse(slot, reason);
    if (gPlayState) {
        Fuse_PruneSeekStates(gPlayState);
    }
}

void Fuse::OnHookshotShotStarted(const char* reason) {
    RangedFuseState& state = GetRangedQueued(RangedFuseSlot::Hookshot);
    state.inFlight = true;
    state.hadSuccess = false;
    state.hitResolved = false;
    (void)reason;
}

void Fuse::OnHookshotRetractedOrKilled(const char* reason) {
    RangedFuseState& state = GetRangedQueued(RangedFuseSlot::Hookshot);
    if (!state.inFlight) {
        state.hadSuccess = false;
        return;
    }

    state.inFlight = false;
    if (!state.hadSuccess) {
        Fuse::CancelQueuedRangedFuse_Refund(RangedFuseSlot::Hookshot, reason);
        return;
    }

    state.hadSuccess = false;
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
        FUSE_LOG_MVP("[FuseMVP] Sword fuse broke at frame=%d; clearing fuse and reverting to vanilla (reason=%s)\n",
                     frame, reason ? reason : "unknown");
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
        FUSE_LOG_MVP("[FuseMVP] Boomerang fuse broke at frame=%d; clearing fuse (reason=%s)\n", frame,
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
        FUSE_LOG_MVP("[FuseMVP] Hammer fuse broke at frame=%d; clearing fuse (reason=%s)\n", frame,
                     reason ? reason : "unknown");
        OnHammerFuseBroken(play);
        return true;
    }

    return false;
}

void Fuse::OnSwordFuseBroken(PlayState* play) {
    SetLastEvent("Sword fuse broke");
    FuseHooks::RestoreSwordHitboxVanillaNow(play);
    ClearSwordBeamRuntimeState();
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
    FUSE_LOG_MVP("[FuseMVP] Acquired material: ROCK (count=%d)\n", count);
}

// -----------------------------------------------------------------------------
// Hooks entrypoints
// -----------------------------------------------------------------------------
void Fuse::OnLoadGame(int32_t /*fileNum*/) {
    // Reset runtime state
    gFuseRuntime = FuseRuntimeState{};
    gRangedQueued = {};
    gRangedActive = {};
    gFuseRuntime.enabled = true;
    gFuseRuntime.lastEvent = "Loaded";
    if (sHammerSlotLoadedFromSaveManager) {
        gFuseRuntime.hammerSlot = sLoadedHammerSlot;
    }

    ResetSwordFreezeQueueInternal();
    ResetDekuStunQueueInternal();
    sFuseFrozenTimers.clear();
    sFreezeAppliedFrame.clear();
    sFreezeShatterFrame.clear();
    sFreezeLastShatterFrame.clear();
    sFreezeShatterDamageVictim = nullptr;
    sFreezeShatterDamageFrame = -1;
    sFuseFrozenOrigGravity.clear();
    sFuseFrozenPos.clear();
    sFuseFrozenPinned.clear();
    sBurnStates.clear();
    sSeekStates.clear();
    sShieldBeamState = FuseShieldBeamState{};
    sShieldBeamActor = nullptr;
    sSwordBeamState = FuseSwordBeamState{};
    sSwordBeamActor = nullptr;
    sShatterImpulseUntilFrame.clear();
    sShatterImpulseDir.clear();
    sShatterImpulseYaw.clear();
    sShatterImpulseFlipped.clear();
    sHpOverrideApplied.clear();
    gLastSwordBgExplodeFrame = -999;
    gLastSwordActorExplodeFrame = -999999;

    EnsureMaterialInventoryInitialized();

    if (!sSwordSlotsLoadedFromSaveManager) {
        gFuseSave = FuseSaveData{};
        FusePersistence::ApplySwordStateFromContext(nullptr);
    } else {
        const SwordFuseSlot& slot = gFuseSave.GetActiveSwordSlot(nullptr);
        gFuseRuntime.swordFuseLoadedFromSave = slot.materialId != MaterialId::None;
    }

    FUSE_LOG_MVP("[FuseMVP] Save loaded -> Fuse ACTIVE (always enabled)\n");
    FUSE_LOG_MVP("[FuseMVP] MVP: Throw a liftable rock until it BREAKS to acquire ROCK.\n");
}

static void UpdateRangedFuseLifecycle(PlayState* play) {
    const int currentFrame = GetGameplayFrame();
    for (size_t i = 0; i < gRangedQueued.size(); ++i) {
        RangedFuseState& state = gRangedQueued[i];
        if (state.pendingRefundMaterial != MaterialId::None && state.pendingRefundFrame != currentFrame) {
            state.pendingRefundMaterial = MaterialId::None;
            state.pendingRefundFrame = -1;
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
            RangedFuseState& state = GetRangedQueued(previousSlot);
            if (state.materialId != MaterialId::None && !(previousSlot == RangedFuseSlot::Hookshot && state.inFlight)) {
                // If the slot already resolved via a successful hit, don't allow HeldItemSwitch cancel/refund logic.
                if (!state.hitResolved) {
                    if (!aiming || aimingSlot != previousSlot) {
                        Fuse::CancelQueuedRangedFuse_Refund(previousSlot, "HeldItemSwitch");
                    }
                }
            }
        }
    }

    for (size_t i = 0; i < gRangedQueued.size(); ++i) {
        const RangedFuseSlot slot = static_cast<RangedFuseSlot>(i);
        RangedFuseState& state = gRangedQueued[i];
        if (state.materialId == MaterialId::None) {
            continue;
        }

        if (state.hitResolved) {
            continue;
        }

        if (slot == RangedFuseSlot::Hookshot && state.inFlight) {
            continue;
        }

        if (!aiming || aimingSlot != slot) {
            Fuse::CancelQueuedRangedFuse_Refund(slot, "AimExitOrSwitch");
        }
    }

    gFuseRuntime.lastHeldItemAction = heldAction;
}

static FuseItemType RangedSlotItemType(RangedFuseSlot slot) {
    switch (slot) {
        case RangedFuseSlot::Arrows:
            return FuseItemType::Arrows;
        case RangedFuseSlot::Slingshot:
            return FuseItemType::Slingshot;
        case RangedFuseSlot::Hookshot:
            return FuseItemType::Hookshot;
        default:
            return FuseItemType::Unknown;
    }
}

void Fuse::TickSwordBgExplosions(PlayState* play) {
    if (!play) {
        return;
    }

    Player* player = GET_PLAYER(play);
    if (!player || !Fuse::IsSwordFused()) {
        return;
    }

    const MaterialId materialId = Fuse::GetSwordMaterial();
    const uint8_t explosionLevel =
        Fuse::GetMaterialModifierLevel(materialId, FuseItemType::Sword, ModifierId::Explosion);
    if (explosionLevel == 0) {
        return;
    }

    if (!IsPlayerSwingingSword(player)) {
        return;
    }

    const int curFrame = play->gameplayFrames;
    if (curFrame == gLastSwordActorExplodeFrame) {
        return;
    }

    if (curFrame >= 0 && (curFrame - gLastSwordBgExplodeFrame) < 10) {
        return;
    }

    Vec3f from = player->meleeWeaponInfo[0].base;
    Vec3f to = player->meleeWeaponInfo[0].tip;
    if (Fuse_IsZeroishPos(from) || Fuse_IsZeroishPos(to)) {
        from = GetPlayerImpactPos(player, 10.0f, 14.0f);
        to = GetPlayerImpactPos(player, 40.0f, 14.0f);
    }

    Vec3f hitPos{ 0.0f, 0.0f, 0.0f };
    CollisionPoly* hitPoly = nullptr;
    s32 bgId = -1;
    if (!BgCheck_EntityLineTest1(&play->colCtx, &from, &to, &hitPos, &hitPoly, true, true, true, true, &bgId)) {
        return;
    }
    (void)hitPoly;
    (void)bgId;

    Vec3f explodePos = hitPos;
    explodePos.y += 6.0f;
    const Actor* bombable = Fuse_FindNearbyBombable(play, &explodePos, 120.0f);
    if (bombable) {
        explodePos = Fuse_GetBombableAnchorPos(bombable, 25.0f);
        Fuse_AdjustExplosionPosForBombable(bombable, &player->actor, &explodePos);
    }

    FUSE_LOG_DBG("[FuseDBG] SwordBGLineHit pos=(%.2f %.2f %.2f)\n", hitPos.x, hitPos.y, hitPos.z);
    Fuse_TriggerExplosion(play, explodePos, FuseExplosionSelfMode::DamagePlayer,
                          Fuse_GetExplosionParams(materialId, explosionLevel), "SwordBG");
    gLastSwordBgExplodeFrame = curFrame;

    const int before = Fuse::GetSwordFuseDurability();
    const bool broke = Fuse::DamageSwordFuseDurability(play, 1, "SwordBG");
    const int after = Fuse::GetSwordFuseDurability();
    FUSE_LOG_MVP("[FuseMVP] Sword BG impact DRAIN frame=%d durability=%d->%d%s\n", curFrame, before, after,
                 broke ? " (broke)" : "");
}

void Fuse::TickRangedProjectileSeek(PlayState* play) {
    if (!play) {
        return;
    }

    Fuse_RegisterSeekCVars();

    const float seekRadius = CVarGetFloat("gFuseSeekRadius", 900.0f);
    if (seekRadius <= 1.0f) {
        return;
    }

    const float seekDotMin = std::clamp(CVarGetFloat("gFuseSeekDotMin", 0.65f), -1.0f, 1.0f);
    const float seekMaxTurnDeg = std::max(0.0f, CVarGetFloat("gFuseSeekMaxTurnDeg", 6.0f));
    const float seekTurnScaleFar = std::max(0.0f, CVarGetFloat("gFuseSeekTurnScaleFar", 0.4f));
    const int seekAcquireDelayFrames = std::max(0, CVarGetInteger("gFuseSeekAcquireDelay", 2));
    const bool seekDisableStop = CVarGetInteger("gFuseSeekDisableStop", 0) != 0;
    static bool loggedSeekCVars = false;
    if (Fuse_SeekDebugEnabled() && !loggedSeekCVars) {
        Fuse::Log("[FuseDBG] SeekCVars: radius=%d dotMin=%.2f maxTurnDeg=%.2f farScale=%.2f delay=%d stopDisable=%d\n",
                  static_cast<int>(seekRadius), seekDotMin, seekMaxTurnDeg, seekTurnScaleFar, seekAcquireDelayFrames,
                  seekDisableStop ? 1 : 0);
        loggedSeekCVars = true;
    }

    const int curFrame = play->gameplayFrames;
    const bool shouldCleanup = (curFrame >= 0) && (curFrame % 120 == 0);
    std::unordered_set<Actor*> liveProjectileKeys;
    if (shouldCleanup) {
        liveProjectileKeys.reserve(sSeekStates.size());
    }

    auto stopSeeking = [&](Actor* proj, FuseSeekState& state, const char* reason) {
        state.isSeekingActive = false;
        state.hasAcquired = true;
        state.targetActor = nullptr;
        if (Fuse_SeekDebugEnabled() && !state.loggedStop) {
            Fuse::Log("[FuseDBG] SeekStop proj=%p reason=%s\n", proj, reason);
            state.loggedStop = true;
        }
    };

    for (int i = 0; i < ACTORCAT_MAX; ++i) {
        Actor* proj = play->actorCtx.actorLists[i].head;
        while (proj != nullptr) {
            if (proj->id != ACTOR_EN_ARROW) {
                proj = proj->next;
                continue;
            }

            if (shouldCleanup) {
                liveProjectileKeys.insert(proj);
            }

            const bool isSeed = (proj->params == ARROW_SEED);
            const RangedFuseSlot slot = isSeed ? RangedFuseSlot::Slingshot : RangedFuseSlot::Arrows;
            const RangedFuseState& active = GetRangedActive(slot);
            if (active.materialId == MaterialId::None || active.durabilityCur <= 0 ||
                !Fuse_MaterialHasSeek(active.materialId)) {
                sSeekStates.erase(proj);
                proj = proj->next;
                continue;
            }

            auto [it, inserted] = sSeekStates.emplace(proj, FuseSeekState{});
            FuseSeekState& state = it->second;
            if (inserted) {
                state.acquireDelayFramesRemaining = seekAcquireDelayFrames;
            }

            if (!state.hasPrevPos) {
                state.prevPos = proj->world.pos;
                state.hasPrevPos = true;
            }

            Vec3f move{ proj->world.pos.x - state.prevPos.x, proj->world.pos.y - state.prevPos.y,
                        proj->world.pos.z - state.prevPos.z };
            float dispSpeed = Fuse_Vec3fLength(move);
            const bool hasDispDir = dispSpeed > 0.01f;
            Vec3f projForward{ 0.0f, 0.0f, 0.0f };
            Vec3f dispDir{ 0.0f, 0.0f, 0.0f };
            if (hasDispDir) {
                dispDir = Fuse_Vec3fNormalize(move);
                projForward = dispDir;
            } else {
                Vec3f vel{ proj->velocity.x, proj->velocity.y, proj->velocity.z };
                float velLen = Fuse_Vec3fLength(vel);
                if (velLen > 0.01f) {
                    projForward = Fuse_Vec3fNormalize(vel);
                } else {
                    const float speed = sqrtf((proj->speedXZ * proj->speedXZ) + (proj->velocity.y * proj->velocity.y));
                    const float denom = std::max(speed, 0.0001f);
                    projForward.x = Math_SinS(proj->world.rot.y);
                    projForward.z = Math_CosS(proj->world.rot.y);
                    projForward.y = proj->velocity.y / denom;
                    projForward = Fuse_Vec3fNormalize(projForward);
                }
            }

            auto logSeekStopCheck = [&](float dot, const Vec3f& desiredDir) {
                if (Fuse_SeekDebugEnabled()) {
                    Fuse::Log("[FuseDBG] SeekStopCheck proj=%p dot=%.3f projF=(%.2f %.2f %.2f) tgtD=(%.2f %.2f %.2f)\n",
                              proj, dot, projForward.x, projForward.y, projForward.z, desiredDir.x, desiredDir.y,
                              desiredDir.z);
                }
            };

            if (!state.hasAcquired) {
                if (state.acquireDelayFramesRemaining > 0) {
                    --state.acquireDelayFramesRemaining;
                    state.prevPos = proj->world.pos;
                    proj = proj->next;
                    continue;
                }

                float speed = 0.0f;
                Fuse_GetArrowEffectiveDir(proj, &speed);
                if (speed <= 0.01f) {
                    state.hasAcquired = true;
                    state.isSeekingActive = false;
                    state.prevPos = proj->world.pos;
                    proj = proj->next;
                    continue;
                }

                Actor* bestTarget = nullptr;
                float bestScore = 0.0f;
                float bestDist = 0.0f;
                float bestDot = -1.0f;

                Actor* actor = play->actorCtx.actorLists[ACTORCAT_ENEMY].head;
                while (actor != nullptr) {
                    if (!IsActorAliveInPlay(play, actor) || !FuseBash_IsEnemyActor(actor)) {
                        actor = actor->next;
                        continue;
                    }

                    const Vec3f toTarget{ actor->focus.pos.x - proj->world.pos.x,
                                          actor->focus.pos.y - proj->world.pos.y,
                                          actor->focus.pos.z - proj->world.pos.z };
                    const float dist = Fuse_Vec3fLength(toTarget);
                    if (dist > seekRadius || dist <= 0.01f) {
                        actor = actor->next;
                        continue;
                    }

                    const Vec3f desiredDir = Fuse_Vec3fNormalize(toTarget);
                    const float dot = Fuse_Vec3fDot(projForward, desiredDir);
                    if (dot < seekDotMin) {
                        actor = actor->next;
                        continue;
                    }

                    const float score = (dot * dot) / (dist * dist + (200.0f * 200.0f));
                    if (!bestTarget || score > bestScore) {
                        bestTarget = actor;
                        bestScore = score;
                        bestDist = dist;
                        bestDot = dot;
                    }

                    actor = actor->next;
                }

                state.hasAcquired = true;
                state.targetActor = bestTarget;
                state.isSeekingActive = (bestTarget != nullptr);
                if (state.isSeekingActive) {
                    state.ticksSinceAcquire = 0;
                }
                if (bestTarget && Fuse_SeekDebugEnabled()) {
                    Fuse::Log("[FuseDBG] SeekAcquire proj=%p target=%p dist=%d dot=%.2f\n", proj, bestTarget,
                              static_cast<int>(bestDist), bestDot);
                } else if (!bestTarget && Fuse_SeekDebugEnabled() && !state.loggedNoTarget) {
                    Fuse::Log("[FuseDBG] SeekNoTarget proj=%p\n", proj);
                    state.loggedNoTarget = true;
                }

                state.prevPos = proj->world.pos;
                proj = proj->next;
                continue;
            }

            if (!state.isSeekingActive || !state.targetActor) {
                state.prevPos = proj->world.pos;
                proj = proj->next;
                continue;
            }

            if (!IsActorAliveInPlay(play, state.targetActor)) {
                stopSeeking(proj, state, "target_dead");
                state.prevPos = proj->world.pos;
                proj = proj->next;
                continue;
            }

            const Vec3f toTarget{ state.targetActor->focus.pos.x - proj->world.pos.x,
                                  state.targetActor->focus.pos.y - proj->world.pos.y,
                                  state.targetActor->focus.pos.z - proj->world.pos.z };
            const float dist = Fuse_Vec3fLength(toTarget);
            if (dist > seekRadius && !seekDisableStop) {
                stopSeeking(proj, state, "out_of_range");
                state.prevPos = proj->world.pos;
                proj = proj->next;
                continue;
            }

            const Vec3f desiredDir = Fuse_Vec3fNormalize(toTarget);
            if (state.ticksSinceAcquire < std::numeric_limits<int>::max()) {
                ++state.ticksSinceAcquire;
            }

            float dotForward = Fuse_Vec3fDot(projForward, desiredDir);
            if (hasDispDir || Fuse_Vec3fLength(projForward) > 0.0f) {
                if (state.ticksSinceAcquire >= 2 && dotForward < -0.05f) {
                    logSeekStopCheck(dotForward, desiredDir);
                    if (!seekDisableStop) {
                        stopSeeking(proj, state, "behind");
                        state.prevPos = proj->world.pos;
                        proj = proj->next;
                        continue;
                    }
                }
                if (dotForward < seekDotMin) {
                    logSeekStopCheck(dotForward, desiredDir);
                    if (!seekDisableStop) {
                        stopSeeking(proj, state, "out_of_cone");
                        state.prevPos = proj->world.pos;
                        proj = proj->next;
                        continue;
                    }
                }
            }

            float speedEff = 0.0f;
            Vec3f effDir = Fuse_GetArrowEffectiveDir(proj, &speedEff);

            Vec3f currentDir;
            float speedForSteer = 0.0f;
            const char* basis = hasDispDir ? "disp" : "eff";

            if (hasDispDir) {
                currentDir = dispDir;
                speedForSteer = (speedEff > 0.01f) ? speedEff : dispSpeed;
            } else {
                currentDir = effDir;
                speedForSteer = speedEff;
            }

            if (speedForSteer <= 0.01f) {
                state.prevPos = proj->world.pos;
                proj = proj->next;
                continue;
            }

            const float dot = Fuse_Vec3fDot(currentDir, desiredDir);
            const float t = std::clamp(dist / seekRadius, 0.0f, 1.0f);
            const float turnScale = 1.0f - (1.0f - seekTurnScaleFar) * t;
            constexpr float kDegToRad = 3.1415926535f / 180.0f;
            const float maxTurnRad = (seekMaxTurnDeg * kDegToRad) * turnScale;
            const float angle = acosf(std::clamp(dot, -1.0f, 1.0f));
            Vec3f newDir = desiredDir;
            if (angle > maxTurnRad && maxTurnRad > 0.0f) {
                const float a = maxTurnRad / angle;
                const Vec3f blended{ currentDir.x * (1.0f - a) + desiredDir.x * a,
                                     currentDir.y * (1.0f - a) + desiredDir.y * a,
                                     currentDir.z * (1.0f - a) + desiredDir.z * a };
                newDir = Fuse_Vec3fNormalize(blended);
            }

            if (Fuse_Vec3fLength(newDir) > 0.0f) {
                const float horiz = sqrtf((newDir.x * newDir.x) + (newDir.z * newDir.z));
                const float speedXZ = speedForSteer * horiz;
                const float velY = speedForSteer * newDir.y;
                const s16 yawS = Math_Atan2S(newDir.z, newDir.x);
                const s16 pitchS = (horiz <= 0.0001f) ? 0 : Math_Atan2S(-newDir.y, horiz);

                Fuse_ApplyArrowSteer(proj, newDir, speedForSteer);

                if (Fuse_SeekDebugEnabled() && !state.loggedSteer) {
                    const float forwardXZDenom = std::max(proj->speedXZ, 0.0001f);
                    const float forwardXZx = proj->velocity.x / forwardXZDenom;
                    const float forwardXZz = proj->velocity.z / forwardXZDenom;
                    Fuse::Log("[FuseDBG] SeekSteer proj=%p basis=%s dist=%.1f dot=%.2f yaw=%d pitch=%d speedXZ=%.2f "
                              "velY=%.2f "
                              "dir=(%.2f %.2f %.2f) desiredDir=(%.2f %.2f %.2f) toTarget=(%.2f %.2f %.2f) "
                              "dispDir=(%.2f %.2f %.2f) effDir=(%.2f %.2f %.2f) dispSpeed=%.2f projF=(%.2f %.2f %.2f) "
                              "forwardXZ=(%.2f %.2f) dotF=%.2f\n",
                              proj, basis, dist, dot, yawS, pitchS, speedXZ, velY, newDir.x, newDir.y, newDir.z,
                              desiredDir.x, desiredDir.y, desiredDir.z, toTarget.x, toTarget.y, toTarget.z, dispDir.x,
                              dispDir.y, dispDir.z, effDir.x, effDir.y, effDir.z, dispSpeed, projForward.x,
                              projForward.y, projForward.z, forwardXZx, forwardXZz, dotForward);
                    state.loggedSteer = true;
                }
            }

            state.prevPos = proj->world.pos;
            proj = proj->next;
        }
    }

    if (shouldCleanup && !sSeekStates.empty()) {
        for (auto it = sSeekStates.begin(); it != sSeekStates.end();) {
            if (liveProjectileKeys.find(it->first) == liveProjectileKeys.end()) {
                it = sSeekStates.erase(it);
            } else {
                ++it;
            }
        }
    }
}

void Fuse::TickRangedProjectileBombableProximity(PlayState* play) {
    if (!play) {
        return;
    }

    Player* player = GET_PLAYER(play);
    if (!player) {
        return;
    }

    auto checkSlot = [&](RangedFuseSlot slot, MaterialId* outMaterialId, uint8_t* outLevel) -> bool {
        int matId = static_cast<int>(MaterialId::None);
        int durabilityCur = 0;
        int durabilityMax = 0;
        Fuse_GetRangedFuseStatus(slot, &matId, &durabilityCur, &durabilityMax);
        if (durabilityCur <= 0 || matId == static_cast<int>(MaterialId::None)) {
            return false;
        }

        const MaterialId materialId = static_cast<MaterialId>(matId);
        const uint8_t level =
            Fuse::GetMaterialModifierLevel(materialId, RangedSlotItemType(slot), ModifierId::Explosion);
        if (level == 0) {
            return false;
        }

        if (outMaterialId) {
            *outMaterialId = materialId;
        }
        if (outLevel) {
            *outLevel = level;
        }
        return true;
    };

    RangedFuseSlot slot = RangedFuseSlot::Arrows;
    MaterialId materialId = MaterialId::None;
    uint8_t explosionLevel = 0;
    if (!checkSlot(slot, &materialId, &explosionLevel)) {
        slot = RangedFuseSlot::Slingshot;
        if (!checkSlot(slot, &materialId, &explosionLevel)) {
            return;
        }
    }

    const int curFrame = play->gameplayFrames;
    int candidateCount = 0;
    const bool shouldCleanup = (curFrame >= 0) && (curFrame % 120 == 0);
    std::unordered_set<uintptr_t> liveProjectileKeys;
    if (shouldCleanup) {
        liveProjectileKeys.reserve(sProjPrevPos.size());
    }

    for (int i = 0; i < ACTORCAT_MAX; ++i) {
        Actor* proj = play->actorCtx.actorLists[i].head;
        while (proj != nullptr) {
            if (proj->id != ACTOR_EN_ARROW) {
                proj = proj->next;
                continue;
            }

            ++candidateCount;
            const uintptr_t key = reinterpret_cast<uintptr_t>(proj);
            if (shouldCleanup) {
                liveProjectileKeys.insert(key);
            }

            auto it = sProjPrevPos.find(key);
            Vec3f prev = (it != sProjPrevPos.end()) ? it->second : proj->world.pos;
            const Vec3f cur = proj->world.pos;
            sProjPrevPos[key] = cur;

            Actor* bombable = Fuse_FindNearbyBombable(play, &cur, 220.0f);
            if (!bombable) {
                proj = proj->next;
                continue;
            }

            const Vec3f center = Fuse_GetBombableAnchorPos(bombable, 25.0f);
            float t = 0.0f;
            const float d2 = DistSqPointToSegmentXZ(&center, &prev, &cur, &t);
            const float radius = 110.0f;
            if (d2 > radius * radius) {
                proj = proj->next;
                continue;
            }

            const float y = prev.y + (cur.y - prev.y) * t;
            if (fabsf(y - center.y) > 140.0f) {
                proj = proj->next;
                continue;
            }

            Vec3f explodePos = center;
            Fuse_AdjustExplosionPosForBombable(bombable, &player->actor, &explodePos);
            FUSE_LOG_DBG("[FuseDBG] RangedBombableSweep slot=%s proj=0x%p id=0x%04X d2=%.1f\n", RangedSlotName(slot),
                         proj, bombable->id, d2);
            Fuse_TriggerExplosion(play, explodePos, FuseExplosionSelfMode::DamagePlayer,
                                  Fuse_GetExplosionParams(materialId, explosionLevel), "RangedBombableSweep");
            Actor_Kill(proj);
            sProjPrevPos.erase(key);
            Fuse::OnRangedProjectileHitFinalize(slot, "BombableSweep");
            return;
        }
    }

    static int sRangedProxLogFrame = -999999;
    if (curFrame >= 0 && (curFrame - sRangedProxLogFrame) >= 30) {
        FUSE_LOG_DBG("[FuseDBG] ProxTick slot=%s candidates=%d\n", RangedSlotName(slot), candidateCount);
        sRangedProxLogFrame = curFrame;
    }

    static int sSlingshotScanLogFrame = -999999;
    if (slot == RangedFuseSlot::Slingshot && candidateCount == 0 && curFrame >= 0 &&
        (curFrame - sSlingshotScanLogFrame) >= 60) {
        int logged = 0;
        const int kLikelyCats[] = { ACTORCAT_ITEMACTION, ACTORCAT_PROP, ACTORCAT_MISC, ACTORCAT_NPC };
        for (size_t idx = 0; idx < std::size(kLikelyCats) && logged < 20; ++idx) {
            Actor* actor = play->actorCtx.actorLists[kLikelyCats[idx]].head;
            while (actor != nullptr && logged < 20) {
                FUSE_LOG_DBG("[FuseDBG] SlingshotScan cat=%d id=0x%04X pos=(%.2f %.2f %.2f)\n", kLikelyCats[idx],
                             actor->id, actor->world.pos.x, actor->world.pos.y, actor->world.pos.z);
                ++logged;
                actor = actor->next;
            }
        }
        sSlingshotScanLogFrame = curFrame;
    }

    if (shouldCleanup && !sProjPrevPos.empty()) {
        for (auto it = sProjPrevPos.begin(); it != sProjPrevPos.end();) {
            if (liveProjectileKeys.find(it->first) == liveProjectileKeys.end()) {
                it = sProjPrevPos.erase(it);
            } else {
                ++it;
            }
        }
    }
}

static float Fuse_DistancePointToSegment(const Vec3f& point, const Vec3f& start, const Vec3f& end) {
    Vec3f seg{ end.x - start.x, end.y - start.y, end.z - start.z };
    const float segLenSq = (seg.x * seg.x) + (seg.y * seg.y) + (seg.z * seg.z);

    // Make mutable copies for OoT math helpers that take Vec3f*
    Vec3f p = point;
    Vec3f s = start;
    Vec3f e = end;

    if (segLenSq <= 0.001f) {
        return Math_Vec3f_DistXYZ(&p, &s);
    }

    Vec3f toPoint{ p.x - s.x, p.y - s.y, p.z - s.z };
    float t = (toPoint.x * seg.x) + (toPoint.y * seg.y) + (toPoint.z * seg.z);
    t /= segLenSq;
    t = std::clamp(t, 0.0f, 1.0f);

    Vec3f closest{ s.x + (seg.x * t), s.y + (seg.y * t), s.z + (seg.z * t) };
    return Math_Vec3f_DistXYZ(&p, &closest);
}

static void ClearShieldBeamRuntimeState() {
    sShieldBeamState.active = false;
    sShieldBeamState.nextDamageFrame = -1;
    sShieldBeamState.nextDrainFrame = -1;
    sShieldBeamState.boostUntilFrame = -1;
    sShieldBeamState.sweepYaw = 0;
    sShieldBeamState.sweepInitialized = false;
    sShieldBeamState.turretModeActive = false;

    if (sShieldBeamActor != nullptr) {
        if (IsActorAliveInPlay(gPlayState, sShieldBeamActor) && sShieldBeamActor->id == ACTOR_UNSET_1AA) {
            Actor_Kill(sShieldBeamActor);
        }
        sShieldBeamActor = nullptr;
    }
}

static void ClearSwordBeamRuntimeState() {
    sSwordBeamState.active = false;
    sSwordBeamState.swingActive = false;
    sSwordBeamState.swingConsumedDrain = false;
    sSwordBeamState.swordItemId = ITEM_NONE;
    sSwordBeamState.hitVictims.clear();

    if (sSwordBeamActor != nullptr) {
        if (IsActorAliveInPlay(gPlayState, sSwordBeamActor) && sSwordBeamActor->id == ACTOR_UNSET_1AA) {
            Actor_Kill(sSwordBeamActor);
        }
        sSwordBeamActor = nullptr;
    }
}

static const char* FuseItemTypeToString(FuseItemType itemType) {
    switch (itemType) {
        case FuseItemType::Sword:
            return "Sword";
        case FuseItemType::Shield:
            return "Shield";
        case FuseItemType::Boomerang:
            return "Boomerang";
        case FuseItemType::Hammer:
            return "Hammer";
        case FuseItemType::Arrows:
            return "Arrows";
        case FuseItemType::Slingshot:
            return "Slingshot";
        case FuseItemType::Hookshot:
            return "Hookshot";
        default:
            return "Unknown";
    }
}

static const char* ModifierIdToString(ModifierId id) {
    switch (id) {
        case ModifierId::Hammerize:
            return "Hammerize";
        case ModifierId::Stun:
            return "Stun";
        case ModifierId::MegaStun:
            return "MegaStun";
        case ModifierId::Freeze:
            return "Freeze";
        case ModifierId::Knockback:
            return "Knockback";
        case ModifierId::PoundUp:
            return "PoundUp";
        case ModifierId::NegateKnockback:
            return "NegateKnockback";
        case ModifierId::RangeUp:
            return "RangeUp";
        case ModifierId::WideRange:
            return "WideRange";
        case ModifierId::Explosion:
            return "Explosion";
        case ModifierId::Seek:
            return "Seek";
        case ModifierId::BashAttack:
            return "BashAttack";
        case ModifierId::Burn:
            return "Burn";
        case ModifierId::Beam:
            return "Beam";
        default:
            return "Unknown";
    }
}

static void LogSwordBeamEligibleDiag(const char* reason, PlayState* play, Player* player,
                                     const SwordFuseSlot* slot = nullptr, int beamLevel = -1,
                                     FuseItemType modifierItemType = FuseItemType::Sword,
                                     ModifierId modifierId = ModifierId::Beam) {
    static int sLastFrame = -1;
    static int sLastHeldItemAction = std::numeric_limits<int>::min();
    static int sLastHeldItemId = std::numeric_limits<int>::min();
    static int sLastCurrentSwordItemId = std::numeric_limits<int>::min();
    static int sLastBButtonItem = std::numeric_limits<int>::min();
    static int sLastMaterialId = std::numeric_limits<int>::min();
    static int sLastDurabilityCur = std::numeric_limits<int>::min();
    static int sLastDurabilityMax = std::numeric_limits<int>::min();
    static int sLastBeamLevel = std::numeric_limits<int>::min();
    static int sLastModifierItemType = std::numeric_limits<int>::min();
    static int sLastModifierId = std::numeric_limits<int>::min();
    static int sLastHasMaterialDef = std::numeric_limits<int>::min();
    static int sLastHasBeamInDef = std::numeric_limits<int>::min();
    static uintptr_t sLastSlotAddr = 0;
    static std::string sLastReason;

    const int frame = play != nullptr ? play->gameplayFrames : -1;
    const int heldItemAction = player != nullptr ? player->heldItemAction : ITEM_NONE;
    const int heldItemId = player != nullptr ? player->heldItemId : ITEM_NONE;
    const int currentSwordItemId = player != nullptr ? player->currentSwordItemId : ITEM_NONE;
    const int bButtonItem = gSaveContext.equips.buttonItems[0];
    const int materialId = slot != nullptr ? static_cast<int>(slot->materialId) : static_cast<int>(MaterialId::None);
    const int durabilityCur = slot != nullptr ? slot->durabilityCur : -1;
    const int durabilityMax = slot != nullptr ? slot->durabilityMax : -1;
    const int modifierItemTypeValue = static_cast<int>(modifierItemType);
    const int modifierIdValue = static_cast<int>(modifierId);
    const uintptr_t slotAddr = reinterpret_cast<uintptr_t>(slot);
    const MaterialDef* materialDef = slot != nullptr ? Fuse::GetMaterialDef(slot->materialId) : nullptr;
    const char* materialName = materialDef != nullptr && materialDef->name != nullptr ? materialDef->name : "Unknown";
    const int hasMaterialDef = materialDef != nullptr ? 1 : 0;
    uint8_t beamInDefLevel = 0;
    const bool hasBeamInDef = materialDef != nullptr &&
                              HasModifier(materialDef->modifiers, materialDef->modifierCount, ModifierId::Beam,
                                          &beamInDefLevel) &&
                              beamInDefLevel > 0;
    const int hasBeamInDefValue = hasBeamInDef ? 1 : 0;

    if (frame == sLastFrame && heldItemAction == sLastHeldItemAction && heldItemId == sLastHeldItemId &&
        currentSwordItemId == sLastCurrentSwordItemId && bButtonItem == sLastBButtonItem && materialId == sLastMaterialId &&
        durabilityCur == sLastDurabilityCur && durabilityMax == sLastDurabilityMax && beamLevel == sLastBeamLevel &&
        modifierItemTypeValue == sLastModifierItemType && modifierIdValue == sLastModifierId &&
        hasMaterialDef == sLastHasMaterialDef && hasBeamInDefValue == sLastHasBeamInDef &&
        slotAddr == sLastSlotAddr && sLastReason == reason) {
        return;
    }

    sLastFrame = frame;
    sLastHeldItemAction = heldItemAction;
    sLastHeldItemId = heldItemId;
    sLastCurrentSwordItemId = currentSwordItemId;
    sLastBButtonItem = bButtonItem;
    sLastMaterialId = materialId;
    sLastDurabilityCur = durabilityCur;
    sLastDurabilityMax = durabilityMax;
    sLastBeamLevel = beamLevel;
    sLastModifierItemType = modifierItemTypeValue;
    sLastModifierId = modifierIdValue;
    sLastHasMaterialDef = hasMaterialDef;
    sLastHasBeamInDef = hasBeamInDefValue;
    sLastSlotAddr = slotAddr;
    sLastReason = reason != nullptr ? reason : "unknown";

    FUSE_LOG_DBG("[FuseDBG] SwordBeamEligible reason=%s frame=%d play=%p player=%p fuseEnabled=%d heldItemAction=%d heldItemId=%d currentSwordItemId=%d bButtonItem=%d slot=%p beamosHeadId=%d beamModifierId=%d slotMaterial=%d materialDefValid=%d materialName=%s materialHasBeam=%d dura=%d/%d beamLevel=%d modifierItemType=%s modifierItemTypeValue=%d modifierName=%s modifierId=%d expectedBeamosHead=%d\n",
                 reason != nullptr ? reason : "unknown", frame, static_cast<void*>(play), static_cast<void*>(player),
                 Fuse::IsEnabled() ? 1 : 0, heldItemAction, heldItemId, currentSwordItemId, bButtonItem,
                 static_cast<const void*>(slot), static_cast<int>(MaterialId::BeamosHead),
                 static_cast<int>(ModifierId::Beam), materialId, hasMaterialDef, materialName, hasBeamInDefValue,
                 durabilityCur, durabilityMax, beamLevel, FuseItemTypeToString(modifierItemType),
                 modifierItemTypeValue, ModifierIdToString(modifierId), modifierIdValue,
                 static_cast<int>(MaterialId::BeamosHead));
}

static bool SwordBeamEligible(PlayState* play, Player* player, SwordFuseSlot** outSlot = nullptr) {
    if (outSlot != nullptr) {
        *outSlot = nullptr;
    }

    if (!play || !player || !Fuse::IsEnabled()) {
        LogSwordBeamEligibleDiag("FuseDisabledOrNull", play, player);
        return false;
    }

    if (player->heldItemAction < PLAYER_IA_SWORD_KOKIRI || player->heldItemAction > PLAYER_IA_SWORD_BIGGORON) {
        LogSwordBeamEligibleDiag("HeldItemNotSword", play, player);
        return false;
    }

    SwordFuseSlot& slot = gFuseSave.GetActiveSwordSlot(play);
    if (slot.materialId != MaterialId::BeamosHead) {
        LogSwordBeamEligibleDiag("MaterialMismatch", play, player, &slot);
        return false;
    }

    if (slot.durabilityCur <= 0) {
        LogSwordBeamEligibleDiag("NoDurability", play, player, &slot);
        return false;
    }

    constexpr FuseItemType kSwordBeamModifierItemType = FuseItemType::Sword;
    const uint8_t beamLevel = Fuse::GetMaterialModifierLevel(slot.materialId, kSwordBeamModifierItemType, ModifierId::Beam);
    if (beamLevel == 0) {
        LogSwordBeamEligibleDiag("BeamModifierMissing", play, player, &slot, beamLevel, kSwordBeamModifierItemType,
                                 ModifierId::Beam);
        return false;
    }

    LogSwordBeamEligibleDiag("EligibleTrue", play, player, &slot, beamLevel, kSwordBeamModifierItemType,
                             ModifierId::Beam);

    if (outSlot != nullptr) {
        *outSlot = &slot;
    }

    return true;
}

extern "C" int32_t Fuse_SwordBeamEligibleDebug(PlayState* play, Player* player) {
    SwordFuseSlot* slot = nullptr;
    return SwordBeamEligible(play, player, &slot) && slot != nullptr;
}

extern "C" void Fuse_LogSwordBeamBridge(const char* phase, PlayState* play, Player* player, int32_t q0, int32_t q1,
                                         int32_t eligible) {
    LogSwordBeamDbg(phase, play, player, q0, q1, eligible != 0);
}

static int SwordBeamBaseDamageFromHeldAction(const Player* player) {
    if (!player) {
        return 0;
    }

    switch (player->heldItemAction) {
        case PLAYER_IA_SWORD_KOKIRI:
            return 1;
        case PLAYER_IA_SWORD_MASTER:
            return 2;
        case PLAYER_IA_SWORD_BIGGORON:
            return 4;
        default:
            return 0;
    }
}

static void LogSwordBeamDbg(const char* phase, PlayState* play, Player* player, int32_t q0, int32_t q1, bool eligible) {
    const int frame = play != nullptr ? play->gameplayFrames : -1;
    const int heldItemAction = player != nullptr ? player->heldItemAction : -1;
    const int meleeWeaponState = player != nullptr ? player->meleeWeaponState : -1;
    const int meleeWeaponAnimation = player != nullptr ? player->meleeWeaponAnimation : -1;

    osSyncPrintf("[FuseDBG] %s frame=%d q0=%d q1=%d heldItemAction=%d meleeWeaponState=%d meleeWeaponAnimation=%d eligible=%d\n",
                 phase, frame, q0, q1, heldItemAction, meleeWeaponState, meleeWeaponAnimation, eligible ? 1 : 0);
    Fuse::Log("[FuseDBG] %s frame=%d q0=%d q1=%d heldItemAction=%d meleeWeaponState=%d meleeWeaponAnimation=%d eligible=%d\n",
              phase, frame, q0, q1, heldItemAction, meleeWeaponState, meleeWeaponAnimation, eligible ? 1 : 0);
}

extern "C" void Fuse_SwordBeamQuadActiveBegin(PlayState* play, Player* player, int32_t q0, int32_t q1) {
    SwordFuseSlot* slot = nullptr;
    const bool eligible = SwordBeamEligible(play, player, &slot) && slot != nullptr;
    LogSwordBeamDbg("Fuse_SwordBeamQuadActiveBegin", play, player, q0, q1, eligible);
    BeginSwordBeamSwing(play, player, q0, q1);
}

// Legacy z_player.c bridge symbols are kept as intentional no-ops for linkage
// compatibility only. Sword beam lifecycle ownership now lives exclusively in
// z_player_lib.c via the authoritative quad-publication hooks above.
extern "C" void Fuse_SwordBeamBeginSwing(PlayState* play, Player* player) {
    (void)play;
    (void)player;
}

extern "C" void Fuse_SwordBeamTick(PlayState* play, Player* player) {
    (void)play;
    (void)player;
}

extern "C" void Fuse_SwordBeamEndSwing(PlayState* play, Player* player) {
    (void)play;
    (void)player;
}

extern "C" void Fuse_SwordBeamQuadActiveEnd(PlayState* play, Player* player, int32_t q0, int32_t q1) {
    SwordFuseSlot* slot = nullptr;
    const bool eligible = SwordBeamEligible(play, player, &slot) && slot != nullptr;
    LogSwordBeamDbg("Fuse_SwordBeamQuadActiveEnd", play, player, q0, q1, eligible);
    EndSwordBeamSwing(play, player, q0, q1);
}

extern "C" void Fuse_SwordBeamQuadActiveTick(PlayState* play, Player* player, int32_t q0, int32_t q1) {
    SwordFuseSlot* slot = nullptr;
    const bool eligible = SwordBeamEligible(play, player, &slot) && slot != nullptr;
    LogSwordBeamDbg("Fuse_SwordBeamQuadActiveTick", play, player, q0, q1, eligible);
    TickSwordSwingBeam(play, player, q0, q1);
}

static bool Fuse_IsChildHylianShieldCrouchBeamMode(Player* player, PlayState* play, const FuseSlot& slot) {
    if (!play || !player || !Fuse::IsEnabled()) {
        return false;
    }

    if (!LINK_IS_CHILD || !Player_IsChildWithHylianShield(player) || player->currentShield != PLAYER_SHIELD_HYLIAN) {
        return false;
    }

    if ((player->stateFlags1 & PLAYER_STATE1_SHIELDING) == 0) {
        return false;
    }

    if (slot.materialId != MaterialId::BeamosHead || slot.durabilityCur <= 0) {
        return false;
    }

    const uint8_t beamLevel = Fuse::GetMaterialModifierLevel(slot.materialId, FuseItemType::Shield, ModifierId::Beam);
    return beamLevel > 0;
}

static bool ShieldBeamBoostEligible(PlayState* play, FuseSlot** outSlot = nullptr) {
    if (outSlot != nullptr) {
        *outSlot = nullptr;
    }

    if (!play || !Fuse::IsEnabled()) {
        return false;
    }

    FuseSlot& slot = gFuseSave.GetActiveShieldSlot(play);
    if (slot.materialId != MaterialId::BeamosHead || slot.durabilityCur <= 0) {
        return false;
    }

    const uint8_t beamLevel = Fuse::GetMaterialModifierLevel(slot.materialId, FuseItemType::Shield, ModifierId::Beam);
    if (beamLevel == 0) {
        return false;
    }

    if (!sShieldBeamState.active && (sShieldBeamActor == nullptr || sShieldBeamActor->update == nullptr)) {
        return false;
    }

    if (outSlot != nullptr) {
        *outSlot = &slot;
    }

    return true;
}

void Fuse::OnShieldBashBeamBoost(PlayState* play) {
    FuseSlot* slot = nullptr;
    if (!ShieldBeamBoostEligible(play, &slot) || slot == nullptr) {
        return;
    }

    const int frame = play->gameplayFrames;
    const int oldDurability = slot->durabilityCur;
    slot->durabilityCur = std::max(0, slot->durabilityCur - kShieldBeamBoostExtraDrain);
    sShieldBeamState.boostUntilFrame = frame + kShieldBeamBoostDurationFrames;

    FUSE_LOG_DBG("[FuseDBG] BeamShieldBoost frame=%d mat=%d dura=%d->%d until=%d\n", frame,
                 static_cast<int>(slot->materialId), oldDurability, slot->durabilityCur,
                 sShieldBeamState.boostUntilFrame);

    if (slot->durabilityCur <= 0) {
        slot->ResetToUnfused();
        ClearShieldBeamRuntimeState();
    }
}

extern "C" void Fuse_ShieldBashBeamBoost(PlayState* play) {
    Fuse::OnShieldBashBeamBoost(play);
}

static void TickShieldGuardBeam(PlayState* play) {
    Fuse_RegisterShieldBeamCVars();

    const bool wasActive = sShieldBeamState.active;
    sShieldBeamState.active = false;

    if (!play || !Fuse::IsEnabled()) {
        ClearShieldBeamRuntimeState();
        return;
    }

    Player* player = GET_PLAYER(play);
    if (!player) {
        return;
    }

    const int frame = play->gameplayFrames;
    const uint32_t stateFlags1 = player->stateFlags1;
    const bool guarding = (stateFlags1 & PLAYER_STATE1_SHIELDING) != 0;
    FuseSlot& slot = gFuseSave.GetActiveShieldSlot(play);
    const bool beamosShield = slot.materialId == MaterialId::BeamosHead;
    const uint8_t beamLevel =
        beamosShield ? Fuse::GetMaterialModifierLevel(slot.materialId, FuseItemType::Shield, ModifierId::Beam) : 0;

    static int sBeamShieldGateLogFrame = -999999;
    if (beamosShield && (frame - sBeamShieldGateLogFrame) >= 60) {
        FUSE_LOG_DBG("[FuseDBG] BeamShieldGate frame=%d guarding=%d mat=%d dura=%d beamLvl=%d stateFlags1=0x%08X\n",
                     frame, guarding ? 1 : 0, static_cast<int>(slot.materialId), slot.durabilityCur, beamLevel,
                     stateFlags1);
        sBeamShieldGateLogFrame = frame;
    }

    if (!guarding || !beamosShield || slot.durabilityCur <= 0 || beamLevel == 0) {
        if (wasActive) {
            const char* reason = (!beamosShield || slot.durabilityCur <= 0 || beamLevel == 0) ? "Broken" : "GuardEnd";
            FUSE_LOG_DBG("[FuseDBG] BeamShieldInactive reason=%s frame=%d guarding=%d mat=%d dura=%d beamLvl=%d\n",
                         reason, frame, guarding ? 1 : 0, static_cast<int>(slot.materialId), slot.durabilityCur,
                         beamLevel);
        }
        ClearShieldBeamRuntimeState();
        return;
    }

    const s16 bodyYaw = player->actor.shape.rot.y;
    Vec3s shieldRot{};
    Matrix_MtxFToYXZRotS(&player->shieldMf, &shieldRot, false);
    const bool childHylianCrouchTurretMode = Fuse_IsChildHylianShieldCrouchBeamMode(player, play, slot);

    bool usingShieldYaw = false;
    s16 shieldAimYaw = bodyYaw;
    Vec3f shieldForward{ -player->shieldMf.xz, -player->shieldMf.yz, -player->shieldMf.zz };
    shieldForward = Fuse_Vec3fNormalize(shieldForward);
    const float shieldForwardXZ = sqrtf((shieldForward.x * shieldForward.x) + (shieldForward.z * shieldForward.z));
    if (shieldForwardXZ > 0.001f) {
        shieldAimYaw = Math_Atan2S(shieldForward.z, shieldForward.x);
        usingShieldYaw = true;
    }

    s16 beamYaw = shieldAimYaw;
    if (childHylianCrouchTurretMode) {
        if (!sShieldBeamState.turretModeActive) {
            FUSE_LOG_DBG("[FuseDBG] BeamTurretEnter frame=%d yaw=%d\n", frame, shieldAimYaw);
        }
        if (!sShieldBeamState.sweepInitialized) {
            sShieldBeamState.sweepYaw = shieldAimYaw;
            sShieldBeamState.sweepInitialized = true;
        }

        const float sweepSpeedDeg =
            CVarGetFloat("gFuseBeamShieldChildHylianCrouchSweepSpeedDeg", 1.2f);
        const s16 sweepStep = static_cast<s16>(sweepSpeedDeg * (32768.0f / 180.0f));
        sShieldBeamState.sweepYaw = static_cast<s16>(sShieldBeamState.sweepYaw + sweepStep);
        beamYaw = sShieldBeamState.sweepYaw;
    } else if (sShieldBeamState.turretModeActive) {
        FUSE_LOG_DBG("[FuseDBG] BeamTurretExit frame=%d\n", frame);
        sShieldBeamState.sweepInitialized = false;
    }
    sShieldBeamState.turretModeActive = childHylianCrouchTurretMode;

    const s16 originYaw = childHylianCrouchTurretMode ? bodyYaw : beamYaw;

    Vec3f forward{ Math_SinS(beamYaw), 0.0f, Math_CosS(beamYaw) };
    forward = Fuse_Vec3fNormalize(forward);
    if (Fuse_Vec3fLength(forward) <= 0.001f) {
        return;
    }

    Vec3f right{ Math_CosS(originYaw), 0.0f, -Math_SinS(originYaw) };
    right = Fuse_Vec3fNormalize(right);
    const Vec3f up{ 0.0f, 1.0f, 0.0f };
    Vec3f originForward{ Math_SinS(originYaw), 0.0f, Math_CosS(originYaw) };
    originForward = Fuse_Vec3fNormalize(originForward);

    const float beamRadius =
        (frame < sShieldBeamState.boostUntilFrame) ? kBeamDamageRadiusBoosted : kBeamDamageRadiusNormal;

    const bool beamDebugEnabled = CVarGetInteger("gFuseBeamShieldDebug", 0) != 0;
    const bool isAdult = LINK_IS_ADULT;
    const bool useChildHylianCrouchOffsets = childHylianCrouchTurretMode && !isAdult;
    const float offsetX = CVarGetFloat(useChildHylianCrouchOffsets ? "gFuseBeamShieldChildHylianCrouchOffsetX"
                                                                    : (isAdult ? "gFuseBeamShieldAdultOffsetX"
                                                                               : "gFuseBeamShieldChildOffsetX"),
                                       0.0f);
    const float offsetY = CVarGetFloat(useChildHylianCrouchOffsets ? "gFuseBeamShieldChildHylianCrouchOffsetY"
                                                                    : (isAdult ? "gFuseBeamShieldAdultOffsetY"
                                                                               : "gFuseBeamShieldChildOffsetY"),
                                       0.0f);
    const float offsetZ = CVarGetFloat(useChildHylianCrouchOffsets ? "gFuseBeamShieldChildHylianCrouchOffsetZ"
                                                                    : (isAdult ? "gFuseBeamShieldAdultOffsetZ"
                                                                               : "gFuseBeamShieldChildOffsetZ"),
                                       0.0f);
    const float effectiveOffsetZ = useChildHylianCrouchOffsets
                                       ? (offsetZ - kShieldBeamChildHylianCrouchBaseBackOffset)
                                       : offsetZ;
    const float crouchLeanOffsetX =
        CVarGetFloat("gFuseBeamShieldChildHylianCrouchLeanOffsetX", 2.0f);
    const float crouchLeanOffsetZ =
        CVarGetFloat("gFuseBeamShieldChildHylianCrouchLeanOffsetZ", 2.0f);
    const bool boosted = frame < sShieldBeamState.boostUntilFrame;
    const float baseBeamWidth =
        std::clamp(CVarGetFloat("gFuseBeamShieldScaleX", 0.35f), kBeamShieldWidthMin, kBeamShieldWidthMax);
    const float beamWidth = boosted ? (baseBeamWidth * kShieldBeamBoostWidthMult) : baseBeamWidth;

    Vec3f adultBaseAnchor = player->actor.focus.pos;
    adultBaseAnchor.y -= 10.0f;
    Vec3f childBaseAnchor = player->actor.focus.pos;
    childBaseAnchor.y -= 8.0f;
    const Vec3f baseAnchor = isAdult ? adultBaseAnchor : childBaseAnchor;

    bool usingShieldPitch = false;
    s16 beamPitch = 0;
    const s16 shieldPitch = static_cast<s16>(-shieldRot.x);
    if (!childHylianCrouchTurretMode && std::abs(shieldPitch) <= 0x4000) {
        beamPitch = shieldPitch;
        usingShieldPitch = true;
    }

    const s16 baseBeamPitch = beamPitch;
    const bool zTargeting = (stateFlags1 & PLAYER_STATE1_Z_TARGETING) != 0;
    const bool guardingAndZTargeting = guarding && zTargeting;
    const int zTargetPitchDownRaw = CVarGetInteger("gFuseBeamShieldZTargetPitchDown", 0);
    const s16 zTargetPitchDown = static_cast<s16>(std::max(0, zTargetPitchDownRaw));
    const s16 zTargetAdjustment = (!childHylianCrouchTurretMode && guardingAndZTargeting) ? zTargetPitchDown : 0;
    if (zTargetAdjustment != 0) {
        const int adjustedPitch = static_cast<int>(beamPitch) + static_cast<int>(zTargetAdjustment);
        beamPitch = static_cast<s16>(std::clamp(adjustedPitch, -0x4000, 0x4000));
    }

    static int sBeamShieldZTargetPitchLogFrame = -999999;
    if (Fuse_LogDbgEnabled() && (frame - sBeamShieldZTargetPitchLogFrame) >= 30) {
        Fuse::Log("[FuseDBG] BeamShieldZTargetPitch frame=%d ztarget=%s basePitch=%d ztargetAdjust=%d finalPitch=%d\n",
                  frame, zTargeting ? "active" : "inactive", baseBeamPitch, zTargetAdjustment, beamPitch);
        sBeamShieldZTargetPitchLogFrame = frame;
    }

    const float forwardXZ = Math_CosS(beamPitch);
    forward.x = Math_SinS(beamYaw) * forwardXZ;
    forward.y = -Math_SinS(beamPitch);
    forward.z = Math_CosS(beamYaw) * forwardXZ;
    forward = Fuse_Vec3fNormalize(forward);
    if (Fuse_Vec3fLength(forward) <= 0.001f) {
        return;
    }

    static int sBeamShieldYawSrcLogFrame = -999999;
    if (Fuse_LogDbgEnabled() && (frame - sBeamShieldYawSrcLogFrame) >= 30) {
        Fuse::Log("[FuseDBG] BeamShieldYawSrc frame=%d mode=%s bodyYaw=%d shieldYaw=%d selectedYaw=%d fallback=%d\n",
                  frame, usingShieldYaw ? "shieldMf" : "shape", bodyYaw, shieldRot.y, beamYaw, usingShieldYaw ? 0 : 1);
        sBeamShieldYawSrcLogFrame = frame;
    }

    const Vec3f startForward = useChildHylianCrouchOffsets ? originForward : forward;

    Vec3f beamStart = baseAnchor;
    beamStart.x += (right.x * offsetX) + (up.x * offsetY) + (startForward.x * effectiveOffsetZ);
    beamStart.y += (right.y * offsetX) + (up.y * offsetY) + (startForward.y * effectiveOffsetZ);
    beamStart.z += (right.z * offsetX) + (up.z * offsetY) + (startForward.z * effectiveOffsetZ);

    if (useChildHylianCrouchOffsets) {
        const s16 shieldYawDelta = static_cast<s16>(shieldAimYaw - bodyYaw);
        const float leanFactor = std::clamp(static_cast<float>(shieldYawDelta) / 8192.0f, -1.0f, 1.0f);
        beamStart.x += (right.x * crouchLeanOffsetX * leanFactor) + (originForward.x * crouchLeanOffsetZ * leanFactor);
        beamStart.y += (right.y * crouchLeanOffsetX * leanFactor) + (originForward.y * crouchLeanOffsetZ * leanFactor);
        beamStart.z += (right.z * crouchLeanOffsetX * leanFactor) + (originForward.z * crouchLeanOffsetZ * leanFactor);
    }

    Vec3f beamEnd{ beamStart.x + (forward.x * kBeamRange), beamStart.y + (forward.y * kBeamRange),
                   beamStart.z + (forward.z * kBeamRange) };

    static int sBeamShieldTuningLogFrame = -999999;
    if (beamDebugEnabled && (frame - sBeamShieldTuningLogFrame) >= 30) {
        Fuse::Log("[FuseDBG] BeamShieldTune frame=%d mode=%s base=(%.2f,%.2f,%.2f) offset=(%.2f,%.2f,%.2f) "
                  "start=(%.2f,%.2f,%.2f) yawSrc=%s pitchSrc=%s yaw=%d pitch=%d scaleX=%.2f\n",
                  frame, isAdult ? "adult" : "child", baseAnchor.x, baseAnchor.y, baseAnchor.z, offsetX, offsetY,
                  effectiveOffsetZ, beamStart.x, beamStart.y, beamStart.z, usingShieldYaw ? "shieldMf" : "shape",
                  usingShieldPitch ? "shieldMf" : "flat", beamYaw, beamPitch, beamWidth);
        sBeamShieldTuningLogFrame = frame;
    }

    static int sBeamShieldAimLogFrame = -999999;
    if (Fuse_LogDbgEnabled() && (frame - sBeamShieldAimLogFrame) >= 30) {
        Fuse::Log("[FuseDBG] BeamShieldAim frame=%d mode=%s baseAnchor=(%.1f,%.1f,%.1f) finalStart=(%.1f,%.1f,%.1f) "
                  "yaw=%d pitch=%d yawSrc=%s pitchSrc=%s fallback=%d\n",
                  frame, isAdult ? "adult" : "child", baseAnchor.x, baseAnchor.y, baseAnchor.z, beamStart.x,
                  beamStart.y, beamStart.z, beamYaw, beamPitch, usingShieldYaw ? "shieldMf" : "shape",
                  usingShieldPitch ? "shieldMf" : "flat", usingShieldYaw ? 0 : 1);
        sBeamShieldAimLogFrame = frame;
    }

    sShieldBeamState.active = true;
    sShieldBeamState.start = beamStart;
    sShieldBeamState.end = beamEnd;

    if (!wasActive || sShieldBeamActor == nullptr || sShieldBeamActor->update == nullptr) {
        if (sShieldBeamActor != nullptr) {
            Actor_Kill(sShieldBeamActor);
            sShieldBeamActor = nullptr;
        }
        sShieldBeamActor =
            Actor_Spawn(&play->actorCtx, play, ACTOR_UNSET_1AA, beamStart.x, beamStart.y, beamStart.z, 0, 0, 0, 0);
    }

    if (sShieldBeamActor != nullptr) {
        EnFuseBeam* beam = reinterpret_cast<EnFuseBeam*>(sShieldBeamActor);
        const float beamDistance = Math_Vec3f_DistXYZ(&beamStart, &beamEnd);

        beam->beamPos1 = beamStart;
        beam->beamScale.x = beamWidth;
        beam->beamScale.y = beamWidth;
        beam->beamScale.z = beamDistance;
        beam->beamRot.y = Math_Vec3f_Yaw(&beamStart, &beamEnd);
        beam->beamRot.x = Math_Vec3f_Pitch(&beamStart, &beamEnd);
        beam->beamRot.z = 0;

        static int sBeamShieldTransformLogFrame = -999999;
        if (Fuse_LogDbgEnabled() && (frame - sBeamShieldTransformLogFrame) >= 30) {
            Fuse::Log("[FuseDBG] BeamShieldXform frame=%d start=(%.1f,%.1f,%.1f) end=(%.1f,%.1f,%.1f) rot=(%d,%d) "
                      "scale=(%.2f,%.1f)\n",
                      frame, beamStart.x, beamStart.y, beamStart.z, beamEnd.x, beamEnd.y, beamEnd.z, beam->beamRot.x,
                      beam->beamRot.y, beam->beamScale.x, beam->beamScale.z);
            sBeamShieldTransformLogFrame = frame;
        }
    }

    if (sShieldBeamState.nextDrainFrame < frame) {
        sShieldBeamState.nextDrainFrame = frame + 60;
    }
    if (sShieldBeamState.nextDamageFrame < frame) {
        sShieldBeamState.nextDamageFrame = frame;
    }

    if (!wasActive) {
        FUSE_LOG_DBG("[FuseDBG] BeamShieldActive frame=%d guarding=%d mat=%d dura=%d beamLvl=%d nextDrain=%d "
                     "nextDmg=%d start=(%.1f,%.1f,%.1f) end=(%.1f,%.1f,%.1f)\n",
                     frame, guarding ? 1 : 0, static_cast<int>(slot.materialId), slot.durabilityCur, beamLevel,
                     sShieldBeamState.nextDrainFrame, sShieldBeamState.nextDamageFrame, beamStart.x, beamStart.y,
                     beamStart.z, beamEnd.x, beamEnd.y, beamEnd.z);
    }

    if (frame >= sShieldBeamState.nextDrainFrame) {
        const int prevDura = slot.durabilityCur;
        slot.durabilityCur = std::max(0, slot.durabilityCur - 1);
        FUSE_LOG_DBG("[FuseDBG] BeamShieldDrain frame=%d mat=%d dura=%d->%d\n", frame,
                     static_cast<int>(slot.materialId), prevDura, slot.durabilityCur);

        if (slot.durabilityCur <= 0) {
            slot.ResetToUnfused();
            FUSE_LOG_DBG("[FuseDBG] BeamShieldInactive reason=Broken frame=%d guarding=%d mat=%d dura=%d beamLvl=%d\n",
                         frame, guarding ? 1 : 0, static_cast<int>(slot.materialId), slot.durabilityCur, beamLevel);
            ClearShieldBeamRuntimeState();
            return;
        }

        sShieldBeamState.nextDrainFrame = frame + 60;
    }

    if (frame < sShieldBeamState.nextDamageFrame) {
        return;
    }

    int hitCount = 0;
    Actor* actor = play->actorCtx.actorLists[ACTORCAT_ENEMY].head;
    while (actor != nullptr) {
        if (IsActorAliveInPlay(play, actor) && FuseBash_IsEnemyActor(actor)) {
            Vec3f target = actor->focus.pos;
            Vec3f toVictim{ target.x - beamStart.x, target.y - beamStart.y, target.z - beamStart.z };
            const float dist = Fuse_Vec3fLength(toVictim);
            if (dist > 1.0f && dist <= kBeamRange) {
                Vec3f toVictimDir = Fuse_Vec3fNormalize(toVictim);
                if (Fuse_Vec3fDot(forward, toVictimDir) >= kBeamMinForwardDot) {
                    const float distanceToLine = Fuse_DistancePointToSegment(target, beamStart, beamEnd);
                    if (distanceToLine <= beamRadius) {
                        const int prevDamage = actor->colChkInfo.damage;
                        const int beamDamage =
                            boosted ? (kBeamDamagePerTick * kShieldBeamBoostDamageMult) : kBeamDamagePerTick;
                        actor->colChkInfo.damage = beamDamage;
                        Actor_ApplyDamage(actor);
                        actor->colChkInfo.damage = prevDamage;
                        ++hitCount;
                    }
                }
            }
        }

        actor = actor->next;
    }

    FUSE_LOG_DBG("[FuseDBG] BeamShieldHitTick frame=%d hits=%d radius=%.1f boosted=%d\n", frame, hitCount, beamRadius,
                 boosted ? 1 : 0);
    sShieldBeamState.nextDamageFrame = frame + kBeamTickIntervalFrames;
}

static void TickSwordSwingBeam(PlayState* play, Player* player, int32_t q0, int32_t q1) {
    Fuse_RegisterSwordBeamCVars();

    if (!play || !player || !sSwordBeamState.swingActive) {
        return;
    }

    const int frame = play->gameplayFrames;
    SwordFuseSlot* slot = nullptr;
    const bool eligible = SwordBeamEligible(play, player, &slot) && slot != nullptr;
    LogSwordBeamDbg("TickSwordSwingBeam", play, player, q0, q1, eligible);
    if (!eligible) {
        ClearSwordBeamRuntimeState();
        return;
    }

    const int oldDurability = slot->durabilityCur;
    if (!sSwordBeamState.swingConsumedDrain) {
        const bool broke = Fuse::DamageSwordFuseDurability(play, kSwordBeamDurabilityDrainPerSwing, "SwordBeamSwing");
        sSwordBeamState.swingConsumedDrain = true;
        Fuse::Log("[Fuse] SwordBeam swing drain frame=%d swordItem=%d mat=%d dura=%d->%d cost=%d\n", frame,
                  sSwordBeamState.swordItemId, static_cast<int>(slot->materialId), oldDurability,
                  slot->durabilityCur, kSwordBeamDurabilityDrainPerSwing);
        if (broke || slot->durabilityCur <= 0) {
            ClearSwordBeamRuntimeState();
            return;
        }
    }

    const float beamRange = std::max(100.0f, CVarGetFloat("gFuseBeamSwordRange", kBeamRange));
    const float beamWidth = std::clamp(CVarGetFloat("gFuseBeamSwordScaleX", kBeamWidthNormal), 0.1f, 3.0f);
    const float offsetX = CVarGetFloat("gFuseBeamSwordOffsetX", 0.0f);
    const float offsetY = CVarGetFloat("gFuseBeamSwordOffsetY", 8.0f);
    const float offsetZ = CVarGetFloat("gFuseBeamSwordOffsetZ", 12.0f);

    const s16 beamYaw = player->actor.shape.rot.y;
    Vec3f fallbackForward{ Math_SinS(beamYaw), 0.0f, Math_CosS(beamYaw) };
    fallbackForward = Fuse_Vec3fNormalize(fallbackForward);

    const Vec3f swordBase = player->meleeWeaponInfo[0].base;
    const Vec3f swordTip = player->meleeWeaponInfo[0].tip;
    Vec3f bladeForward{ swordTip.x - swordBase.x, swordTip.y - swordBase.y, swordTip.z - swordBase.z };
    float bladeLength = 0.0f;
    bladeForward = Fuse_Vec3fNormalize(bladeForward, &bladeLength);
    const bool useBladeForward = bladeLength > 0.001f;
    const Vec3f forward = useBladeForward ? bladeForward : fallbackForward;

    Vec3f right{ Math_CosS(beamYaw), 0.0f, -Math_SinS(beamYaw) };
    right = Fuse_Vec3fNormalize(right);

    Vec3f beamAnchor = useBladeForward ? swordBase : player->actor.world.pos;
    Vec3f beamStart{ beamAnchor.x + (right.x * offsetX) + (forward.x * offsetZ),
                     beamAnchor.y + offsetY,
                     beamAnchor.z + (right.z * offsetX) + (forward.z * offsetZ) };
    Vec3f beamEnd{ beamStart.x + (forward.x * beamRange), beamStart.y + (forward.y * beamRange),
                   beamStart.z + (forward.z * beamRange) };

    sSwordBeamState.active = true;
    sSwordBeamState.start = beamStart;
    sSwordBeamState.end = beamEnd;

    bool spawnedActor = false;
    if (!IsActorAliveInPlay(play, sSwordBeamActor)) {
        sSwordBeamActor = nullptr;
    }

    if (sSwordBeamActor == nullptr) {
        sSwordBeamActor =
            Actor_Spawn(&play->actorCtx, play, ACTOR_UNSET_1AA, beamStart.x, beamStart.y, beamStart.z, 0, 0, 0, 0);
        spawnedActor = (sSwordBeamActor != nullptr);
    }

    if (sSwordBeamActor != nullptr) {
        EnFuseBeam* beam = reinterpret_cast<EnFuseBeam*>(sSwordBeamActor);
        const float beamDistance = Math_Vec3f_DistXYZ(&beamStart, &beamEnd);

        beam->beamPos1 = beamStart;
        beam->beamScale.x = beamWidth;
        beam->beamScale.y = beamWidth;
        beam->beamScale.z = beamDistance;
        beam->beamRot.y = Math_Vec3f_Yaw(&beamStart, &beamEnd);
        beam->beamRot.x = Math_Vec3f_Pitch(&beamStart, &beamEnd);
        beam->beamRot.z = 0;

        if (spawnedActor) {
            LogSwordBeamDbg("SwordBeamActorSpawn", play, player, q0, q1, eligible);
            Fuse::Log("[Fuse] SwordBeam actor spawn frame=%d swordItem=%d start=(%.1f,%.1f,%.1f) end=(%.1f,%.1f,%.1f)\n",
                      frame, sSwordBeamState.swordItemId, beamStart.x, beamStart.y, beamStart.z, beamEnd.x, beamEnd.y,
                      beamEnd.z);
        }
    }

    const int beamDamage = std::max(0, SwordBeamBaseDamageFromHeldAction(player)) +
                           std::max(0, Fuse::GetMaterialAttackBonus(slot->materialId));
    if (beamDamage <= 0) {
        return;
    }

    Actor* actor = play->actorCtx.actorLists[ACTORCAT_ENEMY].head;
    while (actor != nullptr) {
        if (IsActorAliveInPlay(play, actor) && FuseBash_IsEnemyActor(actor) &&
            (sSwordBeamState.hitVictims.find(actor) == sSwordBeamState.hitVictims.end())) {
            Vec3f target = actor->focus.pos;
            Vec3f toVictim{ target.x - beamStart.x, target.y - beamStart.y, target.z - beamStart.z };
            const float dist = Fuse_Vec3fLength(toVictim);
            if (dist > 1.0f && dist <= beamRange) {
                Vec3f toVictimDir = Fuse_Vec3fNormalize(toVictim);
                if (Fuse_Vec3fDot(forward, toVictimDir) >= kBeamMinForwardDot) {
                    const float distanceToLine = Fuse_DistancePointToSegment(target, beamStart, beamEnd);
                    if (distanceToLine <= kBeamDamageRadiusNormal) {
                        const int prevDamage = actor->colChkInfo.damage;
                        actor->colChkInfo.damage = beamDamage;
                        Actor_ApplyDamage(actor);
                        actor->colChkInfo.damage = prevDamage;
                        sSwordBeamState.hitVictims.insert(actor);
                    }
                }
            }
        }

        actor = actor->next;
    }
}

static void BeginSwordBeamSwing(PlayState* play, Player* player, int32_t q0, int32_t q1) {
    if (!play || !player || sSwordBeamState.swingActive) {
        return;
    }

    SwordFuseSlot* slot = nullptr;
    const bool eligible = SwordBeamEligible(play, player, &slot) && slot != nullptr;
    LogSwordBeamDbg("BeginSwordBeamSwing", play, player, q0, q1, eligible);
    if (!eligible) {
        return;
    }

    const int frame = play->gameplayFrames;
    sSwordBeamState.swingActive = true;
    sSwordBeamState.active = false;
    sSwordBeamState.swingConsumedDrain = false;
    sSwordBeamState.swordItemId = player->heldItemAction;
    sSwordBeamState.hitVictims.clear();
    Fuse::Log("[Fuse] SwordBeam swing begin frame=%d swordItem=%d mat=%d dura=%d\n", frame,
              sSwordBeamState.swordItemId, static_cast<int>(slot->materialId), slot->durabilityCur);
}

static void EndSwordBeamSwing(PlayState* play, Player* player, int32_t q0, int32_t q1) {
    SwordFuseSlot* slot = nullptr;
    const bool eligible = SwordBeamEligible(play, player, &slot) && slot != nullptr;
    LogSwordBeamDbg("EndSwordBeamSwing", play, player, q0, q1, eligible);
    const int frame = play != nullptr ? play->gameplayFrames : -1;
    const bool hadSwingActive = sSwordBeamState.swingActive;
    const bool hadActor = sSwordBeamActor != nullptr;
    Actor* beamActor = IsActorAliveInPlay(play, sSwordBeamActor) ? sSwordBeamActor : nullptr;
    if (beamActor != nullptr && beamActor->id != ACTOR_UNSET_1AA) {
        beamActor = nullptr;
    }
    const int actorId = beamActor != nullptr ? beamActor->id : -1;
    const bool actorHadUpdate = beamActor != nullptr && beamActor->update != nullptr;
    const bool actorHadDraw = beamActor != nullptr && beamActor->draw != nullptr;
    const bool hadActiveState = sSwordBeamState.active;
    const bool hadConsumedDrain = sSwordBeamState.swingConsumedDrain;
    const int hitVictimCountBefore = static_cast<int>(sSwordBeamState.hitVictims.size());

    Fuse::Log("[Fuse] SwordBeam swing end frame=%d swordItem=%d active=%d\n", frame, sSwordBeamState.swordItemId,
              sSwordBeamState.active ? 1 : 0);

    bool actorKillCalled = false;
    if (beamActor != nullptr) {
        Actor_Kill(beamActor);
        actorKillCalled = true;
        sSwordBeamActor = nullptr;
    }

    sSwordBeamActor = nullptr;
    sSwordBeamState.active = false;
    sSwordBeamState.swingActive = false;
    sSwordBeamState.swingConsumedDrain = false;
    sSwordBeamState.swordItemId = ITEM_NONE;
    sSwordBeamState.hitVictims.clear();

    const bool pointerCleared = sSwordBeamActor == nullptr;
    const bool runtimeReset = !sSwordBeamState.active && !sSwordBeamState.swingActive &&
                              (sSwordBeamState.swordItemId == ITEM_NONE);
    const bool hitVictimsCleared = sSwordBeamState.hitVictims.empty();
    const bool swingConsumedDrainReset = !sSwordBeamState.swingConsumedDrain;

    osSyncPrintf("[FuseDBG] EndSwordBeamSwingCleanup frame=%d hadSwingActive=%d hadActor=%d actor=%p actorId=%d actorUpdate=%d actorDraw=%d actorKillCalled=%d pointerCleared=%d runtimeReset=%d hitVictimsCleared=%d hitVictimCountBefore=%d swingConsumedDrainBefore=%d swingConsumedDrainReset=%d activeBefore=%d eligible=%d\n",
                 frame, hadSwingActive ? 1 : 0, hadActor ? 1 : 0, static_cast<void*>(beamActor), actorId,
                 actorHadUpdate ? 1 : 0, actorHadDraw ? 1 : 0, actorKillCalled ? 1 : 0, pointerCleared ? 1 : 0,
                 runtimeReset ? 1 : 0, hitVictimsCleared ? 1 : 0, hitVictimCountBefore, hadConsumedDrain ? 1 : 0,
                 swingConsumedDrainReset ? 1 : 0, hadActiveState ? 1 : 0, eligible ? 1 : 0);
    Fuse::Log("[FuseDBG] EndSwordBeamSwingCleanup frame=%d hadSwingActive=%d hadActor=%d actor=%p actorId=%d actorUpdate=%d actorDraw=%d actorKillCalled=%d pointerCleared=%d runtimeReset=%d hitVictimsCleared=%d hitVictimCountBefore=%d swingConsumedDrainBefore=%d swingConsumedDrainReset=%d activeBefore=%d eligible=%d\n",
              frame, hadSwingActive ? 1 : 0, hadActor ? 1 : 0, static_cast<void*>(beamActor), actorId,
              actorHadUpdate ? 1 : 0, actorHadDraw ? 1 : 0, actorKillCalled ? 1 : 0, pointerCleared ? 1 : 0,
              runtimeReset ? 1 : 0, hitVictimsCleared ? 1 : 0, hitVictimCountBefore, hadConsumedDrain ? 1 : 0,
              swingConsumedDrainReset ? 1 : 0, hadActiveState ? 1 : 0, eligible ? 1 : 0);
}

static void Fuse_DrawShieldBeam(PlayState* play, Gfx** polyOpaDisp, Gfx**) {
    (void)play;
    (void)polyOpaDisp;
}

extern "C" void Fuse_DrawShieldBeam_Hook(PlayState* play, Gfx** polyOpaDisp, Gfx** polyXluDisp) {
    Fuse_DrawShieldBeam(play, polyOpaDisp, polyXluDisp);
}

void Fuse::OnGameFrameUpdate(PlayState* play) {
    if (CVarGetInteger("gFuse.DebugEnemyHpOverride.Reset", 0) != 0) {
        sHpOverrideApplied.clear();
        CVarSetInteger("gFuse.DebugEnemyHpOverride.Reset", 0);
    }

    const int frame = play != nullptr ? play->gameplayFrames : -1;
    const bool logThisFrame = play != nullptr && (frame % kFuseDbgLogIntervalFrames) == 0;
    if (logThisFrame) {
        osSyncPrintf("[FuseDBG] FuseFrameUpdate frame=%d play=%p\n", frame, static_cast<void*>(play));
    }

    TickFuseFrozenTimers(play);
    TickStatusEffects(play);
    TickShatterImpulse(play);
    ProcessPendingStuns(play);
    UpdateRangedFuseLifecycle(play);
    TickSwordBgExplosions(play);
    TickRangedProjectileSeek(play);
    if (sSwordBeamState.swingActive && play != nullptr) {
        Player* player = GET_PLAYER(play);
        const bool attacking = player != nullptr && player->meleeWeaponState > 0 &&
            (player->meleeWeaponAnimation < 0x18 || (player->stateFlags2 & PLAYER_STATE2_SPIN_ATTACKING));
        if (!attacking || !Fuse::IsEnabled()) {
            EndSwordBeamSwing(play, player, 0, 0);
        }
    }
    TickShieldGuardBeam(play);
    TickRangedProjectileBombableProximity(play);

    if (play != nullptr) {
        const bool hpOverrideEnabled = CVarGetInteger("gFuse.DebugEnemyHpOverride.Enable", 0) != 0;
        if (!hpOverrideEnabled) {
            sHpOverrideApplied.clear();
            return;
        }

        CleanupEnemyHpOverrides(play);
        for (int i = 0; i < ACTORCAT_MAX; ++i) {
            Actor* actor = play->actorCtx.actorLists[i].head;
            while (actor != nullptr) {
                TryApplyEnemyHpOverride(actor);
                actor = actor->next;
            }
        }
    }
}

void Fuse::ProcessPendingStuns(PlayState* play) {
    if (!play) {
        return;
    }

    if (!Fuse::IsEnabled()) {
        ResetDekuStunQueueInternal();
        return;
    }

    const int curFrame = play->gameplayFrames;
    if (curFrame < 0) {
        return;
    }

    auto removeEntry = [](size_t index) {
        if (index >= sPendingStunQueue.size()) {
            return;
        }

        Actor* victim = sPendingStunQueue[index].victim;
        if (victim) {
            sPendingStunIndex.erase(victim);
        }

        const size_t last = sPendingStunQueue.size() - 1;
        if (index != last) {
            sPendingStunQueue[index] = sPendingStunQueue[last];
            if (sPendingStunQueue[index].victim) {
                sPendingStunIndex[sPendingStunQueue[index].victim] = index;
            }
        }
        sPendingStunQueue.pop_back();
    };

    auto isLikelyInvincible = [&](Actor* target, int currentFrame) {
        if (!target) {
            return false;
        }

        auto hitIt = sDekuLastSwordHitFrame.find(target);
        if (hitIt != sDekuLastSwordHitFrame.end()) {
            const int framesSinceHit = currentFrame - hitIt->second;
            if (framesSinceHit >= 0 && framesSinceHit <= kDekuStunSwordIFrameFrames) {
                return true;
            }
        }

        return false;
    };

    for (size_t i = 0; i < sPendingStunQueue.size();) {
        PendingStunRequest& request = sPendingStunQueue[i];
        Actor* victim = request.victim;

        if (!victim || !IsActorAliveInPlay(play, victim)) {
            if (victim) {
                sDekuStunCooldownUntil.erase(victim);
                sDekuLastSwordHitFrame.erase(victim);
            }
            removeEntry(i);
            continue;
        }

        if (curFrame < request.applyNotBeforeFrame) {
            ++i;
            continue;
        }

        if (isLikelyInvincible(victim, curFrame) && request.attemptsRemaining > 0) {
            request.applyNotBeforeFrame = curFrame + request.retryStepFrames;
            --request.attemptsRemaining;
            FUSE_LOG_DBG("[FuseDBG] dekunut_wait victim=%p id=0x%04X reason=invincible next=%d\n", (void*)victim,
                         victim->id, request.applyNotBeforeFrame);
            ++i;
            continue;
        }

        auto cooldownIt = sDekuStunCooldownUntil.find(victim);
        if (cooldownIt != sDekuStunCooldownUntil.end() && curFrame < cooldownIt->second) {
            FUSE_LOG_DBG("[FuseDBG] dekunut_skip_cooldown victim=%p id=0x%04X until=%d\n", (void*)victim, victim->id,
                         cooldownIt->second);
            removeEntry(i);
            continue;
        }

        const char* srcLabel = GetStunSourceLabel(request.itemId);
        FUSE_LOG_DBG("[FuseDBG] dekunut_apply victim=%p id=0x%04X frame=%d src=%s\n", (void*)victim, victim->id,
                     curFrame, srcLabel);
        ApplyDekuNutStunVanilla(play, GET_PLAYER(play), victim, request.level, request.itemId);
        sDekuStunCooldownUntil[victim] = curFrame + kDekuStunCooldownFrames;
        removeEntry(i);
    }
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
        if (!request.victim || !IsActorAliveInPlay(play, request.victim)) {
            continue;
        }
        if (IsActorFrozenInternal(request.victim)) {
            continue;
        }
        if (WasFreezeRecentlyShattered(play, request.victim)) {
            FUSE_LOG_DBG("[FuseDBG] FreezeSkip: reason=RecentlyShattered frame=%d victim=%p\n", curFrame,
                         (void*)request.victim);
            continue;
        }
        if (IsFreezeReapplyBlocked(play, request.victim)) {
            FUSE_LOG_DBG("[FuseDBG] FreezeSkip: reason=NoReapplyWindow frame=%d victim=%p\n", curFrame,
                         (void*)request.victim);
            continue;
        }
        ApplyIceArrowFreeze(play, request.victim, request.level);
    }

    sSwordFreezeQueues[applyIndex].clear();
    sSwordFreezeVictims[applyIndex].clear();
    sSwordFreezeQueueFrames[applyIndex] = -1;
}

void Fuse::ResetSwordFreezeQueue() {
    ResetSwordFreezeQueueInternal();
}

static void ApplyMeleeHitMaterialEffects(PlayState* play, Actor* victim, Actor* attacker, MaterialId materialId,
                                         int itemId, int baseWeaponDamage, const char* srcLabel, bool allowStun) {
    if (!victim) {
        return;
    }

    if (IsActorFrozenInternal(victim)) {
        Fuse::TryFreezeShatterWithDamage(play, victim, attacker, itemId, materialId, baseWeaponDamage, srcLabel);
        return;
    }

    const MaterialDef* def = Fuse::GetMaterialDef(materialId);
    if (!def) {
        return;
    }

    uint8_t stunLevel = 0;
    if (allowStun && HasModifier(def->modifiers, def->modifierCount, ModifierId::Stun, &stunLevel) && stunLevel > 0) {
        Fuse_EnqueuePendingStun(victim, stunLevel, materialId, itemId);
    }

    uint8_t burnLevel = 0;
    if (HasModifier(def->modifiers, def->modifierCount, ModifierId::Burn, &burnLevel) && burnLevel > 0) {
        const char* slotLabel = (itemId == ITEM_HAMMER) ? "Hammer" : "Sword";
        Fuse::ApplyBurn(play, victim, burnLevel, materialId, srcLabel, slotLabel);
    }

    uint8_t freezeLevel = 0;
    const bool shatteredThisHit = (play && sFreezeShatterFrame.find(victim) != sFreezeShatterFrame.end() &&
                                   sFreezeShatterFrame[victim] == play->gameplayFrames);
    if (!shatteredThisHit && HasModifier(def->modifiers, def->modifierCount, ModifierId::Freeze, &freezeLevel) &&
        freezeLevel > 0) {
        const char* slotLabel = (itemId == ITEM_HAMMER) ? "Hammer" : "Sword";
        Fuse::QueueSwordFreeze(play, victim, freezeLevel, srcLabel, slotLabel, materialId);
    }
}

static void LogDurabilityGate(const char* itemLabel, MaterialId materialId, int durabilityCur, int durabilityMax) {
    if (materialId == MaterialId::None || durabilityCur > 0) {
        return;
    }
    FUSE_LOG_DBG("[FuseDBG] ModGate: item=%s mat=%d dura=%d/%d SKIP_REASON=durability_gate\n", itemLabel,
                 static_cast<int>(materialId), durabilityCur, durabilityMax);
}

void Fuse::OnSwordMeleeHit(PlayState* play, Actor* victim, int baseWeaponDamage, const Vec3f* impactPos) {

    if (!Fuse::IsSwordFused()) {
        LogDurabilityGate("sword", Fuse::GetSwordMaterial(), Fuse::GetSwordFuseDurability(),
                          Fuse::GetSwordFuseMaxDurability());
        return;
    }

    if (play && victim) {
        sDekuLastSwordHitFrame[victim] = play->gameplayFrames;
    }

    const MaterialId materialId = Fuse::GetSwordMaterial();
    const MaterialDef* def = Fuse::GetMaterialDef(materialId);
    Player* player = play ? GET_PLAYER(play) : nullptr;
    const int itemId = gSaveContext.equips.buttonItems[0];
    const uint8_t explosionLevel =
        Fuse::GetMaterialModifierLevel(materialId, FuseItemType::Sword, ModifierId::Explosion);
    bool didExplode = false;
    if (explosionLevel > 0 && play && victim) {
        if (Fuse_IsExplosionImmuneVictim(victim)) {
            FUSE_LOG_DBG("[FuseDBG] ExplodeSkip: src=Sword victim=ACTOR_BOSS_DODONGO\n");
        } else if (FuseBash_IsEnemyActor(victim) || Fuse_IsBombableActorId(victim->id)) {
            const int bombable = Fuse_IsBombableActorId(victim->id) ? 1 : 0;
            if (bombable) {
                Vec3f adjustedPos = Fuse_GetBombableAnchorPos(victim, 25.0f);
                Fuse_AdjustExplosionPosForBombable(victim, player ? &player->actor : nullptr, &adjustedPos);
                FUSE_LOG_DBG("[FuseDBG] Explode: src=Sword kind=actor pos=(%.2f %.2f %.2f) victim=0x%04X bombable=%d\n",
                             adjustedPos.x, adjustedPos.y, adjustedPos.z, victim->id, bombable);
                Fuse_TriggerExplosion(play, adjustedPos, FuseExplosionSelfMode::DamagePlayer,
                                      Fuse_GetExplosionParams(materialId, explosionLevel), "Sword");
                didExplode = true;
            } else {
                const Vec3f* explodePos = impactPos ? impactPos : &victim->focus.pos;
                Vec3f adjustedPos = explodePos ? *explodePos : victim->world.pos;
                FUSE_LOG_DBG("[FuseDBG] Explode: src=Sword kind=actor pos=(%.2f %.2f %.2f) victim=0x%04X bombable=%d\n",
                             adjustedPos.x, adjustedPos.y, adjustedPos.z, victim->id, bombable);
                Fuse_TriggerExplosion(play, adjustedPos, FuseExplosionSelfMode::DamagePlayer,
                                      Fuse_GetExplosionParams(materialId, explosionLevel), "Sword");
                didExplode = true;
            }
        }
    }
    if (didExplode && play) {
        gLastSwordActorExplodeFrame = play->gameplayFrames;
    }

    if (Fuse::TryFreezeShatterWithDamage(play, victim, player ? &player->actor : nullptr, itemId, materialId,
                                         baseWeaponDamage, "sword")) {
        return;
    }

    if (def) {
        uint8_t knockbackLevel = 0;
        if (HasModifier(def->modifiers, def->modifierCount, ModifierId::Knockback, &knockbackLevel) &&
            knockbackLevel > 0) {
            Player* player = GET_PLAYER(play);
            ApplyFuseKnockback(play, player, victim, knockbackLevel, "Sword", materialId,
                               Fuse::GetSwordFuseDurability(), Fuse::GetSwordFuseMaxDurability(), "hit");
        }
    }

    ApplyMeleeHitMaterialEffects(play, victim, player ? &player->actor : nullptr, materialId, itemId, baseWeaponDamage,
                                 "sword", true);
}

void Fuse::OnHammerMeleeHit(PlayState* play, Actor* victim, int baseWeaponDamage, const Vec3f* impactPos) {
    if (!Fuse::IsHammerFused()) {
        LogDurabilityGate("hammer", Fuse::GetHammerMaterial(), Fuse::GetHammerFuseDurability(),
                          Fuse::GetHammerFuseMaxDurability());
        return;
    }

    Player* player = play ? GET_PLAYER(play) : nullptr;
    const MaterialId materialId = Fuse::GetHammerMaterial();
    const MaterialDef* def = Fuse::GetMaterialDef(materialId);
    const uint8_t explosionLevel =
        Fuse::GetMaterialModifierLevel(materialId, FuseItemType::Hammer, ModifierId::Explosion);
    auto isZeroishPos = [](const Vec3f& pos) {
        return std::fabs(pos.x) < 0.01f && std::fabs(pos.y) < 0.01f && std::fabs(pos.z) < 0.01f;
    };
    FUSE_LOG_DBG("[FuseDBG] HammerHit: kind=actor victim=0x%04X cat=%d mat=%d(%s) explosion=%u\n",
                 victim ? victim->id : 0, victim ? victim->category : -1, static_cast<int>(materialId),
                 def ? def->name : "unknown", static_cast<unsigned int>(explosionLevel));
    bool didExplode = false;
    if (victim) {
        Fuse::SetHammerHitActorThisSwing(true);
    }
    if (explosionLevel > 0 && play && victim) {
        if (Fuse_IsExplosionImmuneVictim(victim)) {
            FUSE_LOG_DBG("[FuseDBG] ExplodeSkip: src=Hammer victim=ACTOR_BOSS_DODONGO\n");
        } else if (FuseBash_IsEnemyActor(victim) || Fuse_IsBombableActorId(victim->id)) {
            const int bombable = Fuse_IsBombableActorId(victim->id) ? 1 : 0;
            if (bombable) {
                Vec3f adjustedPos = Fuse_GetBombableAnchorPos(victim, 25.0f);
                Fuse_AdjustExplosionPosForBombable(victim, player ? &player->actor : nullptr, &adjustedPos);
                if (isZeroishPos(adjustedPos)) {
                    FUSE_LOG_DBG("[FuseDBG] HammerExplodePosFallback: using victim->world.pos (chosen was zero-ish)\n");
                    adjustedPos = victim->world.pos;
                }
                FUSE_LOG_DBG(
                    "[FuseDBG] Explode: src=Hammer kind=actor pos=(%.2f %.2f %.2f) victim=0x%04X bombable=%d\n",
                    adjustedPos.x, adjustedPos.y, adjustedPos.z, victim->id, bombable);
                Fuse_TriggerExplosion(play, adjustedPos, FuseExplosionSelfMode::DamagePlayer,
                                      Fuse_GetExplosionParams(materialId, explosionLevel), "Hammer");
                didExplode = true;
            } else {
                const Vec3f* explodePos = impactPos ? impactPos : &victim->focus.pos;
                Vec3f adjustedPos = explodePos ? *explodePos : victim->world.pos;
                if (isZeroishPos(adjustedPos)) {
                    FUSE_LOG_DBG("[FuseDBG] HammerExplodePosFallback: using victim->world.pos (chosen was zero-ish)\n");
                    adjustedPos = victim->world.pos;
                }
                FUSE_LOG_DBG(
                    "[FuseDBG] Explode: src=Hammer kind=actor pos=(%.2f %.2f %.2f) victim=0x%04X bombable=%d\n",
                    adjustedPos.x, adjustedPos.y, adjustedPos.z, victim->id, bombable);
                Fuse_TriggerExplosion(play, adjustedPos, FuseExplosionSelfMode::DamagePlayer,
                                      Fuse_GetExplosionParams(materialId, explosionLevel), "Hammer");
                didExplode = true;
            }
        }
    }
    if (didExplode) {
        FUSE_LOG_DBG("[FuseDBG] ExplodeCall: src=Hammer kind=actor\n");
    }
    if (Fuse::TryFreezeShatterWithDamage(play, victim, player ? &player->actor : nullptr, ITEM_HAMMER, materialId,
                                         baseWeaponDamage, "hammer")) {
        return;
    }

    ApplyMeleeHitMaterialEffects(play, victim, player ? &player->actor : nullptr, materialId, ITEM_HAMMER,
                                 baseWeaponDamage, "hammer", false);
}

static bool IsActorAliveInPlay(PlayState* play, Actor* target) {
    if (!play || !target) {
        return false;
    }

    for (int i = 0; i < ACTORCAT_MAX; ++i) {
        Actor* actor = play->actorCtx.actorLists[i].head;
        while (actor != nullptr) {
            if (actor == target) {
                return actor->update != nullptr;
            }
            actor = actor->next;
        }
    }
    return false;
}
