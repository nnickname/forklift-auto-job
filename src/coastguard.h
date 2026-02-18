#pragma once
/**
 * Coastguard Auto-Job Module
 * 
 * State machine:
 *   IDLE → WAITING_CHECKPOINT → TELEPORTING → WAITING_NEXT_CP → ... (learning cycle 1)
 *   IDLE → TURBO_BLAST (cycle 2+, blasts through cached route)
 *        → COOLDOWN → IDLE (next cycle)
 *
 * Strategy:
 *   Cycle 1 (Learning):
 *     1. Detect checkpoint, TP + Enter, wait for next, repeat
 *     2. Cache all CP positions as we go
 *   Cycle 2+ (Turbo):
 *     1. Blast through all cached positions with minimal delay
 *     2. TP + Enter each cached CP sequentially
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
        COOLDOWN,               // Brief pause between full cycles
    };

    struct Config {
        DWORD nextCpWaitMs     = 2500;   // Max wait for next CP to appear (route done if timeout)
        DWORD cooldownMs       = 1500;   // Cooldown between full cycles
        DWORD waitRandomMs     = 200;    // Random variation
        DWORD turboDelayMs     = 150;    // Delay between CPs in turbo mode (ms)
        WORD  boatModelId      = 472;    // Coastguard boat
        bool  checkVehicleModel = false;
    };

    // Route cache — learned from first cycle
    static const int MAX_ROUTE_CPS = 50;
    static Game::Vec3 s_RouteCache[MAX_ROUTE_CPS];
    static bool       s_RouteCPIsRace[MAX_ROUTE_CPS]; // Track if each CP was race type
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
    static int        s_TurboIndex = 0;              // Current index in turbo mode

    // Simple random
    static DWORD s_RandSeed = 0;
    static float RandomFloat(float minVal, float maxVal) {
        if (s_RandSeed == 0) s_RandSeed = GetTickCount();
        s_RandSeed = s_RandSeed * 214013 + 2531011;
        DWORD r = (s_RandSeed >> 16) & 0x7FFF;
        float t = (float)r / 32767.0f;
        return minVal + t * (maxVal - minVal);
    }

    inline void Reset() {
        s_State = State::IDLE;
        s_StateEntryTime = 0;
        s_CycleCount = 0;
        s_CPCount = 0;
        s_IsRace = false;
        s_CurrentCP = {0, 0, 0};
        s_LastCompletedCP = {0, 0, 0};
        s_NewCPDetected = false;
        s_TurboIndex = 0;
        // Keep route cache! Only clear with full reset
        // s_RouteKnown and s_RouteCache persist across cycles
    }

    // Full reset including route cache
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

    // Add CP to route cache (during learning cycle)
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
                s_CurrentCP = pos;
                s_NewCPDetected = true;
                break;
            default:
                break;
        }
    }

    // Main update - called every tick when mod is active
    inline void Update() {
        DWORD now = GetTickCount();

        if (s_Config.checkVehicleModel) {
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
                    // TURBO MODE — we know the route!
                    s_TurboIndex = 0;
                    s_State = State::TURBO_BLAST;
                    Game::Log("[Coastguard] === TURBO MODE === Ciclo #%d - %d CPs cacheados, delay %dms",
                        s_CycleCount, s_RouteCacheSize, s_Config.turboDelayMs);
                } else {
                    // Learning mode — first cycle
                    s_State = State::WAITING_CHECKPOINT;
                    s_RouteCacheSize = 0; // Start fresh cache
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
                    Game::TeleportVehicle(s_CurrentCP.x, s_CurrentCP.y, s_CurrentCP.z + 1.0f);
                    Sender::SendFakeVehicleSync(vehID, s_CurrentCP.x, s_CurrentCP.y, s_CurrentCP.z);
                    
                    SAMP::SetInCheckpoint(true);
                    if (s_IsRace) {
                        Sender::SendEnterRaceCheckpoint();
                    } else {
                        Sender::SendEnterCheckpoint();
                    }
                    SAMP::SetInCheckpoint(false);
                    
                    // Cache this CP position for future turbo cycles
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
                            "RUTA APRENDIDA! %d CPs. Proximo ciclo sera TURBO -> COOLDOWN %dms",
                            s_RouteCacheSize, s_Config.cooldownMs);
                        LogState(buf);
                        Game::Log("[Coastguard] ========================================");
                        Game::Log("[Coastguard] RUTA CACHEADA: %d checkpoints", s_RouteCacheSize);
                        for (int i = 0; i < s_RouteCacheSize; i++) {
                            Game::Log("[Coastguard]   CP[%d] = (%.1f, %.1f, %.1f) %s", 
                                i, s_RouteCache[i].x, s_RouteCache[i].y, s_RouteCache[i].z,
                                s_RouteCPIsRace[i] ? "RACE" : "NORMAL");
                        }
                        Game::Log("[Coastguard] ========================================");
                        s_State = State::COOLDOWN;
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
                
                // Wait turbo delay between each CP
                if (elapsed < s_Config.turboDelayMs) break;
                
                if (s_TurboIndex >= s_RouteCacheSize) {
                    // All CPs blasted! Route complete
                    char buf[128];
                    snprintf(buf, sizeof(buf), 
                        "TURBO COMPLETO! %d CPs en %dms -> COOLDOWN",
                        s_RouteCacheSize, (int)(now - s_StateEntryTime));
                    LogState(buf);
                    s_State = State::COOLDOWN;
                    s_StateEntryTime = now;
                    break;
                }
                
                WORD vehID = SAMP::GetVehicleID();
                if (vehID != 0xFFFF) {
                    Game::Vec3 cp = s_RouteCache[s_TurboIndex];
                    bool isRace = s_RouteCPIsRace[s_TurboIndex];
                    
                    // 1. Teleport to cached CP
                    Game::TeleportVehicle(cp.x, cp.y, cp.z + 1.0f);
                    
                    // 2. Vehicle sync
                    Sender::SendFakeVehicleSync(vehID, cp.x, cp.y, cp.z);
                    
                    // 3. Enter checkpoint
                    SAMP::SetInCheckpoint(true);
                    if (isRace) {
                        Sender::SendEnterRaceCheckpoint();
                    } else {
                        Sender::SendEnterCheckpoint();
                    }
                    SAMP::SetInCheckpoint(false);
                    
                    s_TurboIndex++;
                    s_CPCount++;
                    s_StateEntryTime = now; // Reset timer for next CP delay
                    
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
            // COOLDOWN: Wait before starting next cycle
            // ========================================
            case State::COOLDOWN:
            {
                DWORD elapsed = now - s_StateEntryTime;
                
                if (s_NewCPDetected && s_RouteKnown) {
                    // Route restarted, go turbo again
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
                    LogState("Cooldown terminado. Listo para nuevo ciclo.");
                }
                break;
            }
        }
    }

    inline Config& GetConfig() { return s_Config; }
    inline State GetState() { return s_State; }
    inline int GetCycleCount() { return s_CycleCount; }
    inline int GetCPCount() { return s_CPCount; }
    inline bool IsRouteKnown() { return s_RouteKnown; }
    inline int GetRouteCacheSize() { return s_RouteCacheSize; }
}
