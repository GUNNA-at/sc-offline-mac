/* dismiss.exe: presses OK / Yes / Continue on the message boxes StarCitizen.exe and CrashHandler.exe raise while the
 * game starts, and writes the text of every such box to a log, so that an error message is never lost and never
 * blocks the start. The native replacement for dismiss.ps1: no PowerShell needed in the prefix.
 *
 *   x86_64-w64-mingw32-gcc -O2 -Wall -municode -o dismiss.exe dismiss.c
 *   dismiss.exe [log file]        default log: dismiss.log next to the program
 *
 * It leaves by itself 20 seconds after the last game process has gone. */
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <wchar.h>

#define MAXP 512
static struct { DWORD pid; int kind; } procs[MAXP];     /* kind: 1 game, 2 crash handler, 3 the mod's launcher */
static int nprocs;
static FILE *logf;
static HWND seen[256]; static int nseen;

static void say(const wchar_t *fmt, ...)
{
    SYSTEMTIME t; va_list ap;
    if (!logf) return;
    GetLocalTime(&t);
    fwprintf(logf, L"%02d:%02d:%02d ", t.wHour, t.wMinute, t.wSecond);
    va_start(ap, fmt); vfwprintf(logf, fmt, ap); va_end(ap);
    fputwc(L'\n', logf); fflush(logf);
}

static int contains(const wchar_t *s, const wchar_t *part)
{
    size_t n = wcslen(part);
    for (; *s; s++) if (!_wcsnicmp(s, part, n)) return 1;
    return 0;
}

static int scan_processes(void)            /* returns how many game-related processes are alive */
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W e = { sizeof(e) };
    nprocs = 0;
    if (snap == INVALID_HANDLE_VALUE) return 0;
    if (Process32FirstW(snap, &e)) do {
        int kind = contains(e.szExeFile, L"CrashHandler") ? 2 : contains(e.szExeFile, L"StarCitizen") ? 1
                 : contains(e.szExeFile, L"sc-offline") ? 3 : 0;
        if (kind && nprocs < MAXP) { procs[nprocs].pid = e.th32ProcessID; procs[nprocs].kind = kind; nprocs++; }
    } while (Process32NextW(snap, &e));
    CloseHandle(snap);
    return nprocs;
}

static int kind_of(DWORD pid)
{
    for (int i = 0; i < nprocs; i++) if (procs[i].pid == pid) return procs[i].kind;
    return 0;
}

static void text_of(HWND h, wchar_t *buf, int n)
{
    buf[0] = 0;
    if (!GetWindowTextW(h, buf, n) || !buf[0]) SendMessageTimeoutW(h, WM_GETTEXT, n, (LPARAM)buf, SMTO_ABORTIFHUNG, 300, NULL);
    for (wchar_t *p = buf; *p; p++) if (*p == L'\r' || *p == L'\n' || *p == L'\t') *p = L' ';
}

static wchar_t body[6000];
static BOOL CALLBACK child_text(HWND c, LPARAM unused)
{
    wchar_t t[1024], cls[64]; size_t used = wcslen(body);
    (void)unused;
    text_of(c, t, 1024);
    if (!t[0]) return TRUE;
    GetClassNameW(c, cls, 64);
    if (used + wcslen(t) + wcslen(cls) + 8 < sizeof(body) / sizeof(body[0]))
        swprintf(body + used, sizeof(body) / sizeof(body[0]) - used, L"[%ls] %ls | ", cls, t);
    return TRUE;
}

static BOOL CALLBACK top(HWND h, LPARAM unused)
{
    static const wchar_t *labels[] = { L"OK", L"&OK", L"Yes", L"&Yes", L"Continue", L"&Continue", L"Ignore", L"&Ignore",
                                       L"Close", L"&Close", L"Quit", L"&Quit", L"Exit", L"&Exit", NULL };
    DWORD pid = 0; wchar_t cls[64], title[512]; const wchar_t *clicked = NULL; int kind;
    (void)unused;
    if (!IsWindowVisible(h)) return TRUE;
    GetWindowThreadProcessId(h, &pid);
    kind = kind_of(pid);
    if (kind != 1 && kind != 2) return TRUE;
    GetClassNameW(h, cls, 64); text_of(h, title, 512);
    body[0] = 0; EnumChildWindows(h, child_text, 0);
    if (wcscmp(cls, L"#32770")) {                       /* not a dialog: note it once, press nothing (unless the crash handler) */
        int known = 0;
        for (int i = 0; i < nseen; i++) if (seen[i] == h) known = 1;
        if (!known) { if (nseen < 256) seen[nseen++] = h; say(L"[%ls] window class='%ls' title='%ls' :: %.700ls", kind == 2 ? L"CrashHandler" : L"StarCitizen", cls, title, body); }
        if (kind != 2) return TRUE;
    }
    for (int i = 0; labels[i] && !clicked; i++) {
        HWND b = FindWindowExW(h, NULL, L"Button", labels[i]);
        if (b) { SendMessageTimeoutW(b, BM_CLICK, 0, 0, SMTO_ABORTIFHUNG, 1000, NULL); clicked = labels[i]; }
    }
    if (!clicked) { PostMessageW(h, WM_COMMAND, IDOK, 0); clicked = L"IDOK (WM_COMMAND)"; }
    say(L"[%ls] '%ls' -> pressed %ls :: %.700ls", kind == 2 ? L"CrashHandler" : L"StarCitizen", title, clicked, body);
    Sleep(300);
    return TRUE;
}

int wmain(int argc, wchar_t **argv)
{
    wchar_t path[MAX_PATH]; int idle = 0;
    if (argc > 1) wcsncpy(path, argv[1], MAX_PATH - 1), path[MAX_PATH - 1] = 0;
    else {
        wchar_t *slash;
        GetModuleFileNameW(NULL, path, MAX_PATH);
        slash = wcsrchr(path, L'\\'); if (slash) slash[1] = 0;
        wcsncat(path, L"dismiss.log", MAX_PATH - wcslen(path) - 1);
    }
    logf = _wfopen(path, L"a, ccs=UTF-8");
    say(L"dismiss started");
    for (;;) {
        Sleep(500);
        if (scan_processes() == 0) { if (++idle > 40) { say(L"no game processes for 20 s, leaving"); break; } }
        else idle = 0;
        EnumWindows(top, 0);
    }
    return 0;
}
