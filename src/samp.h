#pragma once
/**
 * SA-MP 0.3.DL-1 Interface — powered by SAMP-API
 *
 * All struct layouts and function addresses resolved by the SAMP-API library
 * from https://github.com/BlastHackNet/SAMP-API (multiver branch, 0.3.DL-1).
 *
 * Uses SAMP-API's native Teleport / PutIntoVehicle / Camera functions
 * instead of raw GTA memory writes — this avoids camera lock bugs.
 */

#include <windows.h>

// ── SAMP-API headers ──
#include "sampapi/sampapi.h"
#include "sampapi/CVector.h"
#include "sampapi/0.3.DL-1/CNetGame.h"
#include "sampapi/0.3.DL-1/CGame.h"
#include "sampapi/0.3.DL-1/CChat.h"
#include "sampapi/0.3.DL-1/CInput.h"
#include "sampapi/0.3.DL-1/CCamera.h"
#include "sampapi/0.3.DL-1/CPlayerPool.h"
#include "sampapi/0.3.DL-1/CVehiclePool.h"
#include "sampapi/0.3.DL-1/CLocalPlayer.h"
#include "sampapi/0.3.DL-1/CPed.h"
#include "sampapi/0.3.DL-1/CVehicle.h"
#include "sampapi/0.3.DL-1/CEntity.h"
#include "sampapi/0.3.DL-1/Synchronization.h"

// ── Convenience alias for the 0.3.DL-1 namespace ──
namespace sapi = sampapi::v03dl;

// ════════════════════════════════════════════════════════════
// SA-MP Accessor Functions (SEH protected)
// ════════════════════════════════════════════════════════════
namespace SAMP {

    // ── Core accessors ──

    inline bool IsLoaded() {
        return sampapi::GetBase() != 0;
    }

    inline sapi::CNetGame* GetNetGame() {
        __try { return sapi::RefNetGame(); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    inline bool IsConnected() {
        auto* ng = GetNetGame();
        if (!ng) return false;
        __try {
            return ng->m_nGameState == sapi::CNetGame::GAME_MODE_CONNECTED
                && ng->m_pPools != nullptr;
        } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    inline void* GetRakClient() {
        auto* ng = GetNetGame();
        if (!ng) return nullptr;
        __try { return (void*)ng->m_pRakClient; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    inline sapi::CPlayerPool* GetPlayerPool() {
        auto* ng = GetNetGame();
        if (!ng) return nullptr;
        __try {
            auto* pools = ng->m_pPools;
            return pools ? pools->m_pPlayer : nullptr;
        } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    inline sapi::CLocalPlayer* GetLocalPlayer() {
        auto* pp = GetPlayerPool();
        if (!pp) return nullptr;
        __try { return pp->pLocalPlayer; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    inline sapi::CGame* GetCGame() {
        __try { return sapi::RefGame(); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    inline sapi::CChat* GetChat() {
        __try { return sapi::RefChat(); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    inline sapi::CInput* GetInput() {
        __try { return sapi::RefInputBox(); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    inline sapi::CVehiclePool* GetVehiclePool() {
        auto* ng = GetNetGame();
        if (!ng) return nullptr;
        __try {
            auto* pools = ng->m_pPools;
            return pools ? pools->m_pVehicle : nullptr;
        } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    // ── SAMP Ped (local player as CPed) ──

    inline sapi::CPed* GetPlayerPed() {
        auto* cg = GetCGame();
        if (!cg) return nullptr;
        __try { return cg->GetPlayerPed(); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    // ── Vehicle ID ──

    inline WORD GetVehicleID() {
        auto* lp = GetLocalPlayer();
        if (!lp) return 0xFFFF;
        __try { return lp->m_nCurrentVehicle; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0xFFFF; }
    }

    // ── Get SAMP CVehicle* by SAMP ID ──

    inline sapi::CVehicle* GetSAMPVehicle(WORD id) {
        auto* vp = GetVehiclePool();
        if (!vp || id >= 2000) return nullptr;
        __try {
            if (!vp->m_bNotEmpty[id]) return nullptr;
            return vp->m_pObject[id];
        } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    // ── Checkpoint queries (read CGame struct fields) ──

    inline bool IsCheckpointActive() {
        auto* cg = GetCGame();
        if (!cg) return false;
        __try { return cg->m_checkpoint.m_bEnabled != 0; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    inline bool GetCheckpointPos(float& x, float& y, float& z) {
        auto* cg = GetCGame();
        if (!cg) return false;
        __try {
            x = cg->m_checkpoint.m_position.x;
            y = cg->m_checkpoint.m_position.y;
            z = cg->m_checkpoint.m_position.z;
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    inline bool IsRaceCheckpointActive() {
        auto* cg = GetCGame();
        if (!cg) return false;
        __try { return cg->m_racingCheckpoint.m_bEnabled != 0; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    inline bool GetRaceCheckpointPos(float& x, float& y, float& z) {
        auto* cg = GetCGame();
        if (!cg) return false;
        __try {
            x = cg->m_racingCheckpoint.m_currentPosition.x;
            y = cg->m_racingCheckpoint.m_currentPosition.y;
            z = cg->m_racingCheckpoint.m_currentPosition.z;
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    // ── Chat & Commands (SAMP-API member function calls) ──

    inline void AddChatMessage(DWORD color, const char* text) {
        auto* chat = GetChat();
        if (!chat) return;
        __try { chat->AddMessage(color, text); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    inline void SendChat(const char* text) {
        auto* input = GetInput();
        if (!input) return;
        __try { input->Send(text); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    // ════════════════════════════════════════════════════════
    // CAMERA — uses SAMP-API CCamera native functions
    // ════════════════════════════════════════════════════════

    // Full camera restore: detach + restore + SetToOwner (snaps back to player)
    inline void RestoreCamera() {
        auto* cg = GetCGame();
        if (!cg) return;
        __try {
            auto* cam = cg->m_pCamera;
            if (cam) {
                cam->m_pAttachedTo = nullptr;
                cam->Detach();
                cam->Restore();
                cam->SetToOwner();  // <-- KEY: snaps camera behind player
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}

        // Clear spectating flag
        auto* lp = GetLocalPlayer();
        if (lp) {
            __try { lp->m_bDoesSpectating = 0; }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
    }

    // ════════════════════════════════════════════════════════
    // TELEPORT — uses SAMP-API CEntity::Teleport + CGame::RefreshRenderer
    // These go through samp.dll and properly handle camera/world state
    // ════════════════════════════════════════════════════════

    // Teleport a SAMP vehicle (by SAMP ID) using samp.dll's native Teleport
    inline bool TeleportVehicle(WORD vehicleId, float x, float y, float z) {
        auto* sv = GetSAMPVehicle(vehicleId);
        if (!sv) return false;
        __try {
            // Zero velocity via SAMP-API
            sampapi::CVector zero = {0.0f, 0.0f, 0.0f};
            sv->SetSpeed(zero);
            sv->SetTurnSpeed(zero);
            // Teleport through samp.dll (handles camera/rendering internally)
            sampapi::CVector pos = {x, y, z};
            sv->Teleport(pos);
        } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }

        // Also teleport the ped (in case it desyncs)
        auto* ped = GetPlayerPed();
        if (ped) {
            __try {
                sampapi::CVector pos = {x, y, z};
                ped->Teleport(pos);
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }

        // Refresh world renderer at new position
        auto* cg = GetCGame();
        if (cg) {
            __try { cg->RefreshRenderer(x, y); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }

        // Restore camera to player
        RestoreCamera();
        return true;
    }

    // Teleport just the ped (on foot) using samp.dll's native Teleport
    inline bool TeleportPed(float x, float y, float z) {
        auto* ped = GetPlayerPed();
        if (!ped) return false;
        __try {
            sampapi::CVector pos = {x, y, z};
            ped->Teleport(pos);
        } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }

        auto* cg = GetCGame();
        if (cg) {
            __try { cg->RefreshRenderer(x, y); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        RestoreCamera();
        return true;
    }

    // ════════════════════════════════════════════════════════
    // VEHICLE ENTRY — uses SAMP-API CPed::PutIntoVehicle
    // ════════════════════════════════════════════════════════

    // Put player into vehicle using samp.dll's native function
    inline bool PutIntoVehicle(WORD vehicleId) {
        auto* vp = GetVehiclePool();
        auto* ped = GetPlayerPed();
        if (!vp || !ped) return false;

        __try {
            // Get the GTAREF handle for this vehicle (GTAREF is int)
            int ref = vp->GetRef((int)vehicleId);
            if (!ref) return false;
            ped->PutIntoVehicle(ref, 0); // seat 0 = driver
        } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }

        // Patch SAMP local player vehicle state
        auto* lp = GetLocalPlayer();
        if (lp) {
            __try {
                lp->m_nCurrentVehicle = vehicleId;
                lp->m_incarData.m_nVehicle = vehicleId;
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        return true;
    }

    // Force rotation using samp.dll's CPed::ForceRotation
    inline void SetPedRotation(float angleDeg) {
        auto* ped = GetPlayerPed();
        if (!ped) return;
        float rad = angleDeg * 3.14159265f / 180.0f;
        __try { ped->ForceRotation(rad); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    // ── Vehicle pool helpers ──

    inline DWORD GetGTAVehicle(WORD id) {
        auto* vp = GetVehiclePool();
        if (!vp || id >= 2000) return 0;
        __try {
            if (!vp->m_bNotEmpty[id]) return 0;
            return (DWORD)vp->m_pGameObject[id];
        } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }

    inline WORD FindVehicleID(DWORD gtaPtr) {
        if (!gtaPtr) return 0xFFFF;
        auto* vp = GetVehiclePool();
        if (!vp) return 0xFFFF;
        __try {
            for (WORD i = 0; i < 2000; i++) {
                if (vp->m_bNotEmpty[i] && (DWORD)vp->m_pGameObject[i] == gtaPtr)
                    return i;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        return 0xFFFF;
    }

    // ── Local player field patching (for vehicle sync) ──

    inline void PatchVehicleID(WORD sampId) {
        auto* lp = GetLocalPlayer();
        if (!lp) return;
        __try {
            lp->m_nCurrentVehicle = sampId;
            lp->m_incarData.m_nVehicle = sampId;
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
}
