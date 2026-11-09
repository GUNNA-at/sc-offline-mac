/* wininfo.exe: print who owns the mouse inside the bottle. Read-only; changes nothing.
 *   wininfo.exe            one report
 *   wininfo.exe watch N    a report every 250 ms for N seconds (only lines that changed)
 * For each top-level window of StarCitizen.exe: handle, thread, class, title, rect, styles, and that thread's
 * GUI state (active / focus / capture). Plus the foreground window, the cursor position, visibility and clip rect. */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static char g_buf[16384];
static size_t g_len;
static void out(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(g_buf + g_len, sizeof g_buf - g_len, fmt, ap);
    va_end(ap);
    if (n > 0 && g_len + (size_t)n < sizeof g_buf) g_len += (size_t)n;
}

static DWORD g_period = 250;
static DWORD g_threads[32];
static int g_nthreads;

static BOOL CALLBACK each(HWND h, LPARAM unused) {
    (void)unused;
    char t[128] = "", c[128] = "", exe[MAX_PATH] = "";
    DWORD pid = 0, tid = GetWindowThreadProcessId(h, &pid);
    GetClassNameA(h, c, sizeof c);
    GetWindowTextA(h, t, sizeof t);
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (p) { DWORD n = sizeof exe; QueryFullProcessImageNameA(p, 0, exe, &n); CloseHandle(p); }
    const char *base = strrchr(exe, '\\'); base = base ? base + 1 : exe;
    if (_stricmp(base, "StarCitizen.exe") != 0) return TRUE;
    if (!IsWindowVisible(h) && !strstr(c, "starcitzenofflinemods") && !strstr(t, "Star Citizen")) return TRUE;
    RECT r = {0}; GetWindowRect(h, &r);
    out("  hwnd %p tid %lu %s class=%s title='%s' rect=%ld,%ld %ldx%ld style=%08lx ex=%08lx\n", (void *)h, tid,
        IsWindowVisible(h) ? "visible" : "hidden ", c, t, r.left, r.top, r.right - r.left, r.bottom - r.top,
        (unsigned long)GetWindowLongPtrA(h, GWL_STYLE), (unsigned long)GetWindowLongPtrA(h, GWL_EXSTYLE));
    int known = 0;
    for (int i = 0; i < g_nthreads; i++) known |= g_threads[i] == tid;
    if (!known && g_nthreads < 32) g_threads[g_nthreads++] = tid;
    return TRUE;
}

static void report(void) {
    g_len = 0; g_nthreads = 0;
    HWND fg = GetForegroundWindow();
    char ft[128] = "", fc[128] = "";
    if (fg) { GetWindowTextA(fg, ft, sizeof ft); GetClassNameA(fg, fc, sizeof fc); }
    out("foreground %p class=%s title='%s'\n", (void *)fg, fc, ft);
    POINT pt = {0}; GetCursorPos(&pt);
    CURSORINFO ci; memset(&ci, 0, sizeof ci); ci.cbSize = sizeof ci; GetCursorInfo(&ci);
    RECT clip = {0}; GetClipCursor(&clip);
    HWND under = WindowFromPoint(pt);
    char uc[128] = ""; if (under) GetClassNameA(under, uc, sizeof uc);
    out("cursor %ld,%ld %s handle %p | clip %ld,%ld %ldx%ld | window under cursor %p class=%s\n", pt.x, pt.y,
        (ci.flags & CURSOR_SHOWING) ? "showing" : "hidden", (void *)ci.hCursor, clip.left, clip.top,
        clip.right - clip.left, clip.bottom - clip.top, (void *)under, uc);
    out("buttons L%d R%d | keys M%d ESC%d | message pos %ld,%ld\n", (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0, (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0,
        (GetAsyncKeyState('M') & 0x8000) != 0, (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0, (long)(short)LOWORD(GetMessagePos()), (long)(short)HIWORD(GetMessagePos()));
    out("virtual screen %d,%d %dx%d\n", GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
        GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN));
    EnumWindows(each, 0);
    for (int i = 0; i < g_nthreads; i++) {
        GUITHREADINFO g; memset(&g, 0, sizeof g); g.cbSize = sizeof g;
        if (!GetGUIThreadInfo(g_threads[i], &g)) { out("  thread %lu: GetGUIThreadInfo failed %lu\n", g_threads[i], GetLastError()); continue; }
        out("  thread %lu: active %p focus %p capture %p flags %08lx\n", g_threads[i], (void *)g.hwndActive, (void *)g.hwndFocus,
            (void *)g.hwndCapture, (unsigned long)g.flags);
    }
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc > 2 && !strcmp(argv[1], "watch")) {
        static char last[16384];
        if (argc > 3) g_period = (DWORD)atoi(argv[3]);
        DWORD end = GetTickCount() + (DWORD)atoi(argv[2]) * 1000, start = GetTickCount();
        while ((int)(end - GetTickCount()) > 0) {
            report();
            if (strcmp(last, g_buf) != 0) { printf("--- t+%.2f s\n%s", (GetTickCount() - start) / 1000.0, g_buf); strcpy(last, g_buf); }
            Sleep(g_period);
        }
        return 0;
    }
    report();
    fputs(g_buf, stdout);
    return 0;
}
