#pragma once
/**
 * Forklift Auto-Job Module
 * 
 * State machine:
 *   IDLE → WAITING_CHECKPOINT → TELEPORTING_PICKUP → WAITING_PICKUP
 *   → WAITING_RACE_CP → TELEPORTING_DELIVERY → WAITING_DELIVERY → IDLE
 *
 * Uses "Fake Enter" method:
 *   1. Detect checkpoint (from CGame memory or RPC hook)
 *   2. Send VehicleSync at checkpoint position + RPC_EnterCheckpoint
 *   3. Wait 5 seconds
 *   4. Repeat for delivery
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
        WAITING_CHECKPOINT,
        TELEPORTING_PICKUP,
        WAITING_PICKUP,
        WAITING_RACE_CP,
        TELEPORTING_DELIVERY,
        WAITING_DELIVERY,
    };

    struct Config {
        DWORD pickupWaitMs     = 6500;
        DWORD deliveryWaitMs   = 6500;
        DWORD waitRandomMs     = 800;
        WORD  forkliftModelId  = 530;
        bool  checkVehicleModel = false;
    };

    static State      s_State = State::IDLE;
    static DWORD      s_WaitStart = 0;
    static Config     s_Config;
    static Game::Vec3 s_TargetPos = {0, 0, 0};
    static bool       s_IsRace = false; // Track CP type
    static int        s_CycleCount = 0;
    static DWORD      s_ActualWaitMs = 0;

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
    }

    static DWORD CalcWaitTime(DWORD baseMs) {
        float variation = RandomFloat(-(float)s_Config.waitRandomMs, (float)s_Config.waitRandomMs);
        int result = (int)baseMs + (int)variation;
        if (result < 500) result = 500;
        return (DWORD)result;
    }

    static void LogState(const char* action) {
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "[Forklift] Ciclo #%d - %s", s_CycleCount, action);
        Game::AddChatMessage(0xFF00BFFF, buffer);
    }

    // Called externally when checkpoint state changes
    static void OnCheckpointUpdate(bool active, Game::Vec3 pos, bool isRace) {
        if (!active) return;
        
        // Validate coordinates (reject garbage)
        if (pos.x == 0.0f && pos.y == 0.0f && pos.z == 0.0f) return;
        if (fabsf(pos.x) > 20000.0f || fabsf(pos.y) > 20000.0f) return;

        if (s_State == State::WAITING_CHECKPOINT) {
            s_TargetPos = pos;
            s_IsRace = isRace;
        } else if (s_State == State::WAITING_RACE_CP || s_State == State::WAITING_PICKUP) {
            s_TargetPos = pos;
            s_IsRace = isRace;
            s_State = State::WAITING_RACE_CP;
        } else if (s_State == State::WAITING_DELIVERY) {
            s_TargetPos = pos;
            s_IsRace = isRace;
            s_State = State::WAITING_CHECKPOINT;
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
            case State::IDLE:
            {
                s_State = State::WAITING_CHECKPOINT;
                s_CycleCount++;
                s_TargetPos = {0, 0, 0}; // Reset target
                s_IsRace = false;
                LogState("Buscando checkpoint de recogida...");
                break;
            }

            case State::WAITING_CHECKPOINT:
            {
                // Check if OnCheckpointUpdate set a valid target
                if (s_TargetPos.x != 0.0f || s_TargetPos.y != 0.0f) {
                    s_State = State::TELEPORTING_PICKUP;
                    char buf[128];
                    snprintf(buf, sizeof(buf), "CP encontrado! (%.1f, %.1f, %.1f) Enviando sync...",
                        s_TargetPos.x, s_TargetPos.y, s_TargetPos.z);
                    LogState(buf);
                }
                // Also try direct memory read as fallback
                else if (Game::IsCheckpointActive()) {
                    Game::Vec3 cp = Game::GetCheckpointPosition();
                    if (fabsf(cp.x) > 1.0f || fabsf(cp.y) > 1.0f) {
                        s_TargetPos = cp;
                        s_IsRace = false; // Memory read confirms normal CP
                        s_State = State::TELEPORTING_PICKUP;
                        char buf[128];
                        snprintf(buf, sizeof(buf), "CP (mem) encontrado! (%.1f, %.1f) Sync...",
                            s_TargetPos.x, s_TargetPos.y);
                        LogState(buf);
                    }
                }
                break;
            }

            case State::TELEPORTING_PICKUP:
            {
                WORD vehID = SAMP::GetVehicleID();
                if (vehID != 0xFFFF) {
                    // 1. PHYSICAL TELEPORT (User Request: "move me directly")
                    Game::TeleportVehicle(s_TargetPos.x, s_TargetPos.y, s_TargetPos.z + 1.0f);
                    
                    // 2. FAKE ENTER (Sync + RPC)
                    bool ok = Sender::SendFakeEnterCheckpoint(vehID,
                        s_TargetPos.x, s_TargetPos.y, s_TargetPos.z, s_IsRace);
                    
                    s_State = State::WAITING_PICKUP;
                    s_WaitStart = now;
                    s_ActualWaitMs = CalcWaitTime(s_Config.pickupWaitMs);

                    char buf[128];
                    snprintf(buf, sizeof(buf), "TP + FakeEnter enviado (%s). Esperando %dms...",
                        ok ? "OK" : "FAIL", s_ActualWaitMs);
                    LogState(buf);
                } else {
                    if (!Game::IsPlayerInVehicle()) {
                        s_State = State::IDLE;
                        LogState("Error: No en vehiculo. Reset.");
                    }
                }
                break;
            }

            case State::WAITING_PICKUP:
            {
                // [STABILIZATION] Force vehicle to stay put during wait
                Game::StabilizeVehicle();

                if (now - s_WaitStart >= s_ActualWaitMs) {
                    s_State = State::WAITING_RACE_CP;
                    s_TargetPos = {0, 0, 0}; // Reset for next CP
                    s_IsRace = false;
                    LogState("Buscando checkpoint de entrega...");
                }
                break;
            }

            case State::WAITING_RACE_CP:
            {
                // Check cached target from hook
                if (s_TargetPos.x != 0.0f || s_TargetPos.y != 0.0f) {
                    s_State = State::TELEPORTING_DELIVERY;
                    LogState("CP entrega encontrado. Enviando sync...");
                }
                // Fallback: check race checkpoint
                else if (Game::IsRaceCheckpointActive()) {
                    Game::Vec3 rcp = Game::GetRaceCheckpointPosition();
                    if (fabsf(rcp.x) > 1.0f || fabsf(rcp.y) > 1.0f) {
                        s_TargetPos = rcp;
                        s_IsRace = true; // Race CP confirmed
                        s_State = State::TELEPORTING_DELIVERY;
                        LogState("Race CP (mem) encontrado!");
                    }
                }
                // Fallback: check normal checkpoint
                else if (Game::IsCheckpointActive()) {
                    Game::Vec3 cp = Game::GetCheckpointPosition();
                    if (fabsf(cp.x) > 1.0f || fabsf(cp.y) > 1.0f) {
                        s_TargetPos = cp;
                        s_IsRace = false; // Normal CP fallback
                        s_State = State::TELEPORTING_DELIVERY;
                        LogState("CP entrega (mem) encontrado!");
                    }
                }
                break;
            }

            case State::TELEPORTING_DELIVERY:
            {
                if (fabsf(s_TargetPos.x) > 20000.0f || fabsf(s_TargetPos.y) > 20000.0f) {
                    LogState("Error: Coords invalidas! Abortando.");
                    s_State = State::IDLE;
                    break;
                }

                WORD vehID = SAMP::GetVehicleID();
                if (vehID != 0xFFFF) {
                    // 1. PHYSICAL TELEPORT
                    Game::TeleportVehicle(s_TargetPos.x, s_TargetPos.y, s_TargetPos.z + 1.0f);

                    // 2. FAKE ENTER
                    bool ok = Sender::SendFakeEnterCheckpoint(vehID,
                        s_TargetPos.x, s_TargetPos.y, s_TargetPos.z, s_IsRace);
                    
                    s_State = State::WAITING_DELIVERY;
                    s_WaitStart = now;
                    s_ActualWaitMs = CalcWaitTime(s_Config.deliveryWaitMs);

                    char buf[128];
                    snprintf(buf, sizeof(buf), "TP + FakeEnter entrega (%s). Esperando %dms...",
                        ok ? "OK" : "FAIL", s_ActualWaitMs);
                    LogState(buf);
                } else {
                    LogState("Error: No VehicleID. Reset.");
                    s_State = State::IDLE;
                }
                break;
            }

            case State::WAITING_DELIVERY:
            {
                // [STABILIZATION] Force vehicle to stay put during wait
                Game::StabilizeVehicle();

                if (now - s_WaitStart >= s_ActualWaitMs) {
                    char buf[128];
                    snprintf(buf, sizeof(buf), "Ciclo #%d completado!", s_CycleCount);
                    LogState(buf);
                    s_State = State::IDLE;
                }
                break;
            }
        }
    }

    inline Config& GetConfig() { return s_Config; }
    inline State GetState() { return s_State; }
    inline int GetCycleCount() { return s_CycleCount; }
}
