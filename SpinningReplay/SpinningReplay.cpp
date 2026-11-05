#include "plugin.h"
#include "common.h"

#include "CPed.h"
#include "CBike.h"
#include "CAutomobile.h"
#include "CModelInfo.h"
#include "CVehicleModelInfo.h"
#include "CVehicle.h"
#include "CReplay.h"
#include "CHud.h"
#include "CCamera.h"
#include "CPools.h"
#include "CWorld.h"
#include "CColModel.h"
#include "CCollisionData.h"
#include "CColLine.h"
#include "CColDisk.h"
#include "CColPoint.h"
#include "CTimer.h"
#include "RenderWare.h"
#include "Fx_c.h"

#include <vector>
#include <cstring>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

using namespace plugin;

// ---------------------------------------------------------------------------
// Addresses (GTA SA 1.0 US)
// ---------------------------------------------------------------------------
// All trampolines return through these via memory-indirect jumps
// (jmp [var]) so no general purpose register is clobbered on the way back.

DWORD kFtol                    = 0x821B40; // _ftol
DWORD kCMatrixAssign           = 0x59BBC0; // CMatrix::operator=(CMatrix const&)

// CReplay::StoreCarUpdate -> wheel packet section
DWORD kPacketCarPath           = 0x45B952;
DWORD kPacketAfterWheels       = 0x45BA65;

// CReplay::ProcessCarUpdate -> wheel extraction section
DWORD kExtractCarPath          = 0x45BC85;
DWORD kExtractAfterWheels      = 0x45BEC9; // non-automobile target of the original jne (was 0x45BEF5: skipped flag code)

// CReplay::PlayBackThisFrameInterpolation
DWORD kInterpOrigCall          = 0x721D50;
DWORD kInterpReturn            = 0x45F3A1;

// CBike::PreRender
DWORD kBikePreRenderAfterCheck  = 0x6BD1E2;
DWORD kBikePreRenderBeforeCheck = 0x6BD0CE;

// CPhysical::ProcessCollisionSectorList: contacts on wheel spheres (piece 13..16)
DWORD kWheelPieceSoftCollision = 0x54CCB0;   // soft collision + ApplyFriction
DWORD kColPointLoopNext        = 0x54CCF2;   // next colpoint

// CPhysical::ApplyFriction spark loop: mov edi, 8 at 0x5458B4, then 8x AddSparks(amount 1)
DWORD kSparkLoopStart  = 0x5458B9;
DWORD g_sparkLoopCount = 8;          // set every frame in process()

// ped position update (0x5E2C71 / 0x5E2C79)
DWORD kPedZReturn              = 0x5E2C77;
DWORD kPedOffsetReturn         = 0x5E2C7F;

// CReplay::Mode lives at 0xA43088 (1 == MODE_PLAYBACK)
// Float constants used by the original car wheel code:
//   0x858B40 = suspension pack scale      0x858B38 = suspension unpack scale
//   0x859C34 = rotation pack scale        0x859C44 = rotation unpack scale
//   0x858624 = 1.0f


// ---------------------------------------------------------------------------
// Bike wheel spin: record
// Entered at 0x45B94A (replaces: test eax,eax / jnz 0x45BA65), eax = vehicle type.
// ---------------------------------------------------------------------------
void __declspec(naked) ASM_checkBikePacket() {
    _asm {
        test eax, eax
        jnz notCar
        jmp [kPacketCarPath]
    notCar:
        cmp eax, 9              // VEHICLE_BIKE
        je bikePacket
        cmp eax, 10             // VEHICLE_BMX (inherits CBike layout)
        je bikePacket
        jmp [kPacketAfterWheels]
    bikePacket:
        fld dword ptr[edi + 0x750]
        fmul dword ptr ds:[0x858B40]
        call [kFtol]
        mov [esi + 0x24], al
        fld dword ptr[edi + 0x760]
        fmul dword ptr ds:[0x859C34]
        call [kFtol]
        mov [esi + 0x28], al
        fld dword ptr[edi + 0x754]
        fmul dword ptr ds:[0x858B40]
        call [kFtol]
        mov [esi + 0x25], al
        fld dword ptr[edi + 0x764]
        fmul dword ptr ds:[0x859C34]
        call [kFtol]
        mov [esi + 0x29], al
        jmp [kPacketAfterWheels]
    }
}

// ---------------------------------------------------------------------------
// Bike wheel spin: playback
// Entered at 0x45BC7D (replaces: cmp eax,ebp / jnz 0x45BEF5).
// ---------------------------------------------------------------------------
void __declspec(naked) ASM_checkBikeExtraction() {
    _asm {
        cmp eax, ebp
        jne notCar
        jmp [kExtractCarPath]
    notCar:
        cmp eax, 9
        je bikeExtraction
        cmp eax, 10
        je bikeExtraction
        jmp [kExtractAfterWheels]
    bikeExtraction:
        movsx edx, byte ptr[edi + 0x24]
        mov [esp + 0x0C], edx
        fild dword ptr[esp + 0x0C]
        fmul dword ptr ds:[0x858B38]
        fstp dword ptr[esi + 0x750]
        movsx edx, byte ptr[edi + 0x28]
        mov [esp + 0x0C], edx
        fild dword ptr[esp + 0x0C]
        fmul dword ptr ds:[0x859C44]
        fstp dword ptr[esi + 0x760]
        movsx edx, byte ptr[edi + 0x25]
        mov [esp + 0x0C], edx
        fild dword ptr[esp + 0x0C]
        fmul dword ptr ds:[0x858B38]
        fstp dword ptr[esi + 0x754]
        movsx edx, byte ptr[edi + 0x29]
        mov [esp + 0x0C], edx
        fild dword ptr[esp + 0x0C]
        fmul dword ptr ds:[0x859C44]
        fstp dword ptr[esi + 0x764]
        jmp [kExtractAfterWheels]
    }
}

// ---------------------------------------------------------------------------
// Copy TheCamera.m_mCameraMatrix -> m_mCameraMatrixOld after interpolation.
// Entered at 0x45F39C (replaces: call 0x721D50).
// TheCamera is a global object at 0xB6F028:
//   +0x974 = m_mCameraMatrix    (0xB6F99C)
//   +0x9BC = m_mCameraMatrixOld (0xB6F9E4)
// ---------------------------------------------------------------------------
void __declspec(naked) ASM_copyMatrixToOldPlaybackThisFrameInterpolation() {
    _asm {
        call [kInterpOrigCall]
        push eax                // keep the original call's return value
        push 0xB6F99C           // src
        mov ecx, 0xB6F9E4       // this
        call [kCMatrixAssign]   // thiscall, callee pops src
        pop eax
        jmp [kInterpReturn]
    }
}

// ---------------------------------------------------------------------------
// CBike::PreRender: skip suspension height interaction in replay playback.
// Entered at 0x6BD0C7 (replaces: test byte ptr [esi+0x42B],1).
// ---------------------------------------------------------------------------
void __declspec(naked) ASM_addCheckBikePreRenderReplayMode() {
    _asm {
        cmp byte ptr ds:[0xA43088], 1
        jne normal
        mov [esp + 0x7C], ebx
        jmp [kBikePreRenderAfterCheck]
    normal:
        test byte ptr[esi + 0x42B], 1
        jmp [kBikePreRenderBeforeCheck]
    }
}

// ---------------------------------------------------------------------------
// Spark density: replaces 'mov edi, 8' (spark loop count) in ApplyFriction.
// ---------------------------------------------------------------------------
void __declspec(naked) ASM_sparkLoopCount() {
    _asm {
        mov edi, g_sparkLoopCount
        jmp [kSparkLoopStart]
    }
}

// ---------------------------------------------------------------------------
// Wheel-sphere contacts: in playback, skip them entirely (no soft collision,
// no ApplyFriction -> no sparks from tyres touching the ground).
// Entered from the jbe at 0x54CAA9 (piece type 13..16).
// ---------------------------------------------------------------------------
void __declspec(naked) ASM_wheelPieceContact() {
    _asm {
        cmp byte ptr ds:[0xA43088], 1
        jne normal
        jmp [kColPointLoopNext]
    normal:
        jmp [kWheelPieceSoftCollision]
    }
}

// ---------------------------------------------------------------------------
// Ped position: don't write z in playback.
// Entered at 0x5E2C71 (replaces: mov eax,[esi+14h] / fstp dword ptr [eax+38h]).
// In playback the value is popped with fstp st(0) instead of being left on
// the x87 stack, so the FPU stack depth matches the original code exactly.
// ---------------------------------------------------------------------------
void __declspec(naked) ASM_removeEntityCollisionCPedSetPosition() {
    _asm {
        mov eax, [esi + 0x14]
        cmp byte ptr ds:[0xA43088], 1
        jne normal
        fstp st(0)
        jmp [kPedZReturn]
    normal:
        fstp dword ptr[eax + 0x38]
        jmp [kPedZReturn]
    }
}

// ---------------------------------------------------------------------------
// Ped position: drop the +1.0 offset in playback.
// Entered at 0x5E2C79 (replaces: fadd dword ptr [0x858624]). eax untouched.
// ---------------------------------------------------------------------------
void __declspec(naked) ASM_removeCollisionOffsetReplayMode() {
    _asm {
        cmp byte ptr ds:[0xA43088], 1
        je skip
        fadd dword ptr ds:[0x858624]
    skip:
        jmp [kPedOffsetReturn]
    }
}

// ---------------------------------------------------------------------------
// Replay collision pass
//
// In playback CWorld::Process does not run physics, so the only thing that
// can move entities is the ProcessCollision() we call ourselves. We snapshot
// every vehicle and ped, run collision (which generates sparks through
// CPhysical::ApplyFriction), then put the replay state back. The replay stays
// the sole authority on transforms, so nothing can drift or jitter.
// ---------------------------------------------------------------------------

// Bike/BMX wheel spin + suspension in replays (packet patches).
constexpr bool kEnableWheelSpin = false;

// Ground-following wheels in replays: cast each suspension line against the
// world and write the compression ourselves (no body collision involved).
constexpr bool kGroundFollowWheels = true;
// Old CBike::PreRender patch that skipped suspension height in playback.
// Must be off for kGroundFollowWheels to move bike wheels.
constexpr bool kPatchBikePreRender = !kGroundFollowWheels;

// Distance-based wheel spin in replays (no packet patches needed).
constexpr bool           kDistanceWheelSpin = true;
constexpr unsigned short kDistanceSpinModels[] = {
    522, // NRG-500
    462, // Faggio
    521, // FCR-900
    461, // PCJ-600
    581, // BF-400
    448, // Pizzaboy
    463, // Freeway
    586, // Wayfarer
    523, // HPV1000 (police bike)
    468, // Sanchez
};
constexpr float          kWheelSpinSign     = -1.0f; // game convention: forward = negative
constexpr float          kMaxHubStep        = 5.0f;  // metres/frame; above = seek/teleport
// TEST: burst the front tyre of kDistanceSpinModels bikes during playback
// (checks that index 0 is the front wheel). 0 = ok, 1 = burst, 2 = missing.
constexpr bool           kTestFlatFrontTyre = false;

// Rim sparks from burst tyres (what ProcessBikeWheel does in normal play).
constexpr bool  kBurstTyreSparks     = false;
constexpr float kRimSparkMinSpeed    = 3.0f;   // m/s at the hub
constexpr float kRimSparkFullSpeed   = 20.0f;  // m/s for max amount
constexpr int   kRimSparkMaxAmount   = 8;
constexpr float kRimSparkForce       = 1.0f;
constexpr float kRimSparkSpread      = 0.1f;
constexpr float kRimSparkLife        = 1.0f;
constexpr float kRimContactMargin    = 0.15f;  // ground search below the tyre, metres

// Smooth replays: show every recorded frame over kReplaySubFrames rendered
// frames, blending toward the next recorded frame in between (GTA III's
// slow-motion trick). Replays record at most every 26 ms, i.e. roughly every
// 2nd frame at 50-60 fps, so 2 gives normal speed at a 50-60 fps frame rate.
// At 25 fps (frame limiter on) playback would run at half speed.
constexpr bool kSmoothReplay    = true;    // install the hook at all
constexpr int  kReplaySubFrames = 2;
// Runtime toggle (F7). Starts in the state below.
constexpr unsigned kToggleSmoothKey     = VK_F7;
constexpr bool     kSmoothReplayDefault = false;

// Skip collision (and so sparks) for vehicles that aren't moving in the replay.
constexpr bool  kSkipStationary      = true;
constexpr float kStationarySpeed     = 0.1f;    // m/s, below = standing still
constexpr float kStationaryTurn      = 0.001f;  // change of forward vector per frame

// Minimum friction value for ApplyFriction to spawn sparks. The game uses 0.1;
// the old patch used 3.05e-5 (the float at 0x858B14), which makes almost any
// body/ground contact spark - including cars resting on their wheels whose
// body just grazes the ground in the replay pose.
// Normal gameplay always uses the game's 0.1; only playback uses the value below.
constexpr float kGameSparkMinFriction   = 0.1f;
constexpr float kReplaySparkMinFriction = 3.0517578125e-05f; // old patch value (float at 0x858B14); lower = more sparks
static float g_sparkMinFriction = kGameSparkMinFriction;

// Runtime toggle for replay sparks (press during gameplay or playback).
constexpr unsigned kToggleSparksKey = VK_F8;
static bool g_replaySparksEnabled = true;

// No sparks/friction from wheel collision spheres touching the ground in replays.
constexpr bool kNoWheelSphereContacts = true;

// Re-sync the RenderWare camera after CGame::Process when an external tool
// (Freeplay freeze / X attach cam) has replaced the CCamera::Process call at
// 0x53C104. In playback the RW camera is otherwise only updated inside
// CReplay::Update (ProcessReplayCamera, 0x45D060), i.e. BEFORE that call, so a
// matrix written there would reach the renderer one frame late.
constexpr bool kSyncReplacedCamera = true;

// Spark density in replays: the game spawns 8 sparks per scraping contact.
// In playback that is multiplied by kReplaySparkMultiplier and scaled with the
// frame time (x1 at 50 fps, x2 at 25 fps) so density per second is constant.
// 3 matches the old 3-passes-per-frame look at 50 fps.
constexpr float kReplaySparkMultiplier = 3.0f;
constexpr int   kGameSparkLoopCount    = 8;
constexpr int   kMaxSparkLoopCount     = 64;

// Spark density; more passes = more spark spawns per frame (frame-rate dependent).
constexpr int  kCollisionPassesPerFrame = 1;
// false = only the player's vehicle generates sparks (old behaviour).
constexpr bool kAllVehiclesSpark = true;

struct PhysicalSnapshot {
    CPhysical* ent;
    CVector right, up, at, pos;
    CVector moveSpeed, turnSpeed, frictionMoveSpeed, frictionTurnSpeed;
    // Raw copy of the vehicle's wheel/suspension state (col points, compression,
    // rotation, wheel positions, speeds, offsets) plus contact-wheel counters.
    // Taken before collision and put back afterwards, so collision never moves
    // the wheels; rotation and wheel speed are excluded (see keepPtr).
    unsigned char* wheelPtr;
    size_t         wheelSize;
    unsigned char  wheel[0x200];
    unsigned char* contactPtr;
    unsigned char  contact[2];
    // Sub-ranges of the wheel block NOT restored (rotation / wheel speed).
    unsigned char* keepPtr[2];
    size_t         keepSize[2];

    // Damage protection (set for the duration of the collision passes)
    CVehicle* vehicle;
    bool      collisionProof;
    bool      canBeDamaged;
    bool      tyresDontBurst;
    float     vehicleHealth;
    float     damageIntensity;
};

static std::vector<PhysicalSnapshot> g_snapshots;

static bool isAutomobileType(CVehicle* v) {
    switch (v->m_nVehicleSubClass) {
    case VEHICLE_AUTOMOBILE: case VEHICLE_MTRUCK: case VEHICLE_QUAD:
    case VEHICLE_HELI: case VEHICLE_PLANE: case VEHICLE_TRAILER:
        return true;
    default:
        return false;
    }
}

static bool isBikeType(CVehicle* v) {
    return v->m_nVehicleSubClass == VEHICLE_BIKE || v->m_nVehicleSubClass == VEHICLE_BMX;
}

// An entity is linked into world sectors iff its entry-info list is non-empty.
// During playback the pools can also hold entities that are NOT in the world
// (e.g. the real player vehicle parked at the end-of-recording position);
// those must never be collided or RemoveAndAdd()'d, or they get added to the
// world and show up as a duplicate.
static bool isInWorld(CPhysical* e) {
    return e->m_pCollisionList != nullptr;
}

static void wheelBlockFor(CVehicle* v, unsigned char*& ptr, size_t& size, unsigned char*& contact,
                          unsigned char* keepPtr[2], size_t keepSize[2]) {
    ptr = nullptr; size = 0; contact = nullptr;
    keepPtr[0] = keepPtr[1] = nullptr; keepSize[0] = keepSize[1] = 0;
    if (isAutomobileType(v)) {
        CAutomobile* a = static_cast<CAutomobile*>(v);
        ptr  = reinterpret_cast<unsigned char*>(&a->m_wheelColPoint[0]);
        size = reinterpret_cast<unsigned char*>(&a->field_88C[0]) - ptr;   // up to and incl. wheelOffsetZ
        contact = &a->m_nNumContactWheels;
        keepPtr[0] = reinterpret_cast<unsigned char*>(&a->m_fWheelRotation[0]); keepSize[0] = sizeof(a->m_fWheelRotation);
        keepPtr[1] = reinterpret_cast<unsigned char*>(&a->m_fWheelSpeed[0]);    keepSize[1] = sizeof(a->m_fWheelSpeed);
    }
    else if (isBikeType(v)) {
        CBike* b = static_cast<CBike*>(v);
        ptr  = reinterpret_cast<unsigned char*>(&b->m_anWheelColPoint[0]);
        size = reinterpret_cast<unsigned char*>(&b->m_fHeightAboveRoad) - ptr;
        contact = &b->m_nNumContactWheels;
        keepPtr[0] = reinterpret_cast<unsigned char*>(&b->m_afWheelRotationX[0]); keepSize[0] = sizeof(b->m_afWheelRotationX);
        keepPtr[1] = reinterpret_cast<unsigned char*>(&b->m_fWheelSpeed[0]);      keepSize[1] = sizeof(b->m_fWheelSpeed);
    }
}

static void takeSnapshot(CPhysical* e, CVehicle* asVehicle = nullptr) {
    if (!e->m_matrix || !isInWorld(e))
        return;
    PhysicalSnapshot s;
    s.ent = e;
    s.right = e->m_matrix->right;
    s.up    = e->m_matrix->up;
    s.at    = e->m_matrix->at;
    s.pos   = e->m_matrix->pos;
    s.moveSpeed         = e->m_vecMoveSpeed;
    s.turnSpeed         = e->m_vecTurnSpeed;
    s.frictionMoveSpeed = e->m_vecFrictionMoveSpeed;
    s.frictionTurnSpeed = e->m_vecFrictionTurnSpeed;
    s.wheelPtr = nullptr; s.wheelSize = 0; s.contactPtr = nullptr;
    s.keepPtr[0] = s.keepPtr[1] = nullptr; s.keepSize[0] = s.keepSize[1] = 0;
    s.vehicle         = asVehicle;
    s.collisionProof  = e->m_nPhysicalFlags.bCollisionProof;
    s.damageIntensity = e->m_fDamageIntensity;
    if (asVehicle) {
        s.canBeDamaged   = asVehicle->m_nVehicleFlags.bCanBeDamaged;
        s.tyresDontBurst = asVehicle->m_nVehicleFlags.bTyresDontBurst;
        s.vehicleHealth  = asVehicle->m_fHealth;
    }
    if (asVehicle) {
        wheelBlockFor(asVehicle, s.wheelPtr, s.wheelSize, s.contactPtr, s.keepPtr, s.keepSize);
        if (s.wheelSize > sizeof(s.wheel))
            s.wheelPtr = nullptr, s.wheelSize = 0;   // layout surprise: don't touch it
        if (s.wheelPtr)
            memcpy(s.wheel, s.wheelPtr, s.wheelSize);
        if (s.contactPtr)
            memcpy(s.contact, s.contactPtr, 2);
    }
    g_snapshots.push_back(s);
}

static bool sameVec(const CVector& a, const CVector& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

// Put pose and speeds back to the replay state.
static void restorePose(const PhysicalSnapshot& s) {
    CPhysical* e = s.ent;
    CMatrixLink* m = e->m_matrix;
    if (!m)
        return;

    const bool moved   = !sameVec(m->pos, s.pos);
    const bool rotated = !sameVec(m->right, s.right) || !sameVec(m->up, s.up) || !sameVec(m->at, s.at);

    m->right = s.right;
    m->up    = s.up;
    m->at    = s.at;
    m->pos   = s.pos;
    e->m_vecMoveSpeed         = s.moveSpeed;
    e->m_vecTurnSpeed         = s.turnSpeed;
    e->m_vecFrictionMoveSpeed = s.frictionMoveSpeed;
    e->m_vecFrictionTurnSpeed = s.frictionTurnSpeed;

    if (moved && isInWorld(e))
        e->RemoveAndAdd();   // collision may have moved it to another sector
    if (moved || rotated) {
        e->UpdateRwMatrix();
        e->UpdateRwFrame();
    }
}

// No collision damage while we run collision for sparks.
static void protectFromDamage(const PhysicalSnapshot& s) {
    s.ent->m_nPhysicalFlags.bCollisionProof = 1;
    if (s.vehicle) {
        s.vehicle->m_nVehicleFlags.bCanBeDamaged   = 0;
        s.vehicle->m_nVehicleFlags.bTyresDontBurst = 1;
    }
}

static void restoreDamageState(const PhysicalSnapshot& s) {
    s.ent->m_nPhysicalFlags.bCollisionProof = s.collisionProof;
    s.ent->m_fDamageIntensity = s.damageIntensity;   // don't leave a pending hit for VehicleDamage
    if (s.vehicle) {
        s.vehicle->m_nVehicleFlags.bCanBeDamaged   = s.canBeDamaged;
        s.vehicle->m_nVehicleFlags.bTyresDontBurst = s.tyresDontBurst;
        s.vehicle->m_fHealth = s.vehicleHealth;
    }
}

static void restoreWheels(const PhysicalSnapshot& s) {
    if (s.wheelPtr) {
        unsigned char keep[2][32];
        for (int k = 0; k < 2; ++k)
            if (s.keepPtr[k] && s.keepSize[k] <= sizeof(keep[k]))
                memcpy(keep[k], s.keepPtr[k], s.keepSize[k]);
        memcpy(s.wheelPtr, s.wheel, s.wheelSize);
        for (int k = 0; k < 2; ++k)
            if (s.keepPtr[k] && s.keepSize[k] <= sizeof(keep[k]))
                memcpy(s.keepPtr[k], keep[k], s.keepSize[k]);
    }
    if (s.contactPtr)
        memcpy(s.contactPtr, s.contact, 2);
}

// ---------------------------------------------------------------------------
// Movement tracking: replay vehicles are moved by the replay, not by physics,
// so "moving" is measured from the pose change between frames.
// ---------------------------------------------------------------------------
struct MotionState {
    unsigned       frame;
    unsigned short model;
    CVector        pos;
    CVector        fwd;
};

static std::unordered_map<CVehicle*, MotionState> g_motion;
static std::unordered_set<CVehicle*>              g_moving;
static unsigned g_motionFrame = 0;

static void updateMotion() {
    ++g_motionFrame;
    g_moving.clear();
    const float ts = CTimer::ms_fTimeStep;

    auto* vehPool = CPools::ms_pVehiclePool;
    for (int i = 0; i < vehPool->m_nSize; ++i) {
        CVehicle* v = vehPool->GetAt(i);
        if (!v || !v->m_matrix || !isInWorld(v))
            continue;
        const CVector pos = v->m_matrix->pos;
        const CVector fwd = v->m_matrix->up;

        auto it = g_motion.find(v);
        const bool known = it != g_motion.end()
                        && it->second.model == v->m_nModelIndex
                        && it->second.frame + 1 == g_motionFrame;
        bool moving = true;   // unknown history: treat as moving
        if (known) {
            if (ts <= 0.0f) {
                moving = false;   // paused
            } else {
                const CVector& p0 = it->second.pos;
                const CVector& f0 = it->second.fwd;
                const float dx = pos.x - p0.x, dy = pos.y - p0.y, dz = pos.z - p0.z;
                const float speed = sqrtf(dx * dx + dy * dy + dz * dz) / (ts * 0.02f);
                const float fx = fwd.x - f0.x, fy = fwd.y - f0.y, fz = fwd.z - f0.z;
                const float turn = sqrtf(fx * fx + fy * fy + fz * fz);
                moving = speed >= kStationarySpeed || turn >= kStationaryTurn;
            }
        }
        if (moving)
            g_moving.insert(v);

        MotionState st;
        st.frame = g_motionFrame; st.model = v->m_nModelIndex; st.pos = pos; st.fwd = fwd;
        g_motion[v] = st;
    }
}

static bool shouldCollide(CVehicle* v) {
    if (kSkipStationary && g_moving.find(v) == g_moving.end())
        return false;
    if (!isInWorld(v))
        return false;
    const unsigned entityFlags = *reinterpret_cast<unsigned*>(reinterpret_cast<char*>(v) + 0x1C);
    if (!(entityFlags & 1))   // bUsesCollision
        return false;
    if (v->m_nVehicleSubClass == VEHICLE_TRAIN)
        return false;
    return true;
}

// ---------------------------------------------------------------------------
// Ground-following suspension
// compression: 0 = fully compressed, 1 = fully extended / no ground contact
// ---------------------------------------------------------------------------
static bool suspensionArrays(CVehicle* v, float*& comp, float*& prev) {
    if (isAutomobileType(v)) {
        CAutomobile* a = static_cast<CAutomobile*>(v);
        comp = a->wheelsDistancesToGround1;   // m_fWheelsSuspensionCompression (0x7D4)
        prev = a->wheelsDistancesToGround2;   // ...Prev (0x7E4)
        return true;
    }
    if (isBikeType(v)) {
        CBike* b = static_cast<CBike*>(v);
        comp = b->field_710;                  // m_fWheelsSuspensionCompression (0x710)
        prev = b->field_720;                  // ...Prev (0x720)
        return true;
    }
    return false;
}

static float vecLen(const CVector& a) {
    return sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
}

static void updateGroundSuspension(CVehicle* v) {
    float* comp; float* prev;
    if (!v->m_matrix || !suspensionArrays(v, comp, prev))
        return;
    CColModel* cm = v->GetColModel();
    if (!cm || !cm->m_pColData)
        return;
    CCollisionData* cd = cm->m_pColData;
    const bool disks = cd->m_nFlags.bUsesDisks;
    int n = cd->m_nNumLines;
    if (n > 4) n = 4;

    const CMatrix& m = *v->m_matrix;
    for (int i = 0; i < n; ++i) {
        CVector start, end;
        float radius = 0.0f;
        if (disks) {
            start  = cd->m_pDisks[i].m_vecStart;
            end    = cd->m_pDisks[i].m_vecEnd;
            radius = cd->m_pDisks[i].m_fStartRadius;
        } else {
            start = cd->m_pLines[i].m_vecStart;
            end   = cd->m_pLines[i].m_vecEnd;
        }
        const CVector ws = m * start;
        const CVector we = m * end;
        CVector dir(we.x - ws.x, we.y - ws.y, we.z - ws.z);
        const float len = vecLen(dir);
        if (len < 1e-4f)
            continue;
        dir.x /= len; dir.y /= len; dir.z /= len;

        // For disks the wheel centre runs along the line; extend by the radius
        // so the tyre surface, not the centre, finds the ground.
        const CVector rayEnd(we.x + dir.x * radius, we.y + dir.y * radius, we.z + dir.z * radius);

        float c = 1.0f;
        CColPoint cp;
        CEntity* hit = nullptr;
        // buildings + objects only: never hit our own body or other vehicles
        if (CWorld::ProcessLineOfSight(ws, rayEnd, cp, hit, true, false, false, true, false, false, false, false)) {
            CVector d(cp.m_vecPoint.x - ws.x, cp.m_vecPoint.y - ws.y, cp.m_vecPoint.z - ws.z);
            c = (vecLen(d) - radius) / len;
            if (c < 0.0f) c = 0.0f;
            if (c > 1.0f) c = 1.0f;
        }
        prev[i] = comp[i];
        comp[i] = c;
    }
}

// What CPhysical::ProcessControl (0x5485E0) resets at the start of every frame
// in normal play. ProcessControl never runs in playback, so without this the
// flags below latch once a contact sets them, and collision/sparks depend on
// everything that happened earlier in the replay (e.g. whether sparks were
// toggled off for a while).
static void resetPerFrameCollisionState(CPhysical* e) {
    unsigned& entityFlags = *reinterpret_cast<unsigned*>(reinterpret_cast<char*>(e) + 0x1C);
    entityFlags &= 0xFFFFEF97;   // bHasContacted, bIsInSafePosition, bWasPostponed, bHasHitWall
    e->m_nPhysicalFlags.bOnSolidSurface = 0;
    e->m_nPhysicalFlags.b31 = 0;
    e->m_nNumEntitiesCollided = 0;
    e->m_nPieceType = 0;
    e->m_fDamageIntensity = 0.0f;
    if (e->m_pDamageEntity) {
        e->m_pDamageEntity->CleanUpOldReference(&e->m_pDamageEntity);
        e->m_pDamageEntity = nullptr;
    }
}

static void runReplayCollision() {
    g_snapshots.clear();
    updateMotion();   // before any collision touches the poses

    auto* vehPool = CPools::ms_pVehiclePool;
    for (int i = 0; i < vehPool->m_nSize; ++i)
        if (CVehicle* v = vehPool->GetAt(i))
            if (isInWorld(v))
                resetPerFrameCollisionState(v);   // before the snapshot, so it's part of the restored state

    for (int i = 0; i < vehPool->m_nSize; ++i)
        if (CVehicle* v = vehPool->GetAt(i))
            takeSnapshot(v, v);

    auto* pedPool = CPools::ms_pPedPool;
    for (int i = 0; i < pedPool->m_nSize; ++i)
        if (CPed* p = pedPool->GetAt(i))
            takeSnapshot(p);

    for (const auto& s : g_snapshots)
        protectFromDamage(s);

    // Every pass starts from the exact replay pose; collision is only used for
    // sparks. Afterwards pose, speeds and wheel positions go back to the replay.
    const int passes = g_replaySparksEnabled ? kCollisionPassesPerFrame : 0;
    for (int pass = 0; pass < passes; ++pass) {
        if (pass > 0)
            for (const auto& s : g_snapshots)
                restorePose(s);

        if (kAllVehiclesSpark) {
            for (int i = 0; i < vehPool->m_nSize; ++i)
                if (CVehicle* v = vehPool->GetAt(i))
                    if (shouldCollide(v))
                        v->ProcessCollision();
        }
        else if (CVehicle* v = FindPlayerVehicle(0, false)) {
            if (shouldCollide(v))
                v->ProcessCollision();
        }
    }

    for (const auto& s : g_snapshots) {
        restorePose(s);
        restoreWheels(s);
        restoreDamageState(s);
    }

    if (kGroundFollowWheels)
        for (const auto& s : g_snapshots)
            if (s.vehicle)
                updateGroundSuspension(s.vehicle);
}

// ---------------------------------------------------------------------------
// Distance-based bike wheel spin
//
// The wheel turns by the distance its hub travels along the bike's forward
// direction divided by the wheel radius (what CVehicle::ProcessWheelRotation
// does for a rolling wheel). Hub positions come from the wheel frames, so
// turning, leaning and pivoting are all included.
// ---------------------------------------------------------------------------
struct WheelSpinState {
    unsigned       frame;      // last frame this vehicle was seen
    unsigned short model;
    CVector        hub[2];     // [0] front, [1] rear (world space)
};

static std::unordered_map<CVehicle*, WheelSpinState> g_wheelSpin;
static unsigned g_spinFrame = 0;

static bool wheelHubPos(CBike* b, int node, CVector& out) {
    RwFrame* f = b->m_aBikeNodes[node];
    if (!f)
        return false;
    RwMatrix* ltm = RwFrameGetLTM(f);
    if (!ltm)
        return false;
    out = CVector(ltm->pos.x, ltm->pos.y, ltm->pos.z);
    return true;
}

// Spawn rim sparks for a burst wheel touching the ground.
static void emitRimSparks(CBike* b, const CVector& hub, float radius, const CVector& moveDir, float speed) {
    if (speed < kRimSparkMinSpeed)
        return;

    const CVector& down = b->m_matrix->at;   // bike's up axis; ray goes the other way
    const float reach = radius + kRimContactMargin;
    const CVector rayEnd(hub.x - down.x * reach, hub.y - down.y * reach, hub.z - down.z * reach);

    CColPoint cp;
    CEntity* hit = nullptr;
    if (!CWorld::ProcessLineOfSight(hub, rayEnd, cp, hit, true, false, false, true, false, false, false, false))
        return;

    float t = (speed - kRimSparkMinSpeed) / (kRimSparkFullSpeed - kRimSparkMinSpeed);
    if (t > 1.0f) t = 1.0f;
    const int amount = 1 + static_cast<int>(t * (kRimSparkMaxAmount - 1));

    // thrown backwards, slightly up
    CVector origin = cp.m_vecPoint;
    CVector dir(-moveDir.x + down.x * 0.3f, -moveDir.y + down.y * 0.3f, -moveDir.z + down.z * 0.3f);
    CVector across = b->m_matrix->right;
    g_fx.AddSparks(origin, dir, kRimSparkForce, amount, across, 0, kRimSparkSpread, kRimSparkLife);
}

static void updateBikeWheelSpin(CBike* b) {
    CBaseModelInfo* bmi = CModelInfo::ms_modelInfoPtrs[b->m_nModelIndex];
    if (!bmi || !b->m_matrix)
        return;
    CVehicleModelInfo* mi = static_cast<CVehicleModelInfo*>(bmi);
    const float radius[2] = { mi->m_fWheelSizeFront * 0.5f, mi->m_fWheelSizeRear * 0.5f };
    const int   nodes[2]  = { BIKE_WHEEL_FRONT, BIKE_WHEEL_REAR };

    CVector hub[2];
    if (!wheelHubPos(b, nodes[0], hub[0]) || !wheelHubPos(b, nodes[1], hub[1]))
        return;

    auto it = g_wheelSpin.find(b);
    const bool fresh = it == g_wheelSpin.end()
                    || it->second.model != b->m_nModelIndex
                    || it->second.frame + 1 != g_spinFrame;   // missed a frame: slot reused / seek
    if (fresh) {
        WheelSpinState st;
        st.frame = g_spinFrame; st.model = b->m_nModelIndex;
        st.hub[0] = hub[0]; st.hub[1] = hub[1];
        g_wheelSpin[b] = st;
        return;
    }

    WheelSpinState& st = it->second;
    const CVector fwd = b->m_matrix->up;    // GTA: up vector of the matrix is forward
    const float   ts  = CTimer::ms_fTimeStep;

    for (int i = 0; i < 2; ++i) {
        const CVector d(hub[i].x - st.hub[i].x, hub[i].y - st.hub[i].y, hub[i].z - st.hub[i].z);
        const float dist = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
        if (dist > kMaxHubStep || radius[i] <= 0.0f)
            continue;
        // signed rolling distance: magnitude of hub travel, sign from the bike's heading
        const float along = d.x * fwd.x + d.y * fwd.y + d.z * fwd.z;
        const float rolled = along >= 0.0f ? dist : -dist;
        const float dAngle = kWheelSpinSign * rolled / radius[i];

        if (kBurstTyreSparks && b->m_anWheelDamageState[i] == 1 && ts > 0.0f && dist > 0.0f) {
            const float speed = dist / (ts * 0.02f);   // timestep is in 1/50 s units
            const CVector moveDir(d.x / dist, d.y / dist, d.z / dist);
            emitRimSparks(b, hub[i], radius[i], moveDir, speed);
        }

        float rot = b->m_afWheelRotationX[i] + dAngle;
        rot = fmodf(rot, 6.2831853f);
        b->m_afWheelRotationX[i] = rot;
        if (ts > 0.0f)
            b->m_fWheelSpeed[i] = dAngle / ts;
    }

    st.frame = g_spinFrame;
    st.hub[0] = hub[0];
    st.hub[1] = hub[1];
}

static bool isDistanceSpinModel(unsigned short model) {
    for (unsigned short m : kDistanceSpinModels)
        if (m == model)
            return true;
    return false;
}

static void runDistanceWheelSpin() {
    ++g_spinFrame;
    auto* vehPool = CPools::ms_pVehiclePool;
    for (int i = 0; i < vehPool->m_nSize; ++i) {
        CVehicle* v = vehPool->GetAt(i);
        if (!v || !isDistanceSpinModel(v->m_nModelIndex) || !isBikeType(v) || !isInWorld(v))
            continue;
        CBike* b = static_cast<CBike*>(v);
        if (kTestFlatFrontTyre)
            b->m_anWheelDamageState[0] = 1;
        updateBikeWheelSpin(b);
    }
}

// ---------------------------------------------------------------------------
// Smooth replay playback
//
// 0x4604A0 CReplay::PlayBackThisFrame is a 5-byte jmp to the real body (which
// calls PlayBackThisFrameInterpolation(&Playback, 1.0f, nullptr) and handles
// the end of the replay). We take over that jmp:
//   sub-frames: PlayBackThisFrameInterpolation(copy of Playback, t, nullptr)
//               -> moves everything part of the way to the next recorded frame
//                  without advancing the read position
//   last frame: the original body -> lands exactly on the recorded frame and advances
// With N sub-frames, at sub-frame k the pose is A + k/N (B - A); blending by
// t = 1/(N - k) takes it to A + (k+1)/N (B - A).
// ---------------------------------------------------------------------------
using PlayBackInterp_t = bool(__cdecl*)(void* buffer, float interpolation, unsigned* timer);
static const PlayBackInterp_t PlayBackThisFrameInterpolation = reinterpret_cast<PlayBackInterp_t>(0x45F380);
constexpr unsigned kPlaybackAddress     = 0x97FB64;  // CReplay::Playback (CAddressInReplayBuffer)
constexpr unsigned kPlaybackAddressSize = 12;        // offset, base ptr, slot

using VoidFn_t = void(__cdecl*)();
// CReplay::ProcessLookAroundCam: mouse-controlled replay camera. The original
// PlayBackThisFrame calls it after every frame that isn't the end of the replay.
static const VoidFn_t ProcessLookAroundCam = reinterpret_cast<VoidFn_t>(0x45D760);
static VoidFn_t g_origPlayBackThisFrame = nullptr;
static int      g_replaySubFrame = 0;
static bool     g_smoothReplayEnabled = kSmoothReplayDefault;

static void __cdecl SmoothPlayBackThisFrame() {
    if (g_smoothReplayEnabled && g_replaySubFrame < kReplaySubFrames - 1) {
        unsigned char copy[kPlaybackAddressSize];
        memcpy(copy, reinterpret_cast<void*>(kPlaybackAddress), sizeof(copy));
        const float t = 1.0f / static_cast<float>(kReplaySubFrames - g_replaySubFrame);
        if (!PlayBackThisFrameInterpolation(copy, t, nullptr))
            ProcessLookAroundCam();   // same as the original after a normal frame
        ++g_replaySubFrame;
        return;
    }
    g_replaySubFrame = 0;
    g_origPlayBackThisFrame();
}

static void patchSmoothReplay() {
    const unsigned char* p = reinterpret_cast<const unsigned char*>(0x4604A0);
    if (p[0] != 0xE9)
        return;   // unexpected exe layout: leave playback alone
    const int rel = *reinterpret_cast<const int*>(p + 1);
    g_origPlayBackThisFrame = reinterpret_cast<VoidFn_t>(0x4604A5 + rel);
    patch::RedirectJump(0x4604A0, SmoothPlayBackThisFrame);
}

// ---------------------------------------------------------------------------
// Camera sync for replaced CCamera::Process
// ---------------------------------------------------------------------------
static bool cameraProcessReplaced() {
    // original at 0x53C104: call CCamera::Process (E8 27 F6 FE FF)
    static const unsigned char original[5] = { 0xE8, 0x27, 0xF6, 0xFE, 0xFF };
    return memcmp(reinterpret_cast<const void*>(0x53C104), original, 5) != 0;
}

static void syncCameraToRenderWare() {
    // Same tail as CCamera::Process (0x52C983 / 0x52C98B, ebp == 0 there)
    TheCamera.CalculateDerivedValues(false, true);
    TheCamera.CopyCameraMatrixToRWCam(false);
}

// ---------------------------------------------------------------------------

class SpinningReplay {
public:
    static void patchWheelSpin() {
        patch::RedirectJump(0x45B94A, ASM_checkBikePacket);
        patch::RedirectJump(0x45BC7D, ASM_checkBikeExtraction);
    }

    static void patchSparks() {
        // CPhysical::ApplyFriction (0x5457AA): sparks only when the friction
        // value exceeds a threshold. Original: fcomp dword ptr [0x858B1C] (0.1).
        // Point it at our own float so the threshold is tunable.
        //   fcomp dword ptr [&g_sparkMinFriction]
        {
            unsigned char sparkCondition[6] = { 0xD8, 0x1D, 0, 0, 0, 0 };
            const unsigned addr = reinterpret_cast<unsigned>(&g_sparkMinFriction);
            memcpy(sparkCondition + 2, &addr, 4);
            patch::SetRaw(0x5457AA, sparkCondition, 6);
        }

        // Entity flags on replay restore (CReplay::ProcessCarUpdate).
        // First 13 bytes:
        //   mov eax,[esi+1Ch]
        //   and eax,0FFFBFF81h  ; clear bCollisionProcessed..bWasPostponed and
        //                       ; bIsStaticWaitingForCollision, keep the rest
        //   or  eax,81h         ; bUsesCollision | bIsVisible
        // Old version (wiped every other flag): 83 E0 00 0D 81 00 00 00 90 90 90 90 90
        unsigned char entityFlags[70] = {
            0x8B, 0x46, 0x1C,
            0x25, 0x81, 0xFF, 0xFB, 0xFF,
            0x0D, 0x81, 0x00, 0x00, 0x00,
            0x89, 0x46, 0x1C,
            0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
            0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
            0x0F, 0xB6, 0x4F, 0x2F, 0x0F, 0xB6, 0x57, 0x2F, 0xC1, 0xE1, 0x1C,
            0x33, 0xC8, 0x81, 0xE1, 0xFF, 0xFF, 0xFF, 0x7F, 0xC1, 0xE2, 0x1C,
            0x33, 0xCA, 0x66, 0x81, 0x7E, 0x22, 0xB0, 0x01, 0x90, 0x90, 0x90 };
        patch::SetRaw(0x45BBF7, entityFlags, 70);

        // ApplyFriction: spark count per contact from g_sparkLoopCount
        //   0x5458B4: mov edi, 8 (BF 08 00 00 00) -> jmp ASM_sparkLoopCount
        {
            const unsigned char expected[5] = { 0xBF, 0x08, 0x00, 0x00, 0x00 };
            if (memcmp(reinterpret_cast<const void*>(0x5458B4), expected, 5) == 0)
                patch::RedirectJump(0x5458B4, ASM_sparkLoopCount);
        }

        // ProcessCollisionSectorList: route wheel-sphere contacts through our check
        //   0x54CAA9: jbe 0x54CCB0 (0F 86 01 02 00 00) -> jbe ASM_wheelPieceContact
        if (kNoWheelSphereContacts) {
            const unsigned char expected[6] = { 0x0F, 0x86, 0x01, 0x02, 0x00, 0x00 };
            if (memcmp(reinterpret_cast<const void*>(0x54CAA9), expected, 6) == 0) {
                unsigned char jbe[6] = { 0x0F, 0x86, 0, 0, 0, 0 };
                const int rel = reinterpret_cast<int>(&ASM_wheelPieceContact) - (0x54CAA9 + 6);
                memcpy(jbe + 2, &rel, 4);
                patch::SetRaw(0x54CAA9, jbe, 6);
            }
        }

        // CReplay::PlayBackThisFrameInterpolation: remove AND on entity flags
        patch::Nop(0x45F805, 3);

        // CBike::PreRender: skip suspension height interaction in playback
        if (kPatchBikePreRender)
            patch::RedirectJump(0x6BD0C7, ASM_addCheckBikePreRenderReplayMode);

        // CPhysical::ProcessCollisionSectorList: and dword ptr [esi+1Ch], ~bHasContacted
        unsigned char clearContacted[4] = { 0x83, 0x66, 0x1C, 0xF7 };
        patch::SetRaw(0x54CCEE, clearContacted, 4);

        patch::RedirectJump(0x45F39C, ASM_copyMatrixToOldPlaybackThisFrameInterpolation);

        // Ped position fixes. Probably redundant now that runReplayCollision()
        // restores ped transforms, kept until tested without them.
        patch::RedirectJump(0x5E2C71, ASM_removeEntityCollisionCPedSetPosition);
        patch::RedirectJump(0x5E2C79, ASM_removeCollisionOffsetReplayMode);
        patch::SetUChar(0x5E2C7E, 0x90);
    }

    static void setPlayerFrozen(CPlayerPed* player, bool frozen) {
        player->m_nPhysicalFlags.bDisableCollisionForce = frozen;
        player->m_nPhysicalFlags.bDisableMoveForce      = frozen;
        player->m_nPhysicalFlags.bDontApplySpeed        = frozen;
    }

    static void process() {
        static unsigned char prevMode = 0;
        const unsigned char mode = CReplay::Mode;
        const bool playback = mode == 1;

        if (CPlayerPed* player = FindPlayerPed()) {
            if (playback)
                setPlayerFrozen(player, true);
            else if (prevMode == 1)
                setPlayerFrozen(player, false);   // only once, on leaving playback
        }
        prevMode = mode;
        // F8: toggle replay sparks (edge-triggered)
        static bool keyWasDown = false;
        const bool keyDown = (GetAsyncKeyState(kToggleSparksKey) & 0x8000) != 0;
        if (keyDown && !keyWasDown) {
            g_replaySparksEnabled = !g_replaySparksEnabled;
            CHud::SetHelpMessage(g_replaySparksEnabled ? "Replay sparks: ON" : "Replay sparks: OFF",
                                 true, false, false);
        }
        keyWasDown = keyDown;

        // F7: toggle smooth (interpolated) replay playback
        static bool smoothKeyWasDown = false;
        const bool smoothKeyDown = (GetAsyncKeyState(kToggleSmoothKey) & 0x8000) != 0;
        if (smoothKeyDown && !smoothKeyWasDown) {
            g_smoothReplayEnabled = !g_smoothReplayEnabled;
            g_replaySubFrame = 0;   // next playback frame is a normal full step
            CHud::SetHelpMessage(g_smoothReplayEnabled ? "Replay interpolation: ON" : "Replay interpolation: OFF",
                                 true, false, false);
        }
        smoothKeyWasDown = smoothKeyDown;

        if (playback) {
            const float ts = CTimer::ms_fTimeStep > 0.0f ? CTimer::ms_fTimeStep : 1.0f;
            int n = static_cast<int>(kGameSparkLoopCount * kReplaySparkMultiplier * ts + 0.5f);
            if (n < 1) n = 1;                     // loop is do/while: 0 would run ~4 billion times
            if (n > kMaxSparkLoopCount) n = kMaxSparkLoopCount;
            g_sparkLoopCount = static_cast<DWORD>(n);
        } else {
            g_sparkLoopCount = kGameSparkLoopCount;
        }

        if (!playback)
            g_sparkMinFriction = kGameSparkMinFriction;
        else
            g_sparkMinFriction = g_replaySparksEnabled ? kReplaySparkMinFriction : 1.0e30f;

        if (playback) {
            if (kSyncReplacedCamera && cameraProcessReplaced())
                syncCameraToRenderWare();
            runReplayCollision();
            if (kDistanceWheelSpin)
                runDistanceWheelSpin();
        }
        else {
            g_replaySubFrame = 0;
            if (!g_wheelSpin.empty())
                g_wheelSpin.clear();
            if (!g_motion.empty()) {
                g_motion.clear();
                g_moving.clear();
            }
        }
    }

    SpinningReplay() {
        if (kEnableWheelSpin)
            patchWheelSpin();
        patchSparks();
        if (kSmoothReplay && kReplaySubFrames > 1)
            patchSmoothReplay();
        Events::gameProcessEvent.Add(process);
    }
} spinningReplay;
