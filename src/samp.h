#pragma once
/**
 * SA-MP 0.3DL Memory Structures & Offsets
 * 
 * These offsets are for SA-MP 0.3.DL R1
 * They may vary between versions - verify with IDA/Ghidra
 */

#include <windows.h>

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
    
    // Game states
    constexpr int GAMESTATE_WAIT_CONNECT    = 1;
    constexpr int GAMESTATE_CONNECTING      = 2;
    constexpr int GAMESTATE_CONNECTED       = 5;
    constexpr int GAMESTATE_RESTARTING      = 11;
    
    // Functions offsets (relative to samp.dll)
    constexpr DWORD FUNC_ADDCHATMESSAGE     = 0x64520;
    constexpr DWORD FUNC_SENDCMD            = 0x65C60;
    constexpr DWORD FUNC_SAY                = 0x57F0;
}

// ============================================================
// SA-MP Structures
// ============================================================
#pragma pack(push, 1)

struct stSAMPInfo {
    char pad_0[0x3CD];
    int  iGameState;
    // ... more fields
};

struct stPlayerPool {
    // Local player info
    DWORD pLocalPlayer;  // stLocalPlayer*
    // ... 
};

struct stLocalPlayer {
    char pad_0[0x4];
    WORD  sCurrentVehicleID;
    char pad_1[0x2];
    int   iIsActive;
    int   iIsWasted;
    // ... simplified
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
    BOOL  bActive;
    // padding
};

#pragma pack(pop)

// ============================================================
// SA-MP Interface
// ============================================================
namespace SAMP {
    
    inline bool IsInitialized() {
        DWORD base = SAMPOffsets::GetSAMPBase();
        if (!base) return false;
        
        DWORD* ppSamp = (DWORD*)(base + SAMPOffsets::SAMP_INFO_OFFSET);
        if (!ppSamp || !*ppSamp) return false;
        
        stSAMPInfo* pSamp = (stSAMPInfo*)(*ppSamp);
        return pSamp->iGameState == SAMPOffsets::GAMESTATE_CONNECTED;
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
        if (!ppChat || !*ppChat) return;
        
        typedef void(__thiscall* AddMsg_t)(void*, DWORD, const char*);
        AddMsg_t fnAddMsg = (AddMsg_t)(base + SAMPOffsets::FUNC_ADDCHATMESSAGE);
        fnAddMsg((void*)*ppChat, color, text);
    }
    
    /**
     * Get checkpoint data from SA-MP memory
     * SA-MP manages its own checkpoint rendering
     */
    inline stCheckpoint* GetCurrentCheckpoint() {
        DWORD base = SAMPOffsets::GetSAMPBase();
        if (!base) return nullptr;
        
        DWORD* ppSamp = (DWORD*)(base + SAMPOffsets::SAMP_INFO_OFFSET);
        if (!ppSamp || !*ppSamp) return nullptr;
        
        // The checkpoint is usually at a fixed offset in the SAMP info struct
        // This needs to be found via reverse engineering for your exact version
        // Placeholder offset:
        return nullptr; // TODO: find correct offset
    }

    /**
     * Get race checkpoint data
     */
    inline stRaceCheckpoint* GetRaceCheckpoint() {
        DWORD base = SAMPOffsets::GetSAMPBase();
        if (!base) return nullptr;
        
        // TODO: find correct offset for your SA-MP version
        return nullptr;
    }
}
