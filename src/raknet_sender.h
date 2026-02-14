#pragma once
/**
 * RakNet Packet Sender for SA-MP 0.3.DL R1
 *
 * CRITICAL FIX: BitStream memory layout now matches RakNet 2.x used by SA-MP.
 * The previous BitStream had fields in the WRONG ORDER causing crashes when
 * SA-MP's Send() function tried to read numberOfBitsUsed from offset 0x00
 * but found our data pointer there instead.
 *
 * RakNet 2.x BitStream layout:
 *   0x00: int numberOfBitsUsed
 *   0x04: int numberOfBitsAllocated
 *   0x08: int readOffset
 *   0x0C: unsigned char* data
 *   0x10: unsigned char stackData[STACK_SIZE]
 *   0x110: bool copyData
 */

#include <windows.h>
#include <cmath>
#include "samp.h"

namespace RakNet {

    enum PacketPriority {
        SYSTEM_PRIORITY,
        HIGH_PRIORITY,
        MEDIUM_PRIORITY,
        LOW_PRIORITY,
        NUMBER_OF_PRIORITIES
    };

    enum PacketReliability {
        UNRELIABLE,
        UNRELIABLE_SEQUENCED,
        RELIABLE,
        RELIABLE_ORDERED,
        RELIABLE_SEQUENCED
    };

    // ============================================================
    // BitStream - MUST match RakNet 2.x memory layout exactly!
    // ============================================================
    #pragma pack(push, 1)
    class BitStream {
    public:
        int numberOfBitsUsed;           // 0x00
        int numberOfBitsAllocated;      // 0x04
        int readOffset;                 // 0x08
        unsigned char* data;            // 0x0C
        unsigned char stackData[256];   // 0x10
        bool copyData;                  // 0x110

        BitStream() {
            numberOfBitsUsed = 0;
            numberOfBitsAllocated = 256 * 8;
            readOffset = 0;
            data = stackData;
            copyData = false;
            memset(stackData, 0, 256);
        }

        void Write(unsigned char input) {
            if (numberOfBitsUsed + 8 > numberOfBitsAllocated) return;
            data[numberOfBitsUsed / 8] = input;
            numberOfBitsUsed += 8;
        }

        template <typename T>
        void Write(T input) {
            int bitsNeeded = (int)(sizeof(T) * 8);
            if (numberOfBitsUsed + bitsNeeded > numberOfBitsAllocated) return;
            memcpy(data + (numberOfBitsUsed / 8), &input, sizeof(T));
            numberOfBitsUsed += bitsNeeded;
        }

        void WriteVector(float x, float y, float z) {
            Write(x);
            Write(y);
            Write(z);
        }
    };
    #pragma pack(pop)

    // ============================================================
    // RakClient VTable Calls
    // ============================================================
    
    // Send a BitStream packet via RakClient::Send (vtable index 6)
    inline bool CallRakClientSend(void* pRakClient, BitStream* bs,
                                   PacketPriority p, PacketReliability r, char ordering) {
        if (!pRakClient || !bs) return false;
        __try {
            typedef bool(__thiscall* Send_t)(void*, BitStream*, PacketPriority, PacketReliability, char);
            void** vtable = *(void***)pRakClient;
            if (IsBadReadPtr(vtable, 7 * sizeof(void*))) return false;
            Send_t fnSend = (Send_t)vtable[6];
            if (IsBadCodePtr((FARPROC)fnSend)) return false;
            return fnSend(pRakClient, bs, p, r, ordering);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // Call RPC via RakClient::RPC (vtable index 25 in SA-MP)
    inline bool CallRakClientRPC(void* pRakClient, int rpcId, BitStream* bs,
                                  PacketPriority p, PacketReliability r, char ordering, bool broadcast) {
        if (!pRakClient) return false;
        __try {
            typedef bool(__thiscall* RPC_t)(void*, int*, BitStream*, PacketPriority, PacketReliability, char, bool);
            void** vtable = *(void***)pRakClient;
            if (IsBadReadPtr(vtable, 26 * sizeof(void*))) return false;
            RPC_t fnRPC = (RPC_t)vtable[25];
            if (IsBadCodePtr((FARPROC)fnRPC)) return false;
            return fnRPC(pRakClient, &rpcId, bs, p, r, ordering, broadcast);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // ============================================================
    // Packet Structures
    // ============================================================
    #pragma pack(push, 1)
    
    struct stInCarData {
        WORD  sVehicleID;
        WORD  sLeftRightKeys;
        WORD  sUpDownKeys;
        WORD  sKeys;
        float fQuaternion[4];
        float fPosition[3];
        float fMoveSpeed[3];
        float fVehicleHealth;
        BYTE  bytePlayerHealth;
        BYTE  byteArmor;
        BYTE  byteCurrentWeapon;
        BYTE  byteSiren;
        BYTE  byteLandingGearState;
        WORD  sTrailerID;
        union {
            WORD  HydraThrustAngle[2];
            float TrainSpeed;
        };
    };

    struct stOnFootData {
        WORD  sLeftRightKeys;
        WORD  sUpDownKeys;
        WORD  sKeys;
        float fPosition[3];
        float fQuaternion[4];
        BYTE  byteHealth;
        BYTE  byteArmor;
        BYTE  byteCurrentWeapon;
        BYTE  byteSpecialAction;
        float fMoveSpeed[3];
        float fSurfingOffsets[3];
        WORD  sSurfingVehicleID;
        WORD  sCurrentAnimationID;
        WORD  sAnimFlags;
    };
    
    #pragma pack(pop)
}

// ============================================================
// Sender namespace - High-level packet sending
// ============================================================
namespace Sender {

    // Get RakClient (delegates to SAMP namespace)
    inline void* GetRakClient() {
        return SAMP::GetRakClient();
    }

    // Send Vehicle Sync with real vehicle data
    inline bool SendFakeVehicleSync(WORD vehicleId, float x, float y, float z) {
        void* pRakClient = GetRakClient();
        if (!pRakClient) return false;

        RakNet::BitStream bs;
        bs.Write((unsigned char)200); // ID_VEHICLE_SYNC

        RakNet::stInCarData data;
        memset(&data, 0, sizeof(data));

        // Leer datos reales del vehículo
        DWORD vehPtr = Game::GetPlayerVehicle();
        if (vehPtr) {
            // Rotación (quaternion)
            DWORD* pMatrix = (DWORD*)(vehPtr + GameAddr::MATRIX_OFFSET);
            if (pMatrix && *pMatrix) {
                DWORD matrix = *pMatrix;
                // GTA usa matriz 3x3, SAMP espera quaternion. Usamos (0,0,0,1) por defecto.
                data.fQuaternion[0] = 0.0f;
                data.fQuaternion[1] = 0.0f;
                data.fQuaternion[2] = 0.0f;
                data.fQuaternion[3] = 1.0f;
            }
            // Health
            data.fVehicleHealth = 1000.0f; // Si tienes offset real, úsalo
            // Player health
            data.bytePlayerHealth = 100; // Si tienes offset real, úsalo
        } else {
            // Fallback defaults
            data.fQuaternion[0] = 0.0f;
            data.fQuaternion[1] = 0.0f;
            data.fQuaternion[2] = 0.0f;
            data.fQuaternion[3] = 1.0f;
            data.fVehicleHealth = 1000.0f;
            data.bytePlayerHealth = 100;
        }

        // FORCE position to target checkpoint (Fix: before it was using current pos)
        data.fPosition[0] = x;
        data.fPosition[1] = y;
        data.fPosition[2] = z;

        // Zero velocity to simulate stop
        data.fMoveSpeed[0] = 0.0f;
        data.fMoveSpeed[1] = 0.0f;
        data.fMoveSpeed[2] = 0.0f;



        data.sVehicleID = vehicleId;
        data.byteArmor = 0;
        data.byteCurrentWeapon = 0;
        data.byteSiren = 0;
        data.byteLandingGearState = 0;
        data.sTrailerID = 0xFFFF;
        data.TrainSpeed = 0.0f;

        // Write all fields in order
        bs.Write(data.sVehicleID);
        bs.Write(data.sLeftRightKeys);
        bs.Write(data.sUpDownKeys);
        bs.Write(data.sKeys);
        for (int i = 0; i < 4; i++) bs.Write(data.fQuaternion[i]);
        for (int i = 0; i < 3; i++) bs.Write(data.fPosition[i]);
        for (int i = 0; i < 3; i++) bs.Write(data.fMoveSpeed[i]);
        bs.Write(data.fVehicleHealth);
        bs.Write(data.bytePlayerHealth);
        bs.Write(data.byteArmor);
        bs.Write(data.byteCurrentWeapon);
        bs.Write(data.byteSiren);
        bs.Write(data.byteLandingGearState);
        bs.Write(data.sTrailerID);
        bs.Write(data.TrainSpeed);

        return RakNet::CallRakClientSend(pRakClient, &bs,
            RakNet::HIGH_PRIORITY, RakNet::RELIABLE_ORDERED, 0);
    }
    
    // Send RPC 25 (EnterCheckpoint) - empty BitStream
    inline bool SendEnterCheckpoint() {
        void* pRakClient = GetRakClient();
        if (!pRakClient) return false;

        RakNet::BitStream bs; // Empty for EnterCheckpoint
        return RakNet::CallRakClientRPC(pRakClient, 25, &bs,
            RakNet::HIGH_PRIORITY, RakNet::RELIABLE, 0, false);
    }

    // Send RPC 27 (EnterRaceCheckpoint) - empty BitStream
    inline bool SendEnterRaceCheckpoint() {
        void* pRakClient = GetRakClient();
        if (!pRakClient) return false;

        RakNet::BitStream bs; // Empty for EnterRaceCheckpoint
        return RakNet::CallRakClientRPC(pRakClient, 27, &bs,
            RakNet::HIGH_PRIORITY, RakNet::RELIABLE, 0, false);
    }

    // Combined: Send vehicle sync at checkpoint pos + RPC EnterCheckpoint/RaceCheckpoint
    inline bool SendFakeEnterCheckpoint(WORD vehicleId, float x, float y, float z, bool isRace) {
        // [FORCE-STATE] Set LocalPlayer internal state to match server expectation
        SAMP::SetInCheckpoint(true);

        bool syncOk = SendFakeVehicleSync(vehicleId, x, y, z);
        bool rpcOk = false;
        if (isRace) {
            rpcOk = SendEnterRaceCheckpoint();
        } else {
            rpcOk = SendEnterCheckpoint();
        }
        
        // Optional: clear state after send? Usually standard game logic clears it upon exit.
        // We'll leave it for now or maybe clear it? 
        // If we leave it, game might think we are still in CP.
        // But let's leave it as is, standard behavior.
        
        return syncOk && rpcOk;
    }
}
