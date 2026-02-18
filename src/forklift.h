#pragma once
/**
 * Forklift Auto-Job Module
 * 
 * State machine:
 *   IDLE → WAITING_CHECKPOINT → TELEPORTING (enter pickup)
 *        → EXITING (move ~10 coords away + LeaveCP)
 *        → RE_ENTERING (TP back + EnterCP for delivery)
 *        → WAITING_DONE → IDLE (loop)
 *
 * Strategy:
 *   1. Detect checkpoint (from CGame memory or RPC hook)
 *   2. TP to CP + VehicleSync + EnterCheckpoint RPC (pickup)
 *   3. Move ~10 coords away + LeaveCheckpoint RPC
 *   4. Wait for server to create delivery CP
 *   5. TP to delivery CP + VehicleSync + EnterCheckpoint RPC (delivery)
 *   6. Wait for server to process, then loop
 */

#include <windows.h>
#include <cmath>
#include <stdio.h>
#include "game.h"
#include "samp.h"
#include "raknet_sender.h"

namespace Forklift {

    enum class State {
        IDLE,
        WAITING_CHECKPOINT,     // Looking for pickup CP
        TELEPORTING,            // TP to pickup CP + Enter RPC
        EXITING,                // Move ~10 coords away + Leave RPC
        WAITING_DELIVERY_CP,    // Wait for server to create delivery CP
        RE_ENTERING,            // TP to delivery CP + Enter RPC
        WAITING_DONE,           // Wait for server to process delivery
        COOLDOWN,               // Brief pause to let stale CPs clear before next cycle
    };

    struct Config {
        DWORD waitMs           = 6500;   // Max wait for server processing
        DWORD waitRandomMs     = 800;
        DWORD exitDelayMs      = 400;    // Delay before exit (let server register pickup)
        DWORD reenterDelayMs   = 400;    // Delay before re-enter
        DWORD deliveryCpWaitMs = 5000;   // Max wait for delivery CP to appear
        float exitDistance     = 10.0f;  // Distance to move away for exit
        WORD  forkliftModelId  = 530;
        bool  checkVehicleModel = false;
    };

    static State      s_State = State::IDLE;
    static DWORD      s_WaitStart = 0;
    static Config     s_Config;
    static Game::Vec3 s_PickupPos = {0, 0, 0};     // Saved pickup CP position
    static Game::Vec3 s_DeliveryPos = {0, 0, 0};    // Delivery CP position (from server)
    static Game::Vec3 s_TargetPos = {0, 0, 0};      // Current target from OnCheckpointUpdate
    static bool       s_IsRace = false;
    static int        s_CycleCount = 0;
    static DWORD      s_ActualWaitMs = 0;
    static DWORD      s_StateEntryTime = 0;         // When we entered current state
    static bool       s_DeliveryCpDetected = false;  // Flag: delivery CP appeared
    static const DWORD CYCLE_COOLDOWN_MS = 2000;     // Wait 2s between cycles to clear stale CPs

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
        s_WaitStart = 0;
        s_CycleCount = 0;
        s_IsRace = false;
        s_PickupPos = {0, 0, 0};
        s_DeliveryPos = {0, 0, 0};
        s_TargetPos = {0, 0, 0};
        s_DeliveryCpDetected = false;
    }

    static DWORD CalcWaitTime(DWORD baseMs) {
        float variation = RandomFloat(-(float)s_Config.waitRandomMs, (float)s_Config.waitRandomMs);
        int result = (int)baseMs + (int)variation;
        if (result < 500) result = 500;
        return (DWORD)result;
    }

    static void LogState(const char* action) {
        Game::Log("[Forklift] Ciclo #%d - %s (State=%d)", s_CycleCount, action, (int)s_State);
    }

    // Called externally when checkpoint state changes
    static void OnCheckpointUpdate(bool active, Game::Vec3 pos, bool isRace) {
        if (!active) return;
        if (pos.x == 0.0f && pos.y == 0.0f && pos.z == 0.0f) return;
        if (fabsf(pos.x) > 20000.0f || fabsf(pos.y) > 20000.0f) return;

        s_IsRace = isRace;

        switch (s_State) {
            case State::WAITING_CHECKPOINT:
                // First CP detected = pickup
                s_TargetPos = pos;
                break;
            case State::WAITING_DELIVERY_CP:
            {
                // New CP after pickup = delivery CP
                s_DeliveryPos = pos;
                s_DeliveryCpDetected = true;
                char buf[128];
                snprintf(buf, sizeof(buf), "Delivery CP detectado! (%.1f,%.1f,%.1f)", pos.x, pos.y, pos.z);
                LogState(buf);
                break;
            }
            case State::COOLDOWN:
            case State::WAITING_DONE:
                // Ignore ALL checkpoints during these states — they're stale
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
            if (model != s_Config.forkliftModelId) return;
        }

        switch (s_State) {
            // ========================================
            // IDLE: Start new cycle
            // ========================================
            case State::IDLE:
            {
                s_State = State::WAITING_CHECKPOINT;
                s_CycleCount++;
                s_TargetPos = {0, 0, 0};
                s_PickupPos = {0, 0, 0};
                s_DeliveryPos = {0, 0, 0};
                s_DeliveryCpDetected = false;
                s_IsRace = false;
                s_StateEntryTime = now;
                LogState("Buscando CP de pickup...");
                break;
            }

            // ========================================
            // WAITING_CHECKPOINT: Wait for pickup CP to appear
            // ========================================
            case State::WAITING_CHECKPOINT:
            {
                // Check if OnCheckpointUpdate set a valid target
                if (s_TargetPos.x != 0.0f || s_TargetPos.y != 0.0f) {
                    s_PickupPos = s_TargetPos;
                    s_State = State::TELEPORTING;
                    s_StateEntryTime = now;
                    char buf[128];
                    snprintf(buf, sizeof(buf), "Pickup CP! (%.1f,%.1f,%.1f)",
                        s_PickupPos.x, s_PickupPos.y, s_PickupPos.z);
                    LogState(buf);
                }
                // Fallback: direct memory read
                else if (Game::IsCheckpointActive()) {
                    Game::Vec3 cp = Game::GetCheckpointPosition();
                    if (fabsf(cp.x) > 1.0f || fabsf(cp.y) > 1.0f) {
                        s_PickupPos = cp;
                        s_TargetPos = cp;
                        s_IsRace = false;
                        s_State = State::TELEPORTING;
                        s_StateEntryTime = now;
                        LogState("Pickup CP (mem)!");
                    }
                }
                else if (Game::IsRaceCheckpointActive()) {
                    Game::Vec3 rcp = Game::GetRaceCheckpointPosition();
                    if (fabsf(rcp.x) > 1.0f || fabsf(rcp.y) > 1.0f) {
                        s_PickupPos = rcp;
                        s_TargetPos = rcp;
                        s_IsRace = true;
                        s_State = State::TELEPORTING;
                        s_StateEntryTime = now;
                        LogState("Pickup RaceCP (mem)!");
                    }
                }
                break;
            }

            // ========================================
            // TELEPORTING: TP to pickup CP + send Enter RPC
            // ========================================
            case State::TELEPORTING:
            {
                WORD vehID = SAMP::GetVehicleID();
                if (vehID != 0xFFFF) {
                    // 1. Teleport to pickup CP
                    Game::TeleportVehicle(s_PickupPos.x, s_PickupPos.y, s_PickupPos.z + 1.0f);
                    
                    // 2. Send vehicle sync at CP position
                    Sender::SendFakeVehicleSync(vehID, s_PickupPos.x, s_PickupPos.y, s_PickupPos.z);
                    
                    // 3. Enter checkpoint (pickup)
                    SAMP::SetInCheckpoint(true);
                    if (s_IsRace) {
                        Sender::SendEnterRaceCheckpoint();
                    } else {
                        Sender::SendEnterCheckpoint();
                    }
                    SAMP::SetInCheckpoint(false);
                    
                    // Move to EXITING state after a short delay
                    s_State = State::EXITING;
                    s_StateEntryTime = now;
                    
                    LogState("TP + Enter (pickup). Esperando para exit...");
                } else {
                    if (!Game::IsPlayerInVehicle()) {
                        s_State = State::IDLE;
                        LogState("Error: No vehicle. Reset.");
                    }
                }
                break;
            }

            // ========================================
            // EXITING: Wait a bit, then move ~10 coords away + Leave RPC
            // ========================================
            case State::EXITING:
            {
                Game::StabilizeVehicle();
                
                DWORD elapsed = now - s_StateEntryTime;
                if (elapsed >= s_Config.exitDelayMs) {
                    WORD vehID = SAMP::GetVehicleID();
                    if (vehID != 0xFFFF) {
                        // Calculate exit position (~10 coords away on X axis)
                        float exitX = s_PickupPos.x + s_Config.exitDistance;
                        float exitY = s_PickupPos.y;
                        float exitZ = s_PickupPos.z + 1.0f;
                        
                        // 1. Teleport away
                        Game::TeleportVehicle(exitX, exitY, exitZ);
                        
                        // 2. Send vehicle sync at exit position
                        Sender::SendFakeVehicleSync(vehID, exitX, exitY, exitZ);
                        
                        // 3. Leave checkpoint RPC
                        if (s_IsRace) {
                            Sender::SendLeaveRaceCheckpoint();
                        } else {
                            Sender::SendLeaveCheckpoint();
                        }
                        
                        // Move to waiting for delivery CP
                        s_State = State::WAITING_DELIVERY_CP;
                        s_StateEntryTime = now;
                        s_DeliveryCpDetected = false;
                        
                        char buf[128];
                        snprintf(buf, sizeof(buf), 
                            "EXIT a (%.1f,%.1f,%.1f). Esperando delivery CP...",
                            exitX, exitY, exitZ);
                        LogState(buf);
                    }
                }
                break;
            }

            // ========================================
            // WAITING_DELIVERY_CP: Wait for server to create delivery checkpoint
            // ========================================
            case State::WAITING_DELIVERY_CP:
            {
                Game::StabilizeVehicle();
                
                DWORD elapsed = now - s_StateEntryTime;
                
                // Check if delivery CP was detected by OnCheckpointUpdate
                if (s_DeliveryCpDetected) {
                    // Minimum delay before re-entering (let server settle)
                    if (elapsed >= s_Config.reenterDelayMs) {
                        s_State = State::RE_ENTERING;
                        s_StateEntryTime = now;
                        LogState("Delivery CP listo. Re-entrando...");
                    }
                }
                // Fallback: check memory directly for ANY checkpoint
                else if (elapsed >= s_Config.reenterDelayMs) {
                    bool foundDelivery = false;
                    
                    if (Game::IsCheckpointActive()) {
                        Game::Vec3 cp = Game::GetCheckpointPosition();
                        if (fabsf(cp.x) > 1.0f || fabsf(cp.y) > 1.0f) {
                            s_DeliveryPos = cp;
                            s_DeliveryCpDetected = true;
                            s_IsRace = false;
                            foundDelivery = true;
                        }
                    }
                    if (!foundDelivery && Game::IsRaceCheckpointActive()) {
                        Game::Vec3 rcp = Game::GetRaceCheckpointPosition();
                        if (fabsf(rcp.x) > 1.0f || fabsf(rcp.y) > 1.0f) {
                            s_DeliveryPos = rcp;
                            s_DeliveryCpDetected = true;
                            s_IsRace = true;
                            foundDelivery = true;
                        }
                    }
                    
                    if (foundDelivery) {
                        s_State = State::RE_ENTERING;
                        s_StateEntryTime = now;
                        char buf[128];
                        snprintf(buf, sizeof(buf), 
                            "Delivery CP (fallback) en (%.1f,%.1f,%.1f)",
                            s_DeliveryPos.x, s_DeliveryPos.y, s_DeliveryPos.z);
                        LogState(buf);
                    }
                }
                
                // Timeout: if no delivery CP after max wait, use pickup pos as fallback
                if (!s_DeliveryCpDetected && elapsed >= s_Config.deliveryCpWaitMs) {
                    Game::Log("[Forklift] TIMEOUT esperando delivery CP. Usando pickup pos como fallback.");
                    s_DeliveryPos = s_PickupPos;
                    s_DeliveryCpDetected = true;
                    s_State = State::RE_ENTERING;
                    s_StateEntryTime = now;
                }
                
                break;
            }

            // ========================================
            // RE_ENTERING: TP to delivery CP + send Enter RPC
            // ========================================
            case State::RE_ENTERING:
            {
                WORD vehID = SAMP::GetVehicleID();
                if (vehID != 0xFFFF) {
                    // 1. Teleport back to delivery CP
                    Game::TeleportVehicle(s_DeliveryPos.x, s_DeliveryPos.y, s_DeliveryPos.z + 1.0f);
                    
                    // 2. Send vehicle sync at delivery position
                    Sender::SendFakeVehicleSync(vehID, 
                        s_DeliveryPos.x, s_DeliveryPos.y, s_DeliveryPos.z);
                    
                    // 3. Enter checkpoint (delivery)
                    SAMP::SetInCheckpoint(true);
                    if (s_IsRace) {
                        Sender::SendEnterRaceCheckpoint();
                    } else {
                        Sender::SendEnterCheckpoint();
                    }
                    SAMP::SetInCheckpoint(false);
                    
                    // Move to waiting done
                    s_State = State::WAITING_DONE;
                    s_WaitStart = now;
                    s_ActualWaitMs = CalcWaitTime(s_Config.waitMs);
                    
                    char buf[128];
                    snprintf(buf, sizeof(buf), 
                        "TP + Enter DELIVERY (%.1f,%.1f,%.1f). Wait %dms max",
                        s_DeliveryPos.x, s_DeliveryPos.y, s_DeliveryPos.z, 
                        s_ActualWaitMs);
                    LogState(buf);
                } else {
                    if (!Game::IsPlayerInVehicle()) {
                        s_State = State::IDLE;
                        LogState("Error: No vehicle en RE_ENTERING. Reset.");
                    }
                }
                break;
            }

            // ========================================
            // WAITING_DONE: Wait for delivery to be processed
            // ========================================
            case State::WAITING_DONE:
            {
                Game::StabilizeVehicle();
                DWORD elapsed = now - s_WaitStart;

                // Smart early-exit: after 1s, if CP disappeared = server processed delivery
                bool cpGone = (elapsed > 1000) &&
                    !Game::IsCheckpointActive() && !Game::IsRaceCheckpointActive();

                if (cpGone || elapsed >= s_ActualWaitMs) {
                    char buf[128];
                    snprintf(buf, sizeof(buf), "Ciclo #%d COMPLETO! (%dms)%s -> COOLDOWN %dms", 
                        s_CycleCount, (int)elapsed,
                        cpGone ? " (early-exit)" : "",
                        (int)CYCLE_COOLDOWN_MS);
                    LogState(buf);
                    
                    // Go to COOLDOWN — ignore all CPs for a bit so stale ones clear out
                    s_State = State::COOLDOWN;
                    s_StateEntryTime = now;
                }
                break;
            }

            // ========================================
            // COOLDOWN: Wait before starting next cycle
            // ========================================
            case State::COOLDOWN:
            {
                DWORD elapsed = now - s_StateEntryTime;
                if (elapsed >= CYCLE_COOLDOWN_MS) {
                    // Now it's safe — reset everything and start fresh
                    s_PickupPos = {0, 0, 0};
                    s_DeliveryPos = {0, 0, 0};
                    s_TargetPos = {0, 0, 0};
                    s_DeliveryCpDetected = false;
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
}
