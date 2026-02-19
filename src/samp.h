#pragma once
/**
 * SA-MP 0.3.DL R1 Memory Structures & Offsets
 * 
 * Based on BlastHackNet/SAMP-API (branch multiver, 0.3.DL-1)
 * https://github.com/BlastHackNet/SAMP-API/tree/multiver/include/sampapi/0.3.DL-1
 *
 * All structs use #pragma pack(push, 1) to match SA-MP's compiled layout.
 */

#include <windows.h>

// ============================================================
// SA-MP Base Addresses & Offsets (0.3.DL R1)
// ============================================================
namespace SAMPOffsets {
    inline DWORD GetSAMPBase() {
        return (DWORD)GetModuleHandleA("samp.dll");
    }

    // Global pointers in samp.dll data section
    constexpr DWORD SAMP_INFO_OFFSET       = 0x2ACA24;  // CNetGame** (RefNetGame)
    constexpr DWORD SAMP_CHAT_INFO_OFFSET  = 0x2ACA10;  // CChat**
    constexpr DWORD SAMP_CHAT_INPUT_OFFSET = 0x2ACA14;  // CInput**

    // CNetGame internal offsets (pack 1) — SA-MP 0.3.DL R1
    // Verified from BlastHackNet/mod_sa samp-03dl branch stSAMP struct layout.
    //
    // stSAMP layout (pack 1):
    //   _pad0[20]=0x00  pUnk0=0x14  pServerInfo=0x18  _pad1[16]=0x1C
    //   pRakClientInterface=0x2C  szIP[257]=0x30  szHostname[257]=0x131
    //   _pad2=0x232  m_bUpdateCameraTarget=0x233  m_bNoNameTagStatus=0x234
    //   ulPort=0x235  m_bLanMode=0x239  ulMapIcons[100]=0x23D (400 bytes)
    //   iGameState=0x3CD  ulConnectTick=0x3D1  pSettings=0x3D5
    //   _pad3[5]=0x3D9  pPools=0x3DE
    constexpr DWORD NETGAME_RAKCLIENT   = 0x2C;   // void* pRakClientInterface
    constexpr DWORD NETGAME_GAMESTATE   = 0x3CD;  // Gamestate iGameState
    constexpr DWORD NETGAME_POOLS       = 0x3DE;  // stSAMPPools* pPools

    // stSAMPPools layout (pack 1) — 0.3.DL R1:
    //   +0x00 pMenu, +0x04 pActor, +0x08 pPlayer, +0x0C pVehicle,
    //   +0x10 pPickup, +0x14 pObject, +0x18 pGangzone, +0x1C pText3D, +0x20 pTextdraw
    constexpr DWORD POOLS_PLAYERPOOL    = 0x08;   // stPlayerPool*
    constexpr DWORD POOLS_VEHICLEPOOL   = 0x0C;   // stVehiclePool*

    // stPlayerPool layout (pack 1):
    //   +0x00 sLocalPlayerID(2), +0x02 pVTBL_txtHandler(4),
    //   +0x06 strLocalPlayerName(std::string = 24 bytes),
    //   +0x1E pLocalPlayer
    constexpr DWORD PLAYERPOOL_LOCALPLAYER = 0x1E;

    // CLocalPlayer field offsets
    constexpr DWORD LOCALPLAYER_STATE          = 0x04;
    constexpr DWORD LOCALPLAYER_VEHICLEID      = 0xFC;
    constexpr DWORD LOCALPLAYER_IN_CHECKPOINT = 0x1A7;

    // Gamestate: observed values on this server build: 1→2→6→5 during connect, stabilizes at 5.
    // samp-03dl enum says CONNECTED=14 but this samp.dll reports 5 when in-game.
    constexpr int GAMESTATE_CONNECTED = 5;

    // Player States
    constexpr int STATE_ONFOOT = 1;
    constexpr int STATE_DRIVER = 2;

    // Function offsets (verified from samp-03dl SAMP_FUNC_ADDTOCHATWND)
    constexpr DWORD FUNC_ADDCHATMESSAGE  = 0x67650;
    constexpr DWORD FUNC_SENDCMD         = 0x69340;
    constexpr DWORD FUNC_SAY             = 0x5860;
    constexpr DWORD FUNC_PUTINVEHICLE    = 0x162A0; // User provided

    // CGame* global pointer
    constexpr DWORD SAMP_CGAME_PTR       = 0x2ACA3C;
}

#pragma pack(push, 1)

// CGame layout (SAMP-API 0.3.DL-1, pack 1):
//   +0x00 CAudio* m_pAudio
//   +0x04 CCamera* m_pCamera
//   +0x08 CPed* m_pPlayerPed
//   +0x0C m_checkpoint (NORMAL checkpoint):
//         +0x0C CVector m_position  (12 bytes) ← NORMAL CP base
//         +0x18 CVector m_size      (12 bytes)
//         +0x24 BOOL   m_bEnabled   (4 bytes)
//         +0x28 GTAREF m_handle     (4 bytes)  → total 32 bytes
//   +0x2C m_racingCheckpoint (RACE checkpoint):
//         +0x2C CVector m_currentPosition (12 bytes) ← RACE CP base
//         +0x38 CVector m_nextPosition    (12 bytes)
//         +0x44 float   m_fSize           (4 bytes)
//         +0x48 char    m_nType           (1 byte)
//         +0x49 BOOL    m_bEnabled        (4 bytes)  (pack 1 — no gap after char)
//         +0x4D GTAREF  m_marker          (4 bytes)
//         +0x51 GTAREF  m_handle          (4 bytes)  → total 41 bytes

struct stCheckpoint {
    float fX, fY, fZ;                       // m_position (CVector)
    float fExtentX, fExtentY, fExtentZ;     // m_size (CVector)
    int   bEnabled;                          // m_bEnabled (BOOL) — at struct offset +24
};

struct stRaceCheckpoint {
    float fX, fY, fZ;               // m_currentPosition (CVector)
    float fNextX, fNextY, fNextZ;   // m_nextPosition (CVector)
    float fSize;                    // m_fSize
    char  bType;                    // m_nType (1 byte)
    // pack(1): m_bEnabled is immediately after m_nType, no padding
    int   bEnabled;                 // m_bEnabled (BOOL) — at struct offset +29
    int   iMarker;                  // m_marker (GTAREF)
    int   iHandle;                  // m_handle (GTAREF)
};

// CGame pointer offsets (from pCGame = *(CGame**)SAMP_CGAME_PTR)
// Per SAMP-API 0.3.DL-1 CGame.h (pack 1): normal checkpoint FIRST at +0x0C, race SECOND at +0x2C
constexpr DWORD CGAME_CHECKPOINT_OFFSET      = 0x0C;  // m_checkpoint.m_position (NORMAL)
constexpr DWORD CGAME_RACECHECKPOINT_OFFSET  = 0x2C;  // m_racingCheckpoint.m_currentPosition (RACE)

#pragma pack(pop)

namespace SAMP {

    template <typename T>
    inline bool SafeRead(DWORD addr, T& outResult) {
        if (!addr) return false;
        __try {
            if (IsBadReadPtr((void*)addr, sizeof(T))) return false;
            outResult = *(T*)addr;
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    inline DWORD GetSAMPBase() {
        return (DWORD)GetModuleHandleA("samp.dll");
    }

    inline DWORD GetNetGame() {
        DWORD base = SAMPOffsets::GetSAMPBase();
        if (!base) return 0;
        DWORD pNetGame = 0;
        SafeRead<DWORD>(base + SAMPOffsets::SAMP_INFO_OFFSET, pNetGame);
        return pNetGame;
    }

    inline bool IsInitialized() {
        DWORD pNetGame = GetNetGame();
        if (!pNetGame) return false;
        // Check gamestate == CONNECTED (14 in 0.3.DL R1)
        int gameState = 0;
        if (!SafeRead<int>(pNetGame + SAMPOffsets::NETGAME_GAMESTATE, gameState)) return false;
        if (gameState != SAMPOffsets::GAMESTATE_CONNECTED) return false;
        // Also confirm pools pointer is valid
        DWORD pPools = 0;
        SafeRead<DWORD>(pNetGame + SAMPOffsets::NETGAME_POOLS, pPools);
        return (pPools != 0);
    }

    inline void* GetRakClient() {
        DWORD pNetGame = GetNetGame();
        if (!pNetGame) return nullptr;
        DWORD pRakClient = 0;
        SafeRead<DWORD>(pNetGame + SAMPOffsets::NETGAME_RAKCLIENT, pRakClient);
        return (void*)pRakClient;
    }

    inline DWORD GetLocalPlayer() {
        DWORD pNetGame = GetNetGame();
        if (!pNetGame) return 0;
        DWORD pPools = 0;
        if (!SafeRead<DWORD>(pNetGame + SAMPOffsets::NETGAME_POOLS, pPools) || !pPools) return 0;
        DWORD pPlayerPool = 0;
        if (!SafeRead<DWORD>(pPools + SAMPOffsets::POOLS_PLAYERPOOL, pPlayerPool) || !pPlayerPool) return 0;
        
        DWORD pLocal = 0;
        // User said LocalPlayer is 0x0 in PlayerPool, so it is the first member pointer?
        // Or is PlayerPool a pointer to LocalPlayer? 
        // "Offset of LocalPlayer: 0x0 (dentro de PlayerPool)" implies dereference 0x0.
        SafeRead<DWORD>(pPlayerPool + SAMPOffsets::PLAYERPOOL_LOCALPLAYER, pLocal);
        return pLocal;
    }

    inline WORD GetVehicleID() {
        DWORD pLocal = GetLocalPlayer();
        if (!pLocal) return 0xFFFF;
        WORD vehID = 0xFFFF;
        SafeRead<WORD>(pLocal + SAMPOffsets::LOCALPLAYER_VEHICLEID, vehID);
        return vehID;
    }

    inline void SetInCheckpoint(bool active) {
        DWORD pLocal = GetLocalPlayer();
        if (!pLocal) return;
        if (IsBadWritePtr((void*)(pLocal + SAMPOffsets::LOCALPLAYER_IN_CHECKPOINT), 1)) return;
        *(BYTE*)(pLocal + SAMPOffsets::LOCALPLAYER_IN_CHECKPOINT) = active ? 1 : 0;
    }

    // Calls CLocalPlayer::EnableSpectating(FALSE) — SA-MP internal function that
    // properly clears m_bDoesSpectating and restores the input/camera pipeline.
    // Offset 0x4080 verified from BlastHackNet/samp-api 0.3.DL-1 CLocalPlayer.cpp.
    // This is the correct fix for frozen camera/input after route ends.
    inline void DisableSpectating() {
        DWORD sampBase = SAMPOffsets::GetSAMPBase();
        DWORD pLocal = GetLocalPlayer();
        if (!sampBase || !pLocal) return;
        __try {
            typedef void(__thiscall* EnableSpectating_t)(void*, BOOL);
            EnableSpectating_t fn = (EnableSpectating_t)(sampBase + 0x4080);
            if (!IsBadCodePtr((FARPROC)fn))
                fn((void*)pLocal, FALSE);
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }

    inline DWORD GetCGame() {
        DWORD base = SAMPOffsets::GetSAMPBase();
        if (!base) return 0;
        DWORD pCGame = 0;
        if (!SafeRead<DWORD>(base + SAMPOffsets::SAMP_CGAME_PTR, pCGame)) return 0;
        if (!pCGame || IsBadReadPtr((void*)pCGame, 0x60)) return 0;
        return pCGame;
    }

    // Calls SA-MP's own CCamera::Restore() on the SAMP camera object (pCGame+0x04).
    // This is the correct fix for camera freeze caused by server-sent camera RPCs
    // (InterpolateCamera, AttachCameraToObject, SetCameraLookAt) that call
    // CCamera::TakeControl() internally. GTA's own Restore() does NOT touch the
    // SAMP camera object state.
    // CCamera::Restore offset 0x9D580 verified from BlastHackNet/samp-api 0.3.DL-1.
    inline void RestoreSAMPCamera() {
        DWORD sampBase = SAMPOffsets::GetSAMPBase();
        if (!sampBase) return;
        __try {
            DWORD pCGame = GetCGame();
            if (!pCGame) return;
            // pCGame+0x04 = CCamera* m_pCamera (from CGame layout in samp-api 0.3.DL-1)
            DWORD pSAMPCamera = 0;
            if (!SafeRead<DWORD>(pCGame + 0x04, pSAMPCamera) || !pSAMPCamera) return;
            if (IsBadReadPtr((void*)pSAMPCamera, 4)) return;
            typedef void(__thiscall* CameraRestore_t)(void*);
            CameraRestore_t fn = (CameraRestore_t)(sampBase + 0x9D580);
            if (!IsBadCodePtr((FARPROC)fn))
                fn((void*)pSAMPCamera);
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }

    inline stCheckpoint* GetCurrentCheckpoint() {
        DWORD pCGame = GetCGame();
        if (!pCGame) return nullptr;
        static stCheckpoint result;
        if (!SafeRead<stCheckpoint>(pCGame + CGAME_CHECKPOINT_OFFSET, result)) return nullptr;
        if (result.fX < -4000.0f || result.fX > 4000.0f) return nullptr;
        return &result;
    }

    inline stRaceCheckpoint* GetRaceCheckpoint() {
        DWORD pCGame = GetCGame();
        if (!pCGame) return nullptr;
        static stRaceCheckpoint result;
        if (!SafeRead<stRaceCheckpoint>(pCGame + CGAME_RACECHECKPOINT_OFFSET, result)) return nullptr;
        if (result.fX < -4000.0f || result.fX > 4000.0f) return nullptr;
        return &result;
    }

    inline bool DiagReadCheckpoint(float& x, float& y, float& z, int& enabled) {
        DWORD pCGame = GetCGame();
        if (!pCGame) return false;
        // Normal checkpoint: m_checkpoint.m_position at +0x0C, m_bEnabled at +0x24
        SafeRead<float>(pCGame + 0x0C, x);
        SafeRead<float>(pCGame + 0x10, y);
        SafeRead<float>(pCGame + 0x14, z);
        SafeRead<int>  (pCGame + 0x24, enabled);
        return true;
    }

    // Add local chat message
    inline void AddChatMessage(DWORD color, const char* text) {
        DWORD base = SAMPOffsets::GetSAMPBase();
        if (!base) return;
        DWORD pChat = 0;
        if (!SafeRead<DWORD>(base + SAMPOffsets::SAMP_CHAT_INFO_OFFSET, pChat) || !pChat) return;
        typedef void (__thiscall *AddEntry_t)(void*, DWORD, const char*, const char*, DWORD, DWORD);
        AddEntry_t fnAdd = (AddEntry_t)(base + SAMPOffsets::FUNC_ADDCHATMESSAGE);
        if (!IsBadCodePtr((FARPROC)fnAdd)) {
            fnAdd((void*)pChat, 8, text, nullptr, color, 0); 
        }
    }

    inline void SendChat(const char* text) {
        DWORD base = SAMPOffsets::GetSAMPBase();
        if (!base) return;
        DWORD pInput = 0;
        if (!SafeRead<DWORD>(base + SAMPOffsets::SAMP_CHAT_INPUT_OFFSET, pInput) || !pInput) return;
        typedef void (__thiscall *Send_t)(void*, const char*);
        Send_t fnSend = (Send_t)(base + SAMPOffsets::FUNC_SENDCMD);
        if (!IsBadCodePtr((FARPROC)fnSend)) fnSend((void*)pInput, text);
    }

    inline DWORD GetGTAVehicleFromSAMPId(WORD id) {
        if (id >= 2000) return 0;
        DWORD pNetGame = GetNetGame();
        if (!pNetGame) return 0;
        DWORD pPools = 0;
        if (!SafeRead<DWORD>(pNetGame + SAMPOffsets::NETGAME_POOLS, pPools) || !pPools) return 0;
        DWORD pVehiclePool = 0;
        if (!SafeRead<DWORD>(pPools + SAMPOffsets::POOLS_VEHICLEPOOL, pVehiclePool) || !pVehiclePool) return 0;

        // CVehiclePool layout (SAMP-API 0.3.DL-1, pack 1):
        //   +0x00   int m_nCount (4)
        //   +0x04   m_waiting: VehicleInfo[100]=4000b + BOOL[100]=400b = 4400b → ends at +0x1134
        //   +0x1134 CVehicle* m_pObject[2000]  (SAMP objects, 8000b) → ends at +0x3074
        //   +0x3074 BOOL m_bNotEmpty[2000]     (used as iIsListed, 8000b) → ends at +0x4FB4
        //   +0x4FB4 ::CVehicle* m_pGameObject[2000] ← GTA vehicle pointers (direct, no extra deref)
        // Read iIsListed to skip vacant slots quickly
        int isListed = 0;
        if (!SafeRead<int>(pVehiclePool + 0x3074 + (id * 4), isListed) || !isListed) return 0;

        // pGTA_Vehicle[id] is the raw GTA CVehicle pointer
        DWORD pGTAVeh = 0;
        if (!SafeRead<DWORD>(pVehiclePool + 0x4FB4 + (id * 4), pGTAVeh)) return 0;
        return pGTAVeh;
    }

    inline WORD GetSAMPIdFromGTAVehicle(DWORD gtaVehPtr) {
        if (!gtaVehPtr) return 0xFFFF;

        // Primary scan: match by GTA pointer directly
        for (WORD i = 0; i < 2000; i++) {
            if (GetGTAVehicleFromSAMPId(i) == gtaVehPtr) return i;
        }

        // Distance fallback: compare world positions (POS_X_SIMPLE = +0x04)
        float targetX = 0, targetY = 0, targetZ = 0;
        if (!SafeRead<float>(gtaVehPtr + 0x04, targetX)) return 0xFFFF;
        SafeRead<float>(gtaVehPtr + 0x08, targetY);
        SafeRead<float>(gtaVehPtr + 0x0C, targetZ);

        for (WORD i = 0; i < 2000; i++) {
            DWORD pGTA = GetGTAVehicleFromSAMPId(i);
            if (!pGTA) continue;
            float x, y, z;
            if (SafeRead<float>(pGTA + 0x04, x) && SafeRead<float>(pGTA + 0x08, y) && SafeRead<float>(pGTA + 0x0C, z)) {
                float dx = x - targetX;
                float dy = y - targetY;
                float dz = z - targetZ;
                if ((dx*dx + dy*dy + dz*dz) < 25.0f) return i; // within 5m
            }
        }
        return 0xFFFF;
    }

    // NOTE: SAMP::PutInVehicle (FUNC_PUTINVEHICLE = 0x162A0) is NOT used.
    // The offset causes a crash on this server's samp.dll build.
    // Vehicle warp is handled by Game::WarpPedIntoVehicle in game.h,
    // which uses GTA's CPed::WarpPedIntoCar + manual SAMP state patch.

} // namespace SAMP
