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
        float stepDistance      = 10.0f;  // Coordenadas por salto (cambiar a gusto)
        DWORD stepDelayMs      = 200;    // Milisegundos entre cada salto
        
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
    // TP POR SALTOS - La función principal
    //
    // Cada vez que se llama (cada tick), verifica si ya pasó
    // el delay entre saltos. Si sí, hace UN salto de stepDistance
    // coordenadas en dirección al checkpoint.
    //
    // Retorna true cuando llegó al destino.
    // ============================================================
    static bool DoStepTP(Game::Vec3 target) {
        DWORD now = GetTickCount();
        
        // Primer salto: inicializar
        if (s_LastStepTime == 0) {
            s_LastStepTime = now;
            s_StepCount = 0;
            // No hacemos nada el primer tick, solo iniciar el timer
            return false;
        }
        
        // Esperar el delay entre saltos
        if (now - s_LastStepTime < s_Config.stepDelayMs) {
            return false; // Todavía no toca saltar
        }
        s_LastStepTime = now;
        
        // Posición actual y distancia al destino
        Game::Vec3 pos = Game::GetPlayerPosition();
        target.z += s_Config.teleportZOffset;
        float dist = Game::Distance3D(pos, target);
        
        // ¿Ya llegamos?
        if (dist <= s_Config.arrivalThreshold) {
            // TP final exacto al checkpoint
            Game::TeleportVehicle(target.x, target.y, target.z);
            s_LastStepTime = 0;
            s_StepCount = 0;
            return true;
        }
        
        // Dirección hacia el checkpoint
        Game::Vec3 dir;
        dir.x = target.x - pos.x;
        dir.y = target.y - pos.y;
        dir.z = target.z - pos.z;
        dir = Normalize(dir);
        
        // Distancia de este salto
        float jump = s_Config.stepDistance;
        
        // Si estamos más cerca que stepDistance, saltar directo
        if (jump >= dist) {
            Game::TeleportVehicle(target.x, target.y, target.z);
            s_LastStepTime = 0;
            s_StepCount = 0;
            return true;
        }
        
        // Calcular nueva posición = actual + dirección * salto
        float newX = pos.x + dir.x * jump;
        float newY = pos.y + dir.y * jump;
        float newZ = pos.z + dir.z * jump;
        
        // Hacer el TP del salto
        Game::TeleportVehicle(newX, newY, newZ);
        s_StepCount++;
        
        return false;
    }

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
                    s_State = State::TELEPORTING_PICKUP;
                    
                    float dist = Game::Distance3D(Game::GetPlayerPosition(), s_TargetPos);
                    char buf[128];
                    snprintf(buf, sizeof(buf), "CP encontrado a %d coords! TPeando de a %.2f...",
                        (int)dist, s_Config.stepDistance);
                    LogState(buf);
                }
                else if (Game::IsRaceCheckpointActive()) {
                    s_TargetPos = Game::GetRaceCheckpointPosition();
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
                if (DoStepTP(s_TargetPos)) {
                    s_State = State::WAITING_PICKUP;
                    s_WaitStart = now;
                    s_ActualWaitMs = CalcWaitTime(s_Config.pickupWaitMs);
                    
                    char buf[128];
                    snprintf(buf, sizeof(buf), "Llegó en %d saltos! Esperando recogida...", s_StepCount);
                    LogState(buf);
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
                if (DoStepTP(s_TargetPos)) {
                    s_State = State::WAITING_DELIVERY;
                    s_WaitStart = now;
                    s_ActualWaitMs = CalcWaitTime(s_Config.deliveryWaitMs);
                    
                    char buf[128];
                    snprintf(buf, sizeof(buf), "Llegó en %d saltos! Esperando entrega...", s_StepCount);
                    LogState(buf);
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
