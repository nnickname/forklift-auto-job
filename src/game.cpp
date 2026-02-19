#include "game.h"
#include <stdio.h>
#include <stdarg.h>

namespace Game {

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
        HANDLE h = CreateFileA("coastguard_debug.log",
            FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD w;
            WriteFile(h, buf, lstrlenA(buf), &w, NULL);
            WriteFile(h, "\r\n", 2, &w, NULL);
            CloseHandle(h);
        }
    }
}
