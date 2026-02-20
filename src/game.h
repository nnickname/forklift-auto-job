#pragma once
/**
 * GTA San Andreas (1.0 US) — Minimal helpers.
 *
 * Position queries + vehicle detection via GTA memory.
 * Everything else goes through SAMP-API (samp.h).
 * No PressKey/ReleaseKey — use SAMP::SetPedKeys() instead.
 */

#include <windows.h>
#include <cmath>
#include "samp.h"

// ════════════════════════════════════════════════════════════
// GTA SA Memory Addresses (1.0 US)
// ════════════════════════════════════════════════════════════
namespace GameAddr {
    constexpr DWORD PLAYER_PED_PTR = 0xB6F5F0;
    constexpr DWORD VEHICLE_PTR    = 0x58C;
    constexpr DWORD MATRIX_PTR    = 0x14;
    constexpr DWORD MAT_POS_X     = 0x30;
    constexpr DWORD MAT_POS_Y     = 0x34;
    constexpr DWORD MAT_POS_Z     = 0x38;
    constexpr DWORD SIMPLE_X      = 0x04;
    constexpr DWORD SIMPLE_Y      = 0x08;
    constexpr DWORD SIMPLE_Z      = 0x0C;
    constexpr DWORD MODEL_INDEX   = 0x22;
}

struct Vec3 { float x, y, z; };

// ════════════════════════════════════════════════════════════
// Game namespace
// ════════════════════════════════════════════════════════════
namespace Game {

    // Implemented in game.cpp
    void Log(const char* fmt, ...);
    void AddChatMessage(DWORD color, const char* text);

    // ── Player queries ──

    inline DWORD GetPlayerPed() {
        __try {
            DWORD* p = (DWORD*)GameAddr::PLAYER_PED_PTR;
            if (!p) return 0;
            return *p;
        } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }

    inline DWORD GetPlayerVehicle() {
        DWORD ped = GetPlayerPed();
        if (!ped) return 0;
        __try {
            return *(DWORD*)(ped + GameAddr::VEHICLE_PTR);
        } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }

    inline bool IsInVehicle() { return GetPlayerVehicle() != 0; }

    inline WORD GetVehicleModel() {
        DWORD veh = GetPlayerVehicle();
        if (!veh) return 0;
        return *(WORD*)(veh + GameAddr::MODEL_INDEX);
    }

    inline Vec3 GetPosition(DWORD entity) {
        Vec3 p = {0, 0, 0};
        if (!entity) return p;
        __try {
            DWORD* pMat = (DWORD*)(entity + GameAddr::MATRIX_PTR);
            if (pMat && *pMat) {
                DWORD m = *pMat;
                p.x = *(float*)(m + GameAddr::MAT_POS_X);
                p.y = *(float*)(m + GameAddr::MAT_POS_Y);
                p.z = *(float*)(m + GameAddr::MAT_POS_Z);
            } else {
                p.x = *(float*)(entity + GameAddr::SIMPLE_X);
                p.y = *(float*)(entity + GameAddr::SIMPLE_Y);
                p.z = *(float*)(entity + GameAddr::SIMPLE_Z);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        return p;
    }

    inline Vec3 GetPlayerPos() { return GetPosition(GetPlayerPed()); }

    inline float Distance(Vec3 a, Vec3 b) {
        float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
        return sqrtf(dx*dx + dy*dy + dz*dz);
    }
}
