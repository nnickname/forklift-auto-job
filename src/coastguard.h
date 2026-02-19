#pragma once
/**
 * Coastguard Auto-Job Module
 *
 * State machine:
 *   IDLE → WAITING_CP → TELEPORTING → WAITING_NEXT → ... (learning cycle 1)
 *   IDLE → TURBO (cycle 2+, blast through cached route) → RESTARTING → IDLE
 *
 * Learning: detect checkpoint, TP + enter, wait for next, repeat, cache route.
 * Turbo:    blast through all cached CPs with minimal delay.
 * Restart:  exit vehicle → TP to boat → press F → press 2 → IDLE.
 */

#include <windows.h>
#include <cmath>
#include "game.h"
#include "net.h"

namespace Coastguard {

    // ════════════════════════════════════════════════════════
    // Types & Constants
    // ════════════════════════════════════════════════════════

    enum class State {
        IDLE,
        WAITING_CP,     // Wait for checkpoint to appear
        TELEPORTING,    // TP to CP + send enter RPC
        WAITING_NEXT,   // Wait for next CP after entering
        TURBO,          // Blast through cached route
        RESTARTING      // Re-enter boat + start route
    };

    // Boat spawn (Coastguard dock)
    static constexpr WORD  BOAT_MODEL   = 472;
    static constexpr float BOAT_X       = 719.1288f;
    static constexpr float BOAT_Y       = -1698.4248f;
    static constexpr float BOAT_Z       = 1.7874f;
    static constexpr float BOAT_HEADING = 190.97f;

    // Timings (ms)
    static constexpr DWORD TURBO_DELAY   = 150;   // Between CPs in turbo
    static constexpr DWORD NEXT_CP_WAIT  = 1500;  // Max wait for next CP
    static constexpr DWORD RESTART_INIT  = 1500;  // Initial wait after route ends
    static constexpr DWORD RESTART_MAX   = 15000; // Max wait for F entry
    static constexpr DWORD F_RETRY       = 1000;  // Retry F press interval
    static constexpr DWORD KEY_HOLD      = 150;   // Key hold duration
    static constexpr DWORD AFTER_KEY2    = 2000;  // Wait after pressing 2

    static constexpr int MAX_ROUTE = 150;

    // ════════════════════════════════════════════════════════
    // State variables
    // ════════════════════════════════════════════════════════

    static State s_State     = State::IDLE;
    static DWORD s_StateTime = 0;
    static int   s_Cycle     = 0;
    static int   s_CPCount   = 0;

    // Route cache
    static Vec3  s_Route[MAX_ROUTE];
    static bool  s_RouteRace[MAX_ROUTE];
    static int   s_RouteSize  = 0;
    static bool  s_RouteKnown = false;
    static int   s_TurboIdx   = 0;

    // Current CP tracking
    static Vec3  s_CurCP  = {0, 0, 0};
    static Vec3  s_LastCP  = {0, 0, 0};
    static bool  s_IsRace  = false;
    static WORD  s_KnownVeh = 0xFFFF;

    // Restart
    static int   s_RestartStep   = 0;
    static bool  s_KeyHeld       = false;
    static DWORD s_RestartBegin  = 0;
    static DWORD s_LastFPress    = 0;
    static bool  s_FDown         = false;
    static DWORD s_FDownAt       = 0;

    // Same-position retry counter (WAITING_NEXT)
    static int   s_SamePosRetry  = 0;

    // ════════════════════════════════════════════════════════
    // Helpers
    // ════════════════════════════════════════════════════════

    static bool IsDifferent(Vec3 a, Vec3 b) {
        return fabsf(a.x - b.x) > 1.0f || fabsf(a.y - b.y) > 1.0f || fabsf(a.z - b.z) > 1.0f;
    }

    static bool IsValidPos(float x, float y) {
        return !(fabsf(x) < 1.0f && fabsf(y) < 1.0f) && fabsf(x) < 20000.0f && fabsf(y) < 20000.0f;
    }

    static void CacheCP(Vec3 pos, bool isRace) {
        if (s_RouteSize > 0 && !IsDifferent(pos, s_Route[s_RouteSize - 1])) return;
        if (s_RouteSize < MAX_ROUTE) {
            s_Route[s_RouteSize] = pos;
            s_RouteRace[s_RouteSize] = isRace;
            s_RouteSize++;
            Game::Log("[CG] Cached CP #%d (%.1f,%.1f,%.1f)", s_RouteSize, pos.x, pos.y, pos.z);
        }
    }

    // Get reliable SAMP vehicle ID, patching state if needed
    static WORD GetActiveVehID() {
        WORD id = SAMP::GetVehicleID();
        if (id != 0xFFFF) { s_KnownVeh = id; return id; }
        if (!Game::IsInVehicle()) return 0xFFFF;
        if (s_KnownVeh == 0xFFFF) {
            DWORD gta = Game::GetPlayerVehicle();
            if (gta) {
                WORD found = SAMP::FindVehicleID(gta);
                if (found != 0xFFFF) s_KnownVeh = found;
            }
        }
        if (s_KnownVeh != 0xFFFF) {
            SAMP::PatchVehicleID(s_KnownVeh);
        }
        return s_KnownVeh;
    }

    // Poll CGame for active checkpoint
    static bool PollCheckpoint(Vec3& out, bool& outRace) {
        if (SAMP::IsCheckpointActive()) {
            float x = 0, y = 0, z = 0;
            SAMP::GetCheckpointPos(x, y, z);
            if (IsValidPos(x, y)) { out = {x, y, z}; outRace = false; return true; }
        }
        if (SAMP::IsRaceCheckpointActive()) {
            float x = 0, y = 0, z = 0;
            SAMP::GetRaceCheckpointPos(x, y, z);
            if (IsValidPos(x, y)) { out = {x, y, z}; outRace = true; return true; }
        }
        return false;
    }

    // Teleport + enter checkpoint (core action)
    static void DoTeleportAndEnter(Vec3 cp, bool isRace, WORD vehID) {
        SAMP::RestoreCamera();
        Game::RestoreGTACamera();
        Game::TeleportVehicle(cp.x, cp.y, cp.z);
        Net::SendVehicleSync(vehID, cp.x, cp.y, cp.z);
        if (isRace) Net::SendEnterRaceCheckpoint();
        else        Net::SendEnterCheckpoint();
    }

    // ════════════════════════════════════════════════════════
    // Public Interface
    // ════════════════════════════════════════════════════════

    inline State GetState() { return s_State; }
    inline int GetCPCount() { return s_CPCount; }

    inline void Reset() {
        if (s_KeyHeld) { Game::ReleaseKey('2'); s_KeyHeld = false; }
        if (s_FDown) { Game::ReleaseKey('F'); s_FDown = false; }
        s_State = State::IDLE;
        s_Cycle = 0;
        s_CPCount = 0;
        s_RestartStep = 0;
        s_KnownVeh = 0xFFFF;
        s_SamePosRetry = 0;
    }

    inline void FullReset() {
        Reset();
        s_RouteKnown = false;
        s_RouteSize = 0;
    }

    inline void StartRestart() {
        s_State = State::RESTARTING;
        s_RestartStep = 0;
        s_StateTime = GetTickCount();
    }

    // ════════════════════════════════════════════════════════
    // Main Update — call every tick when mod is active
    // ════════════════════════════════════════════════════════

    inline void Update() {
        DWORD now = GetTickCount();

        // Periodic camera restore (every 150ms while active)
        if (s_State != State::IDLE) {
            static DWORD s_LastCam = 0;
            if (now - s_LastCam > 150) {
                s_LastCam = now;
                SAMP::RestoreCamera();
            }
        }

        switch (s_State) {

        // ────────────────────────────────────────────
        // IDLE: Start a new cycle
        // ────────────────────────────────────────────
        case State::IDLE:
        {
            s_Cycle++;
            s_CPCount = 0;
            s_CurCP = s_LastCP = {0, 0, 0};
            s_IsRace = false;
            s_SamePosRetry = 0;

            if (s_RouteKnown && s_RouteSize > 0) {
                s_TurboIdx = 0;
                s_State = State::TURBO;
                Game::Log("[CG] TURBO cycle #%d (%d CPs)", s_Cycle, s_RouteSize);
            } else {
                s_RouteSize = 0;
                s_State = State::WAITING_CP;
                Game::Log("[CG] LEARNING cycle #%d", s_Cycle);
            }
            s_StateTime = now;
            break;
        }

        // ────────────────────────────────────────────
        // WAITING_CP: Wait for first checkpoint
        // ────────────────────────────────────────────
        case State::WAITING_CP:
        {
            DWORD elapsed = now - s_StateTime;

            // Keep vehicle synced while waiting
            WORD vid = GetActiveVehID();
            if (vid != 0xFFFF) {
                static DWORD s_LastSync = 0;
                if (now - s_LastSync > 300) {
                    s_LastSync = now;
                    Vec3 p = Game::GetPosition(Game::GetPlayerVehicle());
                    Net::SendVehicleSync(vid, p.x, p.y, p.z);
                }
            }

            // Wait 1500ms for server to clear previous cycle's CP
            if (elapsed < 1500) break;

            Vec3 cp; bool race;
            if (PollCheckpoint(cp, race)) {
                s_CurCP = cp;
                s_IsRace = race;
                s_State = State::TELEPORTING;
                s_StateTime = now;
                Game::Log("[CG] CP found (%.1f,%.1f,%.1f)", cp.x, cp.y, cp.z);
            }
            break;
        }

        // ────────────────────────────────────────────
        // TELEPORTING: TP to CP + enter
        // ────────────────────────────────────────────
        case State::TELEPORTING:
        {
            if (!IsValidPos(s_CurCP.x, s_CurCP.y)) {
                s_State = State::IDLE;
                break;
            }
            WORD vid = GetActiveVehID();
            if (vid == 0xFFFF) {
                if (!Game::IsInVehicle()) s_State = State::IDLE;
                break;
            }

            DoTeleportAndEnter(s_CurCP, s_IsRace, vid);
            CacheCP(s_CurCP, s_IsRace);
            s_CPCount++;
            s_LastCP = s_CurCP;
            s_SamePosRetry = 0;
            s_State = State::WAITING_NEXT;
            s_StateTime = now;
            Game::Log("[CG] TP+Enter CP #%d (%.1f,%.1f,%.1f)", s_CPCount, s_CurCP.x, s_CurCP.y, s_CurCP.z);
            break;
        }

        // ────────────────────────────────────────────
        // WAITING_NEXT: Wait for server to send next CP
        // ────────────────────────────────────────────
        case State::WAITING_NEXT:
        {
            DWORD elapsed = now - s_StateTime;

            // Ejected from vehicle = route ended
            if (!Game::IsInVehicle() && SAMP::GetVehicleID() == 0xFFFF) {
                s_RouteKnown = true;
                Game::Log("[CG] Ejected → route done (%d CPs)", s_RouteSize);
                s_State = State::RESTARTING;
                s_RestartStep = 0;
                s_StateTime = now;
                break;
            }

            Game::StabilizeVehicle();

            // Look for a new (different) CP
            if (elapsed >= 200) {
                Vec3 cp; bool race;
                if (PollCheckpoint(cp, race) && IsDifferent(cp, s_LastCP)) {
                    s_CurCP = cp;
                    s_IsRace = race;
                    s_State = State::TELEPORTING;
                    s_StateTime = now;
                    break;
                }
            }

            // Timeout
            if (elapsed >= NEXT_CP_WAIT) {
                if (!SAMP::IsCheckpointActive() && !SAMP::IsRaceCheckpointActive()) {
                    // No checkpoint → route learned
                    s_RouteKnown = true;
                    Game::Log("[CG] Route learned (%d CPs) → RESTARTING", s_RouteSize);
                    s_State = State::RESTARTING;
                    s_RestartStep = 0;
                    s_StateTime = now;
                } else {
                    // CP still active at same position — retry mechanism
                    s_SamePosRetry++;
                    if (s_SamePosRetry >= 4) {
                        s_SamePosRetry = 0;
                        s_LastCP = {0, 0, 0}; // force-accept next poll
                        Game::Log("[CG] Same-pos timeout x4, forcing accept");
                    } else {
                        s_StateTime = now; // extend wait
                    }
                }
            }
            break;
        }

        // ────────────────────────────────────────────
        // TURBO: Blast through cached route
        // ────────────────────────────────────────────
        case State::TURBO:
        {
            if (now - s_StateTime < TURBO_DELAY) break;

            if (s_TurboIdx >= s_RouteSize) {
                Game::Log("[CG] TURBO done → RESTARTING");
                s_State = State::RESTARTING;
                s_RestartStep = 0;
                s_StateTime = now;
                break;
            }

            WORD vid = GetActiveVehID();
            if (vid == 0xFFFF) {
                if (!Game::IsInVehicle()) s_State = State::IDLE;
                break;
            }

            Vec3 cp = s_Route[s_TurboIdx];
            bool race = s_RouteRace[s_TurboIdx];
            if (!IsValidPos(cp.x, cp.y)) { s_TurboIdx++; s_StateTime = now; break; }

            // On last CP, pre-position at boat spawn for faster restart
            bool isLast = (s_TurboIdx == s_RouteSize - 1);
            if (isLast) {
                // TP to boat spawn BUT send sync at CP position
                SAMP::RestoreCamera();
                Game::RestoreGTACamera();
                Game::TeleportVehicle(BOAT_X, BOAT_Y, BOAT_Z + 1.0f);
                Net::SendVehicleSync(vid, cp.x, cp.y, cp.z);
                if (race) Net::SendEnterRaceCheckpoint();
                else      Net::SendEnterCheckpoint();
            } else {
                DoTeleportAndEnter(cp, race, vid);
            }

            s_TurboIdx++;
            s_CPCount++;
            s_StateTime = now;
            Game::Log("[CG] TURBO #%d/%d", s_TurboIdx, s_RouteSize);
            break;
        }

        // ────────────────────────────────────────────
        // RESTARTING: Re-enter boat + start route
        //   Step 0: Exit vehicle + wait
        //   Step 1: TP ped to boat + settle camera (300ms)
        //   Step 2: Press F until in vehicle
        //   Step 3: Sync + press 2
        //   Step 4: Release 2 + wait → IDLE
        // ────────────────────────────────────────────
        case State::RESTARTING:
        {
            DWORD elapsed = now - s_StateTime;

            switch (s_RestartStep) {

            case 0: // Exit vehicle state
            {
                if (elapsed < 50) {
                    Game::FullCameraRestore();
                    Game::ForceExitVehicle();
                    s_KnownVeh = 0xFFFF;
                }
                bool ejected = (SAMP::GetVehicleID() == 0xFFFF && !Game::IsInVehicle());
                if (ejected || elapsed >= RESTART_INIT) {
                    s_RestartBegin = now;
                    s_LastFPress = 0;
                    s_FDown = false;
                    s_RestartStep = 1;
                    s_StateTime = now;
                    Game::Log("[CG] Restart step 1: TP to boat");
                }
                break;
            }

            case 1: // TP ped above boat + settle camera
            {
                Game::FullCameraRestore();
                if (elapsed < 50) {
                    DWORD ped = Game::GetPlayerPed();
                    if (ped && !IsBadReadPtr((void*)ped, 0x600)) {
                        Game::SetPosition(ped, BOAT_X, BOAT_Y, BOAT_Z);
                        Game::SetHeading(BOAT_HEADING);
                    }
                }
                if (elapsed >= 300) {
                    s_RestartStep = 2;
                    s_StateTime = now;
                    Game::Log("[CG] Restart step 2: pressing F");
                }
                break;
            }

            case 2: // Press F until in vehicle
            {
                // Release F after key hold time
                if (s_FDown && now - s_FDownAt >= KEY_HOLD) {
                    Game::ReleaseKey('F');
                    s_FDown = false;
                }

                Game::FullCameraRestore();

                if (Game::IsInVehicle()) {
                    if (s_FDown) { Game::ReleaseKey('F'); s_FDown = false; }
                    DWORD gta = Game::GetPlayerVehicle();
                    if (gta) {
                        WORD found = SAMP::FindVehicleID(gta);
                        if (found != 0xFFFF) s_KnownVeh = found;
                    }
                    Game::Log("[CG] Restart: in vehicle (ID=%d)", (int)s_KnownVeh);
                    s_RestartStep = 3;
                    s_StateTime = now;
                    break;
                }

                if (now - s_RestartBegin >= RESTART_MAX) {
                    if (s_FDown) { Game::ReleaseKey('F'); s_FDown = false; }
                    s_RestartStep = 0;
                    s_StateTime = now;
                    Game::Log("[CG] Restart: F timeout, retrying");
                    break;
                }

                if (!s_FDown && now - s_LastFPress >= F_RETRY) {
                    s_LastFPress = now;
                    // Re-position ped each retry (boat may be respawning)
                    DWORD ped = Game::GetPlayerPed();
                    if (ped && !IsBadReadPtr((void*)ped, 0x600)) {
                        Game::SetPosition(ped, BOAT_X, BOAT_Y, BOAT_Z);
                        Game::SetHeading(BOAT_HEADING);
                    }
                    Game::PressKey('F');
                    s_FDownAt = now;
                    s_FDown = true;
                }
                break;
            }

            case 3: // In vehicle → sync + press 2
            {
                WORD vid = GetActiveVehID();
                if (vid != 0xFFFF) {
                    Vec3 p = Game::GetPosition(Game::GetPlayerVehicle());
                    Net::SendVehicleSync(vid, p.x, p.y, p.z, 0);
                    Net::SendVehicleSync(vid, p.x, p.y, p.z, Net::KEY_START_ROUTE);
                }
                Game::PressKey('2');
                s_KeyHeld = true;
                s_RestartStep = 4;
                s_StateTime = now;
                Game::Log("[CG] Restart step 3: syncs + key 2");
                break;
            }

            case 4: // Release 2 + keep-alive → IDLE
            {
                if (s_KeyHeld && elapsed >= KEY_HOLD) {
                    Game::ReleaseKey('2');
                    s_KeyHeld = false;
                }

                // Keep-alive sync
                WORD vid = GetActiveVehID();
                if (vid != 0xFFFF) {
                    static DWORD s_LastKA = 0;
                    if (now - s_LastKA > 250) {
                        s_LastKA = now;
                        Vec3 p = Game::GetPosition(Game::GetPlayerVehicle());
                        Net::SendVehicleSync(vid, p.x, p.y, p.z);
                    }
                }

                if (elapsed >= AFTER_KEY2) {
                    Game::FullCameraRestore();
                    Game::Log("[CG] Restart complete → IDLE");
                    s_RestartStep = 0;
                    s_State = State::IDLE;
                    s_StateTime = now;
                }
                break;
            }

            } // switch s_RestartStep
            break;
        }

        } // switch s_State
    }

} // namespace Coastguard
