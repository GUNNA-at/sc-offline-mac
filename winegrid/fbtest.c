// Kernel test: is a heap backed by a file on the SSD better than anonymous (compressed) memory when it does not fit?
// Build x86_64 (runs under Rosetta like the game):  clang -arch x86_64 -O2 -o fbtest fbtest.c -lpthread
// usage: fbtest file|anon <total GB> <hot GB> <wired GB> <seconds> [threads]
//   total: size of the "heap" (filled once with half-compressible data)   hot: the part that is touched all the time
//   wired: memory pinned for the whole test (stands in for the game's GPU memory)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <mach/mach.h>
static double now(void) { struct timeval tv; gettimeofday(&tv, 0); return tv.tv_sec + tv.tv_usec / 1e6; }
static unsigned char *heap; static size_t total, hot; static atomic_ulong ops, slow; static atomic_int stop; static const size_t PG = 16384;
static void vmline(const char *what, vm_statistics64_data_t *prev) {
    vm_statistics64_data_t vs; mach_msg_type_number_t n = HOST_VM_INFO64_COUNT; host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t)&vs, &n);
    if (prev->decompressions || prev->pageins) printf("   %-18s decompress %8llu  compress %8llu  page-in %8llu  page-out %8llu  swap-in %7llu  swap-out %7llu | compressor %.1f GB RAM, file-backed %.1f GB, anonymous %.1f GB\n", what,
        vs.decompressions - prev->decompressions, vs.compressions - prev->compressions, vs.pageins - prev->pageins, vs.pageouts - prev->pageouts, vs.swapins - prev->swapins, vs.swapouts - prev->swapouts,
        vs.compressor_page_count * 16384.0 / 1073741824, vs.external_page_count * 16384.0 / 1073741824, vs.internal_page_count * 16384.0 / 1073741824);
    *prev = vs; fflush(stdout);
}
static void *worker(void *arg) {
    unsigned long long x = 88172645463325252ull * ((unsigned long)arg + 1);
    while (!atomic_load(&stop)) {
        x ^= x << 13; x ^= x >> 7; x ^= x << 17;
        size_t span = (x % 1000 < 995) ? hot : total;                 /* 99.5% of touches in the hot part */
        size_t off = ((x >> 12) % (span / PG)) * PG + ((x >> 40) & 0x3ff8);
        double t0 = (x & 0x3f) == 0 ? now() : 0;
        unsigned long long *p = (unsigned long long *)(heap + off); *p = *p * 3 + x;
        if (t0) { double dt = now() - t0; if (dt > 0.002) atomic_fetch_add(&slow, 1); }
        atomic_fetch_add_explicit(&ops, 1, memory_order_relaxed);
    }
    return NULL;
}
int main(int argc, char **argv) {
    if (argc < 6) { printf("usage: fbtest file|anon totalGB hotGB wiredGB seconds [threads]\n"); return 2; }
    int file = !strcmp(argv[1], "file"); total = (size_t)atoi(argv[2]) << 30; hot = (size_t)atoi(argv[3]) << 30; size_t wired = (size_t)atoi(argv[4]) << 30; int secs = atoi(argv[5]), nth = argc > 6 ? atoi(argv[6]) : 8;
    setvbuf(stdout, NULL, _IONBF, 0);
    void *w = NULL;
    if (wired) { w = mmap(NULL, wired, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0); memset(w, 0x5a, wired); if (mlock(w, wired)) perror("mlock"); printf("%zu GB pinned\n", wired >> 30); }
    if (file) {
        char path[] = "/private/tmp/fbtest-heap.XXXXXX"; int fd = mkstemp(path); unlink(path);
        if (ftruncate(fd, (off_t)total)) { perror("ftruncate"); return 1; }
        heap = mmap(NULL, total, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    } else heap = mmap(NULL, total, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (heap == MAP_FAILED) { perror("mmap"); return 1; }
    vm_statistics64_data_t prev; memset(&prev, 0, sizeof prev); vmline("", &prev);
    double t0 = now(); unsigned long long x = 0x9E3779B97F4A7C15ull;
    for (size_t o = 0; o < total; o += PG) {                           /* half of every page random, half zero: compresses about 2x */
        unsigned long long *p = (unsigned long long *)(heap + o);
        for (size_t i = 0; i < PG / 16; i++) { x ^= x << 13; x ^= x >> 7; x ^= x << 17; p[i] = x; }
    }
    double t1 = now(); printf("%s: filled %zu GB in %.1f s (%.0f MB/s)\n", argv[1], total >> 30, t1 - t0, (total >> 20) / (t1 - t0)); vmline("fill:", &prev);
    pthread_t th[32]; for (long i = 0; i < nth; i++) pthread_create(&th[i], NULL, worker, (void *)i);
    unsigned long last = 0;
    for (int s = 0; s < secs; s += 5) { sleep(5); unsigned long o = atomic_load(&ops); char lbl[32]; snprintf(lbl, sizeof lbl, "%2d-%2d s:", s, s + 5);
        printf("   touches per second %9.0f (slow touches so far %lu)\n", (o - last) / 5.0, atomic_load(&slow)); last = o; vmline(lbl, &prev); }
    atomic_store(&stop, 1); for (int i = 0; i < nth; i++) pthread_join(th[i], NULL);
    printf("%s: %zu GB total, %zu GB hot, %zu GB pinned: %.0f touches per second on average\n", argv[1], total >> 30, hot >> 30, wired >> 30, atomic_load(&ops) / (double)secs);
    return 0;
}
