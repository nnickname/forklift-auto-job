#pragma once
/**
 * Coastguard Auto-Job Module
 * 
 * State machine:
 *   IDLE → WAITING_CHECKPOINT → TELEPORTING → WAITING_NEXT_CP → ... (learning cycle 1)
 *   IDLE → TURBO_BLAST (cycle 2+, blasts through cached route)
 *        → RESTARTING (WarpIntoVehicle 30 + "2" key to restart route)
 *        → IDLE (next cycle)
 *
 * Strategy:
 *   Cycle 1 (Learning):
 *     1. Detect checkpoint, TP + Enter, wait for next, repeat
 *     2. Cache all CP positions as we go
 *   Cycle 2+ (Turbo):
 *     1. Blast through all cached positions with minimal delay
 *     2. TP + Enter each cached CP sequentially
 *   After each route:
 *     1. WarpIntoVehicleByModel(472) — scans GTA pool, finds closest boat
 *     2. Press 2 (start route)
 */

#include <windows.h>
#include <cmath>
#include <stdio.h>
#include "game.h"
#include "samp.h"
#include "raknet_sender.h"

namespace Coastguard {

    enum class State {
        IDLE,
        WAITING_CHECKPOINT,     // Looking for next CP in sequence
        TELEPORTING,            // TP to CP + Enter RPC
        WAITING_NEXT_CP,        // Wait for server to create next CP
        TURBO_BLAST,            // Blasting through cached route (cycle 2+)
        RESTARTING,             // Re-enter boat + /arrancar + press 2
        COOLDOWN,               // Brief pause between full cycles
    };

    struct Config {
        DWORD nextCpWaitMs     = 2500;   // Max wait for next CP to appear (route done if timeout)
        DWORD cooldownMs       = 1500;   // Cooldown between full cycles
        DWORD waitRandomMs     = 200;    // Random variation
        DWORD turboDelayMs     = 150;    // Delay between CPs in turbo mode (ms)
        WORD  boatModelId      = 472;    // Coastguard boat
        DWORD tpDelayMs        = 1500;   // Delay before TP (anti-crash)
        bool  checkVehicleModel = false;
        // Restart sequence timings
        DWORD restartInitialMs = 1500;   // Initial wait after route ends
        DWORD restartWarpRetryMs = 1000; // Retry warp every 1s until in vehicle
        DWORD restartMaxWaitMs = 15000;  // Max wait for vehicle respawn (15s timeout)
        DWORD restartAfterKeyMs = 2000;  // Wait after pressing 2 (route starts)
        DWORD keyHoldMs        = 150;    // How long to hold a key down
    };

    // Route cache — learned from first cycle
    static const int MAX_ROUTE_CPS = 50;
    static Game::Vec3 s_RouteCache[MAX_ROUTE_CPS];
    static bool       s_RouteCPIsRace[MAX_ROUTE_CPS];
    static int        s_RouteCacheSize = 0;
    static bool       s_RouteKnown = false;

    static State      s_State = State::IDLE;
    static DWORD      s_StateEntryTime = 0;
    static Config     s_Config;
    static Game::Vec3 s_CurrentCP = {0, 0, 0};
    static Game::Vec3 s_LastCompletedCP = {0, 0, 0};
    static bool       s_IsRace = false;
    static int        s_CycleCount = 0;
    static int        s_CPCount = 0;
    static bool       s_NewCPDetected = false;
    static int        s_TurboIndex = 0;
    static DWORD      s_LastTeleport = 0; // Last time we teleported
    
    // Restart sequence
    static int        s_RestartStep = 0;
    static bool       s_KeyHeld = false;   // Track if we're holding a key
    static DWORD      s_LastFPress = 0;    // Last time F was pressed (for retry)
    static DWORD      s_RestartBegin = 0;  // When restart sequence began (for timeout)

    // Simple random
    static DWORD s_RandSeed = 0;
    static float RandomFloat(float minVal, float maxVal) {
        if (s_RandSeed == 0) s_RandSeed = GetTickCount();
        s_RandSeed = s_RandSeed * 214013 + 2531011;
        DWORD r = (s_RandSeed >> 16) & 0x7FFF;
        float t = (float)r / 32767.0f;
        return minVal + t * (maxVal - minVal);
    }

    // ========================================
    // Key simulation — uses scan codes for DirectInput compatibility
    // ========================================
    static void PressKey(BYTE vk) {
        BYTE scan = (BYTE)MapVirtualKey(vk, MAPVK_VK_TO_VSC);
        INPUT inp = {};
        inp.type = INPUT_KEYBOARD;
        inp.ki.wVk = vk;
        inp.ki.wScan = scan;
        inp.ki.dwFlags = 0;
        SendInput(1, &inp, sizeof(INPUT));
        Game::Log("[Coastguard] Key DOWN: VK=0x%02X Scan=0x%02X", vk, scan);
    }

    static void ReleaseKey(BYTE vk) {
        BYTE scan = (BYTE)MapVirtualKey(vk, MAPVK_VK_TO_VSC);
        INPUT inp = {};
        inp.type = INPUT_KEYBOARD;
        inp.ki.wVk = vk;
        inp.ki.wScan = scan;
        inp.ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(1, &inp, sizeof(INPUT));
        Game::Log("[Coastguard] Key UP: VK=0x%02X", vk);
    }

    inline void Reset() {
        // Release any held keys
        if (s_KeyHeld) {
            ReleaseKey('2');
            s_KeyHeld = false;
        }
        s_State = State::IDLE;
        s_StateEntryTime = 0;
        s_CycleCount = 0;
        s_CPCount = 0;
        s_IsRace = false;
        s_CurrentCP = {0, 0, 0};
        s_LastCompletedCP = {0, 0, 0};
        s_NewCPDetected = false;
        s_TurboIndex = 0;
        s_RestartStep = 0;
    }

    inline void FullReset() {
        Reset();
        s_RouteKnown = false;
        s_RouteCacheSize = 0;
    }

    static void LogState(const char* action) {
        Game::Log("[Coastguard] Ciclo #%d CP #%d - %s (State=%d)", 
            s_CycleCount, s_CPCount, action, (int)s_State);
    }

    static bool IsDifferentPosition(Game::Vec3 a, Game::Vec3 b) {
        return (fabsf(a.x - b.x) > 1.0f || fabsf(a.y - b.y) > 1.0f || fabsf(a.z - b.z) > 1.0f);
    }

    static void CacheCP(Game::Vec3 pos, bool isRace) {
        if (s_RouteCacheSize < MAX_ROUTE_CPS) {
            s_RouteCache[s_RouteCacheSize] = pos;
            s_RouteCPIsRace[s_RouteCacheSize] = isRace;
            s_RouteCacheSize++;
            Game::Log("[Coastguard] Ruta cacheada: CP #%d (%.1f,%.1f,%.1f) %s", 
                s_RouteCacheSize, pos.x, pos.y, pos.z, isRace ? "RACE" : "NORMAL");
        }
    }

    // Called externally when checkpoint state changes
    static void OnCheckpointUpdate(bool active, Game::Vec3 pos, bool isRace) {
        if (!active) return;
        if (pos.x == 0.0f && pos.y == 0.0f && pos.z == 0.0f) return;
        if (fabsf(pos.x) > 20000.0f || fabsf(pos.y) > 20000.0f) return;

        s_IsRace = isRace;

        switch (s_State) {
            case State::WAITING_CHECKPOINT:
                s_CurrentCP = pos;
                s_NewCPDetected = true;
                break;
            case State::WAITING_NEXT_CP:
            {
                if (IsDifferentPosition(pos, s_LastCompletedCP)) {
                    s_CurrentCP = pos;
                    s_NewCPDetected = true;
                    char buf[128];
                    snprintf(buf, sizeof(buf), "Nuevo CP detectado! (%.1f,%.1f,%.1f)", pos.x, pos.y, pos.z);
                    LogState(buf);
                }
                break;
            }
            case State::COOLDOWN:
            case State::RESTARTING:
                s_CurrentCP = pos;
                s_NewCPDetected = true;
                break;
            default:
                break;
        }
    }

    // Check if we're in a state safe for admin checks
    static bool IsSafeForAdminCheck() {
        return (s_State == State::IDLE || 
                s_State == State::COOLDOWN || 
                s_State == State::RESTARTING);
    }

    // Main update - called every tick when mod is active
    inline void Update() {
        DWORD now = GetTickCount();

        if (s_Config.checkVehicleModel && s_State != State::RESTARTING) {
            WORD model = Game::GetVehicleModelId();
            if (model != s_Config.boatModelId) return;
        }

        switch (s_State) {
            // ========================================
            // IDLE: Start new cycle
            // ========================================
            case State::IDLE:
            {
                s_CycleCount++;
                s_CPCount = 0;
                s_CurrentCP = {0, 0, 0};
                s_LastCompletedCP = {0, 0, 0};
                s_NewCPDetected = false;
                s_IsRace = false;
                s_StateEntryTime = now;

                if (s_RouteKnown && s_RouteCacheSize > 0) {
                    s_TurboIndex = 0;
                    s_State = State::TURBO_BLAST;
                    Game::Log("[Coastguard] === TURBO MODE === Ciclo #%d - %d CPs cacheados, delay %dms",
                        s_CycleCount, s_RouteCacheSize, s_Config.turboDelayMs);
                } else {
                    s_State = State::WAITING_CHECKPOINT;
                    s_RouteCacheSize = 0;
                    LogState("LEARNING MODE: Buscando primer CP...");
                }
                break;
            }

            // ========================================
            // WAITING_CHECKPOINT: Wait for first CP (learning mode)
            // ========================================
            case State::WAITING_CHECKPOINT:
            {
                if (s_NewCPDetected) {
                    s_State = State::TELEPORTING;
                    s_StateEntryTime = now;
                    s_NewCPDetected = false;
                    char buf[128];
                    snprintf(buf, sizeof(buf), "CP encontrado! (%.1f,%.1f,%.1f)",
                        s_CurrentCP.x, s_CurrentCP.y, s_CurrentCP.z);
                    LogState(buf);
                }
                else if (Game::IsCheckpointActive()) {
                    Game::Vec3 cp = Game::GetCheckpointPosition();
                    if (fabsf(cp.x) > 1.0f || fabsf(cp.y) > 1.0f) {
                        s_CurrentCP = cp;
                        s_IsRace = false;
                        s_State = State::TELEPORTING;
                        s_StateEntryTime = now;
                        LogState("CP (mem)!");
                    }
                }
                else if (Game::IsRaceCheckpointActive()) {
                    Game::Vec3 rcp = Game::GetRaceCheckpointPosition();
                    if (fabsf(rcp.x) > 1.0f || fabsf(rcp.y) > 1.0f) {
                        s_CurrentCP = rcp;
                        s_IsRace = true;
                        s_State = State::TELEPORTING;
                        s_StateEntryTime = now;
                        LogState("RaceCP (mem)!");
                    }
                }
                break;
            }

            // ========================================
            // TELEPORTING: TP to CP + Enter (learning mode)
            // ========================================
            case State::TELEPORTING:
            {
                WORD vehID = SAMP::GetVehicleID();
                if (vehID != 0xFFFF) {
                    if (now - s_LastTeleport >= s_Config.tpDelayMs) {
                        s_LastTeleport = now;
                        Game::TeleportVehicle(s_CurrentCP.x, s_CurrentCP.y, s_CurrentCP.z);
                    }
                    Sender::SendFakeVehicleSync(vehID, s_CurrentCP.x, s_CurrentCP.y, s_CurrentCP.z);
                    
                    SAMP::SetInCheckpoint(true);
                    if (s_IsRace) {
                        Sender::SendEnterRaceCheckpoint();
                    } else {
                        Sender::SendEnterCheckpoint();
                    }
                    SAMP::SetInCheckpoint(false);
                    
                    CacheCP(s_CurrentCP, s_IsRace);
                    
                    s_CPCount++;
                    s_LastCompletedCP = s_CurrentCP;
                    s_NewCPDetected = false;
                    s_State = State::WAITING_NEXT_CP;
                    s_StateEntryTime = now;
                    
                    char buf[128];
                    snprintf(buf, sizeof(buf), "LEARN: TP + Enter CP #%d (%.1f,%.1f,%.1f)",
                        s_CPCount, s_CurrentCP.x, s_CurrentCP.y, s_CurrentCP.z);
                    LogState(buf);
                } else {
                    if (!Game::IsPlayerInVehicle()) {
                        s_State = State::IDLE;
                        LogState("Error: No vehicle. Reset.");
                    }
                }
                break;
            }

            // ========================================
            // WAITING_NEXT_CP: Wait for next CP (learning mode)
            // ========================================
            case State::WAITING_NEXT_CP:
            {
                Game::StabilizeVehicle();
                DWORD elapsed = now - s_StateEntryTime;
                
                if (s_NewCPDetected) {
                    s_State = State::TELEPORTING;
                    s_StateEntryTime = now;
                    s_NewCPDetected = false;
                }
                else if (elapsed >= 200) {
                    bool foundNew = false;
                    
                    if (Game::IsCheckpointActive()) {
                        Game::Vec3 cp = Game::GetCheckpointPosition();
                        if ((fabsf(cp.x) > 1.0f || fabsf(cp.y) > 1.0f) &&
                            IsDifferentPosition(cp, s_LastCompletedCP)) {
                            s_CurrentCP = cp;
                            s_IsRace = false;
                            foundNew = true;
                        }
                    }
                    if (!foundNew && Game::IsRaceCheckpointActive()) {
                        Game::Vec3 rcp = Game::GetRaceCheckpointPosition();
                        if ((fabsf(rcp.x) > 1.0f || fabsf(rcp.y) > 1.0f) &&
                            IsDifferentPosition(rcp, s_LastCompletedCP)) {
                            s_CurrentCP = rcp;
                            s_IsRace = true;
                            foundNew = true;
                        }
                    }
                    
                    if (foundNew) {
                        s_State = State::TELEPORTING;
                        s_StateEntryTime = now;
                        s_NewCPDetected = false;
                    }
                }
                
                // Timeout → route learned!
                if (!s_NewCPDetected && elapsed >= s_Config.nextCpWaitMs) {
                    if (!Game::IsCheckpointActive() && !Game::IsRaceCheckpointActive()) {
                        s_RouteKnown = true;
                        char buf[128];
                        snprintf(buf, sizeof(buf), 
                            "RUTA APRENDIDA! %d CPs -> RESTARTING",
                            s_RouteCacheSize);
                        LogState(buf);
                        Game::Log("[Coastguard] ========================================");
                        Game::Log("[Coastguard] RUTA CACHEADA: %d checkpoints", s_RouteCacheSize);
                        for (int i = 0; i < s_RouteCacheSize; i++) {
                            Game::Log("[Coastguard]   CP[%d] = (%.1f, %.1f, %.1f) %s", 
                                i, s_RouteCache[i].x, s_RouteCache[i].y, s_RouteCache[i].z,
                                s_RouteCPIsRace[i] ? "RACE" : "NORMAL");
                        }
                        Game::Log("[Coastguard] ========================================");
                        // Go to restart sequence
                        s_State = State::RESTARTING;
                        s_RestartStep = 0;
                        s_KeyHeld = false;
                        s_StateEntryTime = now;
                    } else {
                        Game::Log("[Coastguard] Timeout pero hay CP activo. Forzando re-read...");
                        s_LastCompletedCP = {0, 0, 0};
                    }
                }
                break;
            }

            // ========================================
            // TURBO_BLAST: Blast through cached route at max speed
            // ========================================
            case State::TURBO_BLAST:
            {
                DWORD elapsed = now - s_StateEntryTime;
                
                if (elapsed < s_Config.turboDelayMs) break;
                
                if (s_TurboIndex >= s_RouteCacheSize) {
                    char buf[128];
                    snprintf(buf, sizeof(buf), 
                        "TURBO COMPLETO! %d CPs -> RESTARTING",
                        s_RouteCacheSize);
                    LogState(buf);
                    // Go to restart sequence
                    s_State = State::RESTARTING;
                    s_RestartStep = 0;
                    s_KeyHeld = false;
                    s_StateEntryTime = now;
                    break;
                }
                
                WORD vehID = SAMP::GetVehicleID();
                if (vehID != 0xFFFF) {
                    Game::Vec3 cp = s_RouteCache[s_TurboIndex];
                    bool isRace = s_RouteCPIsRace[s_TurboIndex];
                    
                    Game::TeleportVehicle(cp.x, cp.y, cp.z + 1.0f);
                    Sender::SendFakeVehicleSync(vehID, cp.x, cp.y, cp.z);
                    
                    SAMP::SetInCheckpoint(true);
                    if (isRace) {
                        Sender::SendEnterRaceCheckpoint();
                    } else {
                        Sender::SendEnterCheckpoint();
                    }
                    SAMP::SetInCheckpoint(false);
                    
                    s_TurboIndex++;
                    s_CPCount++;
                    s_StateEntryTime = now;
                    
                    char buf[128];
                    snprintf(buf, sizeof(buf), "TURBO CP #%d/%d (%.1f,%.1f,%.1f)",
                        s_TurboIndex, s_RouteCacheSize, cp.x, cp.y, cp.z);
                    LogState(buf);
                } else {
                    if (!Game::IsPlayerInVehicle()) {
                        s_State = State::IDLE;
                        LogState("Error: No vehicle in TURBO. Reset.");
                    }
                }
                break;
            }

            // ========================================
            // RESTARTING: Warp into closest boat (model 472) + press 2
            // Step 0: Initial wait (1500ms)
            // Step 1: WarpIntoVehicleByModel(472) retry every 1s (max 15s)
            // Step 2: In vehicle! Press 2
            // Step 3: Release 2
            // Step 4: Wait → IDLE
            // ========================================
            case State::RESTARTING:
            {
                DWORD elapsed = now - s_StateEntryTime;

                switch (s_RestartStep) {
                    case 0: // Initial wait
                    {
                        if (elapsed >= s_Config.restartInitialMs) {
                            s_RestartBegin = now;
                            s_LastFPress = 0;
                            s_RestartStep = 1;
                            s_StateEntryTime = now;
                        }
                        break;
                    }
                    case 1: // Retry warp until in vehicle with VALID SAMP ID
                    {
                        WORD vehID = SAMP::GetVehicleID();
                        if (vehID != 0xFFFF && Game::IsPlayerInVehicle()) {
                            // Notify server we entered officially
                            Sender::SendEnterVehicle(vehID, 0); 
                            
                            s_RestartStep = 2; 
                            s_StateEntryTime = now;
                            break;
                        }
                        
                        DWORD totalElapsed = now - s_RestartBegin;
                        if (totalElapsed >= s_Config.restartMaxWaitMs) {
                            s_RestartStep = 0;
                            s_StateEntryTime = now;
                            break;
                        }
                        
                        if (now - s_LastFPress >= s_Config.restartWarpRetryMs) {
                            s_LastFPress = now;
                            Game::WarpIntoVehicleByModel(s_Config.boatModelId);
                        }
                        break;
                    }
                    case 2: // In vehicle → press 2
                    {
                        PressKey('2');
                        s_KeyHeld = true;
                        s_RestartStep = 3;
                        s_StateEntryTime = now;
                        break;
                    }
                    case 3: // Release 2
                    {
                        if (elapsed >= s_Config.keyHoldMs) {
                            ReleaseKey('2');
                            s_KeyHeld = false;
                            s_RestartStep = 4;
                            s_StateEntryTime = now;
                        }
                        break;
                    }
                    case 4: // Wait → IDLE
                    {
                        if (elapsed >= s_Config.restartAfterKeyMs) {
                            Game::Log("[Coastguard] Restart completado. Esperando CP #0...");
                            s_RestartStep = 0; // Reset step counter
                            s_State = State::IDLE;
                            s_StateEntryTime = now;
                        }
                        break;
                    }
                }
                break;
            }

            // ========================================
            // COOLDOWN: Brief pause (fallback, shouldn't normally reach here)
            // ========================================
            case State::COOLDOWN:
            {
                DWORD elapsed = now - s_StateEntryTime;
                
                if (s_NewCPDetected && s_RouteKnown) {
                    s_NewCPDetected = false;
                    s_CPCount = 0;
                    s_TurboIndex = 0;
                    s_State = State::TURBO_BLAST;
                    s_StateEntryTime = now;
                    s_CycleCount++;
                    LogState("TURBO reiniciado por CP detectado en cooldown!");
                }
                else if (s_NewCPDetected) {
                    s_NewCPDetected = false;
                    s_CPCount = 0;
                    s_LastCompletedCP = {0, 0, 0};
                    s_State = State::TELEPORTING;
                    s_StateEntryTime = now;
                    s_CycleCount++;
                }
                else if (elapsed >= s_Config.cooldownMs) {
                    s_CurrentCP = {0, 0, 0};
                    s_LastCompletedCP = {0, 0, 0};
                    s_NewCPDetected = false;
                    s_IsRace = false;
                    s_State = State::IDLE;
                    LogState("Cooldown terminado.");
                }
                break;
            }
        }
    }

    // Force trigger the boat warp sequence
    inline void StartRestart() {
        s_State = State::RESTARTING;
        s_RestartStep = 0;
        s_StateEntryTime = GetTickCount();
    }

    inline Config& GetConfig() { return s_Config; }
    inline State GetState() { return s_State; }
    inline int GetCycleCount() { return s_CycleCount; }
    inline int GetCPCount() { return s_CPCount; }
    inline bool IsRouteKnown() { return s_RouteKnown; }
    inline int GetRouteCacheSize() { return s_RouteCacheSize; }
}
