/**
 * SA-MP 0.3DL Auto Forklift Job - ASI Plugin
 * 
 * Enhanced diagnostics for debugging checkpoint reading and packet sending.
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
static bool g_ModActive = false;
static bool g_Running   = true;
static HANDLE g_Thread  = NULL;

// ============================================================
// Stealth: Server-side only interior trick for invisibility
// Strategy: Keep GTA interior=0 (normal map), but tell server interior=255
// This makes us invisible to other players without breaking the local map.
// virtualworld stays 0 so checkpoints keep working.
// ============================================================
static const BYTE STEALTH_INTERIOR = 255; // Interior ID to send to server
static bool g_StealthActive = false;
static DWORD g_LastStealthSync = 0; // Timestamp of last RPC re-send
static const DWORD STEALTH_RESYNC_MS = 2000; // Re-send RPC every 2 seconds

// Send SetInterior RPC to server (server-side only, does NOT touch GTA memory)
// RPC ID 118 = SetInteriorId in SA-MP
static void SendSetInteriorRPC(BYTE interior) {
    void* pRakClient = SAMP::GetRakClient();
    if (!pRakClient) return;
    
    __try {
        typedef bool(__thiscall* RPC_t)(void*, int*, void*, int, int, char, bool);
        void** vtable = *(void***)pRakClient;
        if (IsBadReadPtr(vtable, 26 * sizeof(void*))) return;
        RPC_t fnRPC = (RPC_t)vtable[25];
        if (IsBadCodePtr((FARPROC)fnRPC)) return;
        
        // Manual bitstream struct matching RakNet 2.x layout
        struct {
            int numberOfBitsUsed;       // 0x00
            int numberOfBitsAllocated;  // 0x04
            int readOffset;             // 0x08
            unsigned char* data;        // 0x0C
            unsigned char stackData[256]; // 0x10
            bool copyData;              // 0x110
        } bs;
        
        bs.numberOfBitsUsed = 8; // 1 byte = 8 bits
        bs.numberOfBitsAllocated = 256 * 8;
        bs.readOffset = 0;
        bs.data = bs.stackData;
        bs.copyData = false;
        memset(bs.stackData, 0, 256);
        bs.stackData[0] = interior;
        
        int rpcId = 118; // SetInteriorId
        fnRPC(pRakClient, &rpcId, &bs, 1, 2, 0, false); // HIGH_PRIORITY, RELIABLE
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Game::Log("[STEALTH] Exception sending SetInterior RPC");
    }
}

// Periodic stealth sync - re-sends interior RPC to counter SA-MP auto-correction
// SA-MP client detects GTA interior=0 and may try to re-sync with server,
// so we periodically override it back to 255.
static void StealthTick() {
    if (!g_StealthActive) return;
    
    DWORD now = GetTickCount();
    if (now - g_LastStealthSync >= STEALTH_RESYNC_MS) {
        g_LastStealthSync = now;
        SendSetInteriorRPC(STEALTH_INTERIOR);
    }
}

// Activate stealth mode (server-side only)
static void ActivateStealth() {
    if (g_StealthActive) return;
    g_StealthActive = true;
    g_LastStealthSync = GetTickCount();
    SendSetInteriorRPC(STEALTH_INTERIOR);
    Game::Log("[STEALTH] RPC sent: interior=%d (server-side invisible, GTA stays normal)", (int)STEALTH_INTERIOR);
}

// Deactivate stealth mode - tell server we're back to interior 0
static void DeactivateStealth() {
    if (!g_StealthActive) return;
    g_StealthActive = false;
    SendSetInteriorRPC(0);
    Game::Log("[STEALTH] RPC sent: interior=0 (visible again)");
}

// ============================================================
// Diagnóstico completo de la cadena de punteros SA-MP
// ============================================================
static void DumpDiagnostics(DWORD pNetGame) {
    Game::Log("--- DIAGNOSTICO SA-MP 0.3.DL ---");
    
    // RakClient
    DWORD pRakClient = 0;
    SAMP::SafeRead<DWORD>(pNetGame + SAMPOffsets::NETGAME_RAKCLIENT, pRakClient);
    Game::Log("  pRakClient=0x%lX (NetGame+0x2C)", pRakClient);

    // GameState
    int gameState = 0;
    SAMP::SafeRead<int>(pNetGame + SAMPOffsets::NETGAME_GAMESTATE, gameState);
    Game::Log("  gameState=%d (NetGame+0x3CD)", gameState);

    // Pools
    DWORD pPools = 0;
    SAMP::SafeRead<DWORD>(pNetGame + SAMPOffsets::NETGAME_POOLS, pPools);
    Game::Log("  pPools=0x%lX (NetGame+0x3DE)", pPools);

    if (pPools) {
        // Dump todos los punteros del pool para ver cuál es PlayerPool
        for (int i = 0; i < 10; i++) {
            DWORD val = 0;
            SAMP::SafeRead<DWORD>(pPools + i * 4, val);
            Game::Log("    Pools+0x%02X = 0x%lX", i * 4, val);
        }

        DWORD pPlayerPool = 0;
        SAMP::SafeRead<DWORD>(pPools + SAMPOffsets::POOLS_PLAYERPOOL, pPlayerPool);
        Game::Log("  pPlayerPool=0x%lX (Pools+0x08)", pPlayerPool);

        if (pPlayerPool) {
            // Dump primeros 64 bytes del PlayerPool para encontrar pLocalPlayer
            Game::Log("    PlayerPool raw dump (primeros 0x40 bytes):");
            for (int i = 0; i < 16; i++) {
                DWORD val = 0;
                SAMP::SafeRead<DWORD>(pPlayerPool + i * 4, val);
                Game::Log("      +0x%02X = 0x%08lX", i * 4, val);
            }

            // pLocalPlayer (offset 0x1E confirmado por dump SSO)
            DWORD pLocal = 0;
            SAMP::SafeRead<DWORD>(pPlayerPool + SAMPOffsets::PLAYERPOOL_LOCALPLAYER, pLocal);
            Game::Log("  pLocalPlayer=0x%lX (PlayerPool+0x1E)", pLocal);

            if (pLocal > 0x10000 && !IsBadReadPtr((void*)pLocal, 4)) {
                Game::Log("    pLocalPlayer VALIDO!");
                WORD vehID = 0xFFFF;
                SAMP::SafeRead<WORD>(pLocal + SAMPOffsets::LOCALPLAYER_VEHICLEID, vehID);
                Game::Log("    vehicleID=%d (LocalPlayer+0xFC)", vehID);
            } else {
                Game::Log("    pLocalPlayer INVALIDO (0x%lX)", pLocal);
            }
        }
    }

    // CGame (checkpoints)
    DWORD pCGame = SAMP::GetCGame();
    Game::Log("  pCGame=0x%lX (from global 0x2ACA3C)", pCGame);

    if (pCGame) {
        // Dump CGame: 3 punteros + checkpoint data
        Game::Log("    CGame raw dump (primeros 0x30 bytes):");
        for (int i = 0; i < 12; i++) {
            DWORD val = 0;
            SAMP::SafeRead<DWORD>(pCGame + i * 4, val);
            // También interpretar como float
            float fval = 0;
            SAMP::SafeRead<float>(pCGame + i * 4, fval);
            Game::Log("      +0x%02X = 0x%08lX (float: %.4f)", i * 4, val, fval);
        }
    }
    
    Game::Log("--- FIN DIAGNOSTICO ---");
}

// ============================================================
// F5 toggle
// ============================================================
static void CheckToggle() {
    static bool keyWasDown = false;
    bool keyIsDown = (GetAsyncKeyState(VK_F5) & 0x8000) != 0;

    if (keyIsDown && !keyWasDown) {
        g_ModActive = !g_ModActive;

        if (g_ModActive) {
            Beep(1000, 150);
            Game::Log("[F5] Mod ACTIVADO");
            // Activate stealth (interior 255 = invisible)
            __try {
                ActivateStealth();
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                Game::Log("[F5] Error activando stealth");
            }
        } else {
            Beep(400, 150);
            Game::Log("[F5] Mod DESACTIVADO");
            Forklift::Reset();
            // Restore stealth (back to interior 0 = visible)
            __try {
                DeactivateStealth();
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                Game::Log("[F5] Error desactivando stealth");
            }
            // Restaurar cámara y estado del jugador al desactivar
            __try {
                Game::RestorePlayerState();
                Game::Log("[F5] Camera y estado restaurados");
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                Game::Log("[F5] Error restaurando estado");
            }
        }

        __try {
            if (g_ModActive)
                Game::AddChatMessage(0xFF00FF00, "[Forklift] {FFFFFF}Mod ACTIVADO - F5 para desactivar");
            else
                Game::AddChatMessage(0xFFFF0000, "[Forklift] {FFFFFF}Mod DESACTIVADO");
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    keyWasDown = keyIsDown;
}

// ============================================================
// Main Thread
// ============================================================
static DWORD WINAPI MainThread(LPVOID lpParam) {
    Game::Log("=== Forklift Plugin v2.1 iniciado ===");

    // Espera inicial para que GTA cargue
    Sleep(10000);
    Game::Log("Espera inicial completada. Buscando samp.dll...");

    // Ready beep
    Beep(800, 100); Sleep(100); Beep(800, 100);

    // Estado de detección (NO es one-shot, se reintenta cada tick)
    bool sampDetected = false;
    bool diagDumped = false;
    DWORD lastDiagAttempt = 0;

    // ========================
    // MAIN LOOP
    // ========================
    while (g_Running) {
        __try {
            CheckToggle();
        } __except(EXCEPTION_EXECUTE_HANDLER) {}

        DWORD now = GetTickCount();

        // ============================================
        // DETECCIÓN CONTINUA DE SA-MP
        // ============================================
        if (!sampDetected) {
            // Intentar cada 2 segundos para no spamear
            if (now - lastDiagAttempt > 2000) {
                lastDiagAttempt = now;

                __try {
                    DWORD sampBase = SAMPOffsets::GetSAMPBase();
                    
                    if (!sampBase) {
                        // samp.dll no encontrado aún
                        static bool loggedOnce = false;
                        if (!loggedOnce) {
                            Game::Log("[DETECT] samp.dll no encontrado. Reintentando...");
                            loggedOnce = true;
                        }
                    } else {
                        // samp.dll encontrado! Verificar si está conectado
                        DWORD pNetGame = 0;
                        SAMP::SafeRead<DWORD>(sampBase + SAMPOffsets::SAMP_INFO_OFFSET, pNetGame);

                        if (!pNetGame) {
                            Game::Log("[DETECT] samp.dll=0x%lX pero pNetGame=NULL (aun inicializando...)", sampBase);
                        } else {
                            int gameState = 0;
                            SAMP::SafeRead<int>(pNetGame + SAMPOffsets::NETGAME_GAMESTATE, gameState);

                            if (gameState != SAMPOffsets::GAMESTATE_CONNECTED) {
                                Game::Log("[DETECT] pNetGame=0x%lX gameState=%d (esperando conexion, necesita %d)...",
                                    pNetGame, gameState, SAMPOffsets::GAMESTATE_CONNECTED);
                            } else {
                                // ¡CONECTADO!
                                sampDetected = true;
                                Game::Log("[DETECT] SA-MP CONECTADO! Base=0x%lX pNetGame=0x%lX", sampBase, pNetGame);
                                Beep(1200, 100); Sleep(50); Beep(1500, 100);

                                // Dump diagnóstico completo
                                DumpDiagnostics(pNetGame);
                            }
                        }
                    }
                } __except(EXCEPTION_EXECUTE_HANDLER) {
                    Game::Log("[DETECT] Exception durante deteccion");
                }
            }
        } else {
            // SA-MP ya detectado - verificar que sigue conectado
            if (!SAMP::IsInitialized()) {
                Game::Log("[DETECT] SA-MP desconectado! Reseteando deteccion...");
                sampDetected = false;
                diagDumped = false;
                Forklift::Reset();
            }
        }

        // ============================================
        // LÓGICA DEL MOD
        // ============================================
        if (g_ModActive && sampDetected) {
            // Debug log cada 3 segundos
            static DWORD lastDbg = 0;
            if (now - lastDbg > 3000) {
                lastDbg = now;
                __try {
                    DWORD ped = Game::GetPlayerPed();
                    bool inVeh = Game::IsPlayerInVehicle();
                    Game::Vec3 p = Game::GetPlayerPosition();

                    // Checkpoint diagnostic
                    float cx = 0, cy = 0, cz = 0;
                    int cEnabled = 0;
                    SAMP::DiagReadCheckpoint(cx, cy, cz, cEnabled);

                    WORD vehID = SAMP::GetVehicleID();

                    bool cpActive = Game::IsCheckpointActive();
                    bool rcpActive = Game::IsRaceCheckpointActive();

                    Game::Log("[DBG] St=%d Ped=0x%lX Veh=%d VehID=%d Pos=(%.1f,%.1f,%.1f) CP(%.1f,%.1f,%.1f en=%d) cpAct=%d rcpAct=%d",
                        (int)Forklift::GetState(), ped, inVeh ? 1 : 0, vehID,
                        p.x, p.y, p.z,
                        cx, cy, cz, cEnabled,
                        cpActive ? 1 : 0, rcpActive ? 1 : 0);
                } __except(EXCEPTION_EXECUTE_HANDLER) {
                    Game::Log("[DBG] Exception en debug log");
                }
            }

            // Forklift logic
            __try {
                if (Game::IsPlayerInVehicle()) {
                    Forklift::Update();
                    RakNetHook::Update();
                } else {
                    auto st = Forklift::GetState();
                    if (st == Forklift::State::TELEPORTING_PICKUP ||
                        st == Forklift::State::TELEPORTING_DELIVERY) {
                        Forklift::Reset();
                    }
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                Game::Log("[MAIN] Exception en logic loop");
            }

            // [STEALTH] Periodically re-send interior RPC to server
            // (does NOT touch GTA memory, only server-side)
            __try {
                StealthTick();
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                Game::Log("[STEALTH] Exception in StealthTick");
            }
        }

        Sleep(50);
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
