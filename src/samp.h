#pragma once
/**
 * SA-MP 0.3DL Memory Structures & Offsets
 * 
 * These offsets are for SA-MP 0.3.DL R1
 * They may vary between versions - verify with IDA/Ghidra
 */

#include <windows.h>
#include <vector>
#include <string>

// ============================================================
// SA-MP Base Addresses
// ============================================================
namespace SAMPOffsets {
    // samp.dll base is dynamic, resolved at runtime
    inline DWORD GetSAMPBase() {
        static DWORD base = 0;
        if (!base) {
            base = (DWORD)GetModuleHandleA("samp.dll");
        }
        return base;
    }

    // SA-MP 0.3DL offsets (relative to samp.dll base)
    // verified for 0.3.DL R1
    constexpr DWORD SAMP_INFO_OFFSET         = 0x2ACA24;  // pSAMP pointer
    constexpr DWORD SAMP_CHAT_INFO_OFFSET    = 0x2ACA10;  // pChat pointer
    constexpr DWORD SAMP_CHAT_INPUT_OFFSET   = 0x2ACA14;  // pInput pointer
    constexpr DWORD SAMP_CHECKPOINTS_OFFSET  = 0x2ACA3C;  // pCheckpoints pointer (Local Player)
    
    // Game states
    constexpr int GAMESTATE_WAIT_CONNECT    = 1;
    constexpr int GAMESTATE_CONNECTING      = 2;
    constexpr int GAMESTATE_CONNECTED       = 5;
    constexpr int GAMESTATE_RESTARTING      = 11;
    
    // Functions offsets (relative to samp.dll)
    constexpr DWORD FUNC_ADDCHATMESSAGE     = 0x64520;
    constexpr DWORD FUNC_SENDCMD            = 0x65C60;
    constexpr DWORD FUNC_SAY                = 0x57F0;
    
    // Checkpoint Manager (0.3.DL R1)
    constexpr DWORD SAMP_CHECKPOINT_MGR     = 0x2ACA3C;
}

// ============================================================
// SA-MP Structures
// ============================================================
#pragma pack(push, 1)

struct stSAMPInfo {
    char pad_0[0x3CD];
    int  iGameState;
    char pad_1[0x1D - sizeof(int)]; // Padding to 0x3DE
    DWORD pPools;
};

struct stLocalPlayer {
    char pad_0[0x4];
    WORD  sCurrentVehicleID;
    char pad_1[0x2];
    int   iIsActive;
    int   iIsWasted;
};

struct stPlayerPool {
    DWORD ulMaxPlayerID;
    DWORD ulLocalPlayerID;
    void* pLocalPlayer; // stLocalPlayer*, offset 0x8
};

struct stPools {
    void* pActorPool;
    void* pObjectPool;
    void* pGangzonePool;
    void* pLabelPool;
    void* pTextdrawPool;
    void* pMenuPool;
    stPlayerPool* pPlayerPool; // 0x18
    void* pVehiclePool;
};

struct stCheckpoint {
    float fX, fY, fZ;       // Position
    float fSize;             // Radius
    BOOL  bActive;           // Is visible
};

struct stRaceCheckpoint {
    float fX, fY, fZ;       // Current position
    float fNextX, fNextY, fNextZ; // Next CP direction
    float fSize;
    BYTE  bType;             // 0-8 types
    char  pad_0[3];          // Alignment
    BOOL  bActive;
};

struct stSAMPCheckpoints {
    stRaceCheckpoint raceCheckpoint;
    stCheckpoint     normalCheckpoint;
};

#pragma pack(pop)

// ============================================================
// SA-MP Interface
// ============================================================
namespace SAMP {
    
    // Helper to safely read memory
    template <typename T>
    inline bool SafeRead(void* ptr, T& outResult) {
        if (!ptr) return false;
        __try {
            // Check if readable first to avoid overhead if obviously bad
            if (IsBadReadPtr(ptr, sizeof(T))) return false;
            outResult = *(T*)ptr;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // Overload for DWORD addresses for convenience
    template <typename T>
    inline bool SafeRead(DWORD addr, T& outResult) {
        return SafeRead((void*)addr, outResult);
    }

    inline bool IsInitialized() {
        DWORD base = SAMPOffsets::GetSAMPBase();
        if (!base) return false;
        
        DWORD* ppSamp = (DWORD*)(base + SAMPOffsets::SAMP_INFO_OFFSET);
        DWORD pSampVal = 0;
        if (!SafeRead<DWORD>(ppSamp, pSampVal) || !pSampVal) return false;
        
        stSAMPInfo pSampInfo;
        if (SafeRead<stSAMPInfo>((void*)pSampVal, pSampInfo)) {
             return pSampInfo.iGameState == SAMPOffsets::GAMESTATE_CONNECTED;
        }
        return false;
    }

    inline stLocalPlayer* GetLocalPlayer() {
        DWORD base = SAMPOffsets::GetSAMPBase();
        if (!base) return nullptr;
        
        DWORD* ppSamp = (DWORD*)(base + SAMPOffsets::SAMP_INFO_OFFSET);
        DWORD pSampVal = 0;
        if (!SafeRead<DWORD>(ppSamp, pSampVal) || !pSampVal) return nullptr;
        
        // pPools is at offset 0x3DE in stSAMPInfo (0.3.DL R1)
        DWORD pPoolsPtrAddress = pSampVal + 0x3DE;
        DWORD pPoolsVal = 0;

        // Read the pointer to pools
        if (!SafeRead<DWORD>((void*)pPoolsPtrAddress, pPoolsVal) || !pPoolsVal) return nullptr;
        
        stPools poolStruct;
        if (!SafeRead<stPools>((void*)pPoolsVal, poolStruct)) return nullptr;

        stPlayerPool* pPlayerPool = poolStruct.pPlayerPool;
        if (!pPlayerPool) return nullptr;
        
        // Read pLocalPlayer from PlayerPool (Offset 0x8)
        stLocalPlayer* pLocalPlayer = nullptr;
        if (SafeRead<stLocalPlayer*>((void*)((DWORD)pPlayerPool + 8), pLocalPlayer)) {
            return pLocalPlayer;
        }

        return nullptr;
    }

    inline WORD GetVehicleID() {
        stLocalPlayer* pLocal = GetLocalPlayer();
        if (!pLocal) return 0xFFFF;

        // Use SafeRead to access the member
        WORD vehID = 0xFFFF;
        // Calculate address of sCurrentVehicleID (Offset 4 per struct def)
        if (SafeRead<WORD>((void*)((DWORD)pLocal + 4), vehID)) {
             return vehID;
        }
        return 0xFFFF;
    }
    
    /**
     * Send a chat message / command to server
     * If message starts with '/', it is treated as a command
     */
    inline void SendChat(const char* message) {
        DWORD base = SAMPOffsets::GetSAMPBase();
        if (!base) return;
        
        if (message[0] == '/') {
            // Send as command
            DWORD* ppInput = (DWORD*)(base + SAMPOffsets::SAMP_CHAT_INPUT_OFFSET);
            if (!ppInput || !*ppInput) return;
            
            // Check safe read before dereferencing
            if (IsBadReadPtr(ppInput, 4)) return;

            typedef void(__thiscall* SendCmd_t)(void*, const char*);
            SendCmd_t fnSendCmd = (SendCmd_t)(base + SAMPOffsets::FUNC_SENDCMD);
            fnSendCmd((void*)*ppInput, message);
        } else {
            // Send as regular chat
            typedef void(__cdecl* Say_t)(const char*);
            Say_t fnSay = (Say_t)(base + SAMPOffsets::FUNC_SAY);
            fnSay(message);
        }
    }
    
    /**
     * Add a message to the local chat (only visible to you)
     */
    inline void AddChatMessage(DWORD color, const char* text) {
        DWORD base = SAMPOffsets::GetSAMPBase();
        if (!base) return;
        
        DWORD* ppChat = (DWORD*)(base + SAMPOffsets::SAMP_CHAT_INFO_OFFSET);
        if (!ppChat || IsBadReadPtr(ppChat, 4)) return;
        if (!*ppChat || IsBadReadPtr((void*)*ppChat, 4)) return;
        
        // Validate the function address before calling
        DWORD fnAddr = base + SAMPOffsets::FUNC_ADDCHATMESSAGE;
        if (IsBadCodePtr((FARPROC)fnAddr)) return;
        
        typedef void(__thiscall* AddMsg_t)(void*, DWORD, const char*);
        AddMsg_t fnAddMsg = (AddMsg_t)fnAddr;
        fnAddMsg((void*)*ppChat, color, text);
    }
    
    /**
     * Get checkpoint data from SA-MP memory
     */
    inline stCheckpoint* GetCurrentCheckpoint() {
        DWORD base = SAMPOffsets::GetSAMPBase();
        if (!base) return nullptr;
        
        DWORD* ppCheckpoints = (DWORD*)(base + SAMPOffsets::SAMP_CHECKPOINT_MGR);
        if (!ppCheckpoints || IsBadReadPtr(ppCheckpoints, 4)) return nullptr;
        if (!*ppCheckpoints || IsBadReadPtr((void*)*ppCheckpoints, sizeof(stSAMPCheckpoints))) return nullptr;
        
        stSAMPCheckpoints* pCheckpoints = (stSAMPCheckpoints*)(*ppCheckpoints);
        return &pCheckpoints->normalCheckpoint;
    }

    /**
     * Get race checkpoint data
     */
    inline stRaceCheckpoint* GetRaceCheckpoint() {
        DWORD base = SAMPOffsets::GetSAMPBase();
        if (!base) return nullptr;
        
        DWORD* ppCheckpoints = (DWORD*)(base + SAMPOffsets::SAMP_CHECKPOINT_MGR);
        if (!ppCheckpoints || IsBadReadPtr(ppCheckpoints, 4)) return nullptr;
        if (!*ppCheckpoints || IsBadReadPtr((void*)*ppCheckpoints, sizeof(stSAMPCheckpoints))) return nullptr;
        
        stSAMPCheckpoints* pCheckpoints = (stSAMPCheckpoints*)(*ppCheckpoints);
        return &pCheckpoints->raceCheckpoint;
    }
}

