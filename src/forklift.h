#pragma once
/**
 * Forklift Auto-Job Module (TP por saltos)
 * 
 * Automatiza el trabajo de montacargas teletransportando
 * en saltos pequeños (ej: 10 coordenadas) en vez de un TP 
 * directo al checkpoint. Así el anticheat ve movimientos
 * cortos en vez de un salto enorme.
 *
 * Ejemplo con stepDistance = 10 y checkpoint a 80u:
 *   Pos(100,200) → TP +10 → (100,210) → espera → TP +10 → (100,220) → ...
 *   ... → (100,280) → llegó al checkpoint!
 *
 * Configuración clave:
 *   stepDistance  = distancia de cada salto (default: 10.0)
 *   stepDelayMs  = milisegundos entre cada salto (default: 200)
 */

#include <windows.h>
#include <cmath>
#include <stdio.h>
#include "game.h"
#include "samp.h"
#include "raknet_sender.h"

namespace Forklift {

    // ============================================================
    // State Machine
    // ============================================================
    enum class State {
        IDLE,                   // Esperando que empiece el job
        WAITING_CHECKPOINT,     // Buscando checkpoint de recogida
        TELEPORTING_PICKUP,     // Haciendo TPs hacia el checkpoint
        WAITING_PICKUP,         // Esperando en el punto de recogida
        WAITING_RACE_CP,       // Buscando checkpoint de entrega
        TELEPORTING_DELIVERY,   // Haciendo TPs hacia la entrega
        WAITING_DELIVERY,       // Esperando en el punto de entrega
    };

    // ============================================================
    // Configuration - EDITA ESTOS VALORES
    // ============================================================
    struct Config {
        // --- TP POR SALTOS ---
        float stepDistance      = 35.0f;  // Coordenadas por salto (más = menos saltos)
        DWORD stepDelayMs      = 400;    // Milisegundos entre cada salto
        
        // --- ESPERAS EN CHECKPOINT ---
        DWORD pickupWaitMs     = 2500;   // Espera en recogida (ms)
        DWORD deliveryWaitMs   = 2500;   // Espera en entrega (ms)
        DWORD waitRandomMs     = 800;    // Variación random ±ms en esperas
        
        // --- MISC ---
        float teleportZOffset  = 0.5f;   // Offset Z para no clipear el suelo
        float arrivalThreshold = 3.0f;   // Distancia para considerar "llegó"
        
        // --- FILTRO VEHÍCULO ---
        WORD  forkliftModelId  = 530;    // Model ID del forklift
        bool  checkVehicleModel = false; // Solo funcionar en forklift
    };

    // ============================================================
    // Internal state
    // ============================================================
    static State      s_State = State::IDLE;
    static DWORD      s_WaitStart = 0;
    static Config     s_Config;
    static Game::Vec3 s_TargetPos = {0, 0, 0};
    static int        s_CycleCount = 0;
    
    // TP por saltos state
    static DWORD      s_LastStepTime = 0;       // Timestamp del último salto
    static DWORD      s_ActualWaitMs = 0;       // Tiempo de espera randomizado
    static int        s_StepCount = 0;          // Contador de saltos en este viaje

    // ============================================================
    // Simple random
    // ============================================================
    static DWORD s_RandSeed = 0;
    static float RandomFloat(float minVal, float maxVal) {
        if (s_RandSeed == 0) s_RandSeed = GetTickCount();
        s_RandSeed = s_RandSeed * 214013 + 2531011;
        DWORD r = (s_RandSeed >> 16) & 0x7FFF;
        float t = (float)r / 32767.0f;
        return minVal + t * (maxVal - minVal);
    }

    // ============================================================
    // Reset
    // ============================================================
    inline void Reset() {
        s_State = State::IDLE;
        s_WaitStart = 0;
        s_CycleCount = 0;
        s_LastStepTime = 0;
        s_StepCount = 0;
    }

    // ============================================================
    // Normalizar vector
    // ============================================================
    static Game::Vec3 Normalize(Game::Vec3 v) {
        float len = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
        if (len < 0.0001f) return {0, 0, 0};
        return { v.x / len, v.y / len, v.z / len };
    }

    // ============================================================
    // Calcular wait time con variación random
    // ============================================================
    static DWORD CalcWaitTime(DWORD baseMs) {
        float variation = RandomFloat(-(float)s_Config.waitRandomMs, (float)s_Config.waitRandomMs);
        int result = (int)baseMs + (int)variation;
        if (result < 500) result = 500;
        return (DWORD)result;
    }

    // ============================================================
    // TP POR SALTOS - DEPRECATED / REMOVED
    // ============================================================
    /*
    static bool DoStepTP(Game::Vec3 target) {
        // ... (removed for RakNet Sync method)
        return true;
    }
    */

    // ============================================================
    // Log al chat
    // ============================================================
    static void LogState(const char* action) {
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "[Forklift] Ciclo #%d - %s", s_CycleCount, action);
        Game::AddChatMessage(0xFF00BFFF, buffer);
    }

    // ============================================================
    // External Update (Called from RakNetHook or other source)
    // ============================================================
    static void OnCheckpointUpdate(bool active, Game::Vec3 pos) {
        if (!active) return;
        
        // Override state if we are waiting/searching
        if (s_State == State::WAITING_CHECKPOINT || s_State == State::WAITING_RACE_CP) {
             s_TargetPos = pos;
             
             // Decide next state based on current logic flow
             // Simplification: if searching for pickup -> go pickup. If searching delivery -> go delivery.
             // Since we don't know "which" CP this is just by coords, we assume flow order.
             
             if (s_State == State::WAITING_CHECKPOINT) {
                 s_State = State::TELEPORTING_PICKUP;
                 LogState("RakNet: Checkpoint recibido -> Pickup");
             } else {
                 s_State = State::TELEPORTING_DELIVERY;
                 LogState("RakNet: Checkpoint recibido -> Delivery");
             }
        }
    }

    // ============================================================
    // Main Update - se llama cada tick cuando el mod está activo
    // ============================================================
    inline void Update() {
        DWORD now = GetTickCount();
        
        // Filtro de vehículo (opcional)
        if (s_Config.checkVehicleModel) {
            WORD model = Game::GetVehicleModelId();
            if (model != s_Config.forkliftModelId) return;
        }
        
        switch (s_State) {
            // ---------------------------------------------------------
            case State::IDLE:
            {
                s_State = State::WAITING_CHECKPOINT;
                s_CycleCount++;
                LogState("Buscando checkpoint de recogida...");
                break;
            }
            
            // ---------------------------------------------------------
            case State::WAITING_CHECKPOINT:
            {
                if (Game::IsCheckpointActive()) {
                    s_TargetPos = Game::GetCheckpointPosition();
                    
                    // Sanity check coordinates (Map bounds approx +/- 20000)
                    if (abs(s_TargetPos.x) > 20000.0f || abs(s_TargetPos.y) > 20000.0f || abs(s_TargetPos.z) > 20000.0f) {
                         LogState("Error: Checkpoint coords invalid / garbage. Aborting.");
                         s_State = State::IDLE;
                         break;
                    }

                    s_State = State::TELEPORTING_PICKUP;
                    
                    float dist = Game::Distance3D(Game::GetPlayerPosition(), s_TargetPos);
                    char buf[128];
                    snprintf(buf, sizeof(buf), "CP encontrado a %d coords! TPeando de a %.2f...",
                        (int)dist, s_Config.stepDistance);
                    LogState(buf);
                }
                else if (Game::IsRaceCheckpointActive()) {
                    s_TargetPos = Game::GetRaceCheckpointPosition();

                    // Sanity check coordinates
                    if (abs(s_TargetPos.x) > 20000.0f || abs(s_TargetPos.y) > 20000.0f || abs(s_TargetPos.z) > 20000.0f) {
                         LogState("Error: Race Checkpoint coords invalid / garbage. Aborting.");
                         s_State = State::IDLE;
                         break;
                    }

                    s_State = State::TELEPORTING_PICKUP;
                    
                    float dist = Game::Distance3D(Game::GetPlayerPosition(), s_TargetPos);
                    char buf[128];
                    snprintf(buf, sizeof(buf), "Race CP encontrado a %d coords! TPeando de a %.2f...",
                        (int)dist, s_Config.stepDistance);
                    LogState(buf);
                }
                break;
            }
            
            // ---------------------------------------------------------
            case State::TELEPORTING_PICKUP:
            {
                WORD vehID = SAMP::GetVehicleID();
                if (vehID != 0xFFFF) {
                    Sender::SendFakeVehicleSync(vehID, s_TargetPos.x, s_TargetPos.y, s_TargetPos.z);
                    s_State = State::WAITING_PICKUP;
                    s_WaitStart = now;
                    s_ActualWaitMs = CalcWaitTime(s_Config.pickupWaitMs);
                    
                    char buf[128];
                    snprintf(buf, sizeof(buf), "Fake Sync enviado! Esperando recogida...");
                    LogState(buf);
                } else {
                     LogState("Error: GetVehicleID failed (returned 0xFFFF or Exception).");
                     // We reset state to avoid infinite loop of trying and failing
                     s_State = State::IDLE;
                }
                break;
            }
            
            // ---------------------------------------------------------
            case State::WAITING_PICKUP:
            {
                if (now - s_WaitStart >= s_ActualWaitMs) {
                    s_State = State::WAITING_RACE_CP;
                    LogState("Buscando checkpoint de entrega...");
                }
                break;
            }
            
            // ---------------------------------------------------------
            case State::WAITING_RACE_CP:
            {
                if (Game::IsRaceCheckpointActive()) {
                    s_TargetPos = Game::GetRaceCheckpointPosition();
                    s_State = State::TELEPORTING_DELIVERY;
                    
                    float dist = Game::Distance3D(Game::GetPlayerPosition(), s_TargetPos);
                    char buf[128];
                    snprintf(buf, sizeof(buf), "CP entrega a %d coords! TPeando de a %.2f...",
                        (int)dist, s_Config.stepDistance);
                    LogState(buf);
                }
                else if (Game::IsCheckpointActive()) {
                    s_TargetPos = Game::GetCheckpointPosition();
                    s_State = State::TELEPORTING_DELIVERY;
                    
                    float dist = Game::Distance3D(Game::GetPlayerPosition(), s_TargetPos);
                    char buf[128];
                    snprintf(buf, sizeof(buf), "CP entrega a %d coords! TPeando de a %.2f...",
                        (int)dist, s_Config.stepDistance);
                    LogState(buf);
                }
                break;
            }
            
            // ---------------------------------------------------------
            case State::TELEPORTING_DELIVERY:
            {
                // Safety check for absurd coordinates (e.g. invalid memory read)
                if (abs(s_TargetPos.x) > 20000.0f || abs(s_TargetPos.y) > 20000.0f) {
                     LogState("Error: Coordenadas de Pickup invalidas! Abortando.");
                     s_State = State::IDLE;
                     break;
                }

                WORD vehID = SAMP::GetVehicleID();
                if (vehID != 0xFFFF) {
                    Sender::SendFakeVehicleSync(vehID, s_TargetPos.x, s_TargetPos.y, s_TargetPos.z);
                    s_State = State::WAITING_PICKUP;
                    s_WaitStart = now;
                    s_ActualWaitMs = CalcWaitTime(s_Config.pickupWaitMs);
                    
                    char buf[128];
                    snprintf(buf, sizeof(buf), "Fake Sync enviado! Esperando recogida...");
                    LogState(buf);
                } else {
                     LogState("Error: No se pudo obtener VehicleID (Crash prevented). Resetting.");
                     s_State = State::IDLE;
                }
                break;
            }
            
            // ---------------------------------------------------------
            case State::WAITING_DELIVERY:
            {
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
    
    // ============================================================
    // Getters
    // ============================================================
    inline Config& GetConfig() { return s_Config; }
    inline State GetState() { return s_State; }
    inline int GetCycleCount() { return s_CycleCount; }
}
