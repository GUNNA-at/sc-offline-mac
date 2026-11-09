/* Native test for winegrid (build as x86_64 so it runs under Rosetta with 4 KB pages, like Wine does):
 *   clang -arch x86_64 -O1 -DGRID_TEST -o grid_test grid_test.c && ./grid_test
 * It calls the two redirected functions the way Wine's virtual memory code does and checks contents, zero-fill after
 * decommit, bookkeeping and the real memory cost. */
#include "grid.c"
#include <sys/wait.h>
#include <signal.h>

int winegrid_test_enable(int inplace); void winegrid_test_stats(void); size_t winegrid_test_spans(void);
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("  FAIL: " __VA_ARGS__); printf("\n"); } } while (0)
static const size_t K = 4096;
static double gbf(double b) { return b / 1073741824.0; }
static double nowf(void) { struct timeval tv; gettimeofday(&tv, 0); return tv.tv_sec + tv.tv_usec / 1e6; }
static double real_anon(void) {
    vm_statistics64_data_t vs; mach_msg_type_number_t n = HOST_VM_INFO64_COUNT; vm_size_t hps; host_page_size(mach_host_self(), &hps);
    host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t)&vs, &n);
    return ((double)vs.internal_page_count + (double)vs.total_uncompressed_pages_in_compressor) * hps;
}
static double ledger(void) { task_vm_info_data_t ti; mach_msg_type_number_t c = TASK_VM_INFO_COUNT; task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&ti, &c); return (double)ti.phys_footprint; }
static long entries(uintptr_t lo, uintptr_t hi) {
    long n = 0; mach_vm_address_t a = lo;
    while (a < hi) { mach_vm_size_t sz = 0; natural_t depth = 0; vm_region_submap_info_data_64_t info; mach_msg_type_number_t cnt = VM_REGION_SUBMAP_INFO_COUNT_64;
        if (mach_vm_region_recurse(mach_task_self(), &a, &sz, &depth, (vm_region_recurse_info_t)&info, &cnt) != KERN_SUCCESS || a >= hi) break; n++; a += sz; }
    return n;
}
static int prot_of(uintptr_t addr) {
    mach_vm_address_t a = addr; mach_vm_size_t sz = 0; natural_t depth = 0; vm_region_submap_info_data_64_t info; mach_msg_type_number_t cnt = VM_REGION_SUBMAP_INFO_COUNT_64;
    if (mach_vm_region_recurse(mach_task_self(), &a, &sz, &depth, (vm_region_recurse_info_t)&info, &cnt) != KERN_SUCCESS || a > addr) return -1;
    return info.protection;
}
/* Wine's calls */
static void *w_reserve(size_t n) { return winegrid_mmap(NULL, n, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0); }
static void *w_reserve_at(void *p, size_t n) { return winegrid_mmap(p, n, PROT_NONE, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0); }
static int w_commit(void *p, size_t n) { return mprotect(p, n, PROT_READ | PROT_WRITE); }
static void *w_decommit(void *p, size_t n) { return winegrid_mmap(p, n, PROT_NONE, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0); }
static void *w_release_to_reserve(void *p, size_t n) { return winegrid_mmap(p, n, PROT_NONE, MAP_PRIVATE | MAP_ANON | MAP_FIXED | MAP_NORESERVE, -1, 0); }
static int all_zero(const unsigned char *p, size_t n) { for (size_t i = 0; i < n; i++) if (p[i]) return 0; return 1; }
static unsigned rnd_state = 12345; static unsigned rnd(void) { rnd_state = rnd_state * 1103515245u + 12345u; return rnd_state >> 8; }

static void t_cost(size_t mb)
{
    printf("T1  allocator pattern: reserve %zu MB, commit and touch one 4 KB page at a time\n", mb);
    size_t N = mb << 20; double r0 = real_anon(), l0 = ledger();
    unsigned char *b = w_reserve(N); CHECK(b != MAP_FAILED, "reserve");
    double t0 = nowf();
    for (size_t o = 0; o < N; o += K) { if (w_commit(b + o, K)) { CHECK(0, "commit"); break; } b[o] = 1; }
    double t1 = nowf(), real = real_anon() - r0, led = ledger() - l0;
    printf("    %.2f us per page | ledger +%.2f GB | real memory +%.2f GB | map entries %ld\n", (t1 - t0) * 1e6 / (N / K), gbf(led), gbf(real), entries((uintptr_t)b, (uintptr_t)b + N));
    CHECK(real < led * 1.25 + (64 << 20), "real memory %.2f GB is far above the ledger %.2f GB", gbf(real), gbf(led));
    /* release half in place and look at the ledger */
    w_decommit(b + N / 2, N / 2);
    printf("    after decommitting half in place: ledger %+.2f GB\n", gbf(ledger() - l0));
    CHECK(ledger() - l0 < led * 0.6 + (32 << 20), "decommit did not give memory back");
    CHECK(prot_of((uintptr_t)b + N / 2 + 5 * K) == VM_PROT_NONE, "decommitted memory is still accessible");
    w_commit(b + N / 2, N / 2); CHECK(all_zero(b + N / 2, N / 2), "recommitted half is not zero");
    winegrid_munmap(b, N);
}

static void t_random(size_t mb, int ops)
{
    printf("T2  %d random commit / decommit / write / check operations on a %zu MB reservation\n", ops, mb);
    size_t N = mb << 20, P = N / K; unsigned char *tag = calloc(P, 1), *com = calloc(P, 1);
    unsigned char *b = w_reserve(N); CHECK(b != MAP_FAILED, "reserve");
    unsigned long commits = 0, decommits = 0, checks = 0; int before = fails;
    for (int op = 0; op < ops && fails - before < 5; op++) {
        unsigned r = rnd() % 100; size_t a = rnd() % P, len = 1 + (rnd() % 100 < 70 ? rnd() % 6 : rnd() % 300); if (a + len > P) len = P - a;
        if (r < 45) {                                               /* commit: new pages must be zero, old pages keep their data */
            if (w_commit(b + a * K, len * K)) { CHECK(0, "commit failed"); continue; }
            for (size_t p = a; p < a + len; p++) {
                unsigned char *pg = b + p * K;
                if (!com[p]) { if (!all_zero(pg, K)) CHECK(0, "op %d: freshly committed page %zu is not zero", op, p); com[p] = 1; tag[p] = 0; }
                else if (tag[p] && (pg[0] != tag[p] || pg[2000] != tag[p] || pg[K - 1] != tag[p])) CHECK(0, "op %d: page %zu lost its data on recommit", op, p);
            }
            commits++;
        } else if (r < 75) {                                        /* decommit */
            if (w_decommit(b + a * K, len * K) == MAP_FAILED) { CHECK(0, "decommit failed"); continue; }
            for (size_t p = a; p < a + len; p++) { com[p] = 0; tag[p] = 0; }
            decommits++;
        } else if (r < 92) {                                        /* write to committed pages */
            for (size_t p = a; p < a + len; p++) if (com[p]) { unsigned char t = 1 + rnd() % 250, *pg = b + p * K; pg[0] = pg[2000] = pg[K - 1] = t; tag[p] = t; }
        } else {                                                    /* check */
            for (size_t p = a; p < a + len; p++) if (com[p]) { unsigned char *pg = b + p * K; checks++;
                if (pg[0] != tag[p] || pg[2000] != tag[p] || pg[K - 1] != tag[p]) CHECK(0, "op %d: page %zu holds %d, expected %d", op, p, pg[0], tag[p]); }
        }
    }
    size_t live = 0, wrong = 0, open = 0;
    for (size_t p = 0; p < P; p++) {
        if (com[p]) { live++; unsigned char *pg = b + p * K; if (pg[0] != tag[p] || pg[2000] != tag[p] || pg[K - 1] != tag[p]) wrong++; }
        else if ((p & 63) == 0 && prot_of((uintptr_t)b + p * K) != VM_PROT_NONE) open++;
    }
    CHECK(!wrong, "%zu of %zu live pages have wrong contents at the end", wrong, live);
    CHECK(!open, "%zu sampled decommitted pages are accessible", open);
    printf("    %lu commits, %lu decommits, %lu page checks, %zu pages live at the end, map entries %ld\n", commits, decommits, checks, live, entries((uintptr_t)b, (uintptr_t)b + N));
    winegrid_munmap(b, N); free(tag); free(com);
}

static void t_wine_paths(void)
{
    printf("T3  release to Wine's reserve and reuse, committed mapping and file inside a reservation, unmap in the middle\n");
    size_t N = 64 << 20; unsigned char *b = w_reserve(N); size_t s0 = winegrid_test_spans();
    w_commit(b, 1 << 20); memset(b, 0x11, 1 << 20);
    CHECK(w_release_to_reserve(b, N) == b, "release"); CHECK(winegrid_test_spans() == s0 - 1, "release did not drop the tracking (%zu -> %zu)", s0, winegrid_test_spans());
    CHECK(w_reserve_at(b, N) == b, "reserve again at the same place"); CHECK(winegrid_test_spans() == s0, "new reservation is not tracked");
    w_commit(b, 1 << 20); CHECK(all_zero(b, 1 << 20), "memory of a new reservation is not zero");
    /* a committed (read-write) anonymous mapping placed inside: plain private memory, then holes punched into it */
    unsigned char *c = b + (8 << 20) + K;
    CHECK(winegrid_mmap(c, 1 << 20, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0) == c, "committed mapping");
    memset(c, 0x22, 1 << 20);
    CHECK(w_decommit(c + 3 * K, 5 * K) == c + 3 * K, "decommit inside committed mapping");
    CHECK(c[2 * K + 7] == 0x22 && c[8 * K] == 0x22, "neighbours of the hole lost data");
    w_commit(c + 3 * K, 5 * K); CHECK(all_zero(c + 3 * K, 5 * K), "hole is not zero after recommit");
    CHECK(c[2 * K + 7] == 0x22 && c[8 * K] == 0x22, "neighbours of the hole lost data after recommit");
    /* a file mapped over part of the reservation */
    char path[] = "/tmp/winegrid-test-XXXXXX"; int fd = mkstemp(path); char page[4096]; memset(page, 0x33, sizeof page); for (int i = 0; i < 8; i++) (void)!write(fd, page, sizeof page);
    unsigned char *f = b + (20 << 20);
    CHECK(winegrid_mmap(f, 8 * K, PROT_READ, MAP_PRIVATE | MAP_FIXED, fd, 0) == f, "file mapping"); CHECK(f[0] == 0x33 && f[8 * K - 1] == 0x33, "file contents");
    w_commit(f + 8 * K, K); CHECK(f[8 * K] == 0, "page after the file mapping"); f[8 * K] = 9;
    CHECK(w_decommit(f, 8 * K) == f, "reserve over the file mapping"); w_commit(f, 8 * K); CHECK(all_zero(f, 8 * K), "old file range is not zero anonymous memory"); CHECK(f[8 * K] == 9, "neighbour of file range");
    close(fd); unlink(path);
    /* unmap a piece in the middle, then the rest still works */
    size_t s1 = winegrid_test_spans(); CHECK(winegrid_munmap(b + (32 << 20), 1 << 20) == 0, "munmap"); CHECK(winegrid_test_spans() == s1 + 1, "unmap in the middle should split the tracking");
    w_commit(b + (40 << 20), 16 * K); b[(40 << 20) + 5] = 1; w_decommit(b + (40 << 20), 16 * K); w_commit(b + (40 << 20), 16 * K); CHECK(b[(40 << 20) + 5] == 0, "after split");
    winegrid_munmap(b, 32 << 20); winegrid_munmap(b + (33 << 20), N - (33 << 20));
}

static void t_big_decommit(void)
{
    printf("T4  decommit across several 128 MB objects (whole-chunk path), unaligned ends\n");
    size_t N = (size_t)600 << 20; unsigned char *b = w_reserve(N);
    for (size_t o = 0; o < N; o += 1 << 20) { w_commit(b + o, 2 * K); b[o] = 0x44; b[o + K] = 0x45; }
    size_t s = 3 * K, len = N - 7 * K;
    CHECK(w_decommit(b + s, len) == b + s, "decommit"); CHECK(b[0] == 0x44, "page before the range");
    w_commit(b + s, len); int bad = 0; for (size_t o = 1 << 20; o < N - (1 << 20); o += 1 << 20) if (b[o] || b[o + K]) bad++;
    CHECK(!bad, "%d pages kept data across a big decommit", bad);
    winegrid_munmap(b, N);
}

static void t_speed(void)
{
    printf("T5  speed of decommit + recommit cycles\n");
    size_t N = 256 << 20; unsigned char *b = w_reserve(N); w_commit(b, N); for (size_t o = 0; o < N; o += K) b[o] = 1;
    int n = 50000; double t0 = nowf();
    for (int i = 0; i < n; i++) { size_t o = (rnd() % (N / K - 16)) * K; w_decommit(b + o, K); w_commit(b + o, K); b[o] = 2; }
    double t1 = nowf();
    for (int i = 0; i < n; i++) { size_t o = (rnd() % (N / K - 64)) * K; w_decommit(b + o, 16 * K); w_commit(b + o, 16 * K); b[o] = 3; }
    double t2 = nowf();
    printf("    4 KB cycle %.1f us, 64 KB cycle %.1f us\n", (t1 - t0) * 1e6 / n, (t2 - t1) * 1e6 / n);
    winegrid_munmap(b, N);
}

static void t_fork(void)
{
    printf("T6  fork and exec with reservations in place\n");
    size_t N = 64 << 20; unsigned char *b = w_reserve(N); w_commit(b, N / 2); memset(b, 0x55, N / 2);
    pid_t pid = fork(); if (pid == 0) { execl("/usr/bin/true", "true", (char *)0); _exit(9); }
    int st = 0; waitpid(pid, &st, 0); CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0, "child exit %d", st);
    CHECK(b[5] == 0x55 && b[N / 2 - 1] == 0x55, "parent data after fork");
    winegrid_munmap(b, N);
}

int main(int argc, char **argv)
{
    int inplace = !(argc > 1 && !strcmp(argv[1], "noinplace"));
    setenv("WINEGRID_LOG", "/dev/stdout", 1);
    if (!winegrid_test_enable(inplace)) { printf("not applicable here (task page %d): build with -arch x86_64 on Apple Silicon\n", getpagesize()); return 2; }
    printf("task page %zu, machine page %zu, in-place decommit %s\n", g_tp, g_hp, inplace ? "on" : "off");
    t_cost(512); t_random(64, 60000); t_wine_paths(); t_big_decommit(); t_speed(); t_fork();
    winegrid_test_stats();
    printf("%s (%d problems)\n", fails ? "FAIL" : "PASS", fails);
    return fails != 0;
}
