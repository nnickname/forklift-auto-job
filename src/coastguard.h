#pragma once
/**
 * Coastguard Auto-Job — 100% SAMP-API Native Edition
 *
 * Teleport:   CVehicle::Teleport + CPed::Teleport  (physical move)
 * Sync:       CLocalPlayer::SendIncarData()         (reads game state)
 * CP enter:   Automatic via CGame::ProcessCheckpoints() each frame
 * Keys:       CPed::SetKeys()  (injects into GTA CPad)
 * Vehicle:    CPed::PutIntoVehicle + CLocalPlayer::EnterVehicle
 *
 * NO manual BitStreams. NO manual RPCs. NO SendInput.
 */

#include <windows.h>
#include <cmath>
#include "game.h"

namespace Coastguard {

    // ════════════════════════════════════════════════════════
    // Constants
    // ════════════════════════════════════════════════════════

    enum class State {
        IDLE,
        WAITING_CP,
        TELEPORTING,
        WAITING_NEXT,
        TURBO,
        RESTARTING
    };

    static constexpr WORD  BOAT_SAMP_ID = 1;
    static constexpr float BOAT_X       = 719.1288f;
    static constexpr float BOAT_Y       = -1698.4248f;
    static constexpr float BOAT_Z       = 1.7874f;
    static constexpr float BOAT_HEADING = 190.97f;

    // Timings
    static constexpr DWORD TP_SETTLE       = 200;    // After teleport, wait for game frame
    static constexpr DWORD SYNC_AFTER_TP   = 100;    // Extra sync delay after TP
    static constexpr DWORD NEXT_CP_WAIT    = 1500;   // Wait for next CP before retry
    static constexpr DWORD EXIT_SETTLE     = 400;    // After exit vehicle
    static constexpr DWORD ENTER_TP_WAIT   = 200;    // After TP ped to boat, before PutInto
    static constexpr DWORD ENTER_WARP_WAIT = 400;    // After PutIntoVehicle, let game process
    static constexpr DWORD ENTER_SYNC_WAIT = 250;    // Between first and second sync after enter
    static constexpr DWORD ENTER_SETTLE    = 500;    // After entering vehicle, before key2
    static constexpr DWORD KEY2_HOLD       = 200;    // Hold "2" key duration
    static constexpr DWORD KEY2_WAIT       = 3000;   // Wait between retries of "2"
    static constexpr DWORD SYNC_INTERVAL   = 400;    // Keep-alive sync interval
    static constexpr DWORD CP_POLL_MIN     = 100;    // Min wait before polling after TP
    static constexpr DWORD SAME_CP_RETRIES = 8;      // Re-TP same pos before force-accept
    static constexpr DWORD TURBO_SETTLE    = 200;    // Wait per CP in turbo
    static constexpr DWORD TURBO_RESYNC    = 2000;   // Re-sync if stuck in turbo
    static constexpr DWORD TURBO_BAIL      = 15000;  // Bail turbo mode

    // Key "2" as ControllerState bits
    // "2" in SA-MP dialogs maps to submission key — use ShockButtonR (look behind)
    // Actually for SA-MP job start, the "2" key is a keyboard input.
    // CPed::SetKeys uses ControllerState which is for gamepad.
    // We'll use a hybrid: SetKeys for movement sync + Windows input for "2" key.

    static constexpr int MAX_ROUTE = 150;

    // ════════════════════════════════════════════════════════
    // State
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

    // CP tracking
    static Vec3  s_CurCP   = {0, 0, 0};
    static Vec3  s_LastCP  = {0, 0, 0};
    static bool  s_IsRace  = false;

    // Restart
    static int   s_RestartStep  = 0;
    static int   s_Key2Attempts = 0;
    static DWORD s_LastKeyPress = 0;
    static bool  s_KeyHeld      = false;
    static Vec3  s_PreKey2CP    = {0, 0, 0}; // CP that existed BEFORE pressing "2"
    static bool  s_PreKey2Snap  = false;      // have we taken the snapshot?

    // Teleport sub-step
    static int   s_TpSub = 0;

    // Retry counter
    static int   s_SamePosRetry = 0;

    // Sync timer
    static DWORD s_LastSync = 0;

    // ════════════════════════════════════════════════════════
    // Helpers
    // ════════════════════════════════════════════════════════

    static bool IsDifferent(Vec3 a, Vec3 b) {
        return fabsf(a.x - b.x) > 1.0f || fabsf(a.y - b.y) > 1.0f || fabsf(a.z - b.z) > 1.0f;
    }

    static bool IsValidPos(float x, float y) {
        return !(fabsf(x) < 1.0f && fabsf(y) < 1.0f) && fabsf(x) < 20000.0f;
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

    // True in-vehicle check: game-side is authoritative.
    // SAMP's m_nCurrentVehicle can become stale after server-side
    // RemovePlayerFromVehicle — we proactively fix that here.
    static bool InVehicle() {
        bool gameInVeh = Game::IsInVehicle();
        bool sampInVeh = SAMP::GetVehicleID() != 0xFFFF;

        if (!gameInVeh && sampInVeh) {
            // Server ejected us but SAMP state is stale → fix it
            auto* lp = SAMP::GetLocalPlayer();
            if (lp) {
                __try { lp->m_nCurrentVehicle = 0xFFFF; }
                __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
            Game::Log("[CG] InVehicle: cleared stale m_nCurrentVehicle");
            return false;
        }

        return gameInVeh;
    }

    // SA-MP's CLocalPlayer::Process() handles sync automatically.
    // We do NOT send manual sync — it causes the server to detect
    // repeated vehicle entries ("No tienes licencia" spam).

    // ════════════════════════════════════════════════════════
    // Public Interface
    // ════════════════════════════════════════════════════════

    inline State GetState() { return s_State; }
    inline int GetCPCount() { return s_CPCount; }

    inline void Reset() {
        if (s_KeyHeld) {
            // Clear KEY_SUBMISSION via direct sync injection
            SAMP::ClearKey2Sync();
            s_KeyHeld = false;
        }
        SAMP::ClearPedKeys();
        s_State = State::IDLE;
        s_Cycle = s_CPCount = 0;
        s_RestartStep = 0;
        s_SamePosRetry = 0;
        s_Key2Attempts = 0;
        s_LastKeyPress = 0;
        s_PreKey2Snap = false;
        s_TpSub = 0;
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
    // Press/Release "2" via Windows SendInput
    // (CPed::SetKeys doesn't have a "2" key — it's keyboard only)
    // ════════════════════════════════════════════════════════

    static void PressKey2() {
        // Direct sync injection — bypasses Windows message queue.
        // Writes KEY_SUBMISSION into CPad + m_incarData and sends
        // via RakNet. Works even if main thread/GPU is frozen.
        if (SAMP::SendKey2Sync()) {
            Game::Log("[KEY2] SendKey2Sync OK");
        } else {
            Game::Log("[KEY2] SendKey2Sync FAILED — fallback SendInput");
            INPUT inp = {};
            inp.type = INPUT_KEYBOARD;
            inp.ki.wVk = '2';
            inp.ki.wScan = (BYTE)MapVirtualKey('2', MAPVK_VK_TO_VSC);
            inp.ki.dwFlags = 0;
            SendInput(1, &inp, sizeof(INPUT));
        }
        s_KeyHeld = true;
    }

    static void ReleaseKey2() {
        if (SAMP::ClearKey2Sync()) {
            Game::Log("[KEY2] ClearKey2Sync OK");
        } else {
            Game::Log("[KEY2] ClearKey2Sync FAILED — fallback SendInput");
            INPUT inp = {};
            inp.type = INPUT_KEYBOARD;
            inp.ki.wVk = '2';
            inp.ki.wScan = (BYTE)MapVirtualKey('2', MAPVK_VK_TO_VSC);
            inp.ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(1, &inp, sizeof(INPUT));
        }
        s_KeyHeld = false;
    }

    // ════════════════════════════════════════════════════════
    // Main Update — call every 5ms
    // ════════════════════════════════════════════════════════

    inline void Update() {
        DWORD now = GetTickCount();
        DWORD elapsed = now - s_StateTime;

        switch (s_State) {

        // ────────────── IDLE → start cycle ──────────────
        case State::IDLE:
        {
            s_Cycle++;
            s_CPCount = 0;
            s_CurCP = s_LastCP = {0, 0, 0};
            s_IsRace = false;
            s_SamePosRetry = 0;

            if (s_RouteKnown && s_RouteSize > 0) {
                s_TurboIdx = 0;
                s_TpSub = 0;
                s_State = State::TURBO;
                Game::Log("[CG] TURBO #%d (%d CPs)", s_Cycle, s_RouteSize);
            } else {
                s_RouteSize = 0;
                s_State = State::WAITING_CP;
                Game::Log("[CG] LEARN #%d", s_Cycle);
            }
            s_StateTime = now;
            break;
        }

        // ────────────── WAITING_CP ──────────────
        case State::WAITING_CP:
        {
            Vec3 cp; bool race;
            if (PollCheckpoint(cp, race)) {
                s_CurCP = cp;
                s_IsRace = race;
                s_TpSub = 0;
                s_State = State::TELEPORTING;
                s_StateTime = now;
                Game::Log("[CG] CP! (%.0f,%.0f,%.0f)", cp.x, cp.y, cp.z);
            }
            break;
        }

        // ────────────── TELEPORTING ──────────────
        // Sub 0: Teleport vehicle+ped to CP position
        // Sub 1: Wait TP_SETTLE for game to process frame
        // Sub 2: Send sync → WAITING_NEXT
        //
        // CGame::ProcessCheckpoints() runs every frame.
        // When it sees the player inside the CP radius,
        // it sends the enter-CP RPC automatically.
        // ─────────────────────────────────────────
        case State::TELEPORTING:
        {
            if (!InVehicle()) {
                Game::Log("[CG] No vehicle → RESTARTING");
                SAMP::SendOnfootSync(); // confirm to server we're on foot
                s_State = State::RESTARTING;
                s_RestartStep = 0;
                s_TpSub = 0;
                s_StateTime = now;
                break;
            }

            if (!IsValidPos(s_CurCP.x, s_CurCP.y)) {
                s_State = State::WAITING_CP;
                s_TpSub = 0;
                s_StateTime = now;
                break;
            }

            WORD vid = SAMP::GetVehicleID();

            if (s_TpSub == 0) {
                // Physical teleport
                if (vid != 0xFFFF)
                    SAMP::TeleportVehicle(vid, s_CurCP.x, s_CurCP.y, s_CurCP.z);
                s_TpSub = 1;
                s_StateTime = now;
                Game::Log("[CG] Move→ (%.0f,%.0f,%.0f)", s_CurCP.x, s_CurCP.y, s_CurCP.z);
                break;
            }

            if (s_TpSub == 1 && elapsed >= TP_SETTLE) {
                // Wait for game to process frame + SA-MP auto-sync
                CacheCP(s_CurCP, s_IsRace);
                s_CPCount++;
                s_LastCP = s_CurCP;
                s_SamePosRetry = 0;
                s_TpSub = 0;
                s_State = State::WAITING_NEXT;
                s_StateTime = now;
                Game::Log("[CG] TP #%d (%.0f,%.0f,%.0f)", s_CPCount, s_CurCP.x, s_CurCP.y, s_CurCP.z);
            }
            break;
        }

        // ────────────── WAITING_NEXT ──────────────
        // Wait for CGame::ProcessCheckpoints to enter CP
        // and server to set next CP.
        // ─────────────────────────────────────────
        case State::WAITING_NEXT:
        {
            // Ejected = route done
            if (!InVehicle()) {
                s_RouteKnown = true;
                SAMP::SendOnfootSync(); // confirm to server we're on foot
                Game::Log("[CG] Ejected → RESTART (%d CPs)", s_RouteSize);
                s_State = State::RESTARTING;
                s_RestartStep = 0;
                s_StateTime = now;
                break;
            }

            if (elapsed < CP_POLL_MIN) break;

            Vec3 cp; bool race;
            if (PollCheckpoint(cp, race) && IsDifferent(cp, s_LastCP)) {
                // New CP appeared → go there
                s_CurCP = cp;
                s_IsRace = race;
                s_TpSub = 0;
                s_State = State::TELEPORTING;
                s_StateTime = now;
                break;
            }

            if (elapsed >= NEXT_CP_WAIT) {
                if (!SAMP::IsCheckpointActive() && !SAMP::IsRaceCheckpointActive()) {
                    s_RouteKnown = true;
                    Game::Log("[CG] No CP → done (%d) → RESTART", s_RouteSize);
                    s_State = State::RESTARTING;
                    s_RestartStep = 0;
                    s_StateTime = now;
                } else {
                    s_SamePosRetry++;
                    if (s_SamePosRetry >= (int)SAME_CP_RETRIES) {
                        // Stuck on same CP too long → assume route done, restart
                        s_SamePosRetry = 0;
                        s_RouteKnown = (s_RouteSize > 0);
                        Game::Log("[CG] Force-accept → RESTART (%d CPs)", s_RouteSize);
                        s_State = State::RESTARTING;
                        s_RestartStep = 0;
                        s_StateTime = now;
                        break;
                    } else {
                        // Re-teleport to same CP
                        WORD vid = SAMP::GetVehicleID();
                        if (vid != 0xFFFF) {
                            SAMP::TeleportVehicle(vid, s_LastCP.x, s_LastCP.y, s_LastCP.z);
                            Game::Log("[CG] Re-TP CP (retry %d)", s_SamePosRetry);
                        }
                    }
                    s_StateTime = now;
                }
            }
            break;
        }

        // ────────────── TURBO ──────────────
        // The route is cached so we know WHERE each CP will be,
        // but we MUST wait for the server to actually SET the CP
        // before teleporting there — otherwise ProcessCheckpoints
        // won't fire onEnterCheckpoint and we don't get paid.
        //
        // Sub 0: Wait for server CP to appear
        // Sub 1: Teleport to it
        // Sub 2: Wait for server to accept (CP changes or disappears)
        // ─────────────────────────────────
        case State::TURBO:
        {
            if (!InVehicle()) {
                Game::Log("[CG] Lost vehicle TURBO → RESTART");
                SAMP::SendOnfootSync(); // confirm to server we're on foot
                s_State = State::RESTARTING;
                s_RestartStep = 0;
                s_TpSub = 0;
                s_StateTime = now;
                break;
            }

            if (s_TurboIdx >= s_RouteSize) {
                Game::Log("[CG] TURBO done → RESTART");
                s_State = State::RESTARTING;
                s_RestartStep = 0;
                s_TpSub = 0;
                s_StateTime = now;
                break;
            }

            Vec3 target = s_Route[s_TurboIdx];
            if (!IsValidPos(target.x, target.y)) {
                s_TurboIdx++;
                s_TpSub = 0;
                s_StateTime = now;
                break;
            }

            WORD vid = SAMP::GetVehicleID();

            // Sub 0: Wait for the server to set a checkpoint
            if (s_TpSub == 0) {
                Vec3 cp; bool race;
                if (PollCheckpoint(cp, race)) {
                    // CP appeared — teleport to it now
                    if (vid != 0xFFFF)
                        SAMP::TeleportVehicle(vid, cp.x, cp.y, cp.z);
                    s_TpSub = 1;
                    s_StateTime = now;
                    Game::Log("[CG] T[%d/%d] → (%.0f,%.0f,%.0f)", s_TurboIdx + 1, s_RouteSize, cp.x, cp.y, cp.z);
                } else if (elapsed >= TURBO_BAIL) {
                    Game::Log("[CG] TURBO bail no CP → LEARN");
                    s_RouteKnown = false;
                    s_RouteSize = 0;
                    s_State = State::RESTARTING;
                    s_RestartStep = 0;
                    s_TpSub = 0;
                    s_StateTime = now;
                }
                break;
            }

            // Sub 1: Wait for ProcessCheckpoints to fire enter event
            if (s_TpSub == 1) {
                if (elapsed < TURBO_SETTLE) break;

                Vec3 cp; bool race;
                bool hasCp = PollCheckpoint(cp, race);

                // CP changed = server accepted the enter → advance
                if (hasCp && IsDifferent(cp, target)) {
                    s_TurboIdx++;
                    s_CPCount++;
                    s_TpSub = 0;
                    s_StateTime = now;
                    break;
                }

                // CP disappeared = last one or server cleared it
                if (!hasCp) {
                    s_TurboIdx++;
                    s_CPCount++;
                    if (s_TurboIdx >= s_RouteSize) {
                        Game::Log("[CG] TURBO no-CP done");
                        s_State = State::RESTARTING;
                        s_RestartStep = 0;
                        s_TpSub = 0;
                    } else {
                        s_TpSub = 0; // wait for next CP
                    }
                    s_StateTime = now;
                    break;
                }

                // Same CP still there — re-teleport to make sure we're inside radius
                if (elapsed >= TURBO_RESYNC) {
                    if (vid != 0xFFFF)
                        SAMP::TeleportVehicle(vid, cp.x, cp.y, cp.z);
                    Game::Log("[CG] TURBO re-tp [%d]", s_TurboIdx);
                    s_StateTime = now;
                    break;
                }

                if (elapsed >= TURBO_BAIL) {
                    Game::Log("[CG] TURBO bail → LEARN");
                    s_RouteKnown = false;
                    s_RouteSize = 0;
                    s_State = State::RESTARTING;
                    s_RestartStep = 0;
                    s_TpSub = 0;
                    s_StateTime = now;
                    break;
                }
            }
            break;
        }

        // ────────────── RESTARTING ──────────────
        // Step 0: Check if already in boat → skip to step 3
        //         Otherwise exit vehicle
        // Step 1: Wait for boat 30
        // Step 2: TP ped + PutIntoVehicle (game warp + sync)
        // Step 3: Press "2" + wait for CP
        // ─────────────────────────────────────
        case State::RESTARTING:
        {
            switch (s_RestartStep) {

            case 0: // Check current state
            {
                // Already in the correct boat? Skip directly to pressing "2"
                WORD curVeh = SAMP::GetVehicleID();
                if (curVeh == BOAT_SAMP_ID && InVehicle()) {
                    s_Key2Attempts = 0;
                    s_LastKeyPress = 0;
                    s_TpSub = 0;
                    s_RestartStep = 3;
                    s_StateTime = now;
                    Game::Log("[CG] R:0 already in boat → R:3");
                    break;
                }

                // Whether on foot or in another vehicle, go straight to
                // waiting for boat → PutIntoVehicle will handle the switch.
                // Never call ExitVehicle — it sends an RPC the server detects.
                s_TpSub = 0;
                s_RestartStep = 1;
                s_StateTime = now;
                Game::Log("[CG] R:0 on foot");
                break;
            }

            case 1: // Wait for boat
            {
                auto* sv = SAMP::GetSAMPVehicle(BOAT_SAMP_ID);
                bool exists = false;
                if (sv) {
                    __try { exists = (sv->DoesExist() != 0); }
                    __except (EXCEPTION_EXECUTE_HANDLER) {}
                }
                if (exists) {
                    s_RestartStep = 2;
                    s_StateTime = now;
                    Game::Log("[CG] R:1 boat ready");
                }
                break;
            }

            case 2: // TP + enter vehicle (non-blocking sub-steps)
            {
                // Sub 0: Teleport ped to boat position
                if (s_TpSub == 0) {
                    SAMP::TeleportPed(BOAT_X, BOAT_Y, BOAT_Z);
                    SAMP::SetPedRotation(BOAT_HEADING);
                    s_TpSub = 1;
                    s_StateTime = now;
                    Game::Log("[CG] R:2 TP to boat");
                    break;
                }

                // Sub 1: Wait for TP to settle, then warp into vehicle
                if (s_TpSub == 1) {
                    if (elapsed < ENTER_TP_WAIT) break;
                    if (SAMP::PutIntoVehicle(BOAT_SAMP_ID)) {
                        s_TpSub = 2;
                        s_StateTime = now;
                        Game::Log("[CG] R:2 put into vehicle");
                    } else {
                        Game::Log("[CG] R:2 PutInto failed → retry");
                        s_TpSub = 0;
                        s_RestartStep = 1;
                        s_StateTime = now;
                    }
                    break;
                }

                // Sub 2: Wait for game to process vehicle entry + SA-MP auto-sync
                if (s_TpSub == 2) {
                    if (elapsed < ENTER_WARP_WAIT) break;
                    if (!InVehicle()) {
                        Game::Log("[CG] R:2 not in vehicle → retry");
                        s_TpSub = 0;
                        s_StateTime = now;
                        break;
                    }
                    // SA-MP syncs automatically via CLocalPlayer::Process()
                    s_Key2Attempts = 0;
                    s_LastKeyPress = 0;
                    s_PreKey2Snap = false;
                    s_TpSub = 0;
                    s_RestartStep = 3;
                    s_StateTime = now;
                    Game::Log("[CG] R:2 in vehicle");
                    break;
                }
                break;
            }

            case 3: // Press "2" + wait for NEW CP
            {
                if (elapsed < ENTER_SETTLE) break;

                if (!InVehicle()) {
                    Game::Log("[CG] R:3 lost vehicle → R:0");
                    s_RestartStep = 0;
                    s_StateTime = now;
                    break;
                }

                // Snapshot current CP state BEFORE first key press
                if (!s_PreKey2Snap) {
                    Vec3 cp; bool race;
                    if (PollCheckpoint(cp, race)) {
                        s_PreKey2CP = cp;
                        Game::Log("[CG] R:3 stale CP (%.0f,%.0f,%.0f)", cp.x, cp.y, cp.z);
                    } else {
                        s_PreKey2CP = {0, 0, 0};
                    }
                    s_PreKey2Snap = true;
                }

                // Release key after hold duration
                if (s_KeyHeld && (now - s_LastKeyPress) >= KEY2_HOLD) {
                    ReleaseKey2();
                }

                // Press "2": first time or after KEY2_WAIT since last
                bool needPress = !s_KeyHeld &&
                    (s_Key2Attempts == 0 || (now - s_LastKeyPress) >= KEY2_WAIT);
                if (needPress) {
                    PressKey2();
                    s_Key2Attempts++;
                    s_LastKeyPress = now;
                    Game::Log("[CG] R:3 key2 #%d", s_Key2Attempts);
                    break;
                }

                // Check if a NEW/DIFFERENT CP appeared (not the stale one)
                if (!s_KeyHeld) {
                    Vec3 cp; bool race;
                    if (PollCheckpoint(cp, race) && IsDifferent(cp, s_PreKey2CP)) {
                        Game::Log("[CG] R:3 NEW CP! → IDLE (#%d) (%.0f,%.0f,%.0f)", s_Key2Attempts, cp.x, cp.y, cp.z);
                        s_RestartStep = 0;
                        s_PreKey2Snap = false;
                        s_State = State::IDLE;
                        s_StateTime = now;
                        break;
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
