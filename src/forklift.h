#pragma once
/**
 * Forklift Auto-Job Module
 * 
 * State machine:
 *   IDLE → WAITING_CHECKPOINT → TELEPORTING (DoubleEnter) → WAITING_DONE → IDLE
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
        WAITING_CHECKPOINT,    // Looking for pickup CP
        TELEPORTING,           // TP + DoubleEnter (pickup+delivery)
        WAITING_DONE,          // Wait for server to process
    };

    struct Config {
        DWORD waitMs           = 6500;  // Max wait for server processing
        DWORD waitRandomMs     = 800;
        WORD  forkliftModelId  = 530;
        bool  checkVehicleModel = false;
    };

    static State      s_State = State::IDLE;
    static DWORD      s_WaitStart = 0;
    static Config     s_Config;
    static Game::Vec3 s_TargetPos = {0, 0, 0};
    static bool       s_IsRace = false;
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
        Game::Log("[Forklift] Ciclo #%d - %s", s_CycleCount, action);
    }

    // Called externally when checkpoint state changes (only care about pickup CPs now)
    static void OnCheckpointUpdate(bool active, Game::Vec3 pos, bool isRace) {
        if (!active) return;
        if (pos.x == 0.0f && pos.y == 0.0f && pos.z == 0.0f) return;
        if (fabsf(pos.x) > 20000.0f || fabsf(pos.y) > 20000.0f) return;

        if (s_State == State::WAITING_CHECKPOINT || s_State == State::WAITING_DONE) {
            s_TargetPos = pos;
            s_IsRace = isRace;
            if (s_State == State::WAITING_DONE) {
                // Server already created next CP while we were waiting
                s_State = State::WAITING_CHECKPOINT;
            }
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
                s_TargetPos = {0, 0, 0};
                s_IsRace = false;
                LogState("Buscando CP...");
                break;
            }

            case State::WAITING_CHECKPOINT:
            {
                // Check if OnCheckpointUpdate set a valid target
                if (s_TargetPos.x != 0.0f || s_TargetPos.y != 0.0f) {
                    s_State = State::TELEPORTING;
                    char buf[128];
                    snprintf(buf, sizeof(buf), "CP! (%.1f,%.1f,%.1f)",
                        s_TargetPos.x, s_TargetPos.y, s_TargetPos.z);
                    LogState(buf);
                }
                // Fallback: direct memory read
                else if (Game::IsCheckpointActive()) {
                    Game::Vec3 cp = Game::GetCheckpointPosition();
                    if (fabsf(cp.x) > 1.0f || fabsf(cp.y) > 1.0f) {
                        s_TargetPos = cp;
                        s_IsRace = false;
                        s_State = State::TELEPORTING;
                        LogState("CP (mem)!");
                    }
                }
                else if (Game::IsRaceCheckpointActive()) {
                    Game::Vec3 rcp = Game::GetRaceCheckpointPosition();
                    if (fabsf(rcp.x) > 1.0f || fabsf(rcp.y) > 1.0f) {
                        s_TargetPos = rcp;
                        s_IsRace = true;
                        s_State = State::TELEPORTING;
                        LogState("RaceCP (mem)!");
                    }
                }
                break;
            }

            case State::TELEPORTING:
            {
                WORD vehID = SAMP::GetVehicleID();
                if (vehID != 0xFFFF) {
                    // 1. Teleport to CP
                    Game::TeleportVehicle(s_TargetPos.x, s_TargetPos.y, s_TargetPos.z + 1.0f);

                    // 2. DOUBLE ENTER: Enter + Exit + Re-Enter (pickup + delivery at same spot)
                    bool ok = Sender::SendDoubleEnter(vehID,
                        s_TargetPos.x, s_TargetPos.y, s_TargetPos.z, s_IsRace);

                    s_State = State::WAITING_DONE;
                    s_WaitStart = now;
                    s_ActualWaitMs = CalcWaitTime(s_Config.waitMs);

                    char buf[128];
                    snprintf(buf, sizeof(buf), "TP + DoubleEnter (%s). Wait %dms max",
                        ok ? "OK" : "FAIL", s_ActualWaitMs);
                    LogState(buf);
                } else {
                    if (!Game::IsPlayerInVehicle()) {
                        s_State = State::IDLE;
                        LogState("Error: No vehicle. Reset.");
                    }
                }
                break;
            }

            case State::WAITING_DONE:
            {
                Game::StabilizeVehicle();
                DWORD elapsed = now - s_WaitStart;

                // Smart early-exit: after 1s, if CP disappeared = server processed both
                bool cpGone = (elapsed > 1000) &&
                    !Game::IsCheckpointActive() && !Game::IsRaceCheckpointActive();

                if (cpGone || elapsed >= s_ActualWaitMs) {
                    char buf[128];
                    snprintf(buf, sizeof(buf), "Ciclo #%d OK! (%dms)", s_CycleCount, (int)elapsed);
                    LogState(buf);
                    s_TargetPos = {0, 0, 0};
                    s_IsRace = false;
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
