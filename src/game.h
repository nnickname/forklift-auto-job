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
    constexpr DWORD FUNC_FIND_GROUND_Z  = 0x569660;
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

    inline void TeleportVehicle(float x, float y, float z) {
        DWORD vehicle = GetPlayerVehicle();
        if (!vehicle) return;
        SetEntityPosition(vehicle, x, y, z);
        DWORD ped = GetPlayerPed();
        if (ped) SetEntityPosition(ped, x, y, z);
        *(float*)(vehicle + GameAddr::VEHICLE_SPEED_X) = 0.0f;
        *(float*)(vehicle + GameAddr::VEHICLE_SPEED_Y) = 0.0f;
        *(float*)(vehicle + GameAddr::VEHICLE_SPEED_Z) = 0.0f;
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

    inline float Distance3D(Vec3 a, Vec3 b) {
        float dx = a.x - b.x;
        float dy = a.y - b.y;
        float dz = a.z - b.z;
        return sqrtf(dx*dx + dy*dy + dz*dz);
    }

    // Chat & logging
    void AddChatMessage(DWORD color, const char* text);
    void Log(const char* fmt, ...);
}
