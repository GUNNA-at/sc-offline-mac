/* macfix.dll: fixes for the sc-offline mod (v0.7.7) under CrossOver on macOS.
 *
 * It loads two ways and does the same work either way:
 *   - as a native sc-offline plugin (data/plugins/macfix/, exports sco_plugin_query/load/unload), or
 *   - injected into a game that is already running (inject.exe), so a fix can be tried without a restart.
 *
 * What was wrong (found 2026-11-09):
 *
 *   The mod looks for the game's window one second after it starts and keeps that handle for good. Under CrossOver the
 *   game destroys and recreates its window while it loads, so the handle is dead by the time you play. The mod posts
 *   to it to keep the game's main thread running the mod's code: every 200 ms for its features, and once per menu
 *   frame for the menu. None of that arrives. The mod's code then runs only when the game's window happens to get a
 *   message of its own (the pointer moving over it, a key), which gives:
 *     - a menu that redraws only while the pointer moves over the game outside it, so clicks seem to do nothing and
 *       the menu's own pointer is not drawn where you are;
 *     - hotkeys (F6, F7, F8) that miss key presses;
 *     - a ship terminal Retrieve whose "this pad is yours" step loses its race with the game, so a hail from your
 *       hangar is taken as a landing request and the doors stay shut.
 *
 * What this does (each part has a switch in C:\users\crossover\macfix.ctl, read twice a second; log in macfix.log):
 *
 *   fixgame   Puts the game's real window into the mod's handle when the mod's is dead.
 *   keepalive Posts an empty message to the game's window every N ms (default 50), as the mod's own thread means to.
 *   padfix    Sits behind the mod's hook on the ATC's ChangeTokenState and makes the pad's owner independent of timing:
 *             a pad the ATC gave you keeps you as owner when the lift's ship replaces the stored one, and the mod's
 *             late spawn cancel may not wipe it.
 *   diag      One line per second in macfix.log while the menu is open.
 *   status    A line every N seconds with how often the mod's code runs (0 = off).
 *   Spare switches for the menu, off unless needed: moves (tell the menu window where the pointer is), nudge (extra
 *   messages while the menu is open), wait (hold the game thread up to 1.5 ms for the menu thread), clip (put the
 *   menu's cursor clip back), oscursor (system cursor over the menu instead of ImGui's).
 *
 *   Commands: as a plugin it runs the mod's commands from C:\users\crossover\macfix.cmd (one per line, for example
 *   "creative.god on" or "quantum.travel L2"; "?list" writes every command to the log). Replies go to macfix.log.
 *   Its own, for getting around without the menu: "!pose" (the zone you are in, your position and heading),
 *   "!find <entity name>" (is that game object loaded), "!goto <entity name> [metres above]" (teleport to it; the mod's
 *   own Travel tab does the same for names in locations.txt), "!seats <ship>", "!sit <ship> [seat words]" (the pilot's
 *   seat by default; the mod's crew.sit needs you aboard already), "!power <ship>", "!tp <zone entity> x y z".
 *   For trying settings in a running game: "!cvar <name>" reads a console variable of the game, "!cvar <name> <number>"
 *   sets it, "!exec <line>" runs a line on the game's console (through the console object the mod found; no whitelist).
 *
 * Every address below belongs to one build of the mod's dinput8.dll (0.7.7, PE timestamp 0x6acc0bc4, image size
 * 0xe75000, sha256 d6d43463...). The build and three code sites are checked at start; on any mismatch nothing is touched.
 */
#include <windows.h>
#include <psapi.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "sco_api.h"

/* ---- the sc-offline build this knows ---------------------------------------------------------------------------- */
#define MOD_TIMESTAMP 0x6acc0bc4u
#define MOD_IMAGESIZE 0x00e75000u
#define RVA_CHANGE_TOKEN_ORIG 0x9C36A0 /* services.cpp g_changeTokenStateOrig (the hook calls the game through it) */
#define RVA_PLAYER            0x9C9B38 /* fleet.cpp g_player: your entity id */
#define RVA_GIMGUI            0x61A950 /* ImGui's context pointer */
#define RVA_MENU_WND          0xAD2270 /* menu.cpp g_wnd */
#define RVA_GAME_WND          0xAD2220 /* menu.cpp g_game */
#define RVA_FRAME_STATE       0x9C9DF8 /* menu.cpp g_frameState: 0 idle, 1 wanted, 2 building, 3 built */
#define RVA_FIND_ENTITY       0x00D540 /* spawner.cpp bool FindEntityByNameEx(const char* name, uintptr_t& entity, uint64_t& id) */
#define RVA_TELEPORT_ENTITY   0x00A300 /* teleport.cpp const char* TeleportToEntity(uint64_t entityId, double up): NULL = done */
#define RVA_TELEPORT_ZONE     0x00B0B0 /* teleport.cpp const char* TeleportIntoZone(uintptr_t zone, const double local[3]) */
#define RVA_CONSOLE           0x7D4FC0 /* cvars.cpp g_console: where the game keeps its console object (a pointer to that pointer) */
#define VSLOT_ENTITY_ZONE     0x6E0    /* entity vtable: the zone this entity is (0 = it is not a zone), as travel.cpp uses it */
#define CTX_FRAMECOUNT        0x004    /* ImGuiContext::FrameCount */
#define CTX_MOUSEDRAWCURSOR   0x078    /* ImGuiContext::IO.MouseDrawCursor */
#define CTX_MOUSEPOS          0x100    /* ImGuiContext::IO.MousePos (two floats) */
#define TOKEN_VEHICLE         0x10     /* pad token: the vehicle id it holds */

static const struct { uint32_t rva; uint8_t n; uint8_t bytes[8]; const char *what; } k_sites[] = {
    { 0x2C897, 7, { 0x4C, 0x8B, 0x15, 0x02, 0x6E, 0x99, 0x00 },       "ChangeTokenState hook: mov r10,[g_changeTokenStateOrig]" },
    { 0x5C787, 4, { 0xC6, 0x43, 0x78, 0x01 },                         "menu thread: io.MouseDrawCursor = true" },
    { 0x5C948, 8, { 0xF0, 0x0F, 0xB1, 0x0D, 0xA8, 0xD4, 0x96, 0x00 }, "menu thread: cmpxchg g_frameState" },
    { 0x0D540, 8, { 0x48, 0x89, 0x5C, 0x24, 0x20, 0x57, 0x48, 0x83 }, "FindEntityByNameEx" },
    { 0x0A300, 8, { 0x48, 0x8B, 0xC4, 0x53, 0x56, 0x57, 0x41, 0x56 }, "TeleportToEntity" },
    { 0x0B0B0, 8, { 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x18, 0x48 }, "TeleportIntoZone" },
};

#define DIR "C:\\users\\crossover\\"
/* -DMACFIX_EXT builds macfix-ext.dll: a second copy to inject (inject.exe) next to an already loaded macfix plugin, for
 * trying new "!" commands without restarting the game. It only runs commands, through the loaded plugin's handle
 * (MACFIX_EXT_API / MACFIX_EXT_SELF are the RVAs of g_api and g_self in that loaded macfix.dll; build-ext.sh fills them in). */
#ifdef MACFIX_EXT
#define CMD_FILE DIR "macfix-ext.cmd"
#define LOG_FILE DIR "macfix-ext.log"
#else
#define CMD_FILE DIR "macfix.cmd"
#define LOG_FILE DIR "macfix.log"
#endif

/* ---- log -------------------------------------------------------------------------------------------------------- */
static CRITICAL_SECTION g_loglock;
static void mlog(const char *fmt, ...) {
    char line[900];
    SYSTEMTIME t;
    GetLocalTime(&t);
    int n = snprintf(line, sizeof line, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    const int m = vsnprintf(line + n, sizeof line - (size_t)n - 3, fmt, ap);
    va_end(ap);
    n = m < 0 ? n : (n + m > (int)sizeof line - 3 ? (int)sizeof line - 3 : n + m);
    line[n++] = '\r'; line[n++] = '\n';
    EnterCriticalSection(&g_loglock);
    HANDLE f = CreateFileA(LOG_FILE, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE) { DWORD w; WriteFile(f, line, (DWORD)n, &w, NULL); CloseHandle(f); }
    LeaveCriticalSection(&g_loglock);
}

/* A read that cannot fault. */
static int rd(const void *addr, void *out, size_t n) {
    SIZE_T got = 0;
    return addr && ReadProcessMemory(GetCurrentProcess(), addr, out, n, &got) && got == n;
}

/* ---- switches --------------------------------------------------------------------------------------------------- */
static struct { volatile LONG padfix, fixgame, keepalive, diag, status, moves, nudge, wait, clip, oscursor; } g_cfg = { 1, 1, 50, 1, 30, 0, 0, 0, 0, 0 };

static void read_switches(void) {
    static FILETIME last;
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExA(DIR "macfix.ctl", GetFileExInfoStandard, &a)) return;
    if (!CompareFileTime(&a.ftLastWriteTime, &last)) return;
    last = a.ftLastWriteTime;
    HANDLE f = CreateFileA(DIR "macfix.ctl", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    char buf[512] = "";
    DWORD got = 0;
    ReadFile(f, buf, sizeof buf - 1, &got, NULL);
    CloseHandle(f);
    buf[got] = 0;
    static const struct { const char *key; volatile LONG *v; } keys[] = {
        { "padfix", &g_cfg.padfix }, { "fixgame", &g_cfg.fixgame }, { "keepalive", &g_cfg.keepalive }, { "diag", &g_cfg.diag },
        { "status", &g_cfg.status }, { "moves", &g_cfg.moves }, { "nudge", &g_cfg.nudge }, { "wait", &g_cfg.wait },
        { "clip", &g_cfg.clip }, { "oscursor", &g_cfg.oscursor } };
    for (size_t i = 0; i < sizeof keys / sizeof *keys; i++) {
        const char *p = buf;
        const size_t kl = strlen(keys[i].key);
        while ((p = strstr(p, keys[i].key)) != NULL) {
            if ((p == buf || p[-1] == ' ' || p[-1] == '\n' || p[-1] == '\r' || p[-1] == '\t') && p[kl] == '=') { *keys[i].v = atoi(p + kl + 1); break; }
            p += kl;
        }
    }
    mlog("switches: padfix=%ld fixgame=%ld keepalive=%ld diag=%ld status=%ld moves=%ld nudge=%ld wait=%ld clip=%ld oscursor=%ld", g_cfg.padfix,
         g_cfg.fixgame, g_cfg.keepalive, g_cfg.diag, g_cfg.status, g_cfg.moves, g_cfg.nudge, g_cfg.wait, g_cfg.clip, g_cfg.oscursor);
}

/* ---- the mod's module ------------------------------------------------------------------------------------------- */
static uint8_t *g_mod;                  /* base of sc-offline's dinput8.dll, NULL until verified */
static volatile LONG g_stop;

static uint8_t *find_mod(void) {
    HMODULE mods[512];
    DWORD need = 0;
    if (!EnumProcessModules(GetCurrentProcess(), mods, sizeof mods, &need)) return NULL;
    for (DWORD i = 0; i < need / sizeof(HMODULE) && i < 512; i++) {
        uint8_t *b = (uint8_t *)mods[i];
        IMAGE_DOS_HEADER dos;
        IMAGE_NT_HEADERS64 nt;
        if (!rd(b, &dos, sizeof dos) || dos.e_magic != IMAGE_DOS_SIGNATURE) continue;
        if (!rd(b + dos.e_lfanew, &nt, sizeof nt) || nt.Signature != IMAGE_NT_SIGNATURE) continue;
        if (nt.FileHeader.TimeDateStamp == MOD_TIMESTAMP && nt.OptionalHeader.SizeOfImage == MOD_IMAGESIZE) return b;
    }
    return NULL;
}

static int verify_mod(uint8_t *b) {
    for (size_t i = 0; i < sizeof k_sites / sizeof *k_sites; i++) {
        uint8_t got[8];
        if (!rd(b + k_sites[i].rva, got, k_sites[i].n) || memcmp(got, k_sites[i].bytes, k_sites[i].n)) {
            mlog("[!] sc-offline's code is not what this was made for (%s): nothing is changed", k_sites[i].what);
            return 0;
        }
    }
    return 1;
}

/* ---- padfix: the hangar pad keeps you as its owner -------------------------------------------------------------- */
typedef uintptr_t (*change_token_fn)(uintptr_t atc, const uintptr_t *token, uintptr_t state, uintptr_t owner, uintptr_t vehicle,
                                     uintptr_t a6, uintptr_t a7, uintptr_t a8);
static change_token_fn g_next_change_token;
static SRWLOCK g_padlock = SRWLOCK_INIT;
static struct pad {
    uintptr_t object;       /* the token object (the shared_ptr's pointee) */
    ULONGLONG owned_at;     /* last time the ATC gave this pad to you */
    uintptr_t state3, vehicle3;
    ULONGLONG at3;          /* when the lift's ship took the pad with owner 0 */
    int armed;              /* a spawn cancel may still come and wipe it */
} g_pads[16];

static struct pad *pad_find(uintptr_t object, int make) {
    struct pad *oldest = &g_pads[0];
    for (int i = 0; i < 16; i++) {
        if (g_pads[i].object == object) return &g_pads[i];
        if (g_pads[i].owned_at < oldest->owned_at) oldest = &g_pads[i];
    }
    if (!make) return NULL;
    memset(oldest, 0, sizeof *oldest);
    oldest->object = object;
    return oldest;
}

static uintptr_t fix_change_token_state(uintptr_t atc, const uintptr_t *token, uintptr_t state, uintptr_t owner, uintptr_t vehicle,
                                        uintptr_t a6, uintptr_t a7, uintptr_t a8) {
    uintptr_t object = 0, player = 0;
    int note = 0;
    const uintptr_t was_state = state, was_owner = owner, was_vehicle = vehicle;
    if (g_cfg.padfix && g_mod && rd(token, &object, sizeof object) && object && rd(g_mod + RVA_PLAYER, &player, sizeof player) && player) {
        const ULONGLONG now = GetTickCount64();
        AcquireSRWLockExclusive(&g_padlock);
        if (owner == player && vehicle) {
            /* The ATC gives this pad to you (a Retrieve, or a landing). */
            struct pad *p = pad_find(object, 1);
            p->owned_at = now;
            p->armed = 0;
            note = 3;
        } else if (!owner && vehicle) {
            /* A ship takes the pad with no owner: on your pad that is the lift's spawn replacing the stored ship. */
            struct pad *p = pad_find(object, 0);
            if (p && now - p->owned_at < 300000) {
                owner = player;
                p->state3 = state; p->vehicle3 = vehicle; p->at3 = now; p->owned_at = now; p->armed = 1;
                note = 1;
            }
        } else if (!owner && !vehicle) {
            /* The pad is cleared. Right after the case above this is the mod's late spawn cancel (rc16): keep the pad. */
            struct pad *p = pad_find(object, 0);
            if (p && p->armed) {
                uintptr_t holds = 0;
                if (now - p->at3 < 30000 && rd((const void *)(object + TOKEN_VEHICLE), &holds, sizeof holds) && holds == p->vehicle3) {
                    state = p->state3; owner = player; vehicle = p->vehicle3;
                    note = 2;
                }
                p->armed = 0;
            } else if (p) note = 4;
        }
        ReleaseSRWLockExclusive(&g_padlock);
    }
    if (note == 1) mlog("[padfix] pad token 0x%llX: ship 0x%llX took the pad with owner 0 -> owner is you (0x%llX), state %llu",
                        (unsigned long long)object, (unsigned long long)vehicle, (unsigned long long)player, (unsigned long long)state);
    if (note == 2) mlog("[padfix] pad token 0x%llX: clear (state %llu) after the lift's spawn kept as state %llu, ship 0x%llX, owner you",
                        (unsigned long long)object, (unsigned long long)was_state, (unsigned long long)state, (unsigned long long)vehicle);
    if (note == 3 || note == 4) mlog("[padfix] pad token 0x%llX: state %llu owner 0x%llX ship 0x%llX (%s)", (unsigned long long)object,
                        (unsigned long long)was_state, (unsigned long long)was_owner, (unsigned long long)was_vehicle,
                        note == 3 ? "the ATC gives it to you" : "your pad is cleared");
    return g_next_change_token(atc, token, state, owner, vehicle, a6, a7, a8);
}

static int g_padfix_in;
static void padfix_install(void) {
    if (g_padfix_in || !g_mod) return;
    change_token_fn *slot = (change_token_fn *)(g_mod + RVA_CHANGE_TOKEN_ORIG);
    change_token_fn cur = *slot;
    if (!cur) return;                                  /* the mod has not hooked the ATC (yet) */
    if (cur == fix_change_token_state) { g_padfix_in = 1; return; }
    g_next_change_token = cur;
    InterlockedExchangePointer((PVOID volatile *)slot, (PVOID)fix_change_token_state);
    g_padfix_in = 1;
    mlog("[padfix] installed behind the mod's ChangeTokenState hook (game function via %p)", (void *)cur);
}
static void padfix_remove(void) {
    if (!g_padfix_in || !g_mod) return;
    change_token_fn *slot = (change_token_fn *)(g_mod + RVA_CHANGE_TOKEN_ORIG);
    InterlockedCompareExchangePointer((PVOID volatile *)slot, (PVOID)g_next_change_token, (PVOID)fix_change_token_state);
    g_padfix_in = 0;
}

/* ---- the game's window and main thread -------------------------------------------------------------------------- */
#define SYNTH_MARK 0x8000 /* in wParam of a WM_MOUSEMOVE this posted */
static HWND g_menu, g_game;
static DWORD g_game_tid;
static WNDPROC g_menu_proc_orig;
static HHOOK g_game_hook;
static volatile LONG g_menu_active;                    /* visible and Wine's foreground window */
static volatile LONG c_ticks_total, c_keepalive_total;
static volatile LONG c_move_real, c_move_synth, c_lbd, c_lbu, c_leave, c_setcursor, c_waits, c_wait_hits, c_nudges, c_reclips;
static volatile LONG g_seen_x = -100000, g_seen_y = -100000;
static volatile LONG g_wait_max_us;

static LONGLONG qpc_us(void) {
    static LARGE_INTEGER f;
    LARGE_INTEGER t;
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t);
    return t.QuadPart * 1000000 / f.QuadPart;
}

/* On the game's main thread, before the mod's own message hook. */
static LRESULT CALLBACK game_getmsg(int code, WPARAM w, LPARAM l) {
    if (code >= 0) {
        InterlockedIncrement(&c_ticks_total);
        if (g_cfg.wait && g_menu_active && g_mod) {
            static LONGLONG last_wait;
            volatile LONG *state = (volatile LONG *)(g_mod + RVA_FRAME_STATE);
            const LONGLONG t0 = qpc_us();
            if (*state == 0 && t0 - last_wait > 8000) {
                LONGLONG t = t0;
                while (*state == 0 && t - t0 < 1500) { SwitchToThread(); t = qpc_us(); }
                last_wait = t;
                InterlockedIncrement(&c_waits);
                if (*state == 1) InterlockedIncrement(&c_wait_hits);
                if (t - t0 > g_wait_max_us) g_wait_max_us = (LONG)(t - t0);
            }
        }
    }
    return CallNextHookEx(g_game_hook, code, w, l);
}

static BOOL CALLBACK find_game_cb(HWND h, LPARAM out) {
    DWORD pid = 0;
    wchar_t cls[64] = L"";
    GetWindowThreadProcessId(h, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(h) || GetWindow(h, GW_OWNER)) return TRUE;
    if (!GetClassNameW(h, cls, 64) || wcscmp(cls, L"CryENGINE")) return TRUE;
    *(HWND *)out = h;
    return FALSE;
}

static void game_attach(void) {
    if (!g_game || !IsWindow(g_game)) {
        HWND real = NULL;
        EnumWindows(find_game_cb, (LPARAM)&real);
        if (!real) return;
        g_game = real;
        const DWORD tid = GetWindowThreadProcessId(real, NULL);
        mlog("[game] the game's window is %p (thread %lu)", (void *)real, tid);
        if (tid != g_game_tid) {
            if (g_game_hook) UnhookWindowsHookEx(g_game_hook);
            g_game_hook = SetWindowsHookExW(WH_GETMESSAGE, game_getmsg, NULL, tid);
            g_game_tid = tid;
            if (!g_game_hook) mlog("[!] [game] no message hook on the game thread %lu (%lu)", tid, GetLastError());
        }
    }
    if (g_cfg.fixgame && g_mod) {
        HWND *slot = (HWND *)(g_mod + RVA_GAME_WND);
        HWND have = *slot;
        /* Only once the mod has stored one (its menu exists): 0 means it has not started its menu. */
        if (have && have != g_game && !IsWindow(have)) {
            InterlockedExchangePointer((PVOID volatile *)slot, (PVOID)g_game);
            mlog("[game] the mod's handle for the game's window (%p) is dead: set to %p", (void *)have, (void *)g_game);
        }
    }
}

/* ---- menu ------------------------------------------------------------------------------------------------------- */
static LRESULT CALLBACK menu_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_MOUSEMOVE:
        if (w & SYNTH_MARK) { InterlockedIncrement(&c_move_synth); w &= ~(WPARAM)SYNTH_MARK; }
        else InterlockedIncrement(&c_move_real);
        g_seen_x = (short)LOWORD(l); g_seen_y = (short)HIWORD(l);
        break;
    case WM_LBUTTONDOWN: InterlockedIncrement(&c_lbd); break;
    case WM_LBUTTONUP:   InterlockedIncrement(&c_lbu); break;
    case WM_MOUSELEAVE:  InterlockedIncrement(&c_leave); g_seen_x = g_seen_y = -100000; break;
    case WM_SETCURSOR:   InterlockedIncrement(&c_setcursor); break;
    }
    return CallWindowProcW(g_menu_proc_orig, h, m, w, l);
}

static void menu_attach(void) {
    static int failed;
    HWND menu = NULL;
    if (!g_mod || failed) return;
    rd(g_mod + RVA_MENU_WND, &menu, sizeof menu);
    if (!menu || menu == g_menu || !IsWindow(menu)) return;
    /* The original procedure is in place before ours can be called (the menu thread may dispatch at any moment). */
    WNDPROC cur = (WNDPROC)GetWindowLongPtrW(menu, GWLP_WNDPROC);
    if (!cur || cur == menu_proc) return;
    g_menu_proc_orig = cur;
    if (SetWindowLongPtrW(menu, GWLP_WNDPROC, (LONG_PTR)menu_proc)) {
        g_menu = menu;
        mlog("[menu] watching the menu window %p (thread %lu)", (void *)menu, GetWindowThreadProcessId(menu, NULL));
    } else { failed = 1; mlog("[!] [menu] could not attach to the menu window %p (%lu)", (void *)menu, GetLastError()); }
}

static void detach_all(void) {
    if (g_menu && g_menu_proc_orig && IsWindow(g_menu) && (WNDPROC)GetWindowLongPtrW(g_menu, GWLP_WNDPROC) == menu_proc)
        SetWindowLongPtrW(g_menu, GWLP_WNDPROC, (LONG_PTR)g_menu_proc_orig);
    if (g_game_hook) UnhookWindowsHookEx(g_game_hook);
    g_game_hook = NULL;
}

static void menu_step(ULONGLONG now) {
    static ULONGLONG last_nudge, last_clip, last_diag, mismatch_since;
    static LONG posted_x = -100000, posted_y = -100000, last_frames, last_ticks, state_hist[4];
    static int was_os_cursor = 0;
    if (!g_menu || !IsWindow(g_menu)) { g_menu_active = 0; return; }
    const int visible = IsWindowVisible(g_menu), fg = GetForegroundWindow() == g_menu;
    g_menu_active = visible && fg;

    uint8_t *ctx = NULL;
    rd(g_mod + RVA_GIMGUI, &ctx, sizeof ctx);
    LONG st = 0;
    if (rd(g_mod + RVA_FRAME_STATE, &st, sizeof st) && st >= 0 && st < 4) state_hist[st]++;

    /* oscursor: ImGui's backend follows this flag at its next frame. */
    if (ctx && was_os_cursor != (int)(g_cfg.oscursor != 0)) {
        const uint8_t want = g_cfg.oscursor ? 0 : 1;
        uint8_t have = 0;
        if (rd(ctx + CTX_MOUSEDRAWCURSOR, &have, 1) && have != want) ctx[CTX_MOUSEDRAWCURSOR] = want;
        was_os_cursor = g_cfg.oscursor != 0;
        mlog("[menu] the pointer over the menu is now %s", want ? "ImGui's drawn cursor" : "the system cursor");
    }

    POINT sp = { 0, 0 }, cp;
    RECT cr = { 0, 0, 0, 0 };
    GetCursorPos(&sp);
    cp = sp;
    ScreenToClient(g_menu, &cp);
    GetClientRect(g_menu, &cr);
    const int inside = PtInRect(&cr, cp);

    if (g_menu_active) {
        if (g_cfg.moves && inside) {
            if (cp.x == g_seen_x && cp.y == g_seen_y) mismatch_since = 0;
            else if (!mismatch_since) mismatch_since = now;
            else if (now - mismatch_since >= 12 && (cp.x != posted_x || cp.y != posted_y)) {
                WPARAM w = SYNTH_MARK;
                if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) w |= MK_LBUTTON;
                if (GetAsyncKeyState(VK_RBUTTON) & 0x8000) w |= MK_RBUTTON;
                PostMessageW(g_menu, WM_MOUSEMOVE, w, MAKELPARAM(cp.x, cp.y));
                posted_x = cp.x; posted_y = cp.y;
            }
        } else mismatch_since = 0;

        if (g_cfg.nudge && g_game && now - last_nudge >= 8) {
            PostMessageW(g_game, WM_NULL, 0, 0);
            InterlockedIncrement(&c_nudges);
            last_nudge = now;
        }

        if (g_cfg.clip && now - last_clip >= 250) {
            RECT have, want;
            last_clip = now;
            if (GetClipCursor(&have) && GetWindowRect(g_menu, &want) && !EqualRect(&have, &want)) {
                ClipCursor(&want);
                InterlockedIncrement(&c_reclips);
            }
        }
    }

    if (g_cfg.diag && visible && now - last_diag >= 1000) {
        LONG frames = 0;
        float mp[2] = { 0, 0 };
        RECT clip = { 0, 0, 0, 0 };
        const LONG ticks = c_ticks_total;
        if (ctx) { rd(ctx + CTX_FRAMECOUNT, &frames, sizeof frames); rd(ctx + CTX_MOUSEPOS, mp, sizeof mp); }
        GetClipCursor(&clip);
        const double secs = last_diag ? (double)(now - last_diag) / 1000.0 : 1.0;
        mlog("[menu] fg %d | frames %.0f/s, mod code runs %.0f/s | moves real %ld synth %ld, down %ld up %ld leave %ld setcursor %ld | "
             "imgui %.0f,%.0f pointer %ld,%ld%s | state idle %ld wanted %ld building %ld built %ld | waits %ld hit %ld max %ld us, "
             "nudges %ld, reclips %ld, clip %ld,%ld %ldx%ld",
             fg, last_diag ? (frames - last_frames) / secs : 0.0, last_diag ? (ticks - last_ticks) / secs : 0.0,
             InterlockedExchange(&c_move_real, 0), InterlockedExchange(&c_move_synth, 0), InterlockedExchange(&c_lbd, 0),
             InterlockedExchange(&c_lbu, 0), InterlockedExchange(&c_leave, 0), InterlockedExchange(&c_setcursor, 0),
             mp[0] < -1e30f ? -1.0 : (double)mp[0], mp[1] < -1e30f ? -1.0 : (double)mp[1], cp.x, cp.y, inside ? "" : " (outside)",
             state_hist[0], state_hist[1], state_hist[2], state_hist[3], InterlockedExchange(&c_waits, 0),
             InterlockedExchange(&c_wait_hits, 0), InterlockedExchange(&g_wait_max_us, 0), InterlockedExchange(&c_nudges, 0),
             InterlockedExchange(&c_reclips, 0), clip.left, clip.top, clip.right - clip.left, clip.bottom - clip.top);
        last_frames = frames;
        last_ticks = ticks;
        last_diag = now;
        memset(state_hist, 0, sizeof state_hist);
    } else if (!visible) {
        last_diag = 0;
        memset(state_hist, 0, sizeof state_hist);
    }
}

/* ---- commands (plugin only) ------------------------------------------------------------------------------------- */
static const sco_api *g_api;
static sco_plugin *g_self;

static const char *result_name(sco_result r) {
    static const char *const names[] = { "OK", "UNAVAILABLE", "NOT_FOUND", "BAD_ARG", "CRASHED", "WRONG_THREAD", "TOO_MANY", "FAILED" };
    return (unsigned)r < 8 ? names[r] : "?";
}

struct cmdline { int done; char text[500]; char shown[500]; };

static void on_cmd_done(sco_result r, const char *reply, void *ctx) {
    struct cmdline *c = ctx;
    c->done = 1;
    mlog("[cmd] %s -> %s%s%s", c->shown, result_name(r), reply && reply[0] ? ": " : "", reply && reply[0] ? reply : "");
}

static const sco_command *find_command(const char *name) {
    static const sco_command *cmds[1024];
    const uint32_t n = g_api->list_commands(cmds, 1024);
    for (uint32_t i = 0; i < n && i < 1024; i++)
        if (cmds[i] && cmds[i]->name && !strcmp(cmds[i]->name, name)) return cmds[i];
    return NULL;
}

static void list_commands_to_log(void) {
    static const sco_command *cmds[1024];
    static const char *const types[] = { "int", "float", "string", "bool" };
    const uint32_t n = g_api->list_commands(cmds, 1024);
    mlog("[cmd] %u commands:", n);
    for (uint32_t i = 0; i < n && i < 1024; i++) {
        char args[300] = "";
        size_t o = 0;
        const sco_command *c = cmds[i];
        if (!c || !c->name) continue;
        for (uint32_t a = 0; a < c->nargs && c->args && o < sizeof args - 40; a++) {
            const sco_arg_def *d = (const sco_arg_def *)((const char *)c->args + (size_t)a * (c->arg_def_size ? c->arg_def_size : sizeof(sco_arg_def)));
            o += (size_t)snprintf(args + o, sizeof args - o, " <%s:%s>", d->name ? d->name : "?", d->type < 4 ? types[d->type] : "?");
        }
        mlog("[cmd]   %s%s | %s | %s%s", c->name, args, c->title ? c->title : "", c->capability ? c->capability : "always",
             c->capability && !g_api->has(c->capability) ? " (NOT AVAILABLE)" : "");
    }
}

/* teleport.spatial 1.0 (sc_spatial.h in the sco SDK): where you are. */
typedef struct sc_spatial_v1 {
    uint32_t size;
    int (*player_pose)(double pos[3], double rot_xyzw[4], uint64_t *zone_id);
    int (*zone_of_entity)(uint64_t entity_id, uint64_t *zone_id);
    int (*local_to_world)(uint64_t zone_id, const double local[3], double world[3]);
    int (*world_to_local)(uint64_t zone_id, const double world[3], double local[3]);
    int (*zone_to_zone)(uint64_t from, uint64_t to, const double in[3], double out[3]);
    int (*zone_name)(uint64_t zone_id, char *out, uint32_t cap);
} sc_spatial_v1;

/* game.vehicles 1.0 (sc_vehicles.h in the sco SDK): seats of a ship, seating your player, Flight Ready. */
typedef struct sc_vehicle_seat { uint32_t index, flags; uint64_t seat_id, occupant_id; uint32_t priority, _pad; char name[64]; } sc_vehicle_seat;
typedef struct sc_vehicles_v1 {
    uint32_t size, _pad;
    sco_result (*player_ship)(uint64_t *out_ship);
    sco_result (*seats)(uint64_t ship, sc_vehicle_seat *out, uint32_t max, uint32_t *out_count, uint32_t *out_more);
    sco_result (*seat_occupant)(uint64_t ship, uint32_t seat_index, uint64_t *out_actor);
    sco_result (*seat)(sco_plugin *self, uint64_t actor, uint64_t ship, uint32_t seat_index);
    sco_result (*eject)(sco_plugin *self, uint64_t actor);
    sco_result (*power_on)(sco_plugin *self, uint64_t ship);
    sco_result (*last_error)(sco_plugin *self, char *out, uint32_t *inout_size);
} sc_vehicles_v1;

typedef unsigned char (*find_entity_fn)(const char *name, uintptr_t *entity, uint64_t *id);
typedef const char *(*teleport_entity_fn)(uint64_t id, double up);

/* "!pose", "!find <name>", "!goto <name> [up]". On the game thread. */
static void run_own_command(struct cmdline *c) {
    char *arg = strchr(c->text, ' ');
    if (arg) { *arg++ = 0; while (*arg == ' ') arg++; }
    if (!strcmp(c->text, "!pose")) {
        const sc_spatial_v1 *sp = NULL;
        double pos[3] = { 0, 0, 0 }, rot[4] = { 0, 0, 0, 1 };
        uint64_t zone = 0;
        char name[96] = "?";
        if (g_api->query_service("teleport.spatial", 0x00010000u, (const void **)&sp) != SCO_OK || !sp || !sp->player_pose(pos, rot, &zone)) {
            mlog("[cmd] !pose -> not available (not spawned yet?)");
            return;
        }
        sp->zone_name(zone, name, sizeof name);
        const double yaw = atan2(2.0 * (rot[3] * rot[2] + rot[0] * rot[1]), 1.0 - 2.0 * (rot[1] * rot[1] + rot[2] * rot[2])) * 57.29577951308232;
        mlog("[cmd] !pose -> zone %s (0x%llX) pos %.2f %.2f %.2f yaw %.0f", name, (unsigned long long)zone, pos[0], pos[1], pos[2], yaw);
        return;
    }
    if (!strcmp(c->text, "!cvar") || !strcmp(c->text, "!exec")) {
        /* "!cvar <name>" reads one of the game's console variables, "!cvar <name> <number>" sets it, "!exec <line>" runs a
           line on the game's console. They go through the console object the mod found for its own use (cvars.cpp), so the
           user.cfg whitelist is not in the way and a setting can be tried in a running game. The slots in the console's and
           the variable's tables are the mod's: find a variable 0xC0, run a line 0x130; read as float 0x20, set a float 0x38. */
        uintptr_t slot = 0, con = 0, vt = 0, fn = 0;
        if (!g_mod || !arg || !*arg) { mlog("[cmd] %s -> needs a variable name or a console line", c->shown); return; }
        if (!rd(g_mod + RVA_CONSOLE, &slot, sizeof slot) || !slot || !rd((const void *)slot, &con, sizeof con) || !con ||
            !rd((const void *)con, &vt, sizeof vt) || !vt) { mlog("[cmd] %s -> the game's console is not there yet", c->shown); return; }
        if (!strcmp(c->text, "!exec")) {
            if (!rd((const void *)(vt + 0x130), &fn, sizeof fn) || !fn) { mlog("[cmd] %s -> could not read the console", c->shown); return; }
            ((void (*)(uintptr_t, const char *, int, int, void *))fn)(con, arg, 0, 0, NULL);
            mlog("[cmd] !exec %s -> sent to the game's console", arg);
            return;
        }
        char *val = strchr(arg, ' ');
        if (val) { *val++ = 0; while (*val == ' ') val++; }
        uintptr_t cvar = 0, cvt = 0, getf = 0, setf = 0;
        if (!rd((const void *)(vt + 0xC0), &fn, sizeof fn) || !fn) { mlog("[cmd] %s -> could not read the console", c->shown); return; }
        cvar = ((uintptr_t (*)(uintptr_t, const char *))fn)(con, arg);
        if (!cvar || !rd((const void *)cvar, &cvt, sizeof cvt) || !rd((const void *)(cvt + 0x20), &getf, sizeof getf) ||
            !rd((const void *)(cvt + 0x38), &setf, sizeof setf) || !getf || !setf) { mlog("[cmd] !cvar %s -> no such variable", arg); return; }
        const float before = ((float (*)(uintptr_t))getf)(cvar);
        if (val && *val) {
            ((void (*)(uintptr_t, float))setf)(cvar, (float)strtod(val, NULL));
            mlog("[cmd] !cvar %s %s -> was %g, now %g", arg, val, before, ((float (*)(uintptr_t))getf)(cvar));
        } else mlog("[cmd] !cvar %s -> %g", arg, before);
        return;
    }
    if (!g_mod || !arg || !*arg) { mlog("[cmd] %s -> needs an entity name", c->shown); return; }
    if (!strcmp(c->text, "!tp")) {
        /* "!tp <zone entity name> x y z": put you at that position of that zone (Spaceport_interior -3.44 55.61 0.05). */
        char *rest = strchr(arg, ' ');
        double local[3] = { 0, 0, 0 };
        uintptr_t ent = 0, zone = 0;
        uint64_t zid = 0;
        if (!rest || sscanf(rest, "%lf %lf %lf", &local[0], &local[1], &local[2]) != 3) { mlog("[cmd] %s -> needs: <zone entity name> x y z", c->shown); return; }
        *rest = 0;
        if (!((find_entity_fn)(g_mod + RVA_FIND_ENTITY))(arg, &ent, &zid) || !ent) { mlog("[cmd] %s %s -> zone not loaded", c->shown, rest + 1); return; }
        uintptr_t vtable = 0, fn = 0;
        if (!rd((const void *)ent, &vtable, sizeof vtable) || !rd((const void *)(vtable + VSLOT_ENTITY_ZONE), &fn, sizeof fn) || !fn) { mlog("[cmd] %s -> could not read the entity", c->shown); return; }
        zone = ((uintptr_t (*)(uintptr_t))fn)(ent);
        if (!zone) { mlog("[cmd] %s %s -> that entity is not a zone", c->shown, rest + 1); return; }
        const char *err = ((const char *(*)(uintptr_t, const double *))(g_mod + RVA_TELEPORT_ZONE))(zone, local);
        mlog("[cmd] %s %.2f %.2f %.2f -> %s", c->shown, local[0], local[1], local[2], err ? err : "teleported");
        return;
    }
    if (!strcmp(c->text, "!seats") || !strcmp(c->text, "!sit") || !strcmp(c->text, "!power")) {
        /* "!seats <ship>", "!sit <ship> [words of the seat's name]" (no words: the pilot's seat), "!power <ship>".
           <ship> is an entity name (AEGS_Avenger_Stalker_200004148657) or its number. */
        const sc_vehicles_v1 *veh = NULL;
        char *words = strchr(arg, ' ');
        if (words) { *words++ = 0; while (*words == ' ') words++; }
        uint64_t ship = 0, player = 0;
        uintptr_t ent = 0;
        if (arg[0] >= '0' && arg[0] <= '9') ship = strtoull(arg, NULL, 10);
        else ((find_entity_fn)(g_mod + RVA_FIND_ENTITY))(arg, &ent, &ship);
        if (g_api->query_service("game.vehicles", 0x00010000u, (const void **)&veh) != SCO_OK || !veh) { mlog("[cmd] %s -> game.vehicles is not available", c->shown); return; }
        if (!ship) { mlog("[cmd] %s -> no such ship loaded", c->shown); return; }
        char why[200] = "";
        uint32_t why_n = sizeof why;
        if (!strcmp(c->text, "!power")) {
            const sco_result r = veh->power_on(g_self, ship);
            if (r != SCO_OK) veh->last_error(g_self, why, &why_n);
            mlog("[cmd] %s -> %s%s%s (ship %llu)", c->shown, result_name(r), why[0] ? ": " : "", why, (unsigned long long)ship);
            return;
        }
        static sc_vehicle_seat seats[32];
        uint32_t n = 0, more = 0;
        sco_result r = veh->seats(ship, seats, 32, &n, &more);
        if (r != SCO_OK) { veh->last_error(g_self, why, &why_n); mlog("[cmd] %s -> seats: %s%s%s", c->shown, result_name(r), why[0] ? ": " : "", why); return; }
        if (!strcmp(c->text, "!seats")) {
            mlog("[cmd] %s -> %u seats (ship %llu)", c->shown, n, (unsigned long long)ship);
            for (uint32_t i = 0; i < n; i++)
                mlog("[cmd]   seat %u %s%s%s%s priority %u occupant %llu", seats[i].index, seats[i].name, seats[i].flags & 8 ? " [pilot]" : "",
                     seats[i].flags & 1 ? " [usable]" : "", seats[i].flags & 4 ? " [occupied]" : "", seats[i].priority, (unsigned long long)seats[i].occupant_id);
            return;
        }
        int pick = -1;
        for (uint32_t i = 0; i < n && pick < 0; i++) {
            if (words && *words) {
                char name[64], want[64];
                int ok = 1;
                strncpy(name, seats[i].name, 63); name[63] = 0; _strlwr(name);
                strncpy(want, words, 63); want[63] = 0; _strlwr(want);
                for (char *t = strtok(want, " "); t && ok; t = strtok(NULL, " ")) ok = strstr(name, t) != NULL;
                if (ok && (seats[i].flags & 1)) pick = (int)i;
            } else if (seats[i].flags & 8) pick = (int)i;
        }
        if (pick < 0) { mlog("[cmd] %s -> no such seat among %u (try !seats)", c->shown, n); return; }
        rd(g_mod + RVA_PLAYER, &player, sizeof player);
        r = veh->seat(g_self, player, ship, seats[pick].index);
        if (r != SCO_OK) veh->last_error(g_self, why, &why_n);
        mlog("[cmd] %s -> %s%s%s (seat %u %s of ship %llu, you are %llu)", c->shown, result_name(r), why[0] ? ": " : "", why, seats[pick].index,
             seats[pick].name, (unsigned long long)ship, (unsigned long long)player);
        return;
    }
    char *up_txt = NULL;
    if (*arg == '"') { arg++; char *q = strchr(arg, '"'); if (q) { *q = 0; up_txt = q + 1; } }
    else { up_txt = strchr(arg, ' '); if (up_txt) *up_txt++ = 0; }
    uintptr_t entity = 0;
    uint64_t id = 0;
    const int found = ((find_entity_fn)(g_mod + RVA_FIND_ENTITY))(arg, &entity, &id) != 0;
    if (!strcmp(c->text, "!find")) {
        mlog("[cmd] %s -> %s (entity %p, id %llu)", c->shown, found ? "loaded" : "not loaded", (void *)entity, (unsigned long long)id);
        return;
    }
    if (!strcmp(c->text, "!goto")) {
        if (!found || !id) { mlog("[cmd] %s -> %s", c->shown, found ? "found, but it has no id" : "not loaded"); return; }
        const double up = up_txt && *up_txt ? strtod(up_txt, NULL) : 2.0;
        const char *err = ((teleport_entity_fn)(g_mod + RVA_TELEPORT_ENTITY))(id, up);
        mlog("[cmd] %s -> %s (id %llu, %.1f m above)", c->shown, err ? err : "teleported", (unsigned long long)id, up);
        return;
    }
    mlog("[cmd] %s -> unknown (own commands: !pose, !find <name>, !goto <name> [up], !seats <ship>, !sit <ship> [seat], !power <ship>, !tp <zone> x y z, !cvar <name> [value], !exec <console line>)", c->shown);
}

/* On the game thread. */
static void run_cmd_task(void *ctx) {
    struct cmdline *c = ctx;
    char *tok[10];
    int ntok = 0;
    strcpy(c->shown, c->text);
    if (!strcmp(c->text, "?list")) { list_commands_to_log(); free(c); return; }
    if (c->text[0] == '!') { run_own_command(c); free(c); return; }
    for (char *p = c->text; *p && ntok < 10;) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        if (*p == '"') { tok[ntok++] = ++p; while (*p && *p != '"') p++; }
        else { tok[ntok++] = p; while (*p && *p != ' ' && *p != '\t') p++; }
        if (*p) *p++ = 0;
    }
    if (!ntok) { free(c); return; }
    const sco_command *def = find_command(tok[0]);
    sco_arg args[9];
    memset(args, 0, sizeof args);
    for (int i = 1; i < ntok; i++) {
        uint32_t type = SCO_ARG_STRING;
        if (def && (uint32_t)(i - 1) < def->nargs && def->args) {
            const sco_arg_def *d = (const sco_arg_def *)((const char *)def->args + (size_t)(i - 1) * (def->arg_def_size ? def->arg_def_size : sizeof(sco_arg_def)));
            type = d->type;
        } else {
            char *end = NULL;
            strtod(tok[i], &end);
            if (end && !*end && end != tok[i]) type = strchr(tok[i], '.') ? SCO_ARG_FLOAT : SCO_ARG_INT;
        }
        args[i - 1].type = type;
        if (type == SCO_ARG_INT) args[i - 1].v.i = strtoll(tok[i], NULL, 0);
        else if (type == SCO_ARG_FLOAT) args[i - 1].v.f = strtod(tok[i], NULL);
        else if (type == SCO_ARG_BOOL) args[i - 1].v.i = !_stricmp(tok[i], "on") || !_stricmp(tok[i], "true") || !_stricmp(tok[i], "yes") || !strcmp(tok[i], "1");
        else args[i - 1].v.s = tok[i];
    }
    const sco_result r = g_api->invoke(g_self, tok[0], args, (uint32_t)(ntok - 1), on_cmd_done, c);
    if (!c->done) mlog("[cmd] %s -> %s%s", c->shown, result_name(r), def ? "" : " (no such command)");
    free(c);
}

static void read_commands(void) {
    static int told;
    if (GetFileAttributesA(CMD_FILE) == INVALID_FILE_ATTRIBUTES) return;
    HANDLE f = CreateFileA(CMD_FILE, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    static char buf[8192];
    DWORD got = 0;
    ReadFile(f, buf, sizeof buf - 1, &got, NULL);
    CloseHandle(f);
    DeleteFileA(CMD_FILE);
    buf[got] = 0;
    if (!g_api || !g_self) {
        if (!told) { told = 1; mlog("[cmd] commands need macfix loaded as a plugin (it was injected): ignored"); }
        return;
    }
    for (char *line = buf; line && *line;) {
        char *next = strpbrk(line, "\r\n");
        if (next) { *next++ = 0; while (*next == '\r' || *next == '\n') next++; }
        while (*line == ' ' || *line == '\t') line++;
        if (*line && *line != '#') {
            struct cmdline *c = calloc(1, sizeof *c);
            if (c) {
                strncpy(c->text, line, sizeof c->text - 1);
                if (g_api->run_on_game_thread(g_self, run_cmd_task, c) != SCO_OK) { mlog("[cmd] %s -> could not be queued", line); free(c); }
            }
        }
        line = next;
    }
}

/* ---- worker ----------------------------------------------------------------------------------------------------- */
static HANDLE g_worker;

#ifdef MACFIX_EXT
static DWORD WINAPI worker(LPVOID unused) {
    (void)unused;
    HMODULE host = NULL;
    mlog("macfix-ext started in process %lu", GetCurrentProcessId());
    for (int i = 0; i < 20 && !host; i++) { host = GetModuleHandleA("macfix.dll"); if (!host) Sleep(250); }
    if (!host) { mlog("[!] macfix.dll (the plugin) is not loaded: nothing to attach to"); return 0; }
    rd((uint8_t *)host + MACFIX_EXT_API, &g_api, sizeof g_api);
    rd((uint8_t *)host + MACFIX_EXT_SELF, &g_self, sizeof g_self);
    uint8_t *b = find_mod();
    if (b && verify_mod(b)) g_mod = b;
    mlog("attached to macfix.dll at %p: api %p (%s), handle %p, sc-offline %s", (void *)host, (void *)g_api,
         g_api && g_api->size >= sizeof(sco_api) ? "looks right" : "SUSPECT", (void *)g_self, g_mod ? "found" : "NOT found");
    if (!g_api || !g_self || g_api->size < sizeof(sco_api) || !g_mod) { g_api = NULL; g_self = NULL; return 0; }
    while (!g_stop) { read_commands(); Sleep(200); }
    return 0;
}
#else
static DWORD WINAPI worker(LPVOID unused) {
    (void)unused;
    ULONGLONG last_slow = 0, last_fast = 0, last_keepalive = 0, last_status = 0;
    LONG status_ticks = 0, status_posts = 0;
    int reported = 0;
    mlog("macfix started in process %lu", GetCurrentProcessId());
    while (!g_stop) {
        const ULONGLONG now = GetTickCount64();
        if (now - last_slow >= 500) {
            last_slow = now;
            read_switches();
            if (!g_mod) {
                uint8_t *b = find_mod();
                if (b && verify_mod(b)) { g_mod = b; mlog("sc-offline 0.7.7 found at %p", (void *)b); }
                else if (b) { g_stop = 1; break; }
                else if (!reported) { reported = 1; mlog("sc-offline's dinput8.dll (0.7.7) is not loaded yet"); }
            }
            if (g_mod) { padfix_install(); game_attach(); menu_attach(); }
        }
        if (now - last_fast >= 250) { last_fast = now; read_commands(); }
        if (g_mod) {
            /* Keep the mod's hook running. While the game thread is not taking messages (a loading screen) at most a few
               wait in its queue: one every 500 ms instead of one every `keepalive` ms. */
            const LONG every = g_cfg.keepalive;
            static LONG ticks_at_post;
            static int unanswered;
            if (c_ticks_total != ticks_at_post) unanswered = 0;
            if (every > 0 && g_game && now - last_keepalive >= (ULONGLONG)(unanswered < 4 ? every : 500)) {
                last_keepalive = now;
                ticks_at_post = c_ticks_total;
                unanswered++;
                if (PostMessageW(g_game, WM_NULL, 0, 0)) InterlockedIncrement(&c_keepalive_total);
            }
            menu_step(now);
            const LONG st = g_cfg.status;
            if (st > 0 && now - last_status >= (ULONGLONG)st * 1000) {
                const LONG ticks = c_ticks_total, posts = c_keepalive_total;
                if (last_status) {
                    const double secs = (double)(now - last_status) / 1000.0;
                    mlog("[game] the mod's code ran %.1f times/s over the last %.0f s (messages posted to keep it running: %.1f/s)%s",
                         (ticks - status_ticks) / secs, secs, (posts - status_posts) / secs, g_game_hook ? "" : " [no hook: not counted]");
                }
                status_ticks = ticks; status_posts = posts;
                last_status = now;
            }
        }
        Sleep(g_menu_active ? 4 : 20);
    }
    return 0;
}

#endif

static void start_once(void) {
    static LONG started;
    if (InterlockedExchange(&started, 1)) return;
    g_worker = CreateThread(NULL, 0, worker, NULL, 0, NULL);
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        InitializeCriticalSection(&g_loglock);
        start_once();
    }
    return TRUE;
}

/* ---- sc-offline plugin exports (sco_api.h) ------------------------------------------------------------------------ */
SCO_EXPORT const sco_plugin_info *sco_plugin_query(void) {
    static const sco_plugin_info info = { sizeof(sco_plugin_info), SCO_API_MAJOR, SCO_API_MINOR, "macfix", "1.1.0", "local" };
    return &info;
}

SCO_EXPORT sco_result sco_plugin_load(const sco_api *api, sco_plugin *self) {
    g_api = api;
    g_self = self;
    start_once();
    mlog("loaded as an sc-offline plugin (host: %s, api %u.%u)", api && api->host_version ? api->host_version() : "?", api ? api->major : 0,
         api ? api->minor : 0);
    return SCO_OK;
}

SCO_EXPORT void sco_plugin_unload(void) {
    g_stop = 1;
    if (g_worker) WaitForSingleObject(g_worker, 2000);
    g_api = NULL;
    g_self = NULL;
    detach_all();
    padfix_remove();
    mlog("unloaded");
}

#ifdef MACFIX_TEST
/* macfix_test.exe: the pad logic against the two orders seen in the game (no game needed). */
static struct { uintptr_t state, owner, vehicle; int calls; } g_seen_call;
static uintptr_t stub(uintptr_t atc, const uintptr_t *token, uintptr_t state, uintptr_t owner, uintptr_t vehicle, uintptr_t a6, uintptr_t a7, uintptr_t a8) {
    (void)atc; (void)a6; (void)a7; (void)a8;
    g_seen_call.state = state; g_seen_call.owner = owner; g_seen_call.vehicle = vehicle; g_seen_call.calls++;
    ((uintptr_t *)token[0])[2] = vehicle;                       /* the game stores the vehicle in the token (+0x10) */
    return 1;
}
static int expect(const char *what, uintptr_t state, uintptr_t owner, uintptr_t vehicle) {
    const int ok = g_seen_call.state == state && g_seen_call.owner == owner && g_seen_call.vehicle == vehicle;
    printf("%-58s -> state %llu owner %llX vehicle %llX  %s\n", what, (unsigned long long)g_seen_call.state, (unsigned long long)g_seen_call.owner,
           (unsigned long long)g_seen_call.vehicle, ok ? "ok" : "WRONG");
    return ok;
}
int main(void) {
    enum { Free = 0, Reserved = 1, Occupied = 2 };
    const uintptr_t player = 0xCAE11A7400000000ull, stored = 0x2E912D2878ull, fresh = 0x2E912D2C03ull, npc = 0x2E90000001ull;
    uintptr_t obj[4] = { 0 }, obj2[4] = { 0 };
    const uintptr_t sp[2] = { (uintptr_t)obj, 0 }, sp2[2] = { (uintptr_t)obj2, 0 };
    int ok = 1;
    InitializeCriticalSection(&g_loglock);
    g_mod = VirtualAlloc(NULL, 0xB00000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    *(uintptr_t *)(g_mod + RVA_PLAYER) = player;
    g_next_change_token = stub;
    printf("order seen on the Mac: the ATC registers the lift's ship first, the mod's spawn cancel comes after\n");
    fix_change_token_state(1, sp, Reserved, player, stored, 0, 0, 0); ok &= expect("Retrieve: pad reserved for you", Reserved, player, stored);
    fix_change_token_state(1, sp, Occupied, player, stored, 0, 0, 0); ok &= expect("Retrieve: pad occupied by the stored id", Occupied, player, stored);
    fix_change_token_state(1, sp, Occupied, 0, fresh, 0, 0, 0);       ok &= expect("lift's ship takes the pad with owner 0", Occupied, player, fresh);
    fix_change_token_state(1, sp, Reserved, 0, 0, 0, 0, 0);           ok &= expect("the mod's spawn cancel clears the pad", Occupied, player, fresh);
    fix_change_token_state(1, sp, Free, 0, 0, 0, 0, 0);               ok &= expect("a later real release (take-off) passes", Free, 0, 0);
    printf("order the mod expects: spawn cancel first, then the lift's ship\n");
    obj[2] = 0;
    fix_change_token_state(1, sp, Reserved, player, stored, 0, 0, 0); ok &= expect("Retrieve: pad reserved for you", Reserved, player, stored);
    fix_change_token_state(1, sp, Occupied, player, stored, 0, 0, 0); ok &= expect("Retrieve: pad occupied by the stored id", Occupied, player, stored);
    fix_change_token_state(1, sp, Reserved, 0, 0, 0, 0, 0);           ok &= expect("the mod's spawn cancel clears the pad", Reserved, 0, 0);
    fix_change_token_state(1, sp, Occupied, player, fresh, 0, 0, 0);  ok &= expect("lift's ship, owner already set by the mod (rc20)", Occupied, player, fresh);
    fix_change_token_state(1, sp, Free, 0, 0, 0, 0, 0);               ok &= expect("a later real release passes", Free, 0, 0);
    printf("someone else's pad\n");
    fix_change_token_state(1, sp2, Occupied, 0, npc, 0, 0, 0);        ok &= expect("an NPC ship lands on another pad", Occupied, 0, npc);
    fix_change_token_state(1, sp2, Free, 0, 0, 0, 0, 0);              ok &= expect("and leaves", Free, 0, 0);
    printf("%s\n", ok ? "ALL OK" : "FAILED");
    return !ok;
}
#endif
