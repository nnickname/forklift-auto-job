#pragma once
/**
 * Auto-Login — Dialog-based automatic login for SA-MP servers.
 *
 * Detects server dialogs and responds automatically:
 *   1. Email dialog  → types email
 *   2. Password dialog → types password
 *   3. Character select → accepts first character
 *   4. Welcome/info → clicks Accept
 *
 * Uses CDialog polling + CDialog::Close() to respond.
 * Falls back to SendInput keystrokes if editbox write fails.
 *
 * IMPORTANT: This module does NOT start the coastguard route.
 *            It only handles the login sequence.
 */

#include <windows.h>
#include <cstring>
#include "game.h"
#include "samp.h"

namespace AutoLogin {

    // ════════════════════════════════════════════════════════
    // Config — hardcoded credentials
    // ════════════════════════════════════════════════════════

    static constexpr const char* LOGIN_EMAIL    = "mquintana1959@outlook.es";
    static constexpr const char* LOGIN_PASSWORD  = "b`:7j/ws&1LmXN{T";

    // SA-MP path (for auto-launch — future use)
    static constexpr const char* SAMP_PATH      = "C:\\Users\\barto\\Desktop\\games\\installergta";
    static constexpr const char* SERVER_IP       = "localhost";
    static constexpr int         SERVER_PORT     = 7777;
    static constexpr const char* PLAYER_NAME     = "Xylos";

    // ════════════════════════════════════════════════════════
    // Timings
    // ════════════════════════════════════════════════════════

    static constexpr DWORD DIALOG_POLL_MS   = 200;   // Poll for dialogs every 200ms
    static constexpr DWORD DIALOG_DELAY_MS  = 800;   // Wait before responding to a dialog
    static constexpr DWORD KEYSTROKE_DELAY  = 30;    // Delay between keystrokes (ms)
    static constexpr DWORD POST_CLOSE_WAIT  = 1500;  // Wait after closing dialog
    static constexpr DWORD LOGIN_TIMEOUT_MS = 60000; // Total timeout for login sequence

    // ════════════════════════════════════════════════════════
    // State
    // ════════════════════════════════════════════════════════

    enum class Phase {
        DISABLED,       // Not active
        WAITING_DIALOG, // Waiting for a dialog to appear
        DIALOG_SEEN,    // Dialog detected, waiting DIALOG_DELAY_MS 
        RESPONDING,     // Sending response
        POST_RESPONSE,  // Waiting POST_CLOSE_WAIT after response
        DONE            // Login complete
    };

    static Phase  s_Phase        = Phase::DISABLED;
    static DWORD  s_PhaseTime    = 0;
    static DWORD  s_StartTime    = 0;
    static DWORD  s_LastPollTime = 0;   // separate poll rate-limiter
    static int    s_DialogsSeen  = 0;
    static int    s_LastDialogID = -1;
    static bool   s_LoginDone    = false;

    // Track which dialog ID we last responded to, to avoid double-responding
    static int    s_LastRespondedID = -1;

    // ════════════════════════════════════════════════════════
    // Helpers
    // ════════════════════════════════════════════════════════

    // Check if caption/text contains a substring (case-insensitive)
    static bool ContainsCI(const char* haystack, const char* needle) {
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

    // Type text via SendInput keystrokes (fallback)
    static void TypeText(const char* text) {
        for (int i = 0; text[i]; i++) {
            INPUT inputs[2] = {};

            // Key down
            inputs[0].type = INPUT_KEYBOARD;
            inputs[0].ki.wVk = 0;
            inputs[0].ki.wScan = (WORD)(unsigned char)text[i];
            inputs[0].ki.dwFlags = KEYEVENTF_UNICODE;

            // Key up
            inputs[1].type = INPUT_KEYBOARD;
            inputs[1].ki.wVk = 0;
            inputs[1].ki.wScan = (WORD)(unsigned char)text[i];
            inputs[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;

            SendInput(2, inputs, sizeof(INPUT));
            Sleep(KEYSTROKE_DELAY);
        }
    }

    // Press Enter key
    static void PressEnter() {
        INPUT inputs[2] = {};
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wVk = VK_RETURN;
        inputs[0].ki.dwFlags = 0;
        inputs[1].type = INPUT_KEYBOARD;
        inputs[1].ki.wVk = VK_RETURN;
        inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(2, inputs, sizeof(INPUT));
    }

    // Respond to a dialog: set input text (if needed) + close
    static bool RespondToDialog(int dialogType, const char* inputText, bool useCancel = false) {
        if (dialogType == 1 || dialogType == 3) {
            // DIALOG_INPUT (1) or DIALOG_PASSWORD (3) — need to set text
            bool textSet = SAMP::SetDialogInputText(inputText);
            if (textSet) {
                Game::Log("[LOGIN] Editbox text set OK");
            } else {
                // Fallback: type via SendInput
                Game::Log("[LOGIN] Editbox write failed — using SendInput");
                TypeText(inputText);
                Sleep(100);
            }
        }
        // Close: 1=Accept (left button), 0=Cancel (right button)
        return SAMP::CloseDialog(useCancel ? 0 : 1);
    }

    // ════════════════════════════════════════════════════════
    // Public Interface
    // ════════════════════════════════════════════════════════

    inline bool IsActive() { return s_Phase != Phase::DISABLED && s_Phase != Phase::DONE; }
    inline bool IsDone()   { return s_LoginDone; }

    inline void Start() {
        s_Phase = Phase::WAITING_DIALOG;
        s_PhaseTime = GetTickCount();
        s_StartTime = GetTickCount();
        s_LastPollTime = 0;
        s_DialogsSeen = 0;
        s_LastDialogID = -1;
        s_LastRespondedID = -1;
        s_LoginDone = false;
        Game::Log("[LOGIN] Auto-login started");
    }

    inline void Stop() {
        s_Phase = Phase::DISABLED;
        Game::Log("[LOGIN] Auto-login stopped");
    }

    // ════════════════════════════════════════════════════════
    // Main Update — call every 5ms from main thread
    // ════════════════════════════════════════════════════════

    inline void Update() {
        if (s_Phase == Phase::DISABLED || s_Phase == Phase::DONE)
            return;

        DWORD now = GetTickCount();
        DWORD elapsed = now - s_PhaseTime;

        // Global timeout
        if (now - s_StartTime > LOGIN_TIMEOUT_MS) {
            Game::Log("[LOGIN] Timeout — aborting");
            s_Phase = Phase::DONE;
            return;
        }

        switch (s_Phase) {

        case Phase::WAITING_DIALOG:
        {
            // Rate-limit polling (separate from s_PhaseTime so finalization works)
            if (now - s_LastPollTime < DIALOG_POLL_MS) break;
            s_LastPollTime = now;

            if (!SAMP::IsDialogActive()) break;

            int dlgId = SAMP::GetDialogID();
            int dlgType = SAMP::GetDialogType();
            char caption[128] = {};
            char text[512] = {};
            SAMP::GetDialogCaption(caption, sizeof(caption));
            SAMP::GetDialogText(text, sizeof(text));

            // Skip if we already responded to this exact dialog ID
            if (dlgId == s_LastRespondedID) break;

            s_LastDialogID = dlgId;
            s_DialogsSeen++;

            Game::Log("[LOGIN] Dialog #%d: id=%d type=%d caption='%s'",
                s_DialogsSeen, dlgId, dlgType, caption);
            Game::Log("[LOGIN] Text: %.200s", text);

            s_Phase = Phase::DIALOG_SEEN;
            s_PhaseTime = now;
            break;
        }

        case Phase::DIALOG_SEEN:
        {
            // Wait before responding (let dialog fully render)
            if (elapsed < DIALOG_DELAY_MS) break;

            // Re-check dialog is still active
            if (!SAMP::IsDialogActive()) {
                Game::Log("[LOGIN] Dialog disappeared before response");
                s_Phase = Phase::WAITING_DIALOG;
                s_PhaseTime = now;
                break;
            }

            int dlgType = SAMP::GetDialogType();
            char caption[128] = {};
            SAMP::GetDialogCaption(caption, sizeof(caption));

            // Decide what to input based on dialog characteristics
            const char* inputText = "";

            // Detection logic:
            // - Email: type=INPUT(1), caption/text contains "email" or "correo" or "mail"
            // - Password: type=PASSWORD(3), or caption/text contains "contrase" or "password"
            // - Character select: type=LIST(2), caption contains "personaje" or "character"
            // - Welcome/info: type=MSGBOX(0), just accept

            if (dlgType == 3) {
                // PASSWORD dialog
                inputText = LOGIN_PASSWORD;
                Game::Log("[LOGIN] → Responding PASSWORD");
            }
            else if (dlgType == 1) {
                // INPUT dialog — check what it's asking for
                if (ContainsCI(caption, "email") || ContainsCI(caption, "correo") || ContainsCI(caption, "mail")) {
                    inputText = LOGIN_EMAIL;
                    Game::Log("[LOGIN] → Responding EMAIL");
                } else if (ContainsCI(caption, "contrase") || ContainsCI(caption, "password")) {
                    inputText = LOGIN_PASSWORD;
                    Game::Log("[LOGIN] → Responding PASSWORD (input type)");
                } else {
                    // Unknown input dialog — try email as default
                    inputText = LOGIN_EMAIL;
                    Game::Log("[LOGIN] → Responding with EMAIL (unknown input dialog)");
                }
            }
            else if (dlgType == 2 || dlgType == 4 || dlgType == 5) {
                // LIST / TABLIST / HEADERSLIST — select first item (character select)
                inputText = "";
                Game::Log("[LOGIN] → Responding LIST/SELECT (accept first)");
            }
            else {
                // MESSAGEBOX or unknown — respond with CANCEL (right button)
                inputText = "";
                Game::Log("[LOGIN] → Responding CANCEL (msgbox/welcome)");
            }

            s_Phase = Phase::RESPONDING;
            s_PhaseTime = now;

            // MSGBOX (type 0) → use Cancel button; everything else → Accept
            bool useCancel = (dlgType == 0);
            bool ok = RespondToDialog(dlgType, inputText, useCancel);
            s_LastRespondedID = s_LastDialogID;

            if (ok) {
                Game::Log("[LOGIN] Dialog closed OK");
            } else {
                Game::Log("[LOGIN] Dialog close FAILED — trying Enter key");
                PressEnter();
            }

            s_Phase = Phase::POST_RESPONSE;
            s_PhaseTime = now;
            break;
        }

        case Phase::POST_RESPONSE:
        {
            // Wait after response for server to process and send next dialog
            if (elapsed < POST_CLOSE_WAIT) break;

            // Check if another dialog appeared
            if (SAMP::IsDialogActive()) {
                int dlgId = SAMP::GetDialogID();
                if (dlgId != s_LastRespondedID) {
                    // New dialog — handle it
                    s_Phase = Phase::WAITING_DIALOG;
                    s_PhaseTime = now;
                    break;
                }
                // Same dialog still showing — might have failed
                Game::Log("[LOGIN] Same dialog still active (id=%d) — retrying", dlgId);
                s_LastRespondedID = -1; // Allow retry
                s_Phase = Phase::WAITING_DIALOG;
                s_PhaseTime = now;
                break;
            }

            // No dialog active — check if we're spawned/playing
            // If no dialog for a while, assume login is done
            Game::Log("[LOGIN] No dialog after response — waiting for next...");
            s_Phase = Phase::WAITING_DIALOG;
            s_PhaseTime = now;

            // After responding to 3+ dialogs with no more appearing,
            // consider login complete on next check
            if (s_DialogsSeen >= 3) {
                Game::Log("[LOGIN] %d dialogs handled — marking ready to finalize", s_DialogsSeen);
            }
            break;
        }

        default:
            break;
        }

        // Finalization check: if we've handled 3+ dialogs and no dialog
        // has been active for 5 seconds, login is done
        if (s_DialogsSeen >= 3 && !SAMP::IsDialogActive() && s_Phase == Phase::WAITING_DIALOG) {
            if (now - s_PhaseTime > 5000) {
                s_LoginDone = true;
                s_Phase = Phase::DONE;
                Game::Log("[LOGIN] === Login complete! (%d dialogs handled) ===", s_DialogsSeen);
            }
        }
    }

} // namespace AutoLogin
