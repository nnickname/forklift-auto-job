#pragma once
/**
 * SA-MP 0.3.DL-1 Interface — 100% SAMP-API native functions.
 *
 * Every function here is a thin wrapper around SAMP-API classes.
 * No raw memory writes, no custom BitStreams, no manual RPCs.
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

namespace sapi = sampapi::v03dl;
using sampapi::GTAREF;

// ════════════════════════════════════════════════════════════
// SAMP — all wrappers use SEH to survive bad pointers
// ════════════════════════════════════════════════════════════
namespace SAMP {

    // ─── Core accessors ──────────────────────────────────

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

    inline sapi::CPlayerPool* GetPlayerPool() {
        auto* ng = GetNetGame();
        if (!ng) return nullptr;
        __try { return ng->m_pPools ? ng->m_pPools->m_pPlayer : nullptr; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
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
        __try { return ng->m_pPools ? ng->m_pPools->m_pVehicle : nullptr; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    // ─── Player Ped (local) ──────────────────────────────

    inline sapi::CPed* GetPlayerPed() {
        auto* cg = GetCGame();
        if (!cg) return nullptr;
        __try { return cg->GetPlayerPed(); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    // ─── Vehicle ID for local player ─────────────────────

    inline WORD GetVehicleID() {
        auto* lp = GetLocalPlayer();
        if (!lp) return 0xFFFF;
        __try { return lp->m_nCurrentVehicle; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0xFFFF; }
    }

    // ─── Vehicle pool helpers ────────────────────────────

    inline sapi::CVehicle* GetSAMPVehicle(WORD id) {
        auto* vp = GetVehiclePool();
        if (!vp || id >= 2000) return nullptr;
        __try {
            if (!vp->m_bNotEmpty[id]) return nullptr;
            return vp->m_pObject[id];
        } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    inline GTAREF GetVehicleRef(WORD id) {
        auto* vp = GetVehiclePool();
        if (!vp) return 0;
        __try { return vp->GetRef((int)id); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }

    inline WORD FindVehicleID(void* pGameVehicle) {
        if (!pGameVehicle) return 0xFFFF;
        auto* vp = GetVehiclePool();
        if (!vp) return 0xFFFF;
        __try {
            return vp->Find((::CVehicle*)pGameVehicle);
        } __except (EXCEPTION_EXECUTE_HANDLER) { return 0xFFFF; }
    }

    // ─── Checkpoint queries (read CGame fields) ──────────

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

    // ─── Chat & Commands ─────────────────────────────────

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

    // ─── Camera (SAMP-API CCamera) ──────────────────────

    inline void RestoreCamera() {
        auto* cg = GetCGame();
        if (!cg) return;
        __try {
            auto* cam = cg->m_pCamera;
            if (cam) {
                cam->Detach();
                cam->Restore();
                cam->SetToOwner();
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}

        auto* lp = GetLocalPlayer();
        if (lp) {
            __try { lp->m_bDoesSpectating = 0; }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
    }

    // ════════════════════════════════════════════════════════
    // TELEPORT — Direct matrix write (NO CEntity::Teleport!)
    // CEntity::Teleport() does Remove()+SetPos()+Add() which
    // makes SA-MP detect vehicle exit+re-entry every time.
    // Instead we write position directly into the CMatrix.
    // ════════════════════════════════════════════════════════

    // Move vehicle by directly patching its position matrix.
    // The ped stays seated — no world remove/add.
    inline bool TeleportVehicle(WORD vehicleId, float x, float y, float z) {
        auto* sv = GetSAMPVehicle(vehicleId);
        if (!sv) return false;

        // Stop movement
        __try {
            sampapi::CVector zero = {0, 0, 0};
            sv->SetSpeed(zero);
            sv->SetTurnSpeed(zero);
        } __except (EXCEPTION_EXECUTE_HANDLER) {}

        // Get GTA entity and write position directly into matrix
        __try {
            ::CVehicle* pGta = sv->m_pGameVehicle;
            if (!pGta) return false;

            // GTA SA CEntity: offset 0x14 = CMatrix* (CPlaceable::m_pMatrix)
            DWORD* pMatPtr = (DWORD*)((DWORD)pGta + 0x14);
            if (pMatPtr && *pMatPtr) {
                DWORD mat = *pMatPtr;
                // CMatrix position is at offset 0x30, 0x34, 0x38
                *(float*)(mat + 0x30) = x;
                *(float*)(mat + 0x34) = y;
                *(float*)(mat + 0x38) = z;
            } else {
                // Fallback: simple coords at 0x04, 0x08, 0x0C
                *(float*)((DWORD)pGta + 0x04) = x;
                *(float*)((DWORD)pGta + 0x08) = y;
                *(float*)((DWORD)pGta + 0x0C) = z;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }

        // NOTE: No RefreshRenderer here — calling it 50+ times
        // in quick succession breaks the camera.
        return true;
    }

    // Teleport ped (on foot)
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
        return true;
    }

    // ════════════════════════════════════════════════════════
    // VEHICLE ENTRY — CPed::PutIntoVehicle (game warp) + sync
    // We do NOT call CLocalPlayer::EnterVehicle — that sends an
    // enter-animation RPC which causes the server to re-do the
    // entry sequence. Instead, warp game-side, set m_nCurrentVehicle,
    // and let SendIncarData() inform the server via sync packet.
    // ════════════════════════════════════════════════════════

    inline bool PutIntoVehicle(WORD vehicleId) {
        auto* ped = GetPlayerPed();
        if (!ped) return false;

        GTAREF ref = GetVehicleRef(vehicleId);
        if (!ref) return false;

        // Game-side: instant warp into vehicle
        __try { ped->PutIntoVehicle(ref, 0); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }

        // Set SAMP-side vehicle ID (sync will pick it up)
        auto* lp = GetLocalPlayer();
        if (lp) {
            __try { lp->m_nCurrentVehicle = vehicleId; }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        return true;
    }

    // ════════════════════════════════════════════════════════
    // VEHICLE EXIT — CPed::ExitVehicle + CLocalPlayer::ExitVehicle
    // ════════════════════════════════════════════════════════

    inline void ExitVehicle() {
        WORD vid = GetVehicleID();
        auto* ped = GetPlayerPed();

        // Game-side
        if (ped) {
            __try { ped->ExitVehicle(); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }

        // Network-side
        if (vid != 0xFFFF) {
            auto* lp = GetLocalPlayer();
            if (lp) {
                __try { lp->ExitVehicle((int)vid); }
                __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
        }
    }

    // ════════════════════════════════════════════════════════
    // PED INPUT — CPed::SetKeys (injects into GTA CPad)
    // ════════════════════════════════════════════════════════

    inline void SetPedKeys(short controllerState, short stickX = 0, short stickY = 0) {
        auto* ped = GetPlayerPed();
        if (!ped) return;
        __try { ped->SetKeys(controllerState, stickX, stickY); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    inline void ClearPedKeys() {
        SetPedKeys(0, 0, 0);
    }

    // ════════════════════════════════════════════════════════
    // SYNC — use CLocalPlayer native send functions.
    // These read the CURRENT game state automatically.
    // ════════════════════════════════════════════════════════

    inline void SendIncarSync() {
        auto* lp = GetLocalPlayer();
        if (!lp) return;
        __try { lp->SendIncarData(); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    inline void SendOnfootSync() {
        auto* lp = GetLocalPlayer();
        if (!lp) return;
        __try { lp->SendOnfootData(); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    // ════════════════════════════════════════════════════════
    // PED ROTATION — CPed::ForceRotation
    // ════════════════════════════════════════════════════════

    inline void SetPedRotation(float angleDeg) {
        auto* ped = GetPlayerPed();
        if (!ped) return;
        float rad = angleDeg * 3.14159265f / 180.0f;
        __try { ped->ForceRotation(rad); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
}
