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

// ════════════════════════════════════════════════════════════
// Globals
// ════════════════════════════════════════════════════════════
static bool   g_ModActive = false;
static bool   g_Running   = true;
static HANDLE g_Thread    = NULL;

// ════════════════════════════════════════════════════════════
// Admin Check — reads chatlog.txt size before/after /admins
// If response has > 1 new line → admins online → block mod.
// ════════════════════════════════════════════════════════════
namespace AdminCheck {
    enum class Phase { IDLE, SNAPSHOT, SENDING, WAITING, EVALUATING };

    static const DWORD WAIT_MS   = 1200;
    static const DWORD PERIOD_MS = 60000;
    static const int   SAFE_LINES = 5;

    static Phase s_Phase       = Phase::IDLE;
    static DWORD s_Timestamp   = 0;
    static DWORD s_LastPeriodic = 0;
    static bool  s_Periodic    = false;
    static char  s_Path[MAX_PATH] = {};
    static DWORD s_PreSize     = 0;

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

    static int CountNewLines() {
        DWORD sz = GetSize();
        if (sz <= s_PreSize) return 0;
        DWORD diff = sz - s_PreSize;
        HANDLE h = CreateFileA(s_Path, GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (h == INVALID_HANDLE_VALUE) return 0;
        SetFilePointer(h, s_PreSize, NULL, FILE_BEGIN);
        char buf[4096];
        int lines = 0;
        DWORD rem = diff;
        while (rem > 0) {
            DWORD toRead = (rem < sizeof(buf)) ? rem : (DWORD)sizeof(buf);
            DWORD rd = 0;
            if (!ReadFile(h, buf, toRead, &rd, NULL) || !rd) break;
            for (DWORD i = 0; i < rd; i++) if (buf[i] == '\n') lines++;
            rem -= rd;
        }
        CloseHandle(h);
        return lines;
    }

    static void Begin(bool periodic) {
        s_Periodic = periodic;
        s_Phase = Phase::SNAPSHOT;
    }

    static bool IsChecking() { return s_Phase != Phase::IDLE; }
    static bool NeedsPeriodic() { return GetTickCount() - s_LastPeriodic >= PERIOD_MS; }

    // Returns true when finished. Sets adminsOnline + lineCount.
    static bool Update(bool& adminsOnline, int& lineCount) {
        adminsOnline = false;
        lineCount = 0;
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
            lineCount = CountNewLines();
            adminsOnline = (lineCount > SAFE_LINES);
            Game::Log("[ADMIN] %d lines → %s", lineCount, adminsOnline ? "ONLINE" : "offline");
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

    static bool keyWas = false;
    bool keyIs = (GetAsyncKeyState(VK_F5) & 0x8000) != 0;
    if (keyIs && !keyWas) {
        if (!g_ModActive) {
            // Start mod IMMEDIATELY — admin check runs in background
            g_ModActive = true;
            Coastguard::StartRestart();
            AdminCheck::Begin(false);
            Beep(1000, 150);
            __try { Game::AddChatMessage(0xFF00FF00, "[Coastguard] {FFFFFF}Mod ON — F5 to disable"); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
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
                }
            }
        } else if (!SAMP::IsConnected()) {
            sampOK = false;
            Coastguard::Reset();
            Game::Log("[DETECT] SA-MP disconnected");
        }

        // ── Admin check processing (runs in background, never blocks coastguard) ──
        if (sampOK && AdminCheck::IsChecking()) {
            __try {
                bool admins = false; int lines = 0;
                if (AdminCheck::Update(admins, lines) && admins && g_ModActive) {
                    // Admins detected → emergency kill game
                    Beep(200, 300); Sleep(100); Beep(200, 300);
                    Deactivate("Admins detected");
                    Game::Log("[MOD] Killing process — admins online");
                    TerminateProcess(GetCurrentProcess(), 0);
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }

        // Periodic admin recheck
        if (g_ModActive && sampOK && !AdminCheck::IsChecking() && AdminCheck::NeedsPeriodic())
            AdminCheck::Begin(true);

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
// DLL Entry Point
// ════════════════════════════════════════════════════════════
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        g_Thread = CreateThread(NULL, 0, MainThread, NULL, 0, NULL);
    } else if (reason == DLL_PROCESS_DETACH) {
        g_Running = false;
        if (g_Thread) { WaitForSingleObject(g_Thread, 2000); CloseHandle(g_Thread); }
    }
    return TRUE;
}
