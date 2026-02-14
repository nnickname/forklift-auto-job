#pragma once
#include <windows.h>
#include <cmath>
#include "samp.h"

namespace RakNet {

    // RakNet Packet Priorities
    enum PacketPriority {
        SYSTEM_PRIORITY,
        HIGH_PRIORITY,
        MEDIUM_PRIORITY,
        LOW_PRIORITY,
        NUMBER_OF_PRIORITIES
    };

    // RakNet Packet Reliability
    enum PacketReliability {
        UNRELIABLE,
        UNRELIABLE_SEQUENCED,
        RELIABLE,
        RELIABLE_ORDERED,
        RELIABLE_SEQUENCED
    };

    // simplified BitStream for writing
    class BitStream {
    public:
        unsigned char* data;
        int numberOfBitsUsed;
        int numberOfBitsAllocated;
        bool readOnly;
        // In real RakNet this is dynamic, but for simple packets a fixed buffer is safer to avoid alloc issues
        unsigned char stackData[256]; 
        bool copyData;

        BitStream() {
            data = stackData;
            numberOfBitsUsed = 0;
            numberOfBitsAllocated = 256 * 8;
            readOnly = false;
            copyData = false;
            memset(data, 0, 256);
        }

        void Write(unsigned char input) {
            if (numberOfBitsUsed + 8 > numberOfBitsAllocated) return; // Buffer overflow protection
            data[numberOfBitsUsed / 8] = input;
            numberOfBitsUsed += 8;
        }

        template <typename T>
        void Write(T input) {
            if (numberOfBitsUsed + sizeof(T)*8 > numberOfBitsAllocated) return;
            memcpy(data + (numberOfBitsUsed / 8), &input, sizeof(T));
            numberOfBitsUsed += sizeof(T) * 8;
        }

        // Write a vector (X, Y, Z)
        void WriteVector(float x, float y, float z) {
            Write(x);
            Write(y);
            Write(z);
        }
    };

    // RakClient Interface (Abstract Class)
    // We only define the virtual functions we need. 
    // The VTable order MUST be correct.
    class RakClientInterface {
    public:
        // VTable indices (approximate for RakNet 2.x used in SA-MP)
        // 0: Connect
        // 1: Disconnect
        // 2: InitializeSecurity
        // 3: SetPassword
        // 4: HasPassword
        // 5: Send (This is what we want)
        // ...
        
        virtual ~RakClientInterface() {}; // Destructor is usually index 0 in MSVC dtor list but let's assume methods start
        
        // This is a guess-work based on standard RakNet 2.x
        // We use a manual call via VTable pointer to be safe if we are unsure of the exact VTable order
    };

    // Helper to call Send function using VTable
    // Index 6 is commonly Send in RakNet 2.x (Connect, Disconnect, ..., Send)
    // Verify with 0.3.DL structure if possible.
    // For SA-MP 0.3.7, Send is index 6.
    inline bool CallRakClientSend(void* pRakClient, BitStream* bs, PacketPriority p, PacketReliability r, char ordering) {
        typedef bool (__thiscall* Send_t)(void*, BitStream*, PacketPriority, PacketReliability, char);
        // Get VTable
        void** vtable = *(void***)pRakClient;
        // Get function at index 6
        Send_t fnSend = (Send_t)vtable[6];
        return fnSend(pRakClient, bs, p, r, ordering);
    }

    // Packet structures
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
            WORD  HydraThrustAngle;
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

namespace Sender {

    // Get RakClient Pointer
    // In SA-MP 0.3.DL, RakClient is likely at struct + 0x2C inside SAMP Info
    inline void* GetRakClient() {
        DWORD base = SAMPOffsets::GetSAMPBase();
        if (!base) return nullptr;
        
        DWORD* pptStSamp = (DWORD*)(base + SAMPOffsets::SAMP_INFO_OFFSET);
        if (!pptStSamp || IsBadReadPtr(pptStSamp, 4)) return nullptr;
        
        void* pSAMP = (void*)*pptStSamp;
        if (!pSAMP) return nullptr;

        // Offset 0x2C is standard for recent SA-MP versions
        void* pRakClient = *(void**)((DWORD)pSAMP + 0x2C);
        return pRakClient;
    }

    // Send a Vehicle Sync packet with spoofed position
    inline void SendFakeVehicleSync(WORD vehicleId, float x, float y, float z) {
        void* pRakClient = GetRakClient();
        if (!pRakClient) return;

        RakNet::BitStream bs;
        
        // Header
        bs.Write((BYTE)200); // ID_VEHICLE_SYNC

        // Construct spoofed data
        RakNet::stInCarData data;
        memset(&data, 0, sizeof(data));
        
        data.sVehicleID = vehicleId;
        data.sKeys = 0;
        
        // Position
        data.fPosition[0] = x;
        data.fPosition[1] = y;
        data.fPosition[2] = z;

        // Quaternion (Identity)
        data.fQuaternion[0] = 0.0f; 
        data.fQuaternion[1] = 0.0f;
        data.fQuaternion[2] = 0.0f;
        data.fQuaternion[3] = 1.0f;

        // Health
        data.fVehicleHealth = 1000.0f;
        data.bytePlayerHealth = 100;
        data.byteArmor = 0;
        
        // Write struct
        bs.Write(data.sVehicleID);
        bs.Write(data.sLeftRightKeys);
        bs.Write(data.sUpDownKeys);
        bs.Write(data.sKeys);
        for(int i=0; i<4; i++) bs.Write(data.fQuaternion[i]);
        for(int i=0; i<3; i++) bs.Write(data.fPosition[i]);
        for(int i=0; i<3; i++) bs.Write(data.fMoveSpeed[i]);
        bs.Write(data.fVehicleHealth);
        bs.Write(data.bytePlayerHealth);
        bs.Write(data.byteArmor);
        bs.Write(data.byteCurrentWeapon);
        bs.Write(data.byteSiren);
        bs.Write(data.byteLandingGearState);
        bs.Write(data.sTrailerID);
        bs.Write(data.TrainSpeed); // or HydraThrustAngle

        // Send
        RakNet::CallRakClientSend(pRakClient, &bs, RakNet::HIGH_PRIORITY, RakNet::UNRELIABLE_SEQUENCED, 0);
    }
    
    // Send Player Sync (OnFoot)
    inline void SendFakePlayerSync(float x, float y, float z) {
        void* pRakClient = GetRakClient();
        if (!pRakClient) return;

        RakNet::BitStream bs;
        bs.Write((BYTE)207); // ID_PLAYER_SYNC

        RakNet::stOnFootData data;
        memset(&data, 0, sizeof(data));
        
        data.fPosition[0] = x;
        data.fPosition[1] = y;
        data.fPosition[2] = z;
        data.byteHealth = 100;
        
        // Write struct components... (simplified for brevity, needs full write)
        bs.Write(data.sLeftRightKeys);
        bs.Write(data.sUpDownKeys);
        bs.Write(data.sKeys);
        for(int i=0; i<3; i++) bs.Write(data.fPosition[i]);
        for(int i=0; i<4; i++) bs.Write(data.fQuaternion[i]);
        bs.Write(data.byteHealth);
        bs.Write(data.byteArmor);
        bs.Write(data.byteCurrentWeapon);
        bs.Write(data.byteSpecialAction);
        for(int i=0; i<3; i++) bs.Write(data.fMoveSpeed[i]);
        for(int i=0; i<3; i++) bs.Write(data.fSurfingOffsets[i]);
        bs.Write(data.sSurfingVehicleID);
        bs.Write(data.sCurrentAnimationID);
        bs.Write(data.sAnimFlags);

        RakNet::CallRakClientSend(pRakClient, &bs, RakNet::HIGH_PRIORITY, RakNet::UNRELIABLE_SEQUENCED, 0);
    }
}
