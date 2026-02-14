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
        // NO caching - samp.dll puede cargarse después del plugin
        return (DWORD)GetModuleHandleA("samp.dll");
    }

    // Global pointers in samp.dll data section
    constexpr DWORD SAMP_INFO_OFFSET       = 0x2ACA24;  // CNetGame** (RefNetGame)
    constexpr DWORD SAMP_CHAT_INFO_OFFSET  = 0x2ACA10;  // CChat**
    constexpr DWORD SAMP_CHAT_INPUT_OFFSET = 0x2ACA14;  // CInput**

    // CNetGame internal offsets (pack 1) - from SAMP-API CNetGame.h
    constexpr DWORD NETGAME_RAKCLIENT   = 0x2C;   // RakClientInterface*
    constexpr DWORD NETGAME_GAMESTATE   = 0x3CD;  // int m_nGameState
    constexpr DWORD NETGAME_POOLS       = 0x3DE;  // Pools*

    // Pools internal offsets (pack 1) - from SAMP-API CNetGame.h::Pools
    //   CMenuPool*     m_pMenu      0x00
    //   CActorPool*    m_pActor     0x04
    //   CPlayerPool*   m_pPlayer    0x08  <-- FIX (was 0x18)
    //   CVehiclePool*  m_pVehicle   0x0C
    constexpr DWORD POOLS_PLAYERPOOL = 0x08;

    // CPlayerPool: pLocalPlayer offset
    // ID(2) + __align(4) + std::string(24 in MSVC x86 SSO) = offset 0x1E
    // Confirmado por dump: _Mysize=6 _Myres=15 → sizeof(string)=24
    constexpr DWORD PLAYERPOOL_LOCALPLAYER = 0x1E;

    // CLocalPlayer: field offsets (calculated from SAMP-API sync struct sizes)
    //   CPed*(4) + TrailerData(54) + OnfootData(68) + PassengerData(24)
    //   + IncarData(63) + AimData(31) = 0xF4
    //   BOOL m_bIsActive at 0xF4, BOOL m_bIsWasted at 0xF8
    //   ID m_nCurrentVehicle at 0xFC
    constexpr DWORD LOCALPLAYER_VEHICLEID  = 0xFC;

    // Game states (from CNetGame::GameMode enum)
    constexpr int GAMESTATE_WAIT_CONNECT  = 1;
    constexpr int GAMESTATE_CONNECTING    = 2;
    constexpr int GAMESTATE_CONNECTED     = 5;
    constexpr int GAMESTATE_RESTARTING    = 11;

    // SA-MP function offsets
    constexpr DWORD FUNC_ADDCHATMESSAGE  = 0x64520;
    constexpr DWORD FUNC_SENDCMD         = 0x65C60;
    constexpr DWORD FUNC_SAY             = 0x57F0;

    // CGame* global pointer (contains checkpoint data)
    constexpr DWORD SAMP_CGAME_PTR       = 0x2ACA3C;
}

// ============================================================
// CGame Checkpoint Structures (pack 1)
// From SAMP-API CGame.h
// ============================================================
#pragma pack(push, 1)

// CGame::m_checkpoint (embedded at CGame + 0x0C)
struct stCheckpoint {
    float fX, fY, fZ;             // CVector m_position  (12 bytes)
    float fSizeX, fSizeY, fSizeZ; // CVector m_size      (12 bytes)
    int   bEnabled;               // BOOL m_bEnabled      (4 bytes)
    int   iHandle;                // GTAREF m_handle      (4 bytes)
};  // Total: 32 bytes

// CGame::m_racingCheckpoint (embedded at CGame + 0x2C)
// NO padding after bType - this is pack(1)!
struct stRaceCheckpoint {
    float fX, fY, fZ;             // CVector m_currentPosition (12 bytes)
    float fNextX, fNextY, fNextZ; // CVector m_nextPosition    (12 bytes)
    float fSize;                  // float m_fSize              (4 bytes)
    char  bType;                  // char m_nType               (1 byte)
    int   bEnabled;               // BOOL m_bEnabled            (4 bytes)
    int   iMarker;                // GTAREF m_marker            (4 bytes)
    int   iHandle;                // GTAREF m_handle            (4 bytes)
};  // Total: 41 bytes

// CGame partial layout (offset-based access is safer than full struct)
// CGame+0x00: CAudio*  (4)
// CGame+0x04: CCamera* (4)
// CGame+0x08: CPed*    (4)
// CGame+0x0C: stCheckpoint (32 bytes)
// CGame+0x2C: stRaceCheckpoint (41 bytes)
constexpr DWORD CGAME_CHECKPOINT_OFFSET      = 0x0C;
constexpr DWORD CGAME_RACECHECKPOINT_OFFSET  = 0x2C;

#pragma pack(pop)

// ============================================================
// SA-MP Interface
// ============================================================
namespace SAMP {

    // Safe memory read with SEH
    template <typename T>
    inline bool SafeRead(DWORD addr, T& outResult) {
        if (!addr) return false;
        __try {
            if (IsBadReadPtr((void*)addr, sizeof(T))) return false;
            outResult = *(T*)addr;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // Get CNetGame pointer
    inline DWORD GetNetGame() {
        DWORD base = SAMPOffsets::GetSAMPBase();
        if (!base) return 0;
        DWORD pNetGame = 0;
        if (!SafeRead<DWORD>(base + SAMPOffsets::SAMP_INFO_OFFSET, pNetGame)) return 0;
        return pNetGame;
    }

    // Is SA-MP connected?
    inline bool IsInitialized() {
        DWORD pNetGame = GetNetGame();
        if (!pNetGame) return false;
        int gameState = 0;
        if (!SafeRead<int>(pNetGame + SAMPOffsets::NETGAME_GAMESTATE, gameState)) return false;
        return gameState == SAMPOffsets::GAMESTATE_CONNECTED;
    }

    // Get RakClient pointer
    inline void* GetRakClient() {
        DWORD pNetGame = GetNetGame();
        if (!pNetGame) return nullptr;
        DWORD pRakClient = 0;
        if (!SafeRead<DWORD>(pNetGame + SAMPOffsets::NETGAME_RAKCLIENT, pRakClient)) return nullptr;
        if (!pRakClient || IsBadReadPtr((void*)pRakClient, 4)) return nullptr;
        return (void*)pRakClient;
    }

    // Get CLocalPlayer pointer (NetGame→Pools→PlayerPool→LocalPlayer)
    inline DWORD GetLocalPlayer() {
        DWORD pNetGame = GetNetGame();
        if (!pNetGame) return 0;
        DWORD pPools = 0;
        if (!SafeRead<DWORD>(pNetGame + SAMPOffsets::NETGAME_POOLS, pPools) || !pPools) return 0;
        DWORD pPlayerPool = 0;
        if (!SafeRead<DWORD>(pPools + SAMPOffsets::POOLS_PLAYERPOOL, pPlayerPool) || !pPlayerPool) return 0;
        DWORD pLocalPlayer = 0;
        if (!SafeRead<DWORD>(pPlayerPool + SAMPOffsets::PLAYERPOOL_LOCALPLAYER, pLocalPlayer)) return 0;
        // Validate: must be a real heap pointer (> 64KB), not garbage like 0x1D
        if (pLocalPlayer < 0x10000 || IsBadReadPtr((void*)pLocalPlayer, 0x100)) return 0;
        return pLocalPlayer;
    }

    // Get current vehicle ID from CLocalPlayer
    inline WORD GetVehicleID() {
        DWORD pLocal = GetLocalPlayer();
        if (!pLocal) return 0xFFFF;
        WORD vehID = 0xFFFF;
        SafeRead<WORD>(pLocal + SAMPOffsets::LOCALPLAYER_VEHICLEID, vehID);
        return vehID;
    }

    // Get CGame pointer
    inline DWORD GetCGame() {
        DWORD base = SAMPOffsets::GetSAMPBase();
        if (!base) return 0;
        DWORD pCGame = 0;
        if (!SafeRead<DWORD>(base + SAMPOffsets::SAMP_CGAME_PTR, pCGame)) return 0;
        if (!pCGame || IsBadReadPtr((void*)pCGame, 0x60)) return 0;
        return pCGame;
    }

    // Get normal checkpoint from CGame
    // Added coordinate sanity check: CGame memory is garbage until server sends SetPlayerCheckpoint
    inline stCheckpoint* GetCurrentCheckpoint() {
        DWORD pCGame = GetCGame();
        if (!pCGame) return nullptr;
        static stCheckpoint result;
        if (!SafeRead<stCheckpoint>(pCGame + CGAME_CHECKPOINT_OFFSET, result)) return nullptr;
        // Sanity: reject garbage coordinates (uninitialized memory)
        if (result.fX < -4000.0f || result.fX > 4000.0f) return nullptr;
        if (result.fY < -4000.0f || result.fY > 4000.0f) return nullptr;
        if (result.fZ < -200.0f  || result.fZ > 2000.0f) return nullptr;
        return &result;
    }

    // Get race checkpoint from CGame
    inline stRaceCheckpoint* GetRaceCheckpoint() {
        DWORD pCGame = GetCGame();
        if (!pCGame) return nullptr;
        static stRaceCheckpoint result;
        if (!SafeRead<stRaceCheckpoint>(pCGame + CGAME_RACECHECKPOINT_OFFSET, result)) return nullptr;
        // Sanity: reject garbage coordinates
        if (result.fX < -4000.0f || result.fX > 4000.0f) return nullptr;
        if (result.fY < -4000.0f || result.fY > 4000.0f) return nullptr;
        if (result.fZ < -200.0f  || result.fZ > 2000.0f) return nullptr;
        return &result;
    }

    // Diagnostic: Raw float reads from CGame for debugging
    inline bool DiagReadCheckpoint(float& x, float& y, float& z, int& enabled) {
        DWORD pCGame = GetCGame();
        if (!pCGame) return false;
        SafeRead<float>(pCGame + 0x0C, x);
        SafeRead<float>(pCGame + 0x10, y);
        SafeRead<float>(pCGame + 0x14, z);
        SafeRead<int>(pCGame + 0x24, enabled);
        return true;
    }

    // Add local chat message
    inline void AddChatMessage(DWORD color, const char* text) {
        DWORD base = SAMPOffsets::GetSAMPBase();
        if (!base) return;
        DWORD pChat = 0;
        if (!SafeRead<DWORD>(base + SAMPOffsets::SAMP_CHAT_INFO_OFFSET, pChat) || !pChat) return;
        if (IsBadReadPtr((void*)pChat, 4)) return;
        DWORD fnAddr = base + SAMPOffsets::FUNC_ADDCHATMESSAGE;
        if (IsBadCodePtr((FARPROC)fnAddr)) return;
        typedef void(__thiscall* AddMsg_t)(void*, DWORD, const char*);
        AddMsg_t fnAddMsg = (AddMsg_t)fnAddr;
        fnAddMsg((void*)pChat, color, text);
    }

    // Send chat/command
    inline void SendChat(const char* message) {
        DWORD base = SAMPOffsets::GetSAMPBase();
        if (!base) return;
        if (message[0] == '/') {
            DWORD pInput = 0;
            if (!SafeRead<DWORD>(base + SAMPOffsets::SAMP_CHAT_INPUT_OFFSET, pInput) || !pInput) return;
            typedef void(__thiscall* SendCmd_t)(void*, const char*);
            SendCmd_t fnSendCmd = (SendCmd_t)(base + SAMPOffsets::FUNC_SENDCMD);
            fnSendCmd((void*)pInput, message);
        } else {
            typedef void(__cdecl* Say_t)(const char*);
            Say_t fnSay = (Say_t)(base + SAMPOffsets::FUNC_SAY);
            fnSay(message);
        }
    }
}

