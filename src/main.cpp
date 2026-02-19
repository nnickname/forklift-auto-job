/**
 * SA-MP 0.3DL Auto Coastguard Job - ASI Plugin
 * 
 * Enhanced diagnostics for debugging checkpoint reading and packet sending.
 */

#include <windows.h>
#include <stdio.h>
#include "samp.h"
#include "game.h"
#include "coastguard.h"
#include "raknet_hook.h"

// ============================================================
// Globals
// ============================================================
static bool g_ModActive = false;
static bool g_Running   = true;
static HANDLE g_Thread  = NULL;
static bool g_PendingActivation = false; // Waiting for admin check before activation

// ============================================================
// Admin Detection System
// Reads chatlog.txt before/after /admins to count response lines.
// If > 2 lines → admins online → block/deactivate mod.
// ============================================================
namespace AdminCheck {
    enum class State { IDLE, SNAPSHOT, SENDING, WAITING, EVALUATING };

    static const DWORD RESPONSE_WAIT_MS = 2500;    // Wait for server response
    static const DWORD PERIODIC_MS = 60000;        // Re-check every 60 seconds
    static const int   MAX_SAFE_LINES = 1;      // DISABLED: admin check bypassed

    static State s_State = State::IDLE;
    static DWORD s_Timestamp = 0;
    static DWORD s_LastPeriodic = 0;
    static bool  s_IsPeriodic = false;

    // File-based approach: read chatlog.txt (reliable, no memory offsets)
    static char  s_ChatlogPath[MAX_PATH] = {0};
    static DWORD s_PreFileSize = 0;

    static bool FindChatlog() {
        if (s_ChatlogPath[0] != 0) return true;

        // Try 1: Documents\GTA San Andreas User Files\SAMP\chatlog.txt
        char docPath[MAX_PATH];
        if (GetEnvironmentVariableA("USERPROFILE", docPath, MAX_PATH)) {
            snprintf(s_ChatlogPath, MAX_PATH,
                "%s\\Documents\\GTA San Andreas User Files\\SAMP\\chatlog.txt", docPath);
            DWORD attr = GetFileAttributesA(s_ChatlogPath);
            if (attr != INVALID_FILE_ATTRIBUTES) {
                Game::Log("[ADMIN] chatlog.txt: %s", s_ChatlogPath);
                return true;
            }
        }

        // Try 2: GTA SA directory (fallback)
        char exePath[MAX_PATH];
        GetModuleFileNameA(NULL, exePath, MAX_PATH);
        char* lastSlash = strrchr(exePath, '\\');
        if (lastSlash) *(lastSlash + 1) = '\0';
        snprintf(s_ChatlogPath, MAX_PATH, "%schatlog.txt", exePath);
        DWORD attr = GetFileAttributesA(s_ChatlogPath);
        if (attr != INVALID_FILE_ATTRIBUTES) {
            Game::Log("[ADMIN] chatlog.txt (game dir): %s", s_ChatlogPath);
            return true;
        }

        Game::Log("[ADMIN] chatlog.txt NOT found in Documents or game dir");
        s_ChatlogPath[0] = 0;
        return false;
    }

    static DWORD GetChatlogSize() {
        if (!FindChatlog()) return 0;
        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (!GetFileAttributesExA(s_ChatlogPath, GetFileExInfoStandard, &fad))
            return 0;
        return fad.nFileSizeLow;
    }

    // Count newlines only in the NEW bytes (after s_PreFileSize)
    static int CountNewLines() {
        DWORD newSize = GetChatlogSize();
        if (newSize <= s_PreFileSize) return 0;
        DWORD diff = newSize - s_PreFileSize;

        HANDLE hFile = CreateFileA(s_ChatlogPath, GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (hFile == INVALID_HANDLE_VALUE) return 0;

        SetFilePointer(hFile, s_PreFileSize, NULL, FILE_BEGIN);
        char buf[4096];
        int lineCount = 0;
        DWORD remaining = diff;
        while (remaining > 0) {
            DWORD toRead = (remaining < sizeof(buf)) ? remaining : (DWORD)sizeof(buf);
            DWORD bytesRead = 0;
            if (!ReadFile(hFile, buf, toRead, &bytesRead, NULL) || bytesRead == 0) break;
            for (DWORD i = 0; i < bytesRead; i++) {
                if (buf[i] == '\n') lineCount++;
            }
            remaining -= bytesRead;
        }
        CloseHandle(hFile);
        Game::Log("[ADMIN] Chatlog: %lu->%lu (+%lu bytes), %d lines",
            s_PreFileSize, newSize, diff, lineCount);
        return lineCount;
    }

    static void BeginCheck(bool periodic) {
        s_IsPeriodic = periodic;
        s_State = State::SNAPSHOT;
    }

    static bool IsChecking() { return s_State != State::IDLE; }

    static bool NeedsPeriodic() {
        return (GetTickCount() - s_LastPeriodic >= PERIODIC_MS);
    }

    static bool Update(bool& adminsOnline, int& lineCount) {
        adminsOnline = false;
        lineCount = 0;
        switch (s_State) {
            case State::IDLE: return false;
            case State::SNAPSHOT:
                s_PreFileSize = GetChatlogSize();
                s_State = State::SENDING;
                Game::Log("[ADMIN] Snapshot: chatlog = %lu bytes", s_PreFileSize);
                return false;
            case State::SENDING:
                __try { SAMP::SendChat("/admins"); }
                __except(EXCEPTION_EXECUTE_HANDLER) {
                    Game::Log("[ADMIN] Exception sending /admins");
                    s_State = State::IDLE;
                    return true;
                }
                s_Timestamp = GetTickCount();
                s_State = State::WAITING;
                Game::Log("[ADMIN] /admins sent, waiting %dms...", RESPONSE_WAIT_MS);
                return false;
            case State::WAITING:
                if (GetTickCount() - s_Timestamp >= RESPONSE_WAIT_MS)
                    s_State = State::EVALUATING;
                return false;
            case State::EVALUATING:
                lineCount = CountNewLines();
                adminsOnline = (lineCount > MAX_SAFE_LINES);
                Game::Log("[ADMIN] Result: %d lines -> admins %s",
                    lineCount, adminsOnline ? "ONLINE" : "offline");
                s_State = State::IDLE;
                s_LastPeriodic = GetTickCount();
                return true;
        }
        return false;
    }
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
// Deactivation helper (used by F5 and admin auto-deactivate)
// ============================================================
static void FullDeactivate(const char* reason) {
    g_ModActive = false;
    g_PendingActivation = false;
    Coastguard::Reset();
    __try { Game::RestorePlayerState(); } __except(EXCEPTION_EXECUTE_HANDLER) {}
    Game::Log("[MOD] Desactivado: %s", reason);
}

// ============================================================
// F5 toggle (with admin check before activation)
// ============================================================
static void CheckToggle() {
    static bool keyWasDown = false;
    bool keyIsDown = (GetAsyncKeyState(VK_F5) & 0x8000) != 0;

    if (keyIsDown && !keyWasDown) {
        if (!g_ModActive && !g_PendingActivation) {
            // Want to activate → start admin check first
            g_PendingActivation = true;
            AdminCheck::BeginCheck(false);
            Beep(600, 100);
            __try {
                Game::AddChatMessage(0xFFFFFF00, "[Coastguard] {FFFFFF}Verificando admins...");
            } __except(EXCEPTION_EXECUTE_HANDLER) {}
            Game::Log("[F5] Admin check iniciado");

        } else if (g_PendingActivation) {
            // Cancel pending check
            g_PendingActivation = false;
            Beep(300, 100);
            __try {
                Game::AddChatMessage(0xFFFF0000, "[Coastguard] {FFFFFF}Activacion cancelada");
            } __except(EXCEPTION_EXECUTE_HANDLER) {}

        } else if (g_ModActive) {
            // Deactivate
            Beep(400, 150);
            FullDeactivate("F5 manual");
            __try {
                Game::AddChatMessage(0xFFFF0000, "[Coastguard] {FFFFFF}Mod DESACTIVADO");
            } __except(EXCEPTION_EXECUTE_HANDLER) {}
        }
    }
    keyWasDown = keyIsDown;
}

// ============================================================
// Main Thread
// ============================================================
static DWORD WINAPI MainThread(LPVOID lpParam) {
    Game::Log("=== Coastguard Plugin v1.0 iniciado ===");

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
                Coastguard::Reset();
            }
        }

        // ============================================
        // ADMIN CHECK - always process when a check is active
        // ============================================
        if (sampDetected && AdminCheck::IsChecking()) {
            __try {
                bool adminsOnline = false;
                int lineCount = 0;
                bool finished = AdminCheck::Update(adminsOnline, lineCount);
                
                if (finished) {
                    if (g_PendingActivation) {
                        // Initial check before activation
                        g_PendingActivation = false;
                        if (adminsOnline) {
                            Beep(200, 300);
                            char buf[128];
                            snprintf(buf, sizeof(buf),
                                "[Coastguard] {FF0000}ADMINS detectados (%d lineas). Mod NO activado.", lineCount);
                            Game::AddChatMessage(0xFFFF0000, buf);
                            Game::Log("[ADMIN] Activation blocked: %d lines", lineCount);
                        } else {
                            // No admins → activate!
                            g_ModActive = true;
                            Coastguard::StartRestart(); // Force warp into vehicle on start
                            Beep(1000, 150);
                            Game::AddChatMessage(0xFF00FF00,
                                "[Coastguard] {FFFFFF}Sin admins. Mod ACTIVADO - F5 para desactivar");
                            Game::Log("[ADMIN] No admins (%d lines), mod activated", lineCount);
                        }
                    } else if (g_ModActive) {
                        // Periodic check while active
                        if (adminsOnline) {
                            // EMERGENCY: deactivate + quit
                            Beep(200, 300); Sleep(100); Beep(200, 300);
                            FullDeactivate("Admins detectados (periodico)");
                            Game::Log("[ADMIN] EMERGENCY: Admin detected while active! Sending /q");
                            __try { SAMP::SendChat("/q"); }
                            __except(EXCEPTION_EXECUTE_HANDLER) {}
                        } else {
                            // No admins - all clear
                            Game::Log("[ADMIN] Periodic check OK: no admins (%d lines)", lineCount);
                        }
                    }
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                Game::Log("[ADMIN] Exception in admin check");
            }
        }

        // Start periodic admin re-check every 60s — runs in background regardless of
        // cycle state (non-blocking state machine, does not pause the route).
        if (g_ModActive && sampDetected && !AdminCheck::IsChecking()) {
            if (AdminCheck::NeedsPeriodic()) {
                AdminCheck::BeginCheck(true);
                Game::Log("[ADMIN] Periodic admin check started");
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

                    float cx = 0, cy = 0, cz = 0;
                    int cEnabled = 0;
                    SAMP::DiagReadCheckpoint(cx, cy, cz, cEnabled);
                    WORD vehID = SAMP::GetVehicleID();
                    bool cpActive = Game::IsCheckpointActive();
                    bool rcpActive = Game::IsRaceCheckpointActive();

                    Game::Log("[DBG] St=%d Ped=0x%lX Veh=%d VehID=%d Pos=(%.1f,%.1f,%.1f) CP(%.1f,%.1f,%.1f en=%d) cpAct=%d rcpAct=%d CPs=%d",
                        (int)Coastguard::GetState(), ped, inVeh ? 1 : 0, vehID,
                        p.x, p.y, p.z, cx, cy, cz, cEnabled,
                        cpActive ? 1 : 0, rcpActive ? 1 : 0, Coastguard::GetCPCount());
                } __except(EXCEPTION_EXECUTE_HANDLER) {
                    Game::Log("[DBG] Exception en debug log");
                }
            }

            // Coastguard logic
            __try {
                if (Game::IsPlayerInVehicle()) {
                    Coastguard::Update();
                    RakNetHook::Update();
                } else {
                    auto st = Coastguard::GetState();
                    if (st == Coastguard::State::RESTARTING) {
                        // Allow restart sequence while on foot (warping into vehicle)
                        Coastguard::Update();
                    } else if (st != Coastguard::State::IDLE && st != Coastguard::State::WAITING_CHECKPOINT) {
                        Coastguard::Reset();
                    }
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                Game::Log("[MAIN] Exception en logic loop");
            }
        }

        Sleep(10); // Fast tick for turbo mode
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
