/**
 * SA-MP 0.3DL Coastguard Auto-Job — ASI Plugin
 *
 * F5 = toggle mod (with admin check before activation)
 * Reference: https://github.com/BlastHackNet/SAMP-API/tree/multiver/include/sampapi/0.3.DL-1
 */

#include <windows.h>
#include <stdio.h>
#include "samp.h"
#include "game.h"
#include "coastguard.h"
#include "autologin.h"

// ════════════════════════════════════════════════════════════
// Globals
// ════════════════════════════════════════════════════════════
static bool   g_ModActive = false;
static bool   g_Running   = true;
static bool   g_LoginNotified = false;   // one-shot: first admin check after login
static bool   g_AdminCheckDone = false;  // true after at least ONE admin check completed
static HANDLE g_Thread    = NULL;
static HANDLE g_Watchdog  = NULL;

// ════════════════════════════════════════════════════════════
// Admin Check — reads chatlog.txt size before/after /admins
// If response has > 1 new line → admins online → block mod.
// ════════════════════════════════════════════════════════════
// Admin Check — reads chatlog after /admins and parses response.
//
// Real format:
//   [ _______________ ADMINISTRADORES _______________ ]
//   (ID: 18) Lead Admin Zoom (reportes: 74) (dudas: 59)
//   (ID: 20) Game Moderator HoneyBooom! (...)
//   (ID: 22) Helper BLK (dudas: 1)
//
// Logic:
//   - Lines containing "Admin" or "Moderator" (but NOT "Helper") = real admins
//   - If 0 real admins → safe to run
//   - If any real admins → pause route, keep checking every minute
// ════════════════════════════════════════════════════════════
namespace AdminCheck {
    enum class Phase { IDLE, SNAPSHOT, SENDING, WAITING, EVALUATING };

    static const DWORD WAIT_MS   = 2000;
    static const DWORD PERIOD_MS = 30000;

    static Phase s_Phase       = Phase::IDLE;
    static DWORD s_Timestamp   = 0;
    static DWORD s_LastPeriodic = 0;
    static bool  s_Periodic    = false;
    static char  s_Path[MAX_PATH] = {};
    static DWORD s_PreSize     = 0;
    static bool  s_AdminsOnline = false; // persistent state

    static bool FindChatlog() {
        if (s_Path[0]) return true;
        char doc[MAX_PATH];
        if (GetEnvironmentVariableA("USERPROFILE", doc, MAX_PATH)) {
            snprintf(s_Path, MAX_PATH,
                "%s\\Documents\\GTA San Andreas User Files\\SAMP\\chatlog.txt", doc);
            if (GetFileAttributesA(s_Path) != INVALID_FILE_ATTRIBUTES) return true;
        }
        char exe[MAX_PATH];
        GetModuleFileNameA(NULL, exe, MAX_PATH);
        char* sl = strrchr(exe, '\\');
        if (sl) *(sl + 1) = '\0';
        snprintf(s_Path, MAX_PATH, "%schatlog.txt", exe);
        if (GetFileAttributesA(s_Path) != INVALID_FILE_ATTRIBUTES) return true;
        s_Path[0] = 0;
        return false;
    }

    static DWORD GetSize() {
        if (!FindChatlog()) return 0;
        WIN32_FILE_ATTRIBUTE_DATA f;
        if (!GetFileAttributesExA(s_Path, GetFileExInfoStandard, &f)) return 0;
        return f.nFileSizeLow;
    }

    // Case-insensitive substring search
    static bool StrContainsCI(const char* haystack, const char* needle) {
        if (!haystack || !needle) return false;
        int hLen = (int)strlen(haystack);
        int nLen = (int)strlen(needle);
        if (nLen > hLen) return false;
        for (int i = 0; i <= hLen - nLen; i++) {
            bool match = true;
            for (int j = 0; j < nLen; j++) {
                char a = haystack[i + j];
                char b = needle[j];
                if (a >= 'A' && a <= 'Z') a += 32;
                if (b >= 'A' && b <= 'Z') b += 32;
                if (a != b) { match = false; break; }
            }
            if (match) return true;
        }
        return false;
    }

    // Read new chatlog content and count REAL admin lines.
    // Returns -1 if chatlog could not be read (treat as "unknown").
    // Returns 0 if no admins, >0 if admins found.
    static int CountRealAdmins() {
        DWORD sz = GetSize();
        Game::Log("[ADMIN] Chatlog size: pre=%lu, now=%lu, path=%s", s_PreSize, sz, s_Path);

        if (sz == 0 && s_PreSize == 0) {
            Game::Log("[ADMIN] Chatlog not found or empty — assuming no admins");
            return -1; // no chatlog
        }
        if (sz <= s_PreSize) {
            Game::Log("[ADMIN] No new data in chatlog (sz=%lu <= pre=%lu)", sz, s_PreSize);
            return -1; // no new data
        }

        DWORD diff = sz - s_PreSize;
        if (diff > 8192) diff = 8192; // cap read

        HANDLE h = CreateFileA(s_Path, GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            Game::Log("[ADMIN] Cannot open chatlog!");
            return -1;
        }
        SetFilePointer(h, s_PreSize, NULL, FILE_BEGIN);

        char buf[8192];
        DWORD rd = 0;
        ReadFile(h, buf, diff, &rd, NULL);
        CloseHandle(h);
        if (!rd) {
            Game::Log("[ADMIN] ReadFile returned 0 bytes");
            return -1;
        }
        buf[rd < sizeof(buf) ? rd : sizeof(buf) - 1] = '\0';
        Game::Log("[ADMIN] Read %lu bytes from chatlog", rd);

        // Log entire content for debugging
        Game::Log("[ADMIN] Raw content: %.500s", buf);

        int adminCount = 0;
        bool gotResponse = false; // did we see ANY /admins response?

        char* line = buf;
        while (line && *line) {
            char* nl = strchr(line, '\n');
            if (nl) *nl = '\0';

            // Skip empty/whitespace lines
            if (strlen(line) < 3) {
                if (nl) { line = nl + 1; continue; } else break;
            }

            // Detect /admins response (header or "no hay")
            if (StrContainsCI(line, "ADMINISTRADORES") || StrContainsCI(line, "No hay administradores")) {
                gotResponse = true;
                Game::Log("[ADMIN] Response line: %s", line);
            }
            // Skip helper lines
            else if (StrContainsCI(line, "Helper")) {
                Game::Log("[ADMIN] Helper (skip): %s", line);
            }
            // Real admin: contains "Admin" or "Moderator" and "(ID:"
            else if (StrContainsCI(line, "(ID:") &&
                     (StrContainsCI(line, "Admin") || StrContainsCI(line, "Moderator"))) {
                adminCount++;
                gotResponse = true;
                Game::Log("[ADMIN] *** ADMIN FOUND: %s", line);
            }

            if (nl) { line = nl + 1; } else { break; }
        }

        if (!gotResponse) {
            Game::Log("[ADMIN] No /admins response detected in chatlog diff");
            return -1;
        }

        return adminCount;
    }

    static void Begin(bool periodic) {
        s_Periodic = periodic;
        s_Phase = Phase::SNAPSHOT;
    }

    static bool IsChecking() { return s_Phase != Phase::IDLE; }
    static bool NeedsPeriodic() { return GetTickCount() - s_LastPeriodic >= PERIOD_MS; }
    static bool AreAdminsOnline() { return s_AdminsOnline; }

    // Returns true when finished. Sets adminsOnline + adminCount.
    static bool Update(bool& adminsOnline, int& adminCount) {
        adminsOnline = s_AdminsOnline;
        adminCount = 0;
        switch (s_Phase) {
        case Phase::IDLE:
            return false;
        case Phase::SNAPSHOT:
            s_PreSize = GetSize();
            s_Phase = Phase::SENDING;
            return false;
        case Phase::SENDING:
            __try { SAMP::SendChat("/admins"); }
            __except (EXCEPTION_EXECUTE_HANDLER) { s_Phase = Phase::IDLE; return true; }
            s_Timestamp = GetTickCount();
            s_Phase = Phase::WAITING;
            return false;
        case Phase::WAITING:
            if (GetTickCount() - s_Timestamp >= WAIT_MS)
                s_Phase = Phase::EVALUATING;
            return false;
        case Phase::EVALUATING:
            adminCount = CountRealAdmins();
            if (adminCount < 0) {
                // Could not read chatlog or no response — assume no admins
                Game::Log("[ADMIN] Chatlog read failed — assuming NO admins");
                adminCount = 0;
                s_AdminsOnline = false;
            } else {
                s_AdminsOnline = (adminCount > 0);
            }
            adminsOnline = s_AdminsOnline;
            Game::Log("[ADMIN] Result: %d real admins → %s", adminCount, s_AdminsOnline ? "ONLINE" : "offline");
            s_Phase = Phase::IDLE;
            s_LastPeriodic = GetTickCount();
            return true;
        }
        return false;
    }
}

// ════════════════════════════════════════════════════════════
// Deactivation
// ════════════════════════════════════════════════════════════
static void Deactivate(const char* reason) {
    g_ModActive = false;
    Coastguard::FullReset();
    __try { SAMP::RestoreCamera(); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    Game::Log("[MOD] Deactivated: %s", reason);
}

// ════════════════════════════════════════════════════════════
// F5 Toggle
// ════════════════════════════════════════════════════════════
static void CheckToggle() {
    // F6 = instant kill game (for testing)
    static bool f6Was = false;
    bool f6Is = (GetAsyncKeyState(VK_F6) & 0x8000) != 0;
    if (f6Is && !f6Was) {
        Game::Log("[MOD] F6 pressed — killing process");
        Beep(200, 100);
        TerminateProcess(GetCurrentProcess(), 0);
    }
    f6Was = f6Is;

    // F7 = toggle auto-login
    static bool f7Was = false;
    bool f7Is = (GetAsyncKeyState(VK_F7) & 0x8000) != 0;
    if (f7Is && !f7Was) {
        if (!AutoLogin::IsActive()) {
            AutoLogin::Start();
            Beep(600, 100); Sleep(50); Beep(800, 100);
            __try { Game::AddChatMessage(0xFF00FFFF, "[AutoLogin] {FFFFFF}Started — waiting for dialogs"); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        } else {
            AutoLogin::Stop();
            Beep(400, 100);
            __try { Game::AddChatMessage(0xFFFF8800, "[AutoLogin] {FFFFFF}Stopped"); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
    }
    f7Was = f7Is;

    static bool keyWas = false;
    bool keyIs = (GetAsyncKeyState(VK_F5) & 0x8000) != 0;
    if (keyIs && !keyWas) {
        if (!g_ModActive) {
            if (!g_AdminCheckDone) {
                // No admin check done yet → run one now, don't activate
                Beep(200, 100);
                __try { Game::AddChatMessage(0xFFFFFF00, "[Coastguard] {FFFFFF}Checking admins first..."); }
                __except (EXCEPTION_EXECUTE_HANDLER) {}
                if (!AdminCheck::IsChecking()) AdminCheck::Begin(false);
            } else if (AdminCheck::AreAdminsOnline()) {
                Beep(200, 100);
                __try { Game::AddChatMessage(0xFFFF0000, "[Coastguard] {FFFFFF}Blocked — admins online"); }
                __except (EXCEPTION_EXECUTE_HANDLER) {}
            } else {
                g_ModActive = true;
                Coastguard::StartRestart();
                Beep(1000, 150);
                __try { Game::AddChatMessage(0xFF00FF00, "[Coastguard] {FFFFFF}Mod ON — F5 to disable"); }
                __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
        } else {
            Beep(400, 150);
            Deactivate("F5");
            __try { Game::AddChatMessage(0xFFFF0000, "[Coastguard] {FFFFFF}Mod OFF"); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
    }
    keyWas = keyIs;
}

// ════════════════════════════════════════════════════════════
// Main Thread
// ════════════════════════════════════════════════════════════
static DWORD WINAPI MainThread(LPVOID) {
    Game::Log("=== Coastguard Plugin started ===");
    Sleep(10000);          // Wait for GTA to load
    Beep(800, 100); Sleep(100); Beep(800, 100);

    bool sampOK = false;
    DWORD lastDetect = 0;

    while (g_Running) {
        __try { CheckToggle(); } __except (EXCEPTION_EXECUTE_HANDLER) {}
        DWORD now = GetTickCount();

        // ── SA-MP detection (poll every 2s) ──
        if (!sampOK) {
            if (now - lastDetect > 2000) {
                lastDetect = now;
                if (SAMP::IsConnected()) {
                    sampOK = true;
                    Game::Log("[DETECT] SA-MP connected");
                    Beep(1200, 100); Sleep(50); Beep(1500, 100);

                    // Auto-start login if not already active
                    if (!AutoLogin::IsActive() && !AutoLogin::IsDone()) {
                        AutoLogin::Start();
                        Game::Log("[MAIN] AutoLogin auto-started on connect");
                    }
                }
            }
        } else if (!SAMP::IsConnected()) {
            sampOK = false;
            Coastguard::Reset();
            Game::Log("[DETECT] SA-MP disconnected");
        }

        // ── Admin check processing ──
        // This processes the state machine for /admins command.
        // When a check completes, it decides: start, pause, or keep waiting.
        if (sampOK && AdminCheck::IsChecking()) {
            __try {
                bool admins = false; int count = 0;
                if (AdminCheck::Update(admins, count)) {
                    g_AdminCheckDone = true;
                    Game::Log("[MAIN] Admin check done: admins=%d, modActive=%d, loginDone=%d",
                        (int)admins, (int)g_ModActive, (int)AutoLogin::IsDone());

                    if (admins && g_ModActive) {
                        // Admins appeared while running → pause
                        Beep(300, 200);
                        Deactivate("Admins online — pausing");
                        __try { Game::AddChatMessage(0xFFFF8800, "[Coastguard] {FFFFFF}Paused — admins online"); }
                        __except (EXCEPTION_EXECUTE_HANDLER) {}
                    }
                    else if (!admins && !g_ModActive && AutoLogin::IsDone()) {
                        // No admins + login done + mod not running → auto-start!
                        g_ModActive = true;
                        Coastguard::StartRestart();
                        Beep(1000, 150);
                        Game::Log("[MOD] Auto-started — no admins");
                        __try { Game::AddChatMessage(0xFF00FF00, "[Coastguard] {FFFFFF}Auto-started — no admins"); }
                        __except (EXCEPTION_EXECUTE_HANDLER) {}
                    }
                    else if (admins && !g_ModActive) {
                        Game::Log("[MAIN] Admins online — waiting...");
                    }
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                Game::Log("[MAIN] Exception in admin check");
            }
        }

        // Periodic admin recheck — ALWAYS when login done (running or waiting)
        if (sampOK && AutoLogin::IsDone() && !AdminCheck::IsChecking() && AdminCheck::NeedsPeriodic()) {
            Game::Log("[MAIN] Periodic admin recheck");
            AdminCheck::Begin(true);
        }

        // ── Auto-Login logic ──
        if (sampOK && AutoLogin::IsActive()) {
            __try {
                AutoLogin::Update();
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                Game::Log("[MAIN] Exception in autologin logic");
            }
        }

        // ── One-shot: login just finished → trigger first admin check ──
        if (sampOK && AutoLogin::IsDone() && !g_LoginNotified) {
            g_LoginNotified = true;
            __try { Game::AddChatMessage(0xFF00FF00, "[AutoLogin] {FFFFFF}Login complete! Checking admins..."); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
            AdminCheck::Begin(false);
            Game::Log("[MAIN] AutoLogin done — first admin check started");
        }

        // ── Coastguard logic (always tick — every state handles its own guards) ──
        if (g_ModActive && sampOK) {
            __try {
                Coastguard::Update();
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                Game::Log("[MAIN] Exception in coastguard logic");
            }
        }

        Sleep(5);
    }

    Game::Log("=== Plugin stopped ===");
    return 0;
}

// ════════════════════════════════════════════════════════════
// Watchdog Thread — independent key2 sender.
//
// This thread does NOTHING except monitor the mod state and
// send key2 sync via RakNet when the mod is stuck in COOLING
// or RESTARTING. It uses ZERO GTA function calls (no camera,
// no ped, no vehicle). Only direct memory writes + RakNet.
//
// Why: When GTA's main thread freezes (GPU stall), our main
// worker thread can also deadlock if it calls any GTA virtual
// functions. This watchdog is designed to survive that freeze.
// ════════════════════════════════════════════════════════════
static DWORD WINAPI WatchdogThread(LPVOID) {
    Game::Log("[WATCHDOG] Started");
    Sleep(15000); // Wait for game to fully load

    DWORD lastCycle = 0;       // Last CP count we saw
    DWORD lastCycleTime = 0;   // When CP count last changed
    int   attempts = 0;        // How many restart attempts this freeze
    static constexpr WORD BOAT_ID = 1;

    while (g_Running) {
        Sleep(200); // Check every 200ms (fast)

        if (!g_ModActive) {
            attempts = 0;
            continue;
        }

        // Track if the main worker is making progress
        int curCPs = Coastguard::GetCPCount();
        DWORD now = GetTickCount();

        if (curCPs != (int)lastCycle) {
            lastCycle = curCPs;
            lastCycleTime = now;
            attempts = 0;
        }

        auto state = Coastguard::GetState();

        // Detect stuck state: either COOLING or worker stuck for 5s+
        bool needsHelp = (state == Coastguard::State::COOLING)
                      || (state == Coastguard::State::RESTARTING)
                      || ((curCPs >= 50) && (now - lastCycleTime > 5000));

        if (!needsHelp || attempts >= 60) continue;

        attempts++;

        __try {
            auto* lp = SAMP::GetLocalPlayer();
            if (!lp) continue;

            // Step 1: Try to put into vehicle (safe: SAMP API, no GTA camera calls)
            // Check if we're already in vehicle via SAMP state
            bool inVeh = (lp->m_nCurrentVehicle != 0xFFFF);

            if (!inVeh) {
                // Try PutIntoVehicle via SAMP API
                auto* ped = SAMP::GetPlayerPed();
                if (ped) {
                    GTAREF ref = SAMP::GetVehicleRef(BOAT_ID);
                    if (ref) {
                        __try { ped->PutIntoVehicle(ref, 0); } __except(EXCEPTION_EXECUTE_HANDLER) {}
                        lp->m_nCurrentVehicle = BOAT_ID;
                        if (attempts % 5 == 1)
                            Game::Log("[WATCHDOG] PutIntoVehicle #%d", attempts);
                    }
                }
                Sleep(500); // Let game process the warp
                continue;
            }

            // Step 2: Already in vehicle → send key2 sync
            lp->m_incarData.m_controllerState.m_bShockButtonR = 1;
            lp->SendIncarData();
            Sleep(200);
            lp->m_incarData.m_controllerState.m_bShockButtonR = 0;
            lp->SendIncarData();

            if (attempts % 5 == 1)
                Game::Log("[WATCHDOG] key2 #%d (state=%d)", attempts, (int)state);

        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    Game::Log("[WATCHDOG] Stopped");
    return 0;
}

// ════════════════════════════════════════════════════════════
// DLL Entry Point
// ════════════════════════════════════════════════════════════
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        g_Thread   = CreateThread(NULL, 0, MainThread, NULL, 0, NULL);
        g_Watchdog = CreateThread(NULL, 0, WatchdogThread, NULL, 0, NULL);
    } else if (reason == DLL_PROCESS_DETACH) {
        g_Running = false;
        if (g_Thread)   { WaitForSingleObject(g_Thread, 2000);   CloseHandle(g_Thread); }
        if (g_Watchdog) { WaitForSingleObject(g_Watchdog, 2000); CloseHandle(g_Watchdog); }
    }
    return TRUE;
}
