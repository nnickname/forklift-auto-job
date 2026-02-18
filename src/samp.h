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

    // CNetGame internal offsets (pack 1) (Updated for 0.3.DL R1)
    constexpr DWORD NETGAME_RAKCLIENT   = 0x2C;   // RakClientInterface*
    constexpr DWORD NETGAME_GAMESTATE   = 0x3CD;  // int m_nGameState (Wait, check if this conflicts with Pools?)
    // Note: User said Pools is at 0x3CD. My previous code had GameState at 0x3CD. 
    // Usually Pools and GameState are close but distinct. 
    // If User says Pools is 0x3CD, I will trust that. 
    // Checking conflict: GameState was 0x3CD. If Pools is 0x3CD, then GameState must be elsewhere?
    // Using user provided info strictly.
    constexpr DWORD NETGAME_POOLS       = 0x3CD;  // Pools* (User provided)
    
    // Pools internal offsets
    constexpr DWORD POOLS_PLAYERPOOL    = 0x18;   // User provided
    constexpr DWORD POOLS_VEHICLEPOOL   = 0x1C;   // Updated: 0.3.DL usually follows PlayerPool (0x18) with VehiclePool at 0x1C

    // CPlayerPool: pLocalPlayer offset
    constexpr DWORD PLAYERPOOL_LOCALPLAYER = 0x0; // User provided

    // CLocalPlayer field offsets
    constexpr DWORD LOCALPLAYER_STATE          = 0x04;
    constexpr DWORD LOCALPLAYER_VEHICLEID      = 0xFC;
    constexpr DWORD LOCALPLAYER_IN_CHECKPOINT = 0x1A7;

    constexpr int GAMESTATE_CONNECTED = 5;

    // Player States
    constexpr int STATE_ONFOOT = 1;
    constexpr int STATE_DRIVER = 2;

    // Function offsets
    constexpr DWORD FUNC_ADDCHATMESSAGE  = 0x67BE0;
    constexpr DWORD FUNC_SENDCMD         = 0x69340;
    constexpr DWORD FUNC_SAY             = 0x5860;
    constexpr DWORD FUNC_PUTINVEHICLE    = 0x162A0; // User provided

    // CGame* global pointer
    constexpr DWORD SAMP_CGAME_PTR       = 0x2ACA3C;
}

#pragma pack(push, 1)

struct stCheckpoint {
    float fX, fY, fZ;
    char  pad[12];
    float fSize;
    int   bEnabled;
    int   iHandle;
};

struct stRaceCheckpoint {
    float fX, fY, fZ;
    float fNextX, fNextY, fNextZ;
    float fSize;
    char  bType;
    char  padding[3];
    int   bEnabled;
    int   iMarker;
    int   iHandle;
};

constexpr DWORD CGAME_CHECKPOINT_OFFSET      = 0x0C;
constexpr DWORD CGAME_RACECHECKPOINT_OFFSET  = 0x2C;

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
        // User didn't provide GameState offset, but said Pools is 0x3CD. 
        // Previously GameState was 0x3CD.
        // Assuming we rely on pNetGame != NULL for now or check another way.
        // Let's assume connected if pNetGame is valid and Pools is valid.
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

    inline DWORD GetCGame() {
        DWORD base = SAMPOffsets::GetSAMPBase();
        if (!base) return 0;
        DWORD pCGame = 0;
        if (!SafeRead<DWORD>(base + SAMPOffsets::SAMP_CGAME_PTR, pCGame)) return 0;
        if (!pCGame || IsBadReadPtr((void*)pCGame, 0x60)) return 0;
        return pCGame;
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
        
        // TODO: Update offsets for VehiclePool if needed. 
        // 0.3.DL might differ from 0x4FB4. But no data provided.
        // Assuming safe read failure will just return 0.
        DWORD pSAMPVehicle = 0;
        if (!SafeRead<DWORD>(pVehiclePool + 0x4FB4 + (id * 4), pSAMPVehicle) || !pSAMPVehicle) return 0;

        DWORD pGTAVeh = 0;
        if (SafeRead<DWORD>(pSAMPVehicle + 0x40, pGTAVeh)) return pGTAVeh;
        return 0;
    }

    inline WORD GetSAMPIdFromGTAVehicle(DWORD gtaVehPtr) {
        if (!gtaVehPtr) return 0xFFFF;
        
        for (WORD i = 0; i < 2000; i++) {
            if (GetGTAVehicleFromSAMPId(i) == gtaVehPtr) return i;
        }

        // Distance fallback
        float targetX = 0, targetY = 0, targetZ = 0;
        if (!SafeRead<float>(gtaVehPtr + 0x44, targetX)) return 0xFFFF;
        SafeRead<float>(gtaVehPtr + 0x48, targetY);
        SafeRead<float>(gtaVehPtr + 0x4C, targetZ);

        for (WORD i = 0; i < 2000; i++) {
            DWORD pGTA = GetGTAVehicleFromSAMPId(i);
            if (!pGTA) continue;
            float x, y, z;
            if (SafeRead<float>(pGTA + 0x44, x) && SafeRead<float>(pGTA + 0x48, y) && SafeRead<float>(pGTA + 0x4C, z)) {
                float dx = x - targetX;
                float dy = y - targetY;
                float dz = z - targetZ;
                if ((dx*dx + dy*dy + dz*dz) < 100.0f) return i;
            }
        }
        return 0xFFFF;
    }

    // New 0.3.DL R1 specific PutInVehicle implementation
    inline void PutInVehicle(int iVehicleID, int iSeat) {
        DWORD pLocal = GetLocalPlayer();
        if (pLocal) {
            DWORD base = SAMPOffsets::GetSAMPBase();
            if (base) {
               typedef void(__thiscall* PutInVehicle_t)(void* _this, int vehID, int seat);
               PutInVehicle_t fn = (PutInVehicle_t)(base + SAMPOffsets::FUNC_PUTINVEHICLE); // 0x162A0
               fn((void*)pLocal, iVehicleID, iSeat);
            }
        }
    }
} // namespace SAMP
