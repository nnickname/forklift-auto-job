#pragma once
/**
 * GTA San Andreas (1.0 US) Game Functions
 *
 * Minimal GTA SA direct-memory helpers.  All teleport, vehicle entry, and
 * camera functions now go through SAMP-API (see samp.h).
 * This file only keeps position queries, vehicle detection, and key input.
 */

#include <windows.h>
#include <cmath>
#include "samp.h"

// ════════════════════════════════════════════════════════════
// GTA SA Memory Addresses (1.0 US)
// ════════════════════════════════════════════════════════════
namespace GameAddr {
    constexpr DWORD PLAYER_PED_PTR  = 0xB6F5F0;  // CPed* pointer
    constexpr DWORD VEHICLE_PTR     = 0x58C;      // CPed → CVehicle*
    constexpr DWORD MATRIX_PTR     = 0x14;       // CEntity → CMatrix*
    constexpr DWORD MAT_POS_X      = 0x30;
    constexpr DWORD MAT_POS_Y      = 0x34;
    constexpr DWORD MAT_POS_Z      = 0x38;
    constexpr DWORD SIMPLE_X       = 0x04;       // CPlaceable raw coords
    constexpr DWORD SIMPLE_Y       = 0x08;
    constexpr DWORD SIMPLE_Z       = 0x0C;
    constexpr DWORD MODEL_INDEX    = 0x22;       // CEntity → WORD nModelIndex
    constexpr DWORD SPEED_X        = 0x44;       // CVehicle velocity
    constexpr DWORD SPEED_Y        = 0x48;
    constexpr DWORD SPEED_Z        = 0x4C;
    constexpr DWORD VEH_DRIVER     = 0x460;      // CVehicle → CPed* pDriver
}

// ════════════════════════════════════════════════════════════
// Vec3
// ════════════════════════════════════════════════════════════
struct Vec3 { float x, y, z; };

// ════════════════════════════════════════════════════════════
// Safe memory read (for GTA SA addresses)
// ════════════════════════════════════════════════════════════
template <typename T>
inline bool SafeRead(DWORD addr, T& out) {
    if (!addr) return false;
    __try {
        if (IsBadReadPtr((void*)addr, sizeof(T))) return false;
        out = *(T*)addr;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// ════════════════════════════════════════════════════════════
// Game Functions
// ════════════════════════════════════════════════════════════
namespace Game {

    // Implemented in game.cpp
    void Log(const char* fmt, ...);
    void AddChatMessage(DWORD color, const char* text);

    // ── Player & Entity ──

    inline DWORD GetPlayerPed() {
        DWORD* p = (DWORD*)GameAddr::PLAYER_PED_PTR;
        if (!p || IsBadReadPtr(p, 4)) return 0;
        return *p;
    }

    inline DWORD GetPlayerVehicle() {
        DWORD ped = GetPlayerPed();
        if (!ped || IsBadReadPtr((void*)ped, 0x600)) return 0;
        return *(DWORD*)(ped + GameAddr::VEHICLE_PTR);
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
        return p;
    }

    inline Vec3 GetPlayerPos() { return GetPosition(GetPlayerPed()); }

    // ── Vehicle operations (stabilize only — teleport via SAMP-API) ──

    inline void StabilizeVehicle() {
        DWORD veh = GetPlayerVehicle();
        if (!veh) return;
        *(float*)(veh + GameAddr::SPEED_X) = 0.0f;
        *(float*)(veh + GameAddr::SPEED_Y) = 0.0f;
        *(float*)(veh + GameAddr::SPEED_Z) = 0.0f;
    }

    // Clean exit from vehicle (clear GTA + SAMP state)
    inline void ForceExitVehicle() {
        // Use SAMP-API CPed::ExitVehicle if possible
        auto* ped = SAMP::GetPlayerPed();
        if (ped) {
            __try { ped->ExitVehicle(); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        // Also clear pointers manually as fallback
        DWORD gtaPed = GetPlayerPed();
        DWORD veh = GetPlayerVehicle();
        if (gtaPed && veh) {
            *(DWORD*)(gtaPed + 0x58C) = 0;
            *(DWORD*)(veh + GameAddr::VEH_DRIVER) = 0;
        }
        auto* lp = SAMP::GetLocalPlayer();
        if (lp) {
            __try { lp->m_nCurrentVehicle = 0xFFFF; }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
    }

    // ── Utility ──

    inline float Distance(Vec3 a, Vec3 b) {
        float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
        return sqrtf(dx*dx + dy*dy + dz*dz);
    }

    inline void PressKey(BYTE vk) {
        INPUT inp = {};
        inp.type = INPUT_KEYBOARD;
        inp.ki.wVk = vk;
        inp.ki.wScan = (BYTE)MapVirtualKey(vk, MAPVK_VK_TO_VSC);
        inp.ki.dwFlags = 0;
        SendInput(1, &inp, sizeof(INPUT));
    }

    inline void ReleaseKey(BYTE vk) {
        INPUT inp = {};
        inp.type = INPUT_KEYBOARD;
        inp.ki.wVk = vk;
        inp.ki.wScan = (BYTE)MapVirtualKey(vk, MAPVK_VK_TO_VSC);
        inp.ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(1, &inp, sizeof(INPUT));
    }
}
