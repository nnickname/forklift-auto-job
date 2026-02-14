#pragma once
/**
 * RakNet Hooking for SA-MP 0.3.DL
 * Intercepts incoming RPCs (Remote Procedure Calls) to detect Checkpoints immediately.
 */

#include <windows.h>
#include "samp.h"
#include "forklift.h"

namespace RakNetHook {

    // ============================================================
    // RakNet Structures
    // ============================================================
    #pragma pack(push, 1)
    struct stRakNetParams {
        char*     input;          // BitStream data
        DWORD     numberOfBitsOfData;
        DWORD     pSender;
    };
    #pragma pack(pop)

    // RPC IDs
    constexpr int RPC_SetPlayerCheckpoint = 107;
    constexpr int RPC_SetPlayerRaceCheckpoint = 38;
    constexpr int RPC_DisablePlayerCheckpoint = 37;
    constexpr int RPC_DisablePlayerRaceCheckpoint = 39;

    // Hook globals
    static bool g_HookInstalled = false;
    static DWORD g_OriginalHandler = 0;

    // BitStream reading helper (Simplified)
    class BitStream {
    public:
        unsigned char* data;
        int lengthInBits;
        int currentReadBit;

        BitStream(char* _data, int _lengthInBits) {
            data = (unsigned char*)_data;
            lengthInBits = _lengthInBits;
            currentReadBit = 0;
        }

        template <typename T>
        void Read(T& out) {
            if (currentReadBit + sizeof(T)*8 > lengthInBits) return;
            // Simplified byte-aligned read (works for standard types usually)
            // Note: RakNet is NOT always byte-aligned, but RPCs usually start aligned
            memcpy(&out, data + (currentReadBit / 8), sizeof(T));
            currentReadBit += sizeof(T) * 8;
        }
        
        // Skip bits (padding)
        void IgnoreBits(int numberOfBits) {
            currentReadBit += numberOfBits;
        }
    };

    // ============================================================
    // The Hook Logic
    // ============================================================
    
    // We update our internal state based on the packet
    void ProcessRPC(int rpcId, char* data, int bits) {
        Game::Log("[RakNet] Received RPC ID: %d (Bits: %d)", rpcId, bits);

        if (rpcId == RPC_SetPlayerCheckpoint) {
            BitStream bs(data, bits);
            float x, y, z, size;
            bs.Read(x);
            bs.Read(y);
            bs.Read(z);
            bs.Read(size);
            
            Game::Log("[RakNet] SetCheckpoint: (%.2f, %.2f, %.2f) Size: %.2f", x, y, z, size);
            
            // Force update our forklift state immediately
            Forklift::OnCheckpointUpdate(true, {x, y, z});
        }
        else if (rpcId == RPC_SetPlayerRaceCheckpoint) {
            BitStream bs(data, bits);
            BYTE type;
            float x, y, z, nextX, nextY, nextZ, size;
            
            bs.Read(type);
            bs.Read(x);
            bs.Read(y);
            bs.Read(z);
            bs.Read(nextX);
            bs.Read(nextY);
            bs.Read(nextZ);
            bs.Read(size);

            Game::Log("[RakNet] SetRaceCheckpoint: (%.2f, %.2f, %.2f) Type: %d", x, y, z, type);
            
            // Force update our forklift state immediately
            Forklift::OnCheckpointUpdate(true, {x, y, z});
        }
        else if (rpcId == RPC_DisablePlayerCheckpoint || rpcId == RPC_DisablePlayerRaceCheckpoint) {
            Game::Log("[RakNet] DisableCheckpoint");
            Forklift::OnCheckpointUpdate(false, {0, 0, 0});
        }
    }

    // ============================================================
    // Hook installation
    // For simplicity/safety, instead of a raw ASM hook on the dispatcher,
    // we will monitor the internal SA-MP Checkpoint Structs which is safer.
    // Raw RakNet hooking requires complex BitStream implementation which crashes easily if wrong.
    // ============================================================
    
    // Pointer to SA-MP Internal Misc Info (Trusted Source)
    // 0.3.DL Addr: samp.dll + 0x2ACA3C
    struct stSAMPMiscInfo { // Simplified stGameInfo
        char pad_0[0x19];
        // Race Checkpoint
        float fRaceCheckpointPos[3];
        float fRaceCheckpointNext[3];
        float fRaceCheckpointSize;
        BYTE  byteRaceType;
        int   bRaceCheckpointsEnabled; // 1 if active
        // Checkpoint
        float fCheckpointPos[3];
        float fCheckpointExtent[3];
        int   bCheckpointsEnabled;     // 1 if active
    };

    inline stSAMPMiscInfo* GetMiscInfo() {
        DWORD base = SAMPOffsets::GetSAMPBase();
        if (!base) return nullptr;
        DWORD* ptr = (DWORD*)(base + 0x2ACA3C); // 0.3.DL specific
        if (!ptr || IsBadReadPtr(ptr, 4)) return nullptr;
        return (stSAMPMiscInfo*)(*ptr);
    }
    
    // Called from Main Loop
    inline void Update() {
        stSAMPMiscInfo* info = GetMiscInfo();
        if (!info || IsBadReadPtr(info, sizeof(stSAMPMiscInfo))) return;

        bool active = false;
        Game::Vec3 pos = {0,0,0};

        // Priority 1: Checkpoint
        if (info->bCheckpointsEnabled) {
            active = true;
            pos.x = info->fCheckpointPos[0];
            pos.y = info->fCheckpointPos[1];
            pos.z = info->fCheckpointPos[2];
        }
        // Priority 2: Race Checkpoint
        else if (info->bRaceCheckpointsEnabled) {
            active = true;
            pos.x = info->fRaceCheckpointPos[0];
            pos.y = info->fRaceCheckpointPos[1];
            pos.z = info->fRaceCheckpointPos[2];
        }
        
        // Push to forklift logic
        if (active) {
            // Only update if changed or unitialized to avoid spamming logs (logic handled in Forklift)
            Forklift::OnCheckpointUpdate(true, pos);
        }
    }
}
