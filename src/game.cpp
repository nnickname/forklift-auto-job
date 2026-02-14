#include "game.h"
#include "samp.h"
#include <stdio.h>
#include <stdarg.h>

namespace Game {
    void AddChatMessage(DWORD color, const char* text) {
        // Log to file only - SA-MP function calls disabled (wrong offsets crash)
        Log("[CHAT] %s", text);
    }

    void Log(const char* fmt, ...) {
        char buffer[1024];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);

        HANDLE hFile = CreateFileA("forklift_debug.log", 
            FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 
            FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            DWORD written;
            WriteFile(hFile, buffer, lstrlenA(buffer), &written, NULL);
            WriteFile(hFile, "\r\n", 2, &written, NULL);
            CloseHandle(hFile);
        }
    }
}
