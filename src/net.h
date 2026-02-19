#pragma once
/**
 * RakNet BitStream & SA-MP Packet Sending
 *
 * BitStream layout matches RakNet 2.x used by SA-MP.
 * IncarData from SAMP-API: sampapi/0.3.DL-1/Synchronization.h
 *
 * RakClient pointer obtained via SAMP-API (CNetGame::m_pRakClient).
 */

#include <windows.h>
#include <string.h>
#include "samp.h"

namespace Net {

    // ════════════════════════════════════════════════════════
    // BitStream (RakNet 2.x memory layout — field order critical!)
    // ════════════════════════════════════════════════════════
    #pragma pack(push, 1)
    class BitStream {
    public:
        int            numberOfBitsUsed;       // 0x00
        int            numberOfBitsAllocated;  // 0x04
        int            readOffset;             // 0x08
        unsigned char* data;                   // 0x0C
        unsigned char  stackData[256];         // 0x10
        bool           copyData;               // 0x110

        BitStream()
            : numberOfBitsUsed(0)
            , numberOfBitsAllocated(256 * 8)
            , readOffset(0)
            , data(stackData)
            , copyData(false) {
            memset(stackData, 0, 256);
        }

        template <typename T>
        void Write(T v) {
            int bits = (int)(sizeof(T) * 8);
            if (numberOfBitsUsed + bits > numberOfBitsAllocated) return;
            memcpy(data + (numberOfBitsUsed / 8), &v, sizeof(T));
            numberOfBitsUsed += bits;
        }

        void WriteByte(unsigned char v) {
            if (numberOfBitsUsed + 8 > numberOfBitsAllocated) return;
            data[numberOfBitsUsed / 8] = v;
            numberOfBitsUsed += 8;
        }
    };

    // ════════════════════════════════════════════════════════
    // IncarData (matches Synchronization::IncarData, pack 1)
    //   Sent with packet ID 200 (ID_VEHICLE_SYNC)
    // ════════════════════════════════════════════════════════
    struct IncarData {
        WORD  vehicleId;
        short leftStickX;
        short leftStickY;
        short keys;
        float quaternion[4];
        float position[3];
        float speed[3];
        float vehicleHealth;
        unsigned char health;
        unsigned char armor;
        unsigned char weapon;
        bool  siren;
        bool  landingGear;
        WORD  trailerId;
        float trainSpeed;
    };
    #pragma pack(pop)

    // Keys bitmask for route-start ('2' key analog)
    // ButtonTriangle(16) | ShockButtonR(512) | LeftShoulder1(1)
    constexpr WORD KEY_START_ROUTE = 16 | 512 | 1;

    // ════════════════════════════════════════════════════════
    // RakClient vtable calls
    // ════════════════════════════════════════════════════════

    // RakClient::Send — vtable[6]
    inline bool Send(void* pRak, BitStream* bs) {
        if (!pRak || !bs) return false;
        __try {
            typedef bool(__thiscall* Fn)(void*, BitStream*, int, int, char);
            void** vt = *(void***)pRak;
            if (IsBadReadPtr(vt, 7 * sizeof(void*))) return false;
            Fn fn = (Fn)vt[6];
            if (IsBadCodePtr((FARPROC)fn)) return false;
            return fn(pRak, bs, 1/*HIGH_PRIORITY*/, 3/*RELIABLE_ORDERED*/, 0);
        } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    // RakClient::RPC — vtable[25]
    inline bool RPC(void* pRak, int id, BitStream* bs) {
        if (!pRak) return false;
        __try {
            typedef bool(__thiscall* Fn)(void*, int*, BitStream*, int, int, char, bool);
            void** vt = *(void***)pRak;
            if (IsBadReadPtr(vt, 26 * sizeof(void*))) return false;
            Fn fn = (Fn)vt[25];
            if (IsBadCodePtr((FARPROC)fn)) return false;
            return fn(pRak, &id, bs, 1/*HIGH_PRIORITY*/, 2/*RELIABLE*/, 0, false);
        } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    // ════════════════════════════════════════════════════════
    // High-level senders
    // ════════════════════════════════════════════════════════

    inline bool SendVehicleSync(WORD vehId, float x, float y, float z, WORD sKeys = 0) {
        void* pRak = SAMP::GetRakClient();
        if (!pRak) return false;

        IncarData d = {};
        d.vehicleId = vehId;
        d.keys = sKeys;
        d.quaternion[3] = 1.0f;  // identity quaternion
        d.position[0] = x;
        d.position[1] = y;
        d.position[2] = z;
        d.vehicleHealth = 1000.0f;
        d.health = 100;
        d.trailerId = 0xFFFF;

        BitStream bs;
        bs.WriteByte(200); // ID_VEHICLE_SYNC
        bs.Write(d.vehicleId);
        bs.Write(d.leftStickX);
        bs.Write(d.leftStickY);
        bs.Write(d.keys);
        for (int i = 0; i < 4; i++) bs.Write(d.quaternion[i]);
        for (int i = 0; i < 3; i++) bs.Write(d.position[i]);
        for (int i = 0; i < 3; i++) bs.Write(d.speed[i]);
        bs.Write(d.vehicleHealth);
        bs.Write(d.health);
        bs.Write(d.armor);
        bs.Write(d.weapon);
        bs.Write(d.siren);
        bs.Write(d.landingGear);
        bs.Write(d.trailerId);
        bs.Write(d.trainSpeed);

        return Send(pRak, &bs);
    }

    // RPC 25 — EnterCheckpoint
    inline bool SendEnterCheckpoint() {
        void* pRak = SAMP::GetRakClient();
        if (!pRak) return false;
        BitStream bs;
        return RPC(pRak, 25, &bs);
    }

    // RPC 27 — EnterRaceCheckpoint
    inline bool SendEnterRaceCheckpoint() {
        void* pRak = SAMP::GetRakClient();
        if (!pRak) return false;
        BitStream bs;
        return RPC(pRak, 27, &bs);
    }
}
