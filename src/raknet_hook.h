#pragma once
/**
 * RakNet Hook - Monitors SA-MP checkpoint state
 * 
 * Instead of hooking the actual RPC dispatcher (risky, version-specific),
 * this polls the CGame checkpoint state each tick and pushes updates
 * to the Coastguard module.
 */

#include <windows.h>
#include "samp.h"
#include "game.h"
#include "coastguard.h"

namespace RakNetHook {

    // Cached checkpoint state to avoid spamming updates
    static bool  s_LastCPActive = false;
    static float s_LastCPX = 0, s_LastCPY = 0, s_LastCPZ = 0;
    static bool  s_LastRCPActive = false;
    static float s_LastRCPX = 0, s_LastRCPY = 0, s_LastRCPZ = 0;

    // Call this when entering WAITING_CHECKPOINT so an already-active CP fires OnCheckpointUpdate
    inline void ResetTrackedState() {
        s_LastCPActive   = false;
        s_LastRCPActive  = false;
        s_LastCPX = s_LastCPY = s_LastCPZ = 0;
        s_LastRCPX = s_LastRCPY = s_LastRCPZ = 0;
    }

    // Called from main loop (every tick)
    inline void Update() {
        if (!SAMP::IsInitialized()) return;

        bool cpActive = false;
        bool isRace = false;
        Game::Vec3 cpPos = {0, 0, 0};

        // Check normal checkpoint
        stCheckpoint* pCP = SAMP::GetCurrentCheckpoint();
        if (pCP && pCP->bEnabled) {
            cpActive = true;
            isRace = false;
            cpPos = { pCP->fX, pCP->fY, pCP->fZ };
        }

        // Check race checkpoint if no normal CP
        if (!cpActive) {
            stRaceCheckpoint* pRCP = SAMP::GetRaceCheckpoint();
            if (pRCP && pRCP->bEnabled) {
                cpActive = true;
                isRace = true;
                cpPos = { pRCP->fX, pRCP->fY, pRCP->fZ };
            }
        }

        // Always push update when coastguard is waiting for its first CP
        // (the CP might have been active before we entered WAITING_CHECKPOINT,
        //  so the normal "state/position changed" guard would suppress it).
        bool forceUpdate = (Coastguard::GetState() == Coastguard::State::WAITING_CHECKPOINT ||
                            Coastguard::GetState() == Coastguard::State::WAITING_NEXT_CP);

        // Only push update if state changed, position changed, or force mode
        if (cpActive) {
            bool posChanged = (cpPos.x != s_LastCPX || cpPos.y != s_LastCPY || cpPos.z != s_LastCPZ);
            bool stateChanged = !s_LastCPActive;
            
            if (stateChanged || posChanged || forceUpdate) {
                s_LastCPActive = true;
                s_LastCPX = cpPos.x;
                s_LastCPY = cpPos.y;
                s_LastCPZ = cpPos.z;
                Coastguard::OnCheckpointUpdate(true, cpPos, isRace);
            }
        } else if (s_LastCPActive) {
            s_LastCPActive = false;
            s_LastCPX = s_LastCPY = s_LastCPZ = 0;
        }
    }
}
