#pragma once
/**
 * GTA San Andreas Game Functions & Structures
 * Base game addresses for GTA SA 1.0 US
 */

#include <windows.h>
#include <cmath>

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
    
    // Checkpoint (GTA internal)
    constexpr DWORD CHECKPOINT_X        = 0xC7DEC8;   // Standard checkpoint position
    constexpr DWORD CHECKPOINT_Y        = 0xC7DECC;
    constexpr DWORD CHECKPOINT_Z        = 0xC7DED0;
    constexpr DWORD CHECKPOINT_SIZE     = 0xC7DED4;
    constexpr DWORD CHECKPOINT_ACTIVE   = 0xC7DEDC;
    
    // Race checkpoint (GTA internal)
    constexpr DWORD RACE_CP_X           = 0xC7DF00;
    constexpr DWORD RACE_CP_Y           = 0xC7DF04;
    constexpr DWORD RACE_CP_Z           = 0xC7DF08;
    constexpr DWORD RACE_CP_ACTIVE      = 0xC7DF30;
    
    // Vehicle speed
    constexpr DWORD VEHICLE_SPEED_X     = 0x44;       // CVehicle -> speedX
    constexpr DWORD VEHICLE_SPEED_Y     = 0x48;
    constexpr DWORD VEHICLE_SPEED_Z     = 0x4C;
    
    // Vehicle model
    constexpr DWORD ENTITY_MODEL_INDEX  = 0x22;       // CEntity -> nModelIndex (WORD)
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
        if (!pPed) return 0;
        return *pPed;
    }
    
    /**
     * Check if player is inside a vehicle
     */
    inline bool IsPlayerInVehicle() {
        DWORD ped = GetPlayerPed();
        if (!ped) return false;
        
        DWORD vehicle = *(DWORD*)(ped + GameAddr::VEHICLE_PTR_OFFSET);
        return vehicle != 0;
    }
    
    /**
     * Get current vehicle pointer
     */
    inline DWORD GetPlayerVehicle() {
        DWORD ped = GetPlayerPed();
        if (!ped) return 0;
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
     * WARNING: Zeroes velocity - easily detectable!
     */
    inline void TeleportVehicle(float x, float y, float z) {
        DWORD vehicle = GetPlayerVehicle();
        if (!vehicle) return;
        
        SetEntityPosition(vehicle, x, y, z);
        
        // Also zero out velocity to avoid weird physics
        *(float*)(vehicle + GameAddr::VEHICLE_SPEED_X) = 0.0f;
        *(float*)(vehicle + GameAddr::VEHICLE_SPEED_Y) = 0.0f;
        *(float*)(vehicle + GameAddr::VEHICLE_SPEED_Z) = 0.0f;
    }
    
    /**
     * Move the vehicle without touching velocity
     * Used by gradual movement system
     */
    inline void SetVehiclePosition(float x, float y, float z) {
        DWORD vehicle = GetPlayerVehicle();
        if (!vehicle) return;
        SetEntityPosition(vehicle, x, y, z);
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
     * Check if standard checkpoint is active
     */
    inline bool IsCheckpointActive() {
        return *(DWORD*)GameAddr::CHECKPOINT_ACTIVE != 0;
    }
    
    /**
     * Get standard checkpoint position
     */
    inline Vec3 GetCheckpointPosition() {
        Vec3 pos;
        pos.x = *(float*)GameAddr::CHECKPOINT_X;
        pos.y = *(float*)GameAddr::CHECKPOINT_Y;
        pos.z = *(float*)GameAddr::CHECKPOINT_Z;
        return pos;
    }
    
    /**
     * Check if race checkpoint is active
     */
    inline bool IsRaceCheckpointActive() {
        return *(DWORD*)GameAddr::RACE_CP_ACTIVE != 0;
    }
    
    /**
     * Get race checkpoint position
     */
    inline Vec3 GetRaceCheckpointPosition() {
        Vec3 pos;
        pos.x = *(float*)GameAddr::RACE_CP_X;
        pos.y = *(float*)GameAddr::RACE_CP_Y;
        pos.z = *(float*)GameAddr::RACE_CP_Z;
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
