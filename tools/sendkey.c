/* sendkey.exe: press keys in the game from inside the bottle (Wine's SendInput), so tests need no hands on the keyboard.
 *   sendkey.exe [focus] <key>[:hold_ms] ...        keys: a letter or digit, or ESC ENTER SPACE TAB UP DOWN LEFT RIGHT F1..F12 LSHIFT LCTRL
 *   sendkey.exe [focus] mouse:<dx>,<dy> ...        relative mouse movement (look around)
 *   sendkey.exe at:<x>,<y> click rclick down up    move the pointer to a screen position; click, or press / release the left button
 *   sendkey.exe LALT+N[:hold_ms]                    a chord: keys go down in order, are held, and come up in reverse order
 *   sendkey.exe +LALT 300 N -LALT                   +KEY holds a key down until -KEY
 *   sendkey.exe list                               print the top-level windows
 * "focus" first makes the Star Citizen window Wine's foreground window. A plain number is a pause in milliseconds. */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static HWND game;
static BOOL CALLBACK each(HWND h, LPARAM list) {
    char t[256] = "", c[128] = ""; GetWindowTextA(h, t, sizeof t); GetClassNameA(h, c, sizeof c);
    if (list && IsWindowVisible(h)) printf("  %p visible class=%s title=%s\n", (void *)h, c, t);
    if (IsWindowVisible(h) && (strstr(t, "Star Citizen") || strstr(c, "CryENGINE") || strstr(c, "StarCitizen"))) game = h;
    return TRUE;
}
static WORD vk_of(const char *k) {
    static const struct { const char *n; WORD vk; } names[] = { {"ESC", VK_ESCAPE}, {"ENTER", VK_RETURN}, {"SPACE", VK_SPACE}, {"TAB", VK_TAB}, {"UP", VK_UP}, {"DOWN", VK_DOWN},
        {"LEFT", VK_LEFT}, {"RIGHT", VK_RIGHT}, {"LSHIFT", VK_LSHIFT}, {"LCTRL", VK_LCONTROL}, {"BACKSPACE", VK_BACK},
        {"LALT", VK_LMENU}, {"RALT", VK_RMENU}, {"RSHIFT", VK_RSHIFT}, {"RCTRL", VK_RCONTROL}, {"CAPS", VK_CAPITAL}, {"DEL", VK_DELETE}, {"HOME", VK_HOME}, {"END", VK_END} };
    for (size_t i = 0; i < sizeof names / sizeof *names; i++) if (!_stricmp(k, names[i].n)) return names[i].vk;
    if ((k[0] == 'F' || k[0] == 'f') && k[1] >= '1' && k[1] <= '9') { int n = atoi(k + 1); if (n >= 1 && n <= 12) return (WORD)(VK_F1 + n - 1); }
    if (strlen(k) == 1) { char ch = k[0]; if (ch >= 'a' && ch <= 'z') ch -= 32; if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) return (WORD)ch; }
    return 0;
}
static void key(WORD vk, int down) {
    INPUT in; memset(&in, 0, sizeof in); in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk; in.ki.wScan = (WORD)MapVirtualKeyA(vk, MAPVK_VK_TO_VSC); in.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    if (vk == VK_UP || vk == VK_DOWN || vk == VK_LEFT || vk == VK_RIGHT) in.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    SendInput(1, &in, sizeof in);
}
int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc > 1 && !strcmp(argv[1], "list")) { EnumWindows(each, 1); return 0; }
    EnumWindows(each, 0);
    printf("game window: %p, foreground now: %p\n", (void *)game, (void *)GetForegroundWindow());
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "focus")) { if (game) { SetForegroundWindow(game); Sleep(400); printf("foreground after focus: %p\n", (void *)GetForegroundWindow()); } continue; }
        char buf[64]; strncpy(buf, argv[i], sizeof buf - 1); buf[sizeof buf - 1] = 0; int hold = 90; char *c = strchr(buf, ':'); if (c) { *c = 0; hold = atoi(c + 1); }
        if (!_strnicmp(buf, "mouse", 5) && c) {                    /* mouse:dx,dy  relative look, sent in small steps */
            int dx = atoi(c + 1), dy = 0; char *cm = strchr(c + 1, ','); if (cm) dy = atoi(cm + 1);
            int steps = (abs(dx) > abs(dy) ? abs(dx) : abs(dy)) / 40 + 1;
            for (int k = 0; k < steps; k++) { INPUT in; memset(&in, 0, sizeof in); in.type = INPUT_MOUSE; in.mi.dx = dx / steps; in.mi.dy = dy / steps; in.mi.dwFlags = MOUSEEVENTF_MOVE; SendInput(1, &in, sizeof in); Sleep(15); }
            printf("mouse moved %d,%d in %d steps\n", dx, dy, steps); continue;
        }
        if (!_strnicmp(buf, "at", 2) && c && strlen(buf) == 2) {                 /* at:x,y  absolute pointer position (Wine screen coordinates) */
            int x = atoi(c + 1), y = 0; char *cm = strchr(c + 1, ','); if (cm) y = atoi(cm + 1);
            int sw = GetSystemMetrics(SM_CXVIRTUALSCREEN), sh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
            INPUT in; memset(&in, 0, sizeof in); in.type = INPUT_MOUSE;
            in.mi.dx = (x * 65535 + sw / 2) / (sw - 1); in.mi.dy = (y * 65535 + sh / 2) / (sh - 1);
            in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
            SendInput(1, &in, sizeof in); Sleep(60);
            POINT p; GetCursorPos(&p); printf("pointer to %d,%d (now %ld,%ld)\n", x, y, p.x, p.y); continue;
        }
        if (!_stricmp(buf, "wheel") && c) {                                      /* wheel:n  n notches, negative scrolls down */
            int n = atoi(c + 1);
            for (int k = 0; k < abs(n); k++) { INPUT in; memset(&in, 0, sizeof in); in.type = INPUT_MOUSE; in.mi.dwFlags = MOUSEEVENTF_WHEEL; in.mi.mouseData = (DWORD)(n > 0 ? 120 : -120); SendInput(1, &in, sizeof in); Sleep(120); }
            printf("wheel %d\n", n); continue;
        }
        if (!_stricmp(buf, "click") || !_stricmp(buf, "rclick") || !_stricmp(buf, "down") || !_stricmp(buf, "up")) {
            const int right = !_stricmp(buf, "rclick");
            INPUT in; memset(&in, 0, sizeof in); in.type = INPUT_MOUSE;
            if (_stricmp(buf, "up")) { in.mi.dwFlags = right ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_LEFTDOWN; SendInput(1, &in, sizeof in); }
            if (!_stricmp(buf, "click") || right) Sleep(c ? hold : 90);
            if (_stricmp(buf, "down")) { in.mi.dwFlags = right ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_LEFTUP; SendInput(1, &in, sizeof in); }
            Sleep(120); printf("%s\n", buf); continue;
        }
        if ((buf[0] == '+' || buf[0] == '-') && vk_of(buf + 1)) {                /* +KEY: key down and stay down, -KEY: key up */
            key(vk_of(buf + 1), buf[0] == '+'); Sleep(60); printf("%s %s\n", buf + 1, buf[0] == '+' ? "down" : "up"); continue;
        }
        if (strchr(buf, '+') && buf[0] != '+') {                                 /* chord: LALT+N */
            WORD vks[4]; int nv = 0, okc = 1; char tmp[64]; strcpy(tmp, buf);
            for (char *t = strtok(tmp, "+"); t && nv < 4; t = strtok(NULL, "+")) { vks[nv] = vk_of(t); if (!vks[nv]) okc = 0; nv++; }
            if (okc && nv > 1) {
                for (int k = 0; k < nv; k++) { key(vks[k], 1); Sleep(60); }
                Sleep(hold);
                for (int k = nv - 1; k >= 0; k--) { key(vks[k], 0); Sleep(40); }
                Sleep(120); printf("pressed %s for %d ms\n", buf, hold); continue;
            }
        }
        WORD vk = vk_of(buf);
        if (!vk) { int ms = atoi(buf); if (ms > 0) { Sleep(ms); continue; } printf("unknown key %s\n", buf); continue; }
        key(vk, 1); Sleep(hold); key(vk, 0); Sleep(120); printf("pressed %s for %d ms\n", buf, hold);
    }
    return 0;
}
