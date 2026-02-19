#pragma once
/**
 * GTA San Andreas (1.0 US) Game Functions
 *
 * GTA SA addresses are NOT part of SAMP-API — we access them directly.
 * SAMP-related state updates use the typed pointers from samp.h.
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
    constexpr DWORD TURN_X         = 0x50;       // CVehicle angular velocity
    constexpr DWORD TURN_Y         = 0x54;
    constexpr DWORD TURN_Z         = 0x58;
    constexpr DWORD VEH_DRIVER     = 0x460;      // CVehicle → CPed* pDriver
    constexpr DWORD GTA_VEH_POOL   = 0xB74494;   // CPool<CVehicle>*
    constexpr DWORD CVEHICLE_SIZE  = 0x5DC;       // sizeof(CVehicle)

    // GTA SA Camera (TheCamera instance + functions)
    constexpr DWORD CAMERA_OBJ     = 0xB6F028;
    constexpr DWORD FN_CAM_RESTORE = 0x50B930;   // CCamera::Restore()
    constexpr DWORD FN_CAM_JUMPCUT = 0x50BAB0;   // CCamera::RestoreWithJumpCut()

    // CPed::WarpPedIntoCar(CVehicle*)
    constexpr DWORD FN_WARP_INTO_CAR = 0x4EF8B0;
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

    inline void SetPosition(DWORD entity, float x, float y, float z) {
        if (!entity) return;
        *(float*)(entity + GameAddr::SIMPLE_X) = x;
        *(float*)(entity + GameAddr::SIMPLE_Y) = y;
        *(float*)(entity + GameAddr::SIMPLE_Z) = z;
        DWORD* pMat = (DWORD*)(entity + GameAddr::MATRIX_PTR);
        if (pMat && *pMat) {
            DWORD m = *pMat;
            *(float*)(m + GameAddr::MAT_POS_X) = x;
            *(float*)(m + GameAddr::MAT_POS_Y) = y;
            *(float*)(m + GameAddr::MAT_POS_Z) = z;
        }
    }

    // ── Vehicle operations ──

    inline void TeleportVehicle(float x, float y, float z) {
        DWORD veh = GetPlayerVehicle();
        if (!veh) return;
        // Zero all velocity
        *(float*)(veh + GameAddr::SPEED_X) = 0.0f;
        *(float*)(veh + GameAddr::SPEED_Y) = 0.0f;
        *(float*)(veh + GameAddr::SPEED_Z) = 0.0f;
        *(float*)(veh + GameAddr::TURN_X)  = 0.0f;
        *(float*)(veh + GameAddr::TURN_Y)  = 0.0f;
        *(float*)(veh + GameAddr::TURN_Z)  = 0.0f;
        // Position vehicle + ped
        SetPosition(veh, x, y, z);
        DWORD ped = GetPlayerPed();
        if (ped) SetPosition(ped, x, y, z);
        // Reset rotation to identity (flat/upright)
        DWORD* pMat = (DWORD*)(veh + GameAddr::MATRIX_PTR);
        if (pMat && *pMat) {
            DWORD m = *pMat;
            *(float*)(m+0x00)=1; *(float*)(m+0x04)=0; *(float*)(m+0x08)=0;
            *(float*)(m+0x10)=0; *(float*)(m+0x14)=1; *(float*)(m+0x18)=0;
            *(float*)(m+0x20)=0; *(float*)(m+0x24)=0; *(float*)(m+0x28)=1;
        }
    }

    inline void StabilizeVehicle() {
        DWORD veh = GetPlayerVehicle();
        if (!veh) return;
        *(float*)(veh + GameAddr::SPEED_X) = 0.0f;
        *(float*)(veh + GameAddr::SPEED_Y) = 0.0f;
        *(float*)(veh + GameAddr::SPEED_Z) = 0.0f;
        *(float*)(veh + GameAddr::TURN_X)  = 0.0f;
        *(float*)(veh + GameAddr::TURN_Y)  = 0.0f;
        *(float*)(veh + GameAddr::TURN_Z)  = 0.0f;
    }

    inline void SetHeading(float angleDeg) {
        DWORD ped = GetPlayerPed();
        if (!ped || IsBadReadPtr((void*)ped, 0x600)) return;
        float rad = angleDeg * 3.14159265f / 180.0f;
        float s = sinf(rad), c = cosf(rad);
        DWORD* pMat = (DWORD*)(ped + GameAddr::MATRIX_PTR);
        if (pMat && *pMat) {
            DWORD m = *pMat;
            *(float*)(m+0x00)=c;  *(float*)(m+0x04)=-s; *(float*)(m+0x08)=0;
            *(float*)(m+0x10)=s;  *(float*)(m+0x14)=c;  *(float*)(m+0x18)=0;
            *(float*)(m+0x20)=0;  *(float*)(m+0x24)=0;  *(float*)(m+0x28)=1;
        }
        if (!IsBadWritePtr((void*)(ped + 0x558), 8)) {
            *(float*)(ped + 0x558) = rad; // m_fCurrentRotation
            *(float*)(ped + 0x55C) = rad; // m_fTargetRotation
        }
    }

    // ── Camera ──

    inline void RestoreGTACamera() {
        __try {
            void* cam = (void*)GameAddr::CAMERA_OBJ;
            typedef void(__thiscall* Fn)(void*);
            Fn fn1 = (Fn)GameAddr::FN_CAM_RESTORE;
            Fn fn2 = (Fn)GameAddr::FN_CAM_JUMPCUT;
            if (!IsBadCodePtr((FARPROC)fn1)) fn1(cam);
            if (!IsBadCodePtr((FARPROC)fn2)) fn2(cam);
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    inline void FullCameraRestore() {
        SAMP::RestoreCamera();
        RestoreGTACamera();
    }

    // ── Vehicle warp ──

    inline DWORD FindGTAVehicleByModel(WORD modelId) {
        DWORD pPool = 0;
        if (!SafeRead(GameAddr::GTA_VEH_POOL, pPool) || !pPool) return 0;
        if (IsBadReadPtr((void*)pPool, 0x10)) return 0;
        DWORD objects = 0, flags = 0; int cap = 0;
        SafeRead(pPool + 0x00, objects);
        SafeRead(pPool + 0x04, flags);
        SafeRead(pPool + 0x08, cap);
        if (!objects || !flags || cap <= 0 || cap > 5000) return 0;
        for (int i = 0; i < cap; i++) {
            BYTE flag = 0;
            if (!SafeRead(flags + (DWORD)i, flag)) continue;
            if (flag & 0x80) continue; // free slot
            DWORD veh = objects + i * GameAddr::CVEHICLE_SIZE;
            if (IsBadReadPtr((void*)veh, 0x100)) continue;
            WORD model = 0;
            SafeRead(veh + GameAddr::MODEL_INDEX, model);
            if (model == modelId) return veh;
        }
        return 0;
    }

    inline bool WarpIntoVehicle(DWORD gtaVeh) {
        if (!gtaVeh || IsBadReadPtr((void*)gtaVeh, 0x470)) return false;
        DWORD ped = GetPlayerPed();
        if (!ped) return false;

        // TP ped to vehicle
        Vec3 vpos = GetPosition(gtaVeh);
        SetPosition(ped, vpos.x, vpos.y, vpos.z + 0.3f);

        // Call CPed::WarpPedIntoCar (with manual fallback)
        __try {
            typedef void(__thiscall* Fn)(DWORD, DWORD);
            Fn fn = (Fn)GameAddr::FN_WARP_INTO_CAR;
            if (!IsBadCodePtr((FARPROC)fn)) fn(ped, gtaVeh);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            *(DWORD*)(ped + 0x58C) = gtaVeh;
            *(DWORD*)(gtaVeh + GameAddr::VEH_DRIVER) = ped;
        }

        // Patch SA-MP vehicle state via typed pointers
        WORD sampId = SAMP::FindVehicleID(gtaVeh);
        if (sampId != 0xFFFF) {
            SAMP::PatchVehicleID(sampId);
        }
        return true;
    }

    // Clean exit from vehicle (clear GTA + SAMP state)
    inline void ForceExitVehicle() {
        DWORD ped = GetPlayerPed();
        DWORD veh = GetPlayerVehicle();
        if (ped && veh) {
            *(DWORD*)(ped + 0x58C) = 0;
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
