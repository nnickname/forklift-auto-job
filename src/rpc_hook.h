#pragma once
#include <windows.h>
#include <cstdint>
#include <iostream>

// RPC IDs
#define RPC_SetPlayerCheckpoint        107
#define RPC_SetPlayerRaceCheckpoint    38
#define RPC_DisablePlayerCheckpoint    37
#define RPC_DisablePlayerRaceCheckpoint 39

// SA-MP 0.3.DL R1 Offsets
#define SAMP_OFFSET_RPC_HOOK_ADDR      0x3A9ED
#define SAMP_OFFSET_INFO               0x2ACA24

// Hook Jump Back Address
DWORD dwHookReturn = 0;

// Structures definition (simplified)
struct RPCNode {
    void* uniqueIdentifier; // Some hash or ID
    // ...
};

struct RPCParameters {
    uint8_t* compressedBitStream; // BitStream pointer
    int numberOfBitsOfData;
    // ...
};

// Global function pointers
typedef void(__cdecl* PacketHandler_t)(int uniqueID, RPCParameters* params);

// This function needs to match the calling convention used by SA-MP (usually __stdcall or custom via assembly)
// Since we are hooking assembly, we handle regs in the naked function.
extern "C" void ProcessRPC(int rpcId, RPCParameters* params) {
    if (rpcId == RPC_SetPlayerCheckpoint) {
        // Read data from params->compressedBitStream
        // Note: You need RakNet BitStream class to read this properly
        // Or manually parse the bytes
        OutputDebugStringA("[SAMP-MOD] Received SetPlayerCheckpoint RPC!");
    }
    else if (rpcId == RPC_DisablePlayerCheckpoint) {
        OutputDebugStringA("[SAMP-MOD] Received DisablePlayerCheckpoint RPC!");
    }
    else if (rpcId == 124) { // RPC_TogglePlayerSpectating
        OutputDebugStringA("[SAMP-MOD] Received TogglePlayerSpectating RPC! (Ignoring?)");
        // To block this, you would need to NOT jump back to the original handler or 
        // modify the return address to skip the call. 
        // Currently this hook (as written in assembly below) executes the original code 
        // after this function returns. So this is just a logger.
    }
}

// Naked hook function
void __declspec(naked) Hook_HandleRPCPacket() {
    static int rpcId;
    static RPCParameters* pParams;
    static RPCNode* pNode;

    __asm {
        // Save registers
        pushad
        
        // Capture context from registers (Specific to 0.3.DL R1 at 0x3A9ED)
        // EAX = RPCParameters*
        // EDI = RPCNode*
        mov pParams, eax
        mov pNode, edi
        
        // Setup for our C++ handler
        // Usually RPC ID is inside RPCNode (uniqueIdentifier)
        // For RakNet, uniqueIdentifier is often the RPC ID (byte) if < 255
    }

    // Access RPC ID - in SA-MP's node structure, the first member is often the ID
    // Warning: uniqueIdentifier is void*, needs casting.
    // In mod_s0beit context, rpcId = pRPCNode->uniqueIdentifier
    rpcId = (int)pNode->uniqueIdentifier; 

    // Call our handler (protect registers)
    ProcessRPC(rpcId, pParams);

    __asm {
        // Restore registers
        popad

        // Execute valid code overwritten or required by the hook location
        // The instruction at 0x3A9ED might need to be replicated or we just jump back
        
        // In mod_s0beit: 
        // HandleRPCPacketFunc(pRPCNode->uniqueIdentifier, pRPCParams, pRPCNode->staticFunctionPointer);
        // It seems 0x3A9ED is the CALL instruction or just before it.
        
        // Based on s0beit:
        // push ...
        // call ...
        // We need to execute the original call or whatever was there.
        
        // Since we don't have the exact bytes of 0x3A9ED handy without disasm, 
        // a common strategy is to Place a CALL to our hook, and in our hook do the logic, then return.
        
        // JMP back to next instruction
        jmp dwHookReturn
    }
}

void InstallRPCHook() {
    DWORD sampBase = (DWORD)GetModuleHandleA("samp.dll");
    if (!sampBase) return;

    DWORD hookAddr = sampBase + SAMP_OFFSET_RPC_HOOK_ADDR;
    
    // Calculate return address (instruction length needs verification, usually 5-6 bytes)
    // NOTE: In mod_s0beit, they use a JMP detour of size 6 bytes.
    dwHookReturn = hookAddr + 6;

    // Install JMP hook
    DWORD oldProtect;
    VirtualProtect((void*)hookAddr, 6, PAGE_EXECUTE_READWRITE, &oldProtect);

    // E9 is JMP
    *(BYTE*)hookAddr = 0xE9;
    *(DWORD*)(hookAddr + 1) = (DWORD)&Hook_HandleRPCPacket - (hookAddr + 5);
    
    // NOP remaining byte if size is 6 and JMP uses 5
    *(BYTE*)(hookAddr + 5) = 0x90;

    VirtualProtect((void*)hookAddr, 6, oldProtect, &oldProtect);
}
