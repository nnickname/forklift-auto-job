#include "game.h"
#include <stdio.h>
#include <stdarg.h>

namespace Game {

    // Resolve log path next to GTA_SA.exe (not CWD, which may differ)
    static const char* GetLogPath() {
        static char path[MAX_PATH] = {};
        if (!path[0]) {
            GetModuleFileNameA(NULL, path, MAX_PATH);
            char* sl = strrchr(path, '\\');
            if (sl) *(sl + 1) = '\0';
            strcat_s(path, "coastguard_debug.log");
        }
        return path;
    }

    void AddChatMessage(DWORD color, const char* text) {
        Log("[CHAT] %s", text);
        __try { SAMP::AddChatMessage(color, text); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    void Log(const char* fmt, ...) {
        char buf[1024];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);

        // Timestamp prefix
        SYSTEMTIME st;
        GetLocalTime(&st);
        char line[1200];
        snprintf(line, sizeof(line), "[%02d:%02d:%02d.%03d] %s",
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, buf);

        HANDLE h = CreateFileA(GetLogPath(),
            FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD w;
            WriteFile(h, line, lstrlenA(line), &w, NULL);
            WriteFile(h, "\r\n", 2, &w, NULL);
            CloseHandle(h);
        }
    }
}
