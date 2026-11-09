// balloon: keep the compressed memory of a memory-hungry game from crowding out its live data.
//
// Under sustained pressure macOS lets compressed memory grow to about 60% of the non-wired RAM before it moves any of
// it to swap. Star Citizen offline holds ~14 GB of data it never touches again after loading; compressed, that sits in
// RAM and leaves only ~4 GB for uncompressed pages, so the pages the game does use keep cycling through the compressor
// (40-90 thousand a second, the main thread waiting on them). Pinning a few GB for a few seconds makes the kernel move
// its OLDEST compressed data to swap, where it stays because nothing touches it; the freed RAM then holds live data.
// Measured 2026-11-09: paging 45k/s -> under 1k/s, uncompressed game memory 4.6 -> 7.5 GB, 26 -> 31+ FPS.
//
//   balloon <GB> <hold seconds> [ramp seconds]      one manual squeeze
//   balloon auto <pid> [log file]                   watch until <pid> exits; squeeze when compressed memory is large
//                                                   AND pages are being decompressed at a high rate
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/sysctl.h>
#include <mach/mach.h>

#define GB 1073741824.0
static FILE *logf;
static void say(const char *fmt, ...) {
    char t[16]; time_t now = time(NULL); strftime(t, sizeof t, "%H:%M:%S", localtime(&now));
    va_list ap; va_start(ap, fmt); FILE *o = logf ? logf : stdout; fprintf(o, "%s ", t); vfprintf(o, fmt, ap); fputc('\n', o); fflush(o); va_end(ap);
}
typedef struct { double comp, anon, file, freeb, nonwired; unsigned long long decomp, swapouts; } vmstate;
static void readvm(vmstate *v) {
    vm_statistics64_data_t s; mach_msg_type_number_t n = HOST_VM_INFO64_COUNT; host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t)&s, &n);
    double pg = 16384.0; vm_size_t hp = 0; if (host_page_size(mach_host_self(), &hp) == KERN_SUCCESS) pg = (double)hp;
    v->comp = s.compressor_page_count * pg; v->anon = s.internal_page_count * pg; v->file = s.external_page_count * pg; v->freeb = s.free_count * pg;
    v->nonwired = (s.active_count + s.inactive_count + s.speculative_count + s.free_count + s.compressor_page_count) * pg;
    v->decomp = s.decompressions; v->swapouts = s.swapouts;
}
static double disk_free_gb(void) { struct statfs f; return statfs("/System/Volumes/Data", &f) == 0 ? (double)f.f_bavail * f.f_bsize / GB : 100.0; }
static double swap_used_gb(void) { struct xsw_usage x; size_t l = sizeof x; return sysctlbyname("vm.swapusage", &x, &l, NULL, 0) == 0 ? x.xsu_used / GB : 0; }

/* Inflate towards `want` bytes at `rate` bytes per second; stop early once compressed memory is down to `comp_target`
 * or uncompressed memory would drop below `floor_bytes`. Hold, release. Returns the bytes actually pinned. */
static double squeeze(double want, double rate, double comp_target, double floor_bytes, int hold) {
    size_t step = 128u << 20, total = (size_t)(want / step) * step, done = 0; if (!total) return 0;
    unsigned char *b = mmap(NULL, total, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0); if (b == MAP_FAILED) return 0;
    vmstate v; useconds_t nap = (useconds_t)(step / rate * 1e6);
    while (done < total) {
        readvm(&v);
        if (comp_target > 0 && v.comp <= comp_target) break;                         /* enough has gone to swap */
        if (floor_bytes > 0 && v.anon + v.file < floor_bytes && done > 0) { usleep(400000); readvm(&v); if (v.anon + v.file < floor_bytes) break; }
        memset(b + done, 0x77, step); if (mlock(b + done, step)) break;
        done += step; usleep(nap);
    }
    for (int s = 0; s < hold * 2; s++) { usleep(500000); readvm(&v); if (comp_target > 0 && v.comp <= comp_target) break; }
    munmap(b, total);
    return (double)done;
}

int main(int argc, char **argv) {
    if (argc >= 3 && !strcmp(argv[1], "auto")) {
        pid_t pid = (pid_t)atoi(argv[2]); if (argc > 3) logf = fopen(argv[3], "a");
        const double comp_high = 5.0 * GB, comp_target = 3.6 * GB, floor_bytes = 2.8 * GB, rate_high = 10000;   /* decompressions per second */
        const int cooldown = 45; vmstate a, b; readvm(&a); time_t last = 0; int hot = 0, count = 0;
        say("watching pid %d: a squeeze starts when compressed memory is above %.1f GB and more than %.0f pages/s are being decompressed", pid, comp_high / GB, rate_high);
        while (kill(pid, 0) == 0 || errno == EPERM) {
            sleep(2); readvm(&b); double rate = (double)(b.decomp - a.decomp) / 2.0; a = b;
            hot = (b.comp > comp_high && rate > rate_high) ? hot + 1 : 0;
            if (hot < 3 || time(NULL) - last < cooldown) continue;
            if (disk_free_gb() < 6.0 || swap_used_gb() > 24.0) { say("not squeezing: disk free %.1f GB, swap used %.1f GB", disk_free_gb(), swap_used_gb()); last = time(NULL); hot = 0; continue; }
            double want = b.nonwired - comp_target / 0.6; if (want > 8 * GB) want = 8 * GB; if (want < 2 * GB) want = 2 * GB;
            unsigned long long so = b.swapouts; double c0 = b.comp, u0 = b.anon;
            double pinned = squeeze(want, 0.4 * GB, comp_target, floor_bytes, 4);
            sleep(3); readvm(&b); a = b; last = time(NULL); hot = 0; count++;
            say("squeeze %d: pinned %.1f GB (%.0f pages/s were being decompressed); compressed memory %.1f -> %.1f GB of RAM, uncompressed %.1f -> %.1f GB, %.1f GB written to swap (swap used %.1f GB, disk free %.1f GB)",
                count, pinned / GB, rate, c0 / GB, b.comp / GB, u0 / GB, b.anon / GB, (b.swapouts - so) * 16384.0 / GB, swap_used_gb(), disk_free_gb());
        }
        say("pid %d is gone, %d squeeze(s) done", pid, count);
        return 0;
    }
    if (argc < 3) { printf("usage: balloon <GB> <hold seconds> [ramp seconds]   |   balloon auto <pid> [log file]\n"); return 2; }
    double gb = atof(argv[1]); int hold = atoi(argv[2]), ramp = argc > 3 ? atoi(argv[3]) : 12; vmstate v; readvm(&v);
    say("before:   compressed %.1f GB of RAM, uncompressed %.1f GB, file cache %.1f GB, free %.1f GB", v.comp / GB, v.anon / GB, v.file / GB, v.freeb / GB);
    unsigned long long so = v.swapouts; double pinned = squeeze(gb * GB, gb * GB / (ramp > 0 ? ramp : 1), 0, 0, hold); sleep(2); readvm(&v);
    say("released: compressed %.1f GB of RAM, uncompressed %.1f GB, file cache %.1f GB, free %.1f GB (pinned %.1f GB, %.1f GB written to swap)", v.comp / GB, v.anon / GB, v.file / GB, v.freeb / GB, pinned / GB, (v.swapouts - so) * 16384.0 / GB);
    return 0;
}
