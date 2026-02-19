#pragma once
/**
 * SA-MP 0.3.DL-1 Interface — powered by SAMP-API
 *
 * All struct layouts and function addresses resolved by the SAMP-API library
 * from https://github.com/BlastHackNet/SAMP-API (multiver branch, 0.3.DL-1).
 *
 * No manual offset arithmetic — we use typed pointers and member access.
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

    // ── Vehicle ID ──

    inline WORD GetVehicleID() {
        auto* lp = GetLocalPlayer();
        if (!lp) return 0xFFFF;
        __try { return lp->m_nCurrentVehicle; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0xFFFF; }
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

    // ── Camera restoration ──

    inline void RestoreCamera() {
        auto* cg = GetCGame();
        if (!cg) return;
        __try {
            auto* cam = cg->m_pCamera;
            if (cam) {
                cam->m_pAttachedTo = nullptr;   // break TakeControl attachment
                cam->Restore();                 // call samp.dll CCamera::Restore
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}

        // Clear spectating flag on local player
        auto* lp = GetLocalPlayer();
        if (lp) {
            __try { lp->m_bDoesSpectating = 0; }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
    }

    // ── Vehicle pool helpers (direct field access, no samp.dll calls) ──

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
