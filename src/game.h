#pragma once
/**
 * GTA San Andreas Game Functions & Structures
 * Base game addresses for GTA SA 1.0 US
 */

#include <windows.h>
#include <cmath>
#include "samp.h"

// ============================================================
// GTA SA Memory Addresses (1.0 US)
// ============================================================
namespace GameAddr {
    constexpr DWORD PLAYER_PED_PTR      = 0xB6F5F0;   // CPed* pointer
    constexpr DWORD VEHICLE_PTR_OFFSET  = 0x58C;      // CPed -> CVehicle*
    constexpr DWORD MATRIX_OFFSET       = 0x14;       // CEntity -> CMatrix*
    constexpr DWORD POS_X_MATRIX        = 0x30;
    constexpr DWORD POS_Y_MATRIX        = 0x34;
    constexpr DWORD POS_Z_MATRIX        = 0x38;
    constexpr DWORD POS_X_SIMPLE        = 0x4;
    constexpr DWORD POS_Y_SIMPLE        = 0x8;
    constexpr DWORD POS_Z_SIMPLE        = 0xC;
    constexpr DWORD ENTITY_MODEL_INDEX  = 0x22;       // CEntity -> nModelIndex (WORD)
    constexpr DWORD VEHICLE_SPEED_X     = 0x44;
    constexpr DWORD VEHICLE_SPEED_Y     = 0x48;
    constexpr DWORD VEHICLE_SPEED_Z     = 0x4C;
    constexpr DWORD VEHICLE_TURN_SPEED_X = 0x50;
    constexpr DWORD VEHICLE_TURN_SPEED_Y = 0x54;
    constexpr DWORD VEHICLE_TURN_SPEED_Z = 0x58;
    constexpr DWORD FUNC_FIND_GROUND_Z  = 0x569660;

    // GTA SA Camera (1.0 US)
    constexpr DWORD CAMERA_OBJECT        = 0xB6F028;   // TheCamera (CCamera instance)
    constexpr DWORD FUNC_RESTORE_CAMERA  = 0x50B930;   // CCamera::Restore()
    constexpr DWORD FUNC_RESTORE_JUMPCUT = 0x50BAB0;   // CCamera::RestoreWithJumpCut()

    // Vehicle warp (GTA SA 1.0 US)
    // CPed::WarpPedIntoCar — verified from BlastHackNet/mod_sa CPoolsSA.h
    constexpr DWORD FUNC_WARP_PED_INTO_CAR = 0x4EF8B0;  // CPed::WarpPedIntoCar(CVehicle*)

    // GTA SA Vehicle Pool (1.0 US)
    constexpr DWORD GTA_VEHICLE_POOL_PTR = 0xB74494;  // CPool<CVehicle>*
    // CPool layout: +0x00=objects base, +0x04=flags base, +0x08=capacity
    constexpr DWORD CVEHICLE_SIZE = 0x5DC;  // Size of one CVehicle struct
}

// ============================================================
// Game Functions
// ============================================================
namespace Game {
    
    struct Vec3 {
        float x, y, z;
    };

    inline DWORD GetPlayerPed() {
        DWORD* pPed = (DWORD*)GameAddr::PLAYER_PED_PTR;
        if (!pPed || IsBadReadPtr(pPed, 4)) return 0;
        return *pPed;
    }

    inline bool IsPlayerInVehicle() {
        DWORD ped = GetPlayerPed();
        if (!ped || IsBadReadPtr((void*)ped, 0x600)) return false;
        DWORD vehicle = *(DWORD*)(ped + GameAddr::VEHICLE_PTR_OFFSET);
        return vehicle != 0;
    }

    inline DWORD GetPlayerVehicle() {
        DWORD ped = GetPlayerPed();
        if (!ped || IsBadReadPtr((void*)ped, 0x600)) return 0;
        return *(DWORD*)(ped + GameAddr::VEHICLE_PTR_OFFSET);
    }

    inline WORD GetVehicleModelId() {
        DWORD vehicle = GetPlayerVehicle();
        if (!vehicle) return 0;
        return *(WORD*)(vehicle + GameAddr::ENTITY_MODEL_INDEX);
    }

    inline Vec3 GetEntityPosition(DWORD entity) {
        Vec3 pos = {0, 0, 0};
        if (!entity) return pos;
        DWORD* pMatrix = (DWORD*)(entity + GameAddr::MATRIX_OFFSET);
        if (pMatrix && *pMatrix) {
            DWORD matrix = *pMatrix;
            pos.x = *(float*)(matrix + GameAddr::POS_X_MATRIX);
            pos.y = *(float*)(matrix + GameAddr::POS_Y_MATRIX);
            pos.z = *(float*)(matrix + GameAddr::POS_Z_MATRIX);
        } else {
            pos.x = *(float*)(entity + GameAddr::POS_X_SIMPLE);
            pos.y = *(float*)(entity + GameAddr::POS_Y_SIMPLE);
            pos.z = *(float*)(entity + GameAddr::POS_Z_SIMPLE);
        }
        return pos;
    }

    inline Vec3 GetPlayerPosition() {
        return GetEntityPosition(GetPlayerPed());
    }

    inline void SetEntityPosition(DWORD entity, float x, float y, float z) {
        if (!entity) return;
        
        // Update CPlaceable raw coordinates (always present)
        *(float*)(entity + GameAddr::POS_X_SIMPLE) = x;
        *(float*)(entity + GameAddr::POS_Y_SIMPLE) = y;
        *(float*)(entity + GameAddr::POS_Z_SIMPLE) = z;

        // Update CMatrix coordinates if present (used for rendering/physics)
        DWORD* pMatrix = (DWORD*)(entity + GameAddr::MATRIX_OFFSET);
        if (pMatrix && *pMatrix) {
            DWORD matrix = *pMatrix;
            *(float*)(matrix + GameAddr::POS_X_MATRIX) = x;
            *(float*)(matrix + GameAddr::POS_Y_MATRIX) = y;
            *(float*)(matrix + GameAddr::POS_Z_MATRIX) = z;
        }
    }

    // Set ped heading in degrees (0=north, 90=east, 180=south, clockwise).
    // Updates both matrix rotation vectors and the SA internal angle floats (0x558/0x55C).
    inline void SetPedHeading(float angleDeg) {
        DWORD ped = GetPlayerPed();
        if (!ped || IsBadReadPtr((void*)ped, 0x600)) return;
        const float PI = 3.14159265f;
        float rad = angleDeg * PI / 180.0f;
        float sinH = sinf(rad);
        float cosH = cosf(rad);
        // CMatrix rotation vectors:
        //   Forward (at)  = (sin, cos, 0)   — facing direction
        //   Right         = (cos, -sin, 0)  — 90° clockwise from forward
        //   Up            = (0, 0, 1)
        DWORD* pMatrix = (DWORD*)(ped + GameAddr::MATRIX_OFFSET);
        if (pMatrix && *pMatrix) {
            DWORD m = *pMatrix;
            // Right vector (matrix row 0)
            *(float*)(m + 0x00) =  cosH;
            *(float*)(m + 0x04) = -sinH;
            *(float*)(m + 0x08) =  0.0f;
            // Forward/At vector (matrix row 1)
            *(float*)(m + 0x10) =  sinH;
            *(float*)(m + 0x14) =  cosH;
            *(float*)(m + 0x18) =  0.0f;
            // Up vector (matrix row 2)
            *(float*)(m + 0x20) =  0.0f;
            *(float*)(m + 0x24) =  0.0f;
            *(float*)(m + 0x28) =  1.0f;
        }
        // SA internal rotation cache (m_fCurrentRotation / m_fTargetRotation)
        // SA stores these as radians where 0 = facing +Y (same as our angle=0=north)
        if (!IsBadWritePtr((void*)(ped + 0x558), 8)) {
            *(float*)(ped + 0x558) = rad; // m_fCurrentRotation
            *(float*)(ped + 0x55C) = rad; // m_fTargetRotation
        }
    }

    inline void TeleportVehicle(float x, float y, float z) {
        DWORD vehicle = GetPlayerVehicle();
        if (!vehicle) return;

        // Reset Physics (Speed)
        *(float*)(vehicle + GameAddr::VEHICLE_SPEED_X) = 0.0f;
        *(float*)(vehicle + GameAddr::VEHICLE_SPEED_Y) = 0.0f;
        *(float*)(vehicle + GameAddr::VEHICLE_SPEED_Z) = 0.0f;
        
        // Reset Angular Physics (Turn Speed) - Prevents spinning
        *(float*)(vehicle + GameAddr::VEHICLE_TURN_SPEED_X) = 0.0f;
        *(float*)(vehicle + GameAddr::VEHICLE_TURN_SPEED_Y) = 0.0f;
        *(float*)(vehicle + GameAddr::VEHICLE_TURN_SPEED_Z) = 0.0f;

        // Set Position
        SetEntityPosition(vehicle, x, y, z);
        DWORD ped = GetPlayerPed();
        if (ped) SetEntityPosition(ped, x, y, z);

        // Force Rotation to Flat (Identity Matrix) to prevent flying/rolling
        DWORD* pMatrix = (DWORD*)(vehicle + GameAddr::MATRIX_OFFSET);
        if (pMatrix && *pMatrix) {
            DWORD m = *pMatrix;
            // Right (X axis)
            *(float*)(m + 0x00) = 1.0f; 
            *(float*)(m + 0x04) = 0.0f; 
            *(float*)(m + 0x08) = 0.0f;
            // Forward (Y axis) NOTE: GTA uses Y as forward? usually Right, Forward, Up.
            // Standard Identity:
            // R: 1 0 0
            // F: 0 1 0
            // U: 0 0 1
            *(float*)(m + 0x10) = 0.0f;
            *(float*)(m + 0x14) = 1.0f;
            *(float*)(m + 0x18) = 0.0f;
            // Up (Z axis)
            *(float*)(m + 0x20) = 0.0f;
            *(float*)(m + 0x24) = 0.0f;
            *(float*)(m + 0x28) = 1.0f;
        }
    }

    // ============================================================
    // Checkpoint reading via SA-MP CGame
    // ============================================================
    
    inline bool IsCheckpointActive() {
        if (SAMP::IsInitialized()) {
            stCheckpoint* pCP = SAMP::GetCurrentCheckpoint();
            if (pCP && pCP->bEnabled) return true;
        }
        return false;
    }

    inline Vec3 GetCheckpointPosition() {
        Vec3 pos = {0, 0, 0};
        if (SAMP::IsInitialized()) {
            stCheckpoint* pCP = SAMP::GetCurrentCheckpoint();
            if (pCP && pCP->bEnabled) {
                return { pCP->fX, pCP->fY, pCP->fZ };
            }
        }
        return pos;
    }

    inline bool IsRaceCheckpointActive() {
        if (SAMP::IsInitialized()) {
            stRaceCheckpoint* pRCP = SAMP::GetRaceCheckpoint();
            if (pRCP && pRCP->bEnabled) return true;
        }
        return false;
    }

    inline Vec3 GetRaceCheckpointPosition() {
        Vec3 pos = {0, 0, 0};
        if (SAMP::IsInitialized()) {
            stRaceCheckpoint* pRCP = SAMP::GetRaceCheckpoint();
            if (pRCP && pRCP->bEnabled) {
                return { pRCP->fX, pRCP->fY, pRCP->fZ };
            }
        }
        return pos;
    }

    inline double Distance3D(Vec3 a, Vec3 b) {
        float dx = a.x - b.x;
        float dy = a.y - b.y;
        float dz = a.z - b.z;
        return sqrtf(dx*dx + dy*dy + dz*dz);
    }

    // Force vehicle to stay still and upright
    inline void StabilizeVehicle() {
        DWORD vehicle = GetPlayerVehicle();
        if (!vehicle) return;

        // Zero Velocities (Freeze movement)
        *(float*)(vehicle + GameAddr::VEHICLE_SPEED_X) = 0.0f;
        *(float*)(vehicle + GameAddr::VEHICLE_SPEED_Y) = 0.0f;
        *(float*)(vehicle + GameAddr::VEHICLE_SPEED_Z) = 0.0f;
        *(float*)(vehicle + GameAddr::VEHICLE_TURN_SPEED_X) = 0.0f;
        *(float*)(vehicle + GameAddr::VEHICLE_TURN_SPEED_Y) = 0.0f;
        *(float*)(vehicle + GameAddr::VEHICLE_TURN_SPEED_Z) = 0.0f;

        // Force Upright Orientation (Identity Matrix)
        DWORD* pMatrix = (DWORD*)(vehicle + GameAddr::MATRIX_OFFSET);
        if (pMatrix && *pMatrix) {
            DWORD m = *pMatrix;
            *(float*)(m + 0x00) = 1.0f; *(float*)(m + 0x04) = 0.0f; *(float*)(m + 0x08) = 0.0f;
            *(float*)(m + 0x10) = 0.0f; *(float*)(m + 0x14) = 1.0f; *(float*)(m + 0x18) = 0.0f;
            *(float*)(m + 0x20) = 0.0f; *(float*)(m + 0x24) = 0.0f; *(float*)(m + 0x28) = 1.0f;
        }
    }

    // Restore GTA SA camera to normal follow mode
    inline void RestoreCamera() {
        __try {
            // CCamera::Restore() - restaura la cámara al modo normal
            typedef void(__thiscall* Restore_t)(void*);
            void* pCamera = (void*)GameAddr::CAMERA_OBJECT;
            if (!IsBadReadPtr(pCamera, 4)) {
                Restore_t fnRestore = (Restore_t)GameAddr::FUNC_RESTORE_CAMERA;
                if (!IsBadCodePtr((FARPROC)fnRestore)) {
                    fnRestore(pCamera);
                }
            }

            // También RestoreWithJumpCut para forzar reset inmediato
            typedef void(__thiscall* RestoreJC_t)(void*);
            RestoreJC_t fnRestoreJC = (RestoreJC_t)GameAddr::FUNC_RESTORE_JUMPCUT;
            if (!IsBadCodePtr((FARPROC)fnRestoreJC)) {
                fnRestoreJC(pCamera);
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            // Camera restore failed - log but don't crash
        }
    }

    // Full cleanup when mod is deactivated
    inline void RestorePlayerState() {
        // 1. Restore camera
        RestoreCamera();

        // 2. Ensure SetInCheckpoint is off
        SAMP::SetInCheckpoint(false);

        // 3. Give vehicle a tiny nudge to "wake up" GTA's physics/camera tracking
        DWORD vehicle = GetPlayerVehicle();
        if (vehicle) {
            *(float*)(vehicle + GameAddr::VEHICLE_SPEED_Z) = 0.001f;
        }
    }

    // ============================================================
    // Chat & logging (forward declarations for use in inline functions)
    // ============================================================
    void AddChatMessage(DWORD color, const char* text);
    void Log(const char* fmt, ...);

    // ============================================================
    // Vehicle Warp — put player directly into a vehicle (no F key)
    //
    // Strategy (0.3.DL R1):
    //   We do NOT call CPed::WarpPedIntoCar or SAMP::PutInVehicle — both crash.
    //   Instead we write GTA internal state manually, then patch SAMP state.
    //
    //   GTA manual state:
    //     ped  +0x58C = vehicle ptr   (CPed::m_pVehicle)
    //     veh  +0x460 = ped ptr       (CVehicle::pDriver)
    //
    //   SAMP state:
    //     LocalPlayer+0xFC = sampId   (m_nCurrentVehicle → GetVehicleID())
    //     LocalPlayer+0x96 = sampId   (m_incarData.m_nVehicle)
    // ============================================================

    // GTA SA 1.0 US: CVehicle::pDriver (first occupant slot) at +0x460
    constexpr DWORD CVEHICLE_DRIVER_OFFSET = 0x460;
    // Offset of inCarData.m_nVehicle inside CLocalPlayer (pack 1, calculated from struct layout)
    constexpr DWORD LOCALPLAYER_INCARDATA_VEHICLEID = 0x96;

    // Warp player ped directly into a GTA vehicle (instant, no animation, no function calls)
    inline bool WarpPedIntoVehicle(DWORD gtaVehiclePtr) {
        if (!gtaVehiclePtr || IsBadReadPtr((void*)gtaVehiclePtr, 0x470)) return false;
        DWORD ped = GetPlayerPed();
        if (!ped || IsBadReadPtr((void*)ped, 0x600)) return false;

        // Read vehicle world position (simple coords at entity+0x04)
        float vx = 0, vy = 0, vz = 0;
        SAMP::SafeRead<float>(gtaVehiclePtr + GameAddr::POS_X_SIMPLE, vx);
        SAMP::SafeRead<float>(gtaVehiclePtr + GameAddr::POS_Y_SIMPLE, vy);
        SAMP::SafeRead<float>(gtaVehiclePtr + GameAddr::POS_Z_SIMPLE, vz);

        // Teleport ped to vehicle position
        *(float*)(ped + GameAddr::POS_X_SIMPLE) = vx;
        *(float*)(ped + GameAddr::POS_Y_SIMPLE) = vy;
        *(float*)(ped + GameAddr::POS_Z_SIMPLE) = vz + 0.3f;

        // Also update matrix if present
        DWORD* pPedMatrix = (DWORD*)(ped + GameAddr::MATRIX_OFFSET);
        if (pPedMatrix && *pPedMatrix) {
            DWORD m = *pPedMatrix;
            *(float*)(m + GameAddr::POS_X_MATRIX) = vx;
            *(float*)(m + GameAddr::POS_Y_MATRIX) = vy;
            *(float*)(m + GameAddr::POS_Z_MATRIX) = vz + 0.3f;
        }

        // Find SA-MP vehicle ID
        WORD sampId = SAMP::GetSAMPIdFromGTAVehicle(gtaVehiclePtr);
        if (sampId == 0xFFFF) {
            WORD model = 0;
            SAMP::SafeRead<WORD>(gtaVehiclePtr + GameAddr::ENTITY_MODEL_INDEX, model);
            if (model == 472) {
                Game::Log("[WARP] Pool lookup failed, Model=472 -> forcing ID=30");
                sampId = 30;
            }
        }
        Game::Log("[WARP] GTA Ptr: 0x%X -> SAMP ID: %d", gtaVehiclePtr, (int)sampId);

        // --- Step 1: Write GTA state directly (no function calls) ---
        __try {
            // CPed::m_pVehicle — GTA reads this in GetPlayerVehicle()
            *(DWORD*)(ped + 0x58C) = gtaVehiclePtr;
            // CVehicle::pDriver — GTA reads this to know who is driving
            *(DWORD*)(gtaVehiclePtr + CVEHICLE_DRIVER_OFFSET) = ped;
            Game::Log("[WARP] GTA state written: ped+0x58C=veh, veh+0x460=ped");
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            Game::Log("[WARP] GTA state write EXCEPTION - no vehicle ptr?");
            return false;
        }

        // --- Step 1b: Call CPed::WarpPedIntoCar to set driving task + seating animations ---
        // This makes the ped visible in the driver seat with proper GTA task state.
        // Calling convention: __thiscall - first arg = this (ECX) = ped, second = CVehicle*
        __try {
            typedef void(__thiscall* WarpPedIntoCar_t)(DWORD pedPtr, DWORD vehiclePtr);
            WarpPedIntoCar_t fnWarp = (WarpPedIntoCar_t)GameAddr::FUNC_WARP_PED_INTO_CAR;
            if (!IsBadCodePtr((FARPROC)fnWarp)) {
                fnWarp(ped, gtaVehiclePtr);
                Game::Log("[WARP] WarpPedIntoCar OK");
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            // Function crashed - manual state from Step 1 is still set, continue
            Game::Log("[WARP] WarpPedIntoCar exception (manual state still valid)");
        }

        // --- Step 2: Update SA-MP internal state so GetVehicleID() != 0xFFFF ---
        if (sampId != 0xFFFF) {
            DWORD pLocal = SAMP::GetLocalPlayer();
            if (pLocal && !IsBadWritePtr((void*)(pLocal + SAMPOffsets::LOCALPLAYER_VEHICLEID), 4)) {
                // m_nCurrentVehicle at +0xFC (ID = 2 bytes)
                *(WORD*)(pLocal + SAMPOffsets::LOCALPLAYER_VEHICLEID) = sampId;
                // m_incarData.m_nVehicle at +0x96 (first field of IncarData, 2 bytes)
                *(WORD*)(pLocal + LOCALPLAYER_INCARDATA_VEHICLEID) = sampId;
                Game::Log("[WARP] SAMP state: m_nCurrentVehicle=%d  m_incarData.m_nVehicle=%d", (int)sampId, (int)sampId);
            }
        }

        RestoreCamera();
        return true;
    }

    // Find a vehicle in GTA's own vehicle pool by model ID and warp into it.
    inline bool WarpIntoVehicleByModel(WORD targetModelId) {
        // Read CPool<CVehicle>* from GTA's global (0xB74494 in 1.0 US)
        DWORD pPool = 0;
        if (!SAMP::SafeRead<DWORD>(GameAddr::GTA_VEHICLE_POOL_PTR, pPool)) return false;
        if (!pPool || IsBadReadPtr((void*)pPool, 0x10)) return false;

        DWORD objectsBase = 0, flagsBase = 0;
        int capacity = 0;
        if (!SAMP::SafeRead<DWORD>(pPool + 0x00, objectsBase)) return false;
        if (!SAMP::SafeRead<DWORD>(pPool + 0x04, flagsBase)) return false;
        if (!SAMP::SafeRead<int>(pPool + 0x08, capacity)) return false;
        if (!objectsBase || !flagsBase || capacity <= 0 || capacity > 5000) return false;

        DWORD bestVehicle = 0;
        // Search for any vehicle matching the model
        for (int i = 0; i < capacity; i++) {
            BYTE flag = 0;
            if (!SAMP::SafeRead<BYTE>(flagsBase + i, flag)) continue;
            if (flag & 0x80) continue; // Slot is free

            DWORD vehicle = objectsBase + i * GameAddr::CVEHICLE_SIZE;
            if (IsBadReadPtr((void*)vehicle, 0x100)) continue;

            WORD model = 0;
            if (!SAMP::SafeRead<WORD>(vehicle + GameAddr::ENTITY_MODEL_INDEX, model)) continue;
            if (model == targetModelId) {
                bestVehicle = vehicle;
                break; // Found one!
            }
        }

        if (!bestVehicle) return false;
        return WarpPedIntoVehicle(bestVehicle);
    }

    // Find vehicle by model in GTA pool and teleport the player PED on top of it (on foot).
    // Does NOT call WarpPedIntoVehicle — the ped is placed above the vehicle so the
    // game's own enter-vehicle logic (F key) can handle the actual entry.
    // Returns the GTA vehicle pointer if the vehicle was found, 0 otherwise.
    inline DWORD TeleportPedAboveVehicle(WORD targetModelId) {
        DWORD pPool = 0;
        if (!SAMP::SafeRead<DWORD>(GameAddr::GTA_VEHICLE_POOL_PTR, pPool)) return 0;
        if (!pPool || IsBadReadPtr((void*)pPool, 0x10)) return 0;

        DWORD objectsBase = 0, flagsBase = 0;
        int capacity = 0;
        if (!SAMP::SafeRead<DWORD>(pPool + 0x00, objectsBase)) return 0;
        if (!SAMP::SafeRead<DWORD>(pPool + 0x04, flagsBase)) return 0;
        if (!SAMP::SafeRead<int>(pPool + 0x08, capacity)) return 0;
        if (!objectsBase || !flagsBase || capacity <= 0 || capacity > 5000) return 0;

        DWORD bestVehicle = 0;
        for (int i = 0; i < capacity; i++) {
            BYTE flag = 0;
            if (!SAMP::SafeRead<BYTE>(flagsBase + i, flag)) continue;
            if (flag & 0x80) continue;
            DWORD vehicle = objectsBase + i * GameAddr::CVEHICLE_SIZE;
            if (IsBadReadPtr((void*)vehicle, 0x100)) continue;
            WORD model = 0;
            if (!SAMP::SafeRead<WORD>(vehicle + GameAddr::ENTITY_MODEL_INDEX, model)) continue;
            if (model == targetModelId) { bestVehicle = vehicle; break; }
        }
        if (!bestVehicle) {
            // Vehicle not in GTA pool yet — still teleport ped to hardcoded spawn
            // so the F-press logic can work once the vehicle streams in
            Log("[TP_ABOVE] bote no en pool, usando coords fijas 718.7285,-1633.8752,0.7480");
            DWORD ped = GetPlayerPed();
            if (!ped || IsBadReadPtr((void*)ped, 0x600)) return 0;
            float topZ = 0.7480f + 2.0f;
            *(float*)(ped + GameAddr::POS_X_SIMPLE) = 718.7285f;
            *(float*)(ped + GameAddr::POS_Y_SIMPLE) = -1633.8752f;
            *(float*)(ped + GameAddr::POS_Z_SIMPLE) = topZ;
            DWORD* pM = (DWORD*)(ped + GameAddr::MATRIX_OFFSET);
            if (pM && *pM) {
                DWORD m = *pM;
                *(float*)(m + GameAddr::POS_X_MATRIX) = 718.7285f;
                *(float*)(m + GameAddr::POS_Y_MATRIX) = -1633.8752f;
                *(float*)(m + GameAddr::POS_Z_MATRIX) = topZ;
            }
            return 1; // non-zero = success (no real ptr but ped is placed)
        }

        // Read vehicle world position from GTA struct
        float vx = 0, vy = 0, vz = 0;
        SAMP::SafeRead<float>(bestVehicle + GameAddr::POS_X_SIMPLE, vx);
        SAMP::SafeRead<float>(bestVehicle + GameAddr::POS_Y_SIMPLE, vy);
        SAMP::SafeRead<float>(bestVehicle + GameAddr::POS_Z_SIMPLE, vz);

        // Fallback: if pool coords are invalid (0,0,0 or near-zero), use hardcoded spawn
        // Boat spawn: 715.9, -1699.5, 2.4
        if (vx == 0.0f && vy == 0.0f) {
            vx = 718.7285f;
            vy = -1633.8752f;
            vz = 0.7480f;
            Log("[TP_ABOVE] pos invalida en struct, usando coords fijas (%.1f,%.1f,%.1f)", vx, vy, vz);
        }

        // Position ped on top of vehicle roof (Z + 2.0f)
        DWORD ped = GetPlayerPed();
        if (!ped || IsBadReadPtr((void*)ped, 0x600)) return 0;
        float topZ = vz + 2.0f;

        *(float*)(ped + GameAddr::POS_X_SIMPLE) = vx;
        *(float*)(ped + GameAddr::POS_Y_SIMPLE) = vy;
        *(float*)(ped + GameAddr::POS_Z_SIMPLE) = topZ;
        DWORD* pPedMatrix = (DWORD*)(ped + GameAddr::MATRIX_OFFSET);
        if (pPedMatrix && *pPedMatrix) {
            DWORD m = *pPedMatrix;
            *(float*)(m + GameAddr::POS_X_MATRIX) = vx;
            *(float*)(m + GameAddr::POS_Y_MATRIX) = vy;
            *(float*)(m + GameAddr::POS_Z_MATRIX) = topZ;
        }

        Log("[TP_ABOVE] model=%d ptr=0x%X veh=(%.1f,%.1f,%.1f) ped z=%.1f",
            (int)targetModelId, bestVehicle, vx, vy, vz, topZ);
        return bestVehicle;
    }
}
