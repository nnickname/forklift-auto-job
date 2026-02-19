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
 * Restart:  wait for boat respawn → PutIntoVehicle → press 2 → retry until CP.
 *
 * ALL teleport/camera/vehicle-entry goes through SAMP-API native functions.
 * Vehicle entry uses CPed::PutIntoVehicle (no F-key simulation).
 * Known boat: model 472, SAMP vehicle ID 30.
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

    // Boat identity
    static constexpr WORD  BOAT_MODEL   = 472;
    static constexpr WORD  BOAT_SAMP_ID = 30;    // Fixed SAMP vehicle ID

    // Boat spawn position (Coastguard dock)
    static constexpr float BOAT_X       = 719.1288f;
    static constexpr float BOAT_Y       = -1698.4248f;
    static constexpr float BOAT_Z       = 1.7874f;
    static constexpr float BOAT_HEADING = 190.97f;

    // Timings (ms)
    static constexpr DWORD TURBO_DELAY      = 150;    // Between CPs in turbo
    static constexpr DWORD NEXT_CP_WAIT     = 1500;   // Max wait for next CP
    static constexpr DWORD RESTART_EXIT_WAIT= 500;    // Wait after exiting vehicle
    static constexpr DWORD RESPAWN_CHECK    = 500;    // Poll interval for boat respawn
    static constexpr DWORD RESPAWN_MAX      = 20000;  // Max wait for boat respawn
    static constexpr DWORD KEY_HOLD         = 150;    // Key hold duration
    static constexpr DWORD KEY2_RETRY       = 2000;   // Retry pressing 2 interval
    static constexpr DWORD KEY2_CP_TIMEOUT  = 5000;   // Max wait for CP after pressing 2
    static constexpr DWORD KEY2_MAX_RETRIES = 5;      // Max retries pressing 2

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
    static int   s_RestartStep    = 0;
    static bool  s_KeyHeld        = false;
    static DWORD s_RestartBegin   = 0;
    static int   s_Key2Retries    = 0;
    static DWORD s_LastKey2Press  = 0;

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

    // Teleport vehicle to CP + send sync + enter checkpoint RPC
    // Uses SAMP-API CEntity::Teleport + CGame::RefreshRenderer + CCamera::SetToOwner
    static void DoTeleportAndEnter(Vec3 cp, bool isRace, WORD vehID) {
        // Teleport via SAMP-API (handles camera/rendering internally)
        SAMP::TeleportVehicle(vehID, cp.x, cp.y, cp.z);

        // Sync + enter CP
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
        s_State = State::IDLE;
        s_Cycle = 0;
        s_CPCount = 0;
        s_RestartStep = 0;
        s_KnownVeh = 0xFFFF;
        s_SamePosRetry = 0;
        s_Key2Retries = 0;
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
                SAMP::TeleportVehicle(vid, BOAT_X, BOAT_Y, BOAT_Z + 1.0f);
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
        //   Step 0: Exit vehicle + short wait
        //   Step 1: Wait for boat (ID 30) to respawn
        //   Step 2: TP ped + PutIntoVehicle(30)
        //   Step 3: Press 2 + wait for CP
        //   Step 4: CP appeared → IDLE (or retry 2)
        // ────────────────────────────────────────────
        case State::RESTARTING:
        {
            DWORD elapsed = now - s_StateTime;

            switch (s_RestartStep) {

            case 0: // Exit vehicle + wait
            {
                if (elapsed < 50) {
                    SAMP::RestoreCamera();
                    Game::ForceExitVehicle();
                    s_KnownVeh = 0xFFFF;
                }
                if (elapsed >= RESTART_EXIT_WAIT) {
                    s_RestartBegin = now;
                    s_RestartStep = 1;
                    s_StateTime = now;
                    Game::Log("[CG] Restart step 1: waiting for boat respawn");
                }
                break;
            }

            case 1: // Wait for boat (SAMP ID 30) to exist
            {
                // Check if vehicle 30 exists in the pool
                auto* sv = SAMP::GetSAMPVehicle(BOAT_SAMP_ID);
                if (sv) {
                    __try {
                        if (sv->DoesExist()) {
                            s_RestartStep = 2;
                            s_StateTime = now;
                            Game::Log("[CG] Restart step 2: boat exists, entering");
                            break;
                        }
                    } __except (EXCEPTION_EXECUTE_HANDLER) {}
                }

                // Timeout — boat didn't respawn
                if (now - s_RestartBegin >= RESPAWN_MAX) {
                    Game::Log("[CG] Restart: boat respawn timeout, retrying from step 0");
                    s_RestartStep = 0;
                    s_StateTime = now;
                }
                break;
            }

            case 2: // TP ped to boat + PutIntoVehicle(30) directly
            {
                // Teleport ped near boat
                SAMP::TeleportPed(BOAT_X, BOAT_Y, BOAT_Z);
                SAMP::SetPedRotation(BOAT_HEADING);

                // Put player directly into vehicle via SAMP-API
                if (SAMP::PutIntoVehicle(BOAT_SAMP_ID)) {
                    s_KnownVeh = BOAT_SAMP_ID;
                    Game::Log("[CG] Restart: PutIntoVehicle(30) success");

                    // Small delay then sync
                    Vec3 p = {BOAT_X, BOAT_Y, BOAT_Z};
                    Net::SendVehicleSync(BOAT_SAMP_ID, p.x, p.y, p.z, 0);

                    s_Key2Retries = 0;
                    s_LastKey2Press = 0;
                    s_RestartStep = 3;
                    s_StateTime = now;
                } else {
                    // PutIntoVehicle failed — retry after short wait
                    Game::Log("[CG] Restart: PutIntoVehicle failed, retrying");
                    s_RestartStep = 1;
                    s_StateTime = now;
                }
                break;
            }

            case 3: // Press 2 to start route
            {
                // Wait a bit after entering vehicle before pressing 2
                if (elapsed < 300) break;

                // Press 2
                Game::PressKey('2');
                s_KeyHeld = true;
                s_LastKey2Press = now;
                s_RestartStep = 4;
                s_StateTime = now;

                // Also send key sync
                WORD vid = GetActiveVehID();
                if (vid != 0xFFFF) {
                    Vec3 p = Game::GetPosition(Game::GetPlayerVehicle());
                    Net::SendVehicleSync(vid, p.x, p.y, p.z, 0);
                    Net::SendVehicleSync(vid, p.x, p.y, p.z, Net::KEY_START_ROUTE);
                }

                Game::Log("[CG] Restart step 3: pressed 2 (attempt %d)", s_Key2Retries + 1);
                break;
            }

            case 4: // Release 2 + wait for CP to appear
            {
                // Release key after hold time
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

                // Check if a checkpoint appeared
                Vec3 cp; bool race;
                if (elapsed >= 500 && PollCheckpoint(cp, race)) {
                    SAMP::RestoreCamera();
                    Game::Log("[CG] Restart: CP appeared! → IDLE");
                    s_RestartStep = 0;
                    s_State = State::IDLE;
                    s_StateTime = now;
                    break;
                }

                // Timeout — no CP appeared after pressing 2
                if (elapsed >= KEY2_CP_TIMEOUT) {
                    s_Key2Retries++;
                    if (s_Key2Retries >= (int)KEY2_MAX_RETRIES) {
                        Game::Log("[CG] Restart: max key-2 retries, re-entering vehicle");
                        s_Key2Retries = 0;
                        s_RestartStep = 0; // full restart
                        s_StateTime = now;
                    } else {
                        Game::Log("[CG] Restart: no CP, retrying key 2");
                        s_RestartStep = 3; // retry pressing 2
                        s_StateTime = now;
                    }
                }
                break;
            }

            } // switch s_RestartStep
            break;
        }

        } // switch s_State
    }

} // namespace Coastguard
