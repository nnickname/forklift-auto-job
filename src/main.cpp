/**
 * SA-MP 0.3DL Auto Forklift Job - ASI Plugin
 * Purpose: Testing anticheat detection capabilities
 * 
 * Uses a hook on the main loop (CGame::Process) to run on the main thread.
 * Address of the call to CGame::Process: 0x53E981 (Call to 0x561B10)
 * Original bytes: E8 8A 31 02 00
 */

#include <windows.h>
#include <stdio.h>
#include "samp.h"
#include "game.h"
#include "forklift.h"
#include "raknet_hook.h"

// ============================================================
// Globals
// ============================================================
static bool g_ModActive       = false;

// ============================================================
// Hooking Helpers
// ============================================================
namespace Memory {
    void InstallCallHook(DWORD address, DWORD hookFunction) {
        DWORD oldProtect;
        VirtualProtect((void*)address, 5, PAGE_EXECUTE_READWRITE, &oldProtect);
        
        // Calculate relative offset: target - (address + 5)
        // address + 5 is the return address (next instruction)
        DWORD offset = hookFunction - (address + 5);
        
        *(BYTE*)(address) = 0xE8; // CALL opcode
        *(DWORD*)(address + 1) = offset;
        
        VirtualProtect((void*)address, 5, oldProtect, &oldProtect);
    }
}

// ============================================================
// Keyboard toggle (F5 to enable/disable)
// ============================================================
static void CheckToggle() {
    static bool keyWasDown = false;
    bool keyIsDown = (GetAsyncKeyState(VK_F5) & 0x8000) != 0;
    
    if (keyIsDown && !keyWasDown) {
        g_ModActive = !g_ModActive;
        // Use standard output debug string just in case
        OutputDebugStringA(g_ModActive ? "[Forklift] F5: ON" : "[Forklift] F5: OFF");
        
        if (g_ModActive) {
            MessageBeep(MB_ICONASTERISK); // Async, system sound
            Game::Log("[F5] Mod ACTIVADO");
        } else {
            MessageBeep(MB_ICONHAND);
            Game::Log("[F5] Mod DESACTIVADO");
            Forklift::Reset();
        }
        
        // Intentar mensaje en chat
        __try {
            if (g_ModActive) {
                Game::AddChatMessage(0xFF00FF00, "[Forklift] {FFFFFF}Mod ACTIVADO");
            } else {
                Game::AddChatMessage(0xFFFF0000, "[Forklift] {FFFFFF}Mod DESACTIVADO");
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    keyWasDown = keyIsDown;
}

// ============================================================
// Hook Function
// ============================================================
typedef void (__cdecl *CGameProcess_t)();
// CGame::Process is at 0x561B10 in GTA SA v1.0 US
static CGameProcess_t OriginalCGameProcess = (CGameProcess_t)0x561B10;

void __cdecl Hooked_CGameProcess() {
    static bool init = false;
    static DWORD frameCount = 0;
    
    if (!init) {
        Game::Log("=== Forklift Plugin Hook Initialized (First Frame) ===");
        init = true;
    }
    
    if (frameCount % 600 == 0) { // Log heartbeat roughly every 10-20 seconds
         // Game::Log("Heartbeat: Hook is running (Frame %d)", frameCount);
    }
    frameCount++;

    // Run our logic
    __try {
        CheckToggle();
        
        if (g_ModActive) {
            // Debug logging (throttled)
            static DWORD lastDebug = 0;
            DWORD now = GetTickCount();
            if (now - lastDebug > 3000) {
                lastDebug = now;
                
                char dbg[512];
                // Check player ped
                DWORD ped = 0;
                // Safely get ped
                if (Game::IsPlayerInVehicle()) { // Uses GetPlayerPed internally
                    ped = Game::GetPlayerPed();
                }
                
                // Check vehicle
                bool inVeh = false;
                DWORD veh = 0;
                
                inVeh = Game::IsPlayerInVehicle();
                if (inVeh) veh = Game::GetPlayerVehicle();
                
                // Check checkpoints
                bool cpActive = false, rcpActive = false;
                float cpX = 0, cpY = 0, cpZ = 0;
                
                cpActive = Game::IsCheckpointActive();
                if (cpActive) {
                    Game::Vec3 cp = Game::GetCheckpointPosition();
                    cpX = cp.x; cpY = cp.y; cpZ = cp.z;
                } else {
                    rcpActive = Game::IsRaceCheckpointActive();
                    if (rcpActive) {
                        Game::Vec3 cp = Game::GetRaceCheckpointPosition();
                        cpX = cp.x; cpY = cp.y; cpZ = cp.z;
                    }
                }
                
                // Check position
                float px = 0, py = 0, pz = 0;
                Game::Vec3 pos = Game::GetPlayerPosition();
                px = pos.x; py = pos.y; pz = pos.z;
                
                snprintf(dbg, sizeof(dbg), "[DEBUG] State=%d Ped=0x%lX Veh=0x%lX InVeh=%d CP=%d RaceCP=%d Pos=(%.2f,%.2f,%.2f) CP=(%.2f,%.2f,%.2f)",
                    (int)Forklift::GetState(), ped, veh, inVeh ? 1 : 0, 
                    cpActive ? 1 : 0, rcpActive ? 1 : 0,
                    px, py, pz, cpX, cpY, cpZ);
                Game::Log(dbg);
            }
            
            // Execute mod logic
            if (Game::IsPlayerInVehicle()) {
                 // Hybrid approach: Check normal game memory AND RakNet/SAMP structs
                 Forklift::Update();
                 RakNetHook::Update(); // Checks SAMP internal struct
            }
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        // Only log critical failures, don't spam
        static DWORD lastError = 0;
        if (GetTickCount() - lastError > 1000) {
             Game::Log("CRITICAL ERROR: Exception inside Main Loop Logic");
             lastError = GetTickCount();
        }
    }

    // Call the original game function
    OriginalCGameProcess();
}


// ============================================================
// DLL Entry Point
// ============================================================
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hModule);
            // Install the hook on the call to CGame::Process inside the main loop (Idle state)
            // Address: 0x53E981
            Memory::InstallCallHook(0x53E981, (DWORD)Hooked_CGameProcess);
            Game::Log("=== Forklift Plugin Loaded (Main Thread Hook) ===");
            break;
            
        case DLL_PROCESS_DETACH:
            // Hooks are generally left in place on detach if the process is ending
            // If unloading dynamically, we should restore original bytes, but ASIs usually don't unload
            break;
    }
    return TRUE;
}
