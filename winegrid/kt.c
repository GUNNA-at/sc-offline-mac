// Kernel behaviour test (run as x86_64 under Rosetta: 4 KB task pages on 16 KB machine pages).
// A: Wine's pattern - reserve PROT_NONE, then commit (mprotect) and touch one 4 KB page at a time.
// B: the same pattern on a reservation that is backed by ONE named memory object from the start.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
static double gb(double b) { return b / 1073741824.0; }
static double now(void) { struct timeval tv; gettimeofday(&tv, 0); return tv.tv_sec + tv.tv_usec / 1e6; }
static double real_anon(void) {
    vm_statistics64_data_t vs; mach_msg_type_number_t n = HOST_VM_INFO64_COUNT; vm_size_t hps; host_page_size(mach_host_self(), &hps);
    host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t)&vs, &n);
    return ((double)vs.internal_page_count + (double)vs.total_uncompressed_pages_in_compressor) * hps;
}
static double base_real;
static void report(const char *what) {
    task_vm_info_data_t ti; mach_msg_type_number_t c = TASK_VM_INFO_COUNT;
    task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&ti, &c);
    printf("  %-52s ledger footprint %5.2f GB | real anonymous memory, system-wide change %+6.2f GB\n", what, gb(ti.phys_footprint), gb(real_anon() - base_real));
    fflush(stdout);
}
static long count_entries(mach_vm_address_t lo, mach_vm_address_t hi) {
    long n = 0; mach_vm_address_t a = lo;
    while (a < hi) {
        mach_vm_size_t sz = 0; natural_t depth = 0; vm_region_submap_info_data_64_t info; mach_msg_type_number_t cnt = VM_REGION_SUBMAP_INFO_COUNT_64;
        if (mach_vm_region_recurse(mach_task_self(), &a, &sz, &depth, (vm_region_recurse_info_t)&info, &cnt) != KERN_SUCCESS) break;
        if (a >= hi) break; n++; a += sz;
    }
    return n;
}
static void *reserve_named(size_t size, int *ok_exec) {
    memory_object_size_t sz = size; mach_port_t port = MACH_PORT_NULL;
    kern_return_t kr = mach_make_memory_entry_64(mach_task_self(), &sz, 0, MAP_MEM_NAMED_CREATE | VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE, &port, MACH_PORT_NULL);
    int exec = 1;
    if (kr != KERN_SUCCESS) { exec = 0; sz = size; kr = mach_make_memory_entry_64(mach_task_self(), &sz, 0, MAP_MEM_NAMED_CREATE | VM_PROT_READ | VM_PROT_WRITE, &port, MACH_PORT_NULL); }
    if (kr != KERN_SUCCESS) { printf("make_memory_entry failed %d\n", kr); return NULL; }
    mach_vm_address_t addr = 0;
    kr = mach_vm_map(mach_task_self(), &addr, size, 0, VM_FLAGS_ANYWHERE, port, 0, FALSE, VM_PROT_NONE, exec ? VM_PROT_ALL : (VM_PROT_READ | VM_PROT_WRITE), VM_INHERIT_SHARE);
    if (kr != KERN_SUCCESS && exec) { exec = 0; kr = mach_vm_map(mach_task_self(), &addr, size, 0, VM_FLAGS_ANYWHERE, port, 0, FALSE, VM_PROT_NONE, VM_PROT_READ | VM_PROT_WRITE, VM_INHERIT_SHARE); }
    mach_port_deallocate(mach_task_self(), port);
    if (kr != KERN_SUCCESS) { printf("mach_vm_map failed %d\n", kr); return NULL; }
    if (ok_exec) *ok_exec = exec;
    return (void *)addr;
}
int main(int argc, char **argv) {
    int variant = argc > 1 ? argv[1][0] : 'A'; size_t N = (size_t)(argc > 2 ? atoi(argv[2]) : 1024) << 20; size_t pg = 4096;
    printf("variant %c, task page size %d, reservation %.2f GB\n", variant, getpagesize(), gb(N));
    base_real = real_anon();
    char *base; int exec = -1;
    if (variant == 'A') base = mmap(NULL, N, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    else base = reserve_named(N, &exec);
    if (!base || base == MAP_FAILED) return 1;
    printf("  reserved at %p%s\n", base, variant == 'A' ? "" : exec ? " (execute allowed in max protection)" : " (no execute in max protection)");
    double t0 = now();
    for (size_t o = 0; o < N; o += pg) { if (mprotect(base + o, pg, PROT_READ | PROT_WRITE)) { perror("mprotect"); return 1; } base[o] = 1; }
    double t1 = now();
    printf("  commit+touch of %zu pages one by one: %.2f s (%.2f us per page)\n", N / pg, t1 - t0, (t1 - t0) * 1e6 / (N / pg));
    report("after committing and touching every 4 KB page");
    printf("  map entries in the reservation: %ld\n", count_entries((mach_vm_address_t)base, (mach_vm_address_t)base + N));
    unsigned char vec[8]; mincore(base, 8 * pg, (char *)vec); printf("  mincore of first pages: %02x %02x %02x %02x\n", vec[0], vec[1], vec[2], vec[3]);
    if (variant != 'A') {
        // in-place decommit of the second half: zero what is resident, MADV_FREE, PROT_NONE
        size_t half = N / 2; char *p = base + half;
        for (size_t o = 0; o < half; o += pg) p[o] = 0x5a;          // leave non-zero data behind on purpose
        double t2 = now();
        if (argc > 3 && argv[3][0] == 'k') { /* kill only: no zeroing, to see whether the kernel really drops the pages */ }
        else memset(p, 0, half);
        int r = madvise(p, half, MADV_FREE); int e = errno;
        mprotect(p, half, PROT_NONE);
        double t3 = now();
        printf("  in-place decommit of %.2f GB: madvise=%d errno=%d, %.3f s\n", gb(half), r, r ? e : 0, t3 - t2);
        report("right after in-place decommit of the second half");
        if (argc > 3 && argv[3][0] == 'k') {
            // squeeze memory so the kernel reclaims the killed pages, then look at them
            size_t hog = (size_t)(argc > 4 ? atoi(argv[4]) : 8) << 30; char *h = mmap(NULL, hog, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
            for (size_t o = 0; o < hog; o += 16384) h[o] = (char)(o >> 14 | 1);
            munmap(h, hog);
        } else sleep(2);
        mprotect(p, half, PROT_READ | PROT_WRITE);
        size_t zero = 0, stale = 0; for (size_t o = 0; o < half; o += pg) { if (p[o] == 0) zero++; else stale++; }
        printf("  recommitted second half: %zu pages read zero, %zu pages still hold old data\n", zero, stale);
        report("after recommitting and reading the second half");
        // execute permission
        int rx = mprotect(base, pg, PROT_READ | PROT_WRITE | PROT_EXEC); printf("  mprotect +exec on a page: %s\n", rx ? strerror(errno) : "ok");
        mprotect(base, pg, PROT_READ | PROT_WRITE);
    }
    // fork + exec cost with this map
    double t4 = now(); pid_t pid = fork();
    if (pid == 0) { execl("/usr/bin/true", "true", (char *)0); _exit(1); }
    int st; waitpid(pid, &st, 0); double t5 = now();
    printf("  fork+exec+wait: %.3f s (exit %d)\n", t5 - t4, WEXITSTATUS(st));
    volatile char sink = 0; for (size_t o = 0; o < N / 2; o += pg) sink += base[o];
    report("parent still fine after fork");
    return 0;
}
