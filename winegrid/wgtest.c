/* Windows-side test for winegrid, run under Wine:
 *   wgtest.exe pattern <MB>            reserve, then commit + touch one 4 KB page at a time; holds so memory can be measured
 *   wgtest.exe stress <MB> <ops> <thr> random commit/decommit/write/check, several threads, one reservation each
 *   wgtest.exe misc                    the other things a program does with virtual memory */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const SIZE_T K = 4096;
static int fails;
#define CHECK(c, ...) do { if (!(c)) { InterlockedIncrement((LONG *)&fails); printf("  FAIL: " __VA_ARGS__); printf("\n"); } } while (0)
static int all_zero(const unsigned char *p, SIZE_T n) { for (SIZE_T i = 0; i < n; i++) if (p[i]) return 0; return 1; }

static int pattern(int mb)
{
    SIZE_T N = (SIZE_T)mb << 20;
    unsigned char *b = VirtualAlloc(NULL, N, MEM_RESERVE, PAGE_NOACCESS); if (!b) { printf("reserve failed\n"); return 1; }
    DWORD t0 = GetTickCount();
    for (SIZE_T o = 0; o < N; o += K) { if (!VirtualAlloc(b + o, K, MEM_COMMIT, PAGE_READWRITE)) { printf("commit failed at %llu\n", (unsigned long long)o); return 1; } b[o] = 1; }
    printf("PATTERN committed %d MB page by page in %lu ms (pid %lu)\n", mb, GetTickCount() - t0, GetCurrentProcessId());
    Sleep(14000);
    for (SIZE_T o = 0; o < N / 2; o += 64 * K) VirtualFree(b + o, 64 * K, MEM_DECOMMIT);
    printf("PATTERN decommitted the first half\n");
    Sleep(8000);
    for (SIZE_T o = 0; o < N / 2; o += K) { VirtualAlloc(b + o, K, MEM_COMMIT, PAGE_READWRITE); if (b[o] != 0 || b[o + K - 1] != 0) { fails++; break; } b[o] = 2; }
    for (SIZE_T o = N / 2; o < N; o += K) if (b[o] != 1) { fails++; break; }
    printf("PATTERN %s\n", fails ? "FAIL" : "PASS");
    return fails != 0;
}

struct job { int mb, ops, id; };
static DWORD WINAPI stress_thread(LPVOID arg)
{
    struct job *j = arg; SIZE_T N = (SIZE_T)j->mb << 20, P = N / K; unsigned st = 777u * (unsigned)(j->id + 1);
#define RND() ((st = st * 1103515245u + 12345u) >> 8)
    unsigned char *tag = calloc(P, 1), *com = calloc(P, 1);
    unsigned char *b = VirtualAlloc(NULL, N, MEM_RESERVE, PAGE_NOACCESS); if (!b) { CHECK(0, "reserve"); return 1; }
    int mine = 0;
    for (int op = 0; op < j->ops && mine < 5; op++) {
        unsigned r = RND() % 100; SIZE_T a = RND() % P, len = 1 + (RND() % 100 < 70 ? RND() % 6 : RND() % 300); if (a + len > P) len = P - a;
        if (r < 45) {
            if (!VirtualAlloc(b + a * K, len * K, MEM_COMMIT, PAGE_READWRITE)) { CHECK(0, "commit"); mine++; continue; }
            for (SIZE_T p = a; p < a + len; p++) { unsigned char *pg = b + p * K;
                if (!com[p]) { if (!all_zero(pg, K)) { CHECK(0, "thread %d op %d: fresh page %llu not zero", j->id, op, (unsigned long long)p); mine++; } com[p] = 1; tag[p] = 0; }
                else if (tag[p] && (pg[0] != tag[p] || pg[2000] != tag[p] || pg[K - 1] != tag[p])) { CHECK(0, "thread %d op %d: page lost data on recommit", j->id, op); mine++; } }
        } else if (r < 75) {
            if (!VirtualFree(b + a * K, len * K, MEM_DECOMMIT)) { CHECK(0, "decommit"); mine++; continue; }
            for (SIZE_T p = a; p < a + len; p++) { com[p] = 0; tag[p] = 0; }
        } else if (r < 92) {
            for (SIZE_T p = a; p < a + len; p++) if (com[p]) { unsigned char t = 1 + RND() % 250, *pg = b + p * K; pg[0] = pg[2000] = pg[K - 1] = t; tag[p] = t; }
        } else {
            for (SIZE_T p = a; p < a + len; p++) if (com[p]) { unsigned char *pg = b + p * K;
                if (pg[0] != tag[p] || pg[2000] != tag[p] || pg[K - 1] != tag[p]) { CHECK(0, "thread %d op %d: page %llu holds %d, expected %d", j->id, op, (unsigned long long)p, pg[0], tag[p]); mine++; } }
        }
    }
    SIZE_T wrong = 0, open = 0, live = 0;
    for (SIZE_T p = 0; p < P; p++) {
        if (com[p]) { live++; unsigned char *pg = b + p * K; if (pg[0] != tag[p] || pg[2000] != tag[p] || pg[K - 1] != tag[p]) wrong++; }
        else if ((p & 127) == 0 && !IsBadReadPtr(b + p * K, 1)) open++;
    }
    CHECK(!wrong, "thread %d: %llu live pages wrong at the end", j->id, (unsigned long long)wrong);
    CHECK(!open, "thread %d: %llu decommitted pages readable", j->id, (unsigned long long)open);
    VirtualFree(b, 0, MEM_RELEASE); free(tag); free(com);
    return 0;
}
static int stress(int mb, int ops, int threads)
{
    HANDLE h[32]; struct job j[32]; if (threads > 32) threads = 32;
    DWORD t0 = GetTickCount();
    for (int i = 0; i < threads; i++) { j[i].mb = mb; j[i].ops = ops; j[i].id = i; h[i] = CreateThread(NULL, 0, stress_thread, &j[i], 0, NULL); }
    WaitForMultipleObjects(threads, h, TRUE, INFINITE);
    printf("STRESS %d threads x %d ops on %d MB each in %lu ms: %s (%d problems)\n", threads, ops, mb, GetTickCount() - t0, fails ? "FAIL" : "PASS", fails);
    return fails != 0;
}

static int depth(int n) { volatile char pad[4096]; pad[0] = (char)n; return n ? depth(n - 1) + pad[0] * 0 + 1 : 0; }
static DWORD WINAPI deep_thread(LPVOID arg) { (void)arg; return (DWORD)depth(1500); }   /* about 6 MB of stack */
static int misc(void)
{
    MEMORY_BASIC_INFORMATION mi; SIZE_T N = 64 << 20;
    unsigned char *b = VirtualAlloc(NULL, N, MEM_RESERVE, PAGE_NOACCESS); CHECK(b != NULL, "reserve");
    VirtualQuery(b, &mi, sizeof mi); CHECK(mi.State == MEM_RESERVE && mi.RegionSize == N, "query of a reservation: state %lx size %llx", mi.State, (unsigned long long)mi.RegionSize);
    CHECK(IsBadReadPtr(b + 5 * K, 1), "reserved memory is readable");
    CHECK(VirtualAlloc(b + 3 * K, 6 * K, MEM_COMMIT, PAGE_READWRITE) == b + 3 * K, "commit"); memset(b + 3 * K, 0x61, 6 * K);
    VirtualQuery(b + 3 * K, &mi, sizeof mi); CHECK(mi.State == MEM_COMMIT && mi.RegionSize == 6 * K && mi.Protect == PAGE_READWRITE, "query of committed pages");
    CHECK(IsBadReadPtr(b + 2 * K, 1) && IsBadReadPtr(b + 9 * K, 1), "pages next to the committed ones are readable");
    CHECK(VirtualAlloc(b + 2 * K, 8 * K, MEM_COMMIT, PAGE_READWRITE) != NULL, "overlapping commit"); CHECK(b[3 * K] == 0x61 && b[9 * K - 1] == 0x61 && b[2 * K] == 0 && b[9 * K] == 0, "overlapping commit changed contents");
    DWORD old; CHECK(VirtualProtect(b + 4 * K, K, PAGE_READONLY, &old) && old == PAGE_READWRITE, "protect"); CHECK(IsBadWritePtr(b + 4 * K, 1) && !IsBadReadPtr(b + 4 * K, 1), "read-only page");
    CHECK(VirtualProtect(b + 4 * K, K, PAGE_READWRITE, &old), "protect back"); CHECK(b[4 * K + 9] == 0x61, "data after protect");
    CHECK(VirtualAlloc(b + 4 * K, K, MEM_RESET, PAGE_READWRITE) != NULL, "reset");
    /* code in committed memory of a reservation */
    unsigned char *x = b + (1 << 20) + K; CHECK(VirtualAlloc(x, K, MEM_COMMIT, PAGE_EXECUTE_READWRITE) == x, "commit executable");
    static const unsigned char code[] = { 0xB8, 0x2A, 0x00, 0x00, 0x00, 0xC3 }; memcpy(x, code, sizeof code);
    int v = ((int (*)(void))x)(); CHECK(v == 42, "code in the reservation returned %d", v);
    x[1] = 0x2B; FlushInstructionCache(GetCurrentProcess(), x, 8); v = ((int (*)(void))x)(); CHECK(v == 43, "modified code returned %d", v);
    VirtualFree(x, K, MEM_DECOMMIT); CHECK(IsBadReadPtr(x, 1), "decommitted code page readable");
    /* release and reserve again */
    CHECK(VirtualFree(b, 0, MEM_RELEASE), "release");
    unsigned char *b2 = VirtualAlloc(b, N, MEM_RESERVE, PAGE_NOACCESS); CHECK(b2 == b, "reserve at the same address again (%p vs %p)", b2, b);
    if (b2) { VirtualAlloc(b2 + 3 * K, 6 * K, MEM_COMMIT, PAGE_READWRITE); CHECK(all_zero(b2 + 3 * K, 6 * K), "new reservation shows old data"); VirtualFree(b2, 0, MEM_RELEASE); }
    /* reserve + commit in one call, then holes */
    unsigned char *c = VirtualAlloc(NULL, 8 << 20, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE); memset(c, 0x62, 8 << 20);
    VirtualFree(c + 5 * K, 3 * K, MEM_DECOMMIT); CHECK(c[5 * K - 1] == 0x62 && c[8 * K] == 0x62, "hole damaged neighbours");
    VirtualAlloc(c + 5 * K, 3 * K, MEM_COMMIT, PAGE_READWRITE); CHECK(all_zero(c + 5 * K, 3 * K), "hole not zero"); VirtualFree(c, 0, MEM_RELEASE);
    /* a huge reservation with scattered commits, like the game's heap */
    SIZE_T H = (SIZE_T)1 << 40; unsigned char *h = VirtualAlloc(NULL, H, MEM_RESERVE, PAGE_NOACCESS); CHECK(h != NULL, "1 TB reservation");
    if (h) { for (int i = 0; i < 2000; i++) { SIZE_T o = ((SIZE_T)i * 0x1F3D5B79ull) % (H / K) * K; if (!VirtualAlloc(h + o, K, MEM_COMMIT, PAGE_READWRITE)) { CHECK(0, "commit in 1 TB"); break; } h[o] = (unsigned char)i; }
             VirtualFree(h, 0, MEM_RELEASE); }
    /* the process heap and a private heap */
    HANDLE hp = HeapCreate(0, 0, 0); void *ptr[2000]; for (int i = 0; i < 2000; i++) { SIZE_T sz = 16 + (i * 7919u) % 300000; ptr[i] = HeapAlloc(hp, HEAP_ZERO_MEMORY, sz); if (!ptr[i] || !all_zero(ptr[i], sz > 256 ? 256 : sz)) { CHECK(0, "heap alloc %d", i); break; } memset(ptr[i], i, sz); }
    for (int i = 0; i < 2000; i += 2) HeapFree(hp, 0, ptr[i]);
    for (int i = 1; i < 2000; i += 2) if (ptr[i] && *(unsigned char *)ptr[i] != (unsigned char)i) { CHECK(0, "heap data %d", i); break; }
    HeapDestroy(hp);
    /* mappings */
    HANDLE m = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 16 << 20, NULL); unsigned char *mv = MapViewOfFile(m, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    CHECK(mv != NULL, "pagefile mapping"); if (mv) { mv[0] = 1; mv[(16 << 20) - 1] = 2; unsigned char *mv2 = MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0); CHECK(mv2 && mv2[0] == 1 && mv2[(16 << 20) - 1] == 2, "second view"); UnmapViewOfFile(mv2); UnmapViewOfFile(mv); } CloseHandle(m);
    char self[MAX_PATH]; GetModuleFileNameA(NULL, self, sizeof self); HANDLE f = CreateFileA(self, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    HANDLE fm = CreateFileMappingA(f, NULL, PAGE_READONLY, 0, 0, NULL); unsigned char *fv = MapViewOfFile(fm, FILE_MAP_READ, 0, 0, 0); CHECK(fv && fv[0] == 'M' && fv[1] == 'Z', "file mapping"); UnmapViewOfFile(fv); CloseHandle(fm); CloseHandle(f);
    /* threads with deep stacks */
    HANDLE th[4]; for (int i = 0; i < 4; i++) th[i] = CreateThread(NULL, 16 << 20, deep_thread, NULL, 0, NULL);
    WaitForMultipleObjects(4, th, TRUE, INFINITE); DWORD ec = 0; GetExitCodeThread(th[0], &ec); CHECK(ec == 1500, "deep recursion returned %lu", ec);
    /* a child process while reservations exist */
    unsigned char *keep = VirtualAlloc(NULL, 256 << 20, MEM_RESERVE, PAGE_NOACCESS); VirtualAlloc(keep, 1 << 20, MEM_COMMIT, PAGE_READWRITE); memset(keep, 0x63, 1 << 20);
    STARTUPINFOA si = { sizeof si }; PROCESS_INFORMATION pi; char cmd[] = "cmd.exe /c exit 7";
    if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) { WaitForSingleObject(pi.hProcess, 30000); DWORD xc = 0; GetExitCodeProcess(pi.hProcess, &xc); CHECK(xc == 7, "child exit code %lu", xc); }
    else CHECK(0, "CreateProcess failed %lu", GetLastError());
    CHECK(keep[0] == 0x63 && keep[(1 << 20) - 1] == 0x63, "data after starting a child");
    printf("MISC %s (%d problems)\n", fails ? "FAIL" : "PASS", fails);
    return fails != 0;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc > 2 && !strcmp(argv[1], "pattern")) return pattern(atoi(argv[2]));
    if (argc > 4 && !strcmp(argv[1], "stress")) return stress(atoi(argv[2]), atoi(argv[3]), atoi(argv[4]));
    if (argc > 1 && !strcmp(argv[1], "misc")) return misc();
    printf("usage: wgtest pattern <MB> | stress <MB> <ops> <threads> | misc\n"); return 2;
}
