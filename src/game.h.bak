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
    // Player / Ped
    constexpr DWORD PLAYER_PED_PTR      = 0xB6F5F0;   // CPed* pointer to local player
    constexpr DWORD PLAYER_CHAR_PTR     = 0xB7CD98;   // Char handle
    
    // Vehicle
    constexpr DWORD VEHICLE_PTR_OFFSET  = 0x58C;      // Offset in CPed to current CVehicle*
    
    // Position (in CPlaceable/CEntity matrix)
    constexpr DWORD MATRIX_OFFSET       = 0x14;       // CEntity -> CMatrix* 
    constexpr DWORD POS_X_MATRIX        = 0x30;       // CMatrix -> posX
    constexpr DWORD POS_Y_MATRIX        = 0x34;       // CMatrix -> posY
    constexpr DWORD POS_Z_MATRIX        = 0x38;       // CMatrix -> posZ
    
    // Simple position (when matrix is null)
    constexpr DWORD POS_X_SIMPLE        = 0x4;        // CPlaceable -> posX (no matrix)
    constexpr DWORD POS_Y_SIMPLE        = 0x8;
    constexpr DWORD POS_Z_SIMPLE        = 0xC;
    
    // Checkpoint Pools (Standard & Race)
    constexpr DWORD CHECKPOINT_ARRAY     = 0xC7DD58;   // CCheckpoint storage (32 items, 56 bytes each)
    constexpr DWORD RACE_CP_ARRAY        = 0xC7F158;   // CRaceCheckpoint storage (16 items)

    // Checkpoint Offsets (Standard CCheckpoint)
    constexpr DWORD CP_OFF_TYPE          = 0x0;        // short (Type)
    constexpr DWORD CP_OFF_ISUSED        = 0x2;        // bool (IsUsed)
    constexpr DWORD CP_OFF_POS           = 0xC;        // CVector (Position)
    constexpr DWORD CP_SIZE              = 56;         // Struct Size

    // Race Checkpoint Offsets (CRaceCheckpoint)
    constexpr DWORD RCP_OFF_TYPE         = 0x0;        // byte (Type)
    constexpr DWORD RCP_OFF_POS          = 0x4;        // CVector (Position)
    constexpr DWORD RCP_SIZE             = 40;         // Struct Size (approx/aligned)
    
    // Vehicle speed
    constexpr DWORD VEHICLE_SPEED_X     = 0x44;       // CVehicle -> speedX
    constexpr DWORD VEHICLE_SPEED_Y     = 0x48;
    constexpr DWORD VEHICLE_SPEED_Z     = 0x4C;
    
    // Vehicle model
    constexpr DWORD ENTITY_MODEL_INDEX  = 0x22;       // CEntity -> nModelIndex (WORD)
    
    // GTA SA internal functions (1.0 US)
    constexpr DWORD FUNC_FIND_GROUND_Z  = 0x569660;   // CWorld::FindGroundZForCoord(float x, float y)
}

// ============================================================
// Helper: Read game memory safely
// ============================================================
namespace Game {
    
    struct Vec3 {
        float x, y, z;
    };
    
    /**
     * Get pointer to local player CPed
     */
    inline DWORD GetPlayerPed() {
        DWORD* pPed = (DWORD*)GameAddr::PLAYER_PED_PTR;
        if (!pPed || IsBadReadPtr(pPed, 4)) return 0;
        return *pPed;
    }
    
    /**
     * Check if player is inside a vehicle
     */
    inline bool IsPlayerInVehicle() {
        DWORD ped = GetPlayerPed();
        if (!ped || IsBadReadPtr((void*)ped, 0x600)) return false; // Basic validation
        
        DWORD vehicle = *(DWORD*)(ped + GameAddr::VEHICLE_PTR_OFFSET);
        return vehicle != 0;
    }
    
    /**
     * Get current vehicle pointer
     */
    inline DWORD GetPlayerVehicle() {
        DWORD ped = GetPlayerPed();
        if (!ped || IsBadReadPtr((void*)ped, 0x600)) return 0;
        return *(DWORD*)(ped + GameAddr::VEHICLE_PTR_OFFSET);
    }
    
    /**
     * Get the model ID of the current vehicle
     */
    inline WORD GetVehicleModelId() {
        DWORD vehicle = GetPlayerVehicle();
        if (!vehicle) return 0;
        return *(WORD*)(vehicle + GameAddr::ENTITY_MODEL_INDEX);
    }
    
    /**
     * Get position of an entity (CPed or CVehicle)
     */
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
            // Fallback to simple position in CPlaceable
            pos.x = *(float*)(entity + GameAddr::POS_X_SIMPLE);
            pos.y = *(float*)(entity + GameAddr::POS_Y_SIMPLE);
            pos.z = *(float*)(entity + GameAddr::POS_Z_SIMPLE);
        }
        return pos;
    }
    
    /**
     * Get player position
     */
    inline Vec3 GetPlayerPosition() {
        return GetEntityPosition(GetPlayerPed());
    }
    
    /**
     * Set entity position (teleport)
     * This directly modifies the matrix - anticheat SHOULD detect this
     */
    inline void SetEntityPosition(DWORD entity, float x, float y, float z) {
        if (!entity) return;
        
        DWORD* pMatrix = (DWORD*)(entity + GameAddr::MATRIX_OFFSET);
        if (pMatrix && *pMatrix) {
            DWORD matrix = *pMatrix;
            *(float*)(matrix + GameAddr::POS_X_MATRIX) = x;
            *(float*)(matrix + GameAddr::POS_Y_MATRIX) = y;
            *(float*)(matrix + GameAddr::POS_Z_MATRIX) = z;
        }
    }
    
    /**
     * Teleport the vehicle the player is in
     * Moves BOTH vehicle AND player ped to keep camera/controls in sync.
     * Zeroes velocity only on final arrival to avoid freezing player controls.
     */
    inline void TeleportVehicle(float x, float y, float z) {
        DWORD vehicle = GetPlayerVehicle();
        if (!vehicle) return;
        
        // Move vehicle position
        SetEntityPosition(vehicle, x, y, z);
        
        // CRITICAL: Also move the player ped so the camera follows!
        // Without this, the camera stays at the old position and controls freeze.
        DWORD ped = GetPlayerPed();
        if (ped) {
            SetEntityPosition(ped, x, y, z);
        }
        
        // Zero out velocity to stop the vehicle
        *(float*)(vehicle + GameAddr::VEHICLE_SPEED_X) = 0.0f;
        *(float*)(vehicle + GameAddr::VEHICLE_SPEED_Y) = 0.0f;
        *(float*)(vehicle + GameAddr::VEHICLE_SPEED_Z) = 0.0f;
    }
    
    /**
     * Move the vehicle without touching velocity
     * Used by gradual/step movement - keeps velocity so physics don't fight us.
     * Also moves the player ped to keep camera in sync.
     */
    inline void SetVehiclePosition(float x, float y, float z) {
        DWORD vehicle = GetPlayerVehicle();
        if (!vehicle) return;
        
        SetEntityPosition(vehicle, x, y, z);
        
        // Also move the player ped so camera follows
        DWORD ped = GetPlayerPed();
        if (ped) {
            SetEntityPosition(ped, x, y, z);
        }
    }
    
    /**
     * Find the ground Z height at a given X/Y coordinate.
     * Uses GTA SA internal CWorld::FindGroundZForCoord.
     * Returns ground height, or fallback if function fails.
     */
    inline float FindGroundZ(float x, float y, float fallbackZ) {
        __try {
            typedef float(__cdecl* FindGroundZ_t)(float, float);
            FindGroundZ_t fn = (FindGroundZ_t)GameAddr::FUNC_FIND_GROUND_Z;
            float z = fn(x, y);
            // Sanity check: if result is way below or 0, use fallback
            if (z < -50.0f || z > 1000.0f) return fallbackZ;
            return z;
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            return fallbackZ;
        }
    }
    
    /**
     * Teleport vehicle + ped to X/Y and find correct ground Z.
     * Zeroes velocity. Used for step teleportation with terrain adaptation.
     */
    inline void TeleportVehicleToGround(float x, float y, float fallbackZ) {
        DWORD vehicle = GetPlayerVehicle();
        if (!vehicle) return;
        
        // Find ground height at destination, place vehicle 1.5 units above
        float groundZ = FindGroundZ(x, y, fallbackZ);
        float z = groundZ + 1.5f;
        
        SetEntityPosition(vehicle, x, y, z);
        
        DWORD ped = GetPlayerPed();
        if (ped) {
            SetEntityPosition(ped, x, y, z);
        }
        
        // Zero velocity to prevent bouncing/physics issues
        *(float*)(vehicle + GameAddr::VEHICLE_SPEED_X) = 0.0f;
        *(float*)(vehicle + GameAddr::VEHICLE_SPEED_Y) = 0.0f;
        *(float*)(vehicle + GameAddr::VEHICLE_SPEED_Z) = 0.0f;
    }
    
    /**
     * Set vehicle velocity vector
     * Used to simulate realistic movement (anticheat checks velocity vs position delta)
     */
    inline void SetVehicleVelocity(float vx, float vy, float vz) {
        DWORD vehicle = GetPlayerVehicle();
        if (!vehicle) return;
        *(float*)(vehicle + GameAddr::VEHICLE_SPEED_X) = vx;
        *(float*)(vehicle + GameAddr::VEHICLE_SPEED_Y) = vy;
        *(float*)(vehicle + GameAddr::VEHICLE_SPEED_Z) = vz;
    }
    
    /**
     * Check if standard checkpoint is active (Iterates pool)
     */
    inline bool IsCheckpointActive() {
        // Try SA-MP first
        if (SAMP::IsInitialized()) {
            stCheckpoint* pCP = SAMP::GetCurrentCheckpoint();
            if (pCP && pCP->bActive) return true;
        }

        for (int i = 0; i < 32; i++) {
            DWORD cp = GameAddr::CHECKPOINT_ARRAY + (i * GameAddr::CP_SIZE);
            // Check m_bIsUsed (offset 0x2)
            if (*(bool*)(cp + GameAddr::CP_OFF_ISUSED)) {
                return true;
            }
        }
        return false;
    }
    
    /**
     * Get standard checkpoint position (First active)
     */
    inline Vec3 GetCheckpointPosition() {
        Vec3 pos = {0, 0, 0};
        
        // Try SA-MP first
        if (SAMP::IsInitialized()) {
             stCheckpoint* pCP = SAMP::GetCurrentCheckpoint();
             if (pCP && pCP->bActive) {
                 return { pCP->fX, pCP->fY, pCP->fZ };
             }
        }

        for (int i = 0; i < 32; i++) {
            DWORD cp = GameAddr::CHECKPOINT_ARRAY + (i * GameAddr::CP_SIZE);
            if (*(bool*)(cp + GameAddr::CP_OFF_ISUSED)) {
                pos.x = *(float*)(cp + GameAddr::CP_OFF_POS);
                pos.y = *(float*)(cp + GameAddr::CP_OFF_POS + 4);
                pos.z = *(float*)(cp + GameAddr::CP_OFF_POS + 8);
                return pos;
            }
        }
        return pos;
    }
    
    /**
     * Check if race checkpoint is active (Iterates pool)
     */
    inline bool IsRaceCheckpointActive() {
        // Try SA-MP first
        if (SAMP::IsInitialized()) {
            stRaceCheckpoint* pCP = SAMP::GetRaceCheckpoint();
            if (pCP && pCP->bActive) return true;
        }

        for (int i = 0; i < 16; i++) {
            DWORD cp = GameAddr::RACE_CP_ARRAY + (i * GameAddr::RCP_SIZE);
            // Check Type != 0
            if (*(BYTE*)(cp + GameAddr::RCP_OFF_TYPE) != 0) {
                return true;
            }
        }
        return false;
    }
    
    /**
     * Get race checkpoint position (First active)
     */
    inline Vec3 GetRaceCheckpointPosition() {
        Vec3 pos = {0, 0, 0};
        
        // Try SA-MP first
        if (SAMP::IsInitialized()) {
             stRaceCheckpoint* pCP = SAMP::GetRaceCheckpoint();
             if (pCP && pCP->bActive) {
                 return { pCP->fX, pCP->fY, pCP->fZ };
             }
        }

        for (int i = 0; i < 16; i++) {
            DWORD cp = GameAddr::RACE_CP_ARRAY + (i * GameAddr::RCP_SIZE);
            if (*(BYTE*)(cp + GameAddr::RCP_OFF_TYPE) != 0) {
                pos.x = *(float*)(cp + GameAddr::RCP_OFF_POS);
                pos.y = *(float*)(cp + GameAddr::RCP_OFF_POS + 4);
                pos.z = *(float*)(cp + GameAddr::RCP_OFF_POS + 8);
                return pos;
            }
        }
        return pos;
    }
    
    /**
     * Distance between two 3D points
     */
    inline float Distance3D(Vec3 a, Vec3 b) {
        float dx = a.x - b.x;
        float dy = a.y - b.y;
        float dz = a.z - b.z;
        return sqrtf(dx*dx + dy*dy + dz*dz);
    }
    
    /**
     * Add message to SA-MP chat (wrapper)
     * Uses SAMP::AddChatMessage internally
     */
    void AddChatMessage(DWORD color, const char* text);

    /**
     * Log to file for debugging
     */
    void Log(const char* fmt, ...);
}
