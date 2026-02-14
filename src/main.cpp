/**
 * SA-MP 0.3DL Auto Forklift Job - ASI Plugin
 * Purpose: Testing anticheat detection capabilities
 * 
 * Usa un thread dedicado en vez de hookear funciones del juego.
 */

#include <windows.h>
#include <stdio.h>
#include "samp.h"
#include "game.h"
#include "forklift.h"

// ============================================================
// Globals
// ============================================================
static bool g_ModActive       = false;
static bool g_Running         = true;
static HANDLE g_Thread        = NULL;

// ============================================================
// Keyboard toggle (F5 to enable/disable)
// ============================================================
static void CheckToggle() {
    static bool keyWasDown = false;
    bool keyIsDown = (GetAsyncKeyState(VK_F5) & 0x8000) != 0;
    
    if (keyIsDown && !keyWasDown) {
        g_ModActive = !g_ModActive;
        
        // Beep para confirmar: agudo = activado, grave = desactivado
        if (g_ModActive) {
            Beep(1000, 200); // beep agudo
            Game::Log("[F5] Mod ACTIVADO");
        } else {
            Beep(400, 200);  // beep grave
            Game::Log("[F5] Mod DESACTIVADO");
            Forklift::Reset();
        }
        
        // Intentar mensaje en chat (puede fallar si offsets no coinciden)
        __try {
            if (g_ModActive) {
                Game::AddChatMessage(0xFF00FF00, "[Forklift] {FFFFFF}Mod ACTIVADO");
            } else {
                Game::AddChatMessage(0xFFFF0000, "[Forklift] {FFFFFF}Mod DESACTIVADO");
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            // Chat no disponible
        }
    }
    keyWasDown = keyIsDown;
}

// ============================================================
// Thread principal
// ============================================================
static DWORD WINAPI MainThread(LPVOID lpParam) {
    Game::Log("=== Forklift Plugin iniciado ===");
    
    // Esperar 15 segundos para que GTA SA + SA-MP carguen
    Sleep(15000);
    
    Game::Log("Espera inicial completada, entrando al loop principal");
    
    // Loop principal
    while (g_Running) {
        __try {
            CheckToggle();
            
            if (g_ModActive) {
                // Debug: loguear estado cada 3 segundos
                static DWORD lastDebug = 0;
                DWORD now = GetTickCount();
                if (now - lastDebug > 3000) {
                    lastDebug = now;
                    
                    char dbg[512];
                    
                    // Verificar player ped
                    DWORD ped = 0;
                    __try { ped = Game::GetPlayerPed(); } __except(EXCEPTION_EXECUTE_HANDLER) { ped = 0; }
                    
                    // Verificar vehículo
                    bool inVeh = false;
                    DWORD veh = 0;
                    __try { 
                        inVeh = Game::IsPlayerInVehicle(); 
                        veh = Game::GetPlayerVehicle();
                    } __except(EXCEPTION_EXECUTE_HANDLER) { inVeh = false; }
                    
                    // Verificar checkpoints
                    bool cpActive = false, rcpActive = false;
                    float cpX = 0, cpY = 0, cpZ = 0;
                    __try {
                        cpActive = Game::IsCheckpointActive();
                        rcpActive = Game::IsRaceCheckpointActive();
                        if (cpActive) {
                            Game::Vec3 cp = Game::GetCheckpointPosition();
                            cpX = cp.x; cpY = cp.y; cpZ = cp.z;
                        } else if (rcpActive) {
                            Game::Vec3 cp = Game::GetRaceCheckpointPosition();
                            cpX = cp.x; cpY = cp.y; cpZ = cp.z;
                        }
                    } __except(EXCEPTION_EXECUTE_HANDLER) { cpActive = false; rcpActive = false; }
                    
                    // Verificar posición del jugador
                    float px = 0, py = 0, pz = 0;
                    __try {
                        Game::Vec3 pos = Game::GetPlayerPosition();
                        px = pos.x; py = pos.y; pz = pos.z;
                    } __except(EXCEPTION_EXECUTE_HANDLER) {}
                    
                    snprintf(dbg, sizeof(dbg), "[DEBUG] State=%d Ped=0x%lX Veh=0x%lX InVeh=%d CP=%d RaceCP=%d Pos=(%.2f,%.2f,%.2f) CP=(%.2f,%.2f,%.2f)",
                        (int)Forklift::GetState(), ped, veh, inVeh ? 1 : 0, 
                        cpActive ? 1 : 0, rcpActive ? 1 : 0,
                        px, py, pz, cpX, cpY, cpZ);
                    Game::Log(dbg);
                }
                
                // Ejecutar lógica
                bool inVehicle = false;
                __try {
                    inVehicle = Game::IsPlayerInVehicle();
                } __except(EXCEPTION_EXECUTE_HANDLER) {
                    inVehicle = false;
                }
                
                if (inVehicle) {
                    __try {
                        Forklift::Update();
                    } __except(EXCEPTION_EXECUTE_HANDLER) {
                        Game::Log("ERROR: Excepcion en Forklift::Update()");
                    }
                }
            }
        }
        __except(EXCEPTION_EXECUTE_HANDLER) {
            Game::Log("ERROR: Excepcion en loop principal");
        }
        
        Sleep(100);
    }
    
    Game::Log("=== Plugin finalizado ===");
    return 0;
}

// ============================================================
// DLL Entry Point
// ============================================================
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hModule);
            g_Thread = CreateThread(NULL, 0, MainThread, NULL, 0, NULL);
            break;
            
        case DLL_PROCESS_DETACH:
            g_Running = false;
            if (g_Thread) {
                WaitForSingleObject(g_Thread, 2000);
                CloseHandle(g_Thread);
            }
            break;
    }
    return TRUE;
}
