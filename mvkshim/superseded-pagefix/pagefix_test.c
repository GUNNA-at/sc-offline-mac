/* Tests for pagefix.c. Build as x86_64 and run under Rosetta: clang -arch x86_64 -O1 -o pagefix_test pagefix_test.c pagefix.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <mach/mach.h>
extern void *pagefix_mmap(void *, size_t, int, int, int, off_t);
extern int pagefix_init(void);
extern unsigned long pagefix_calls, pagefix_split, pagefix_inplace, pagefix_fallback, pagefix_zeroed;
static int fails = 0;
#define CHECK(c, msg) do { if (!(c)) { printf("  FAIL: %s\n", msg); fails++; } else printf("  ok: %s\n", msg); } while (0)
static int all(const unsigned char *p, size_t n, unsigned char v) { for (size_t i = 0; i < n; i++) if (p[i] != v) return 0; return 1; }
static double freegb(void) { vm_statistics64_data_t vs; mach_msg_type_number_t n = HOST_VM_INFO64_COUNT; vm_size_t hps; host_page_size(mach_host_self(), &hps); host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t)&vs, &n); return (double)vs.free_count * hps / 1073741824.0; }
static double footgb(void) { task_vm_info_data_t ti; mach_msg_type_number_t c = TASK_VM_INFO_COUNT; task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&ti, &c); return ti.phys_footprint / 1073741824.0; }
int main(int argc, char **argv) {
    size_t K = 4096; int on = pagefix_init();
    printf("task page %d, fix enabled: %d\n", getpagesize(), on);
    int F = MAP_FIXED | MAP_PRIVATE | MAP_ANON;

    printf("T1 single 4 KB pages inside 16 KB pages\n");
    unsigned char *b = mmap(NULL, 64 * K, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0); memset(b, 0xAB, 64 * K);
    int okr = 1; for (int i = 1; i < 64; i += 2) if (pagefix_mmap(b + i * K, K, PROT_NONE, F, -1, 0) != b + i * K) okr = 0;
    CHECK(okr, "every call returns its address"); int okc = 1, okn = 1;
    for (int i = 1; i < 64; i += 2) { mprotect(b + i * K, K, PROT_READ | PROT_WRITE); if (!all(b + i * K, K, 0)) okc = 0; }
    for (int i = 0; i < 64; i += 2) if (!all(b + i * K, K, 0xAB)) okn = 0;
    CHECK(okc, "released pages read as zero after re-commit"); CHECK(okn, "neighbouring pages keep their data");

    printf("T2 unaligned range: 12 KB head piece, 32 KB interior, 4 KB tail piece\n");
    memset(b, 0xCD, 64 * K);
    CHECK(pagefix_mmap(b + 1 * K, 12 * K, PROT_NONE, F, -1, 0) == b + 1 * K, "call returns the address");
    mprotect(b + K, 12 * K, PROT_READ | PROT_WRITE);
    CHECK(all(b + K, 12 * K, 0), "whole range zero after re-commit"); CHECK(all(b, K, 0xCD) && all(b + 13 * K, 3 * K, 0xCD), "bytes before and after untouched");

    printf("T3 protection is applied\n");
    memset(b, 0x11, 64 * K); pagefix_mmap(b + 5 * K, 2 * K, PROT_READ, F, -1, 0);
    CHECK(all(b + 5 * K, 2 * K, 0), "PROT_READ remap reads zero"); CHECK(mprotect(b + 5 * K, 2 * K, PROT_READ | PROT_WRITE) == 0, "can be made writable again");

    printf("T4 file-backed memory is never written in place\n");
    char path[] = "/tmp/pagefix_test_XXXXXX"; int fd = mkstemp(path); unsigned char buf[16384]; memset(buf, 0x77, sizeof buf); write(fd, buf, sizeof buf);
    unsigned char *fm = mmap(b + 16 * K, 4 * K, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_SHARED, fd, 0);
    CHECK(fm == b + 16 * K, "shared file mapping placed");
    pagefix_mmap(b + 17 * K, 2 * K, PROT_NONE, F, -1, 0);       /* decommit a piece of the file mapping */
    msync(fm, 4 * K, MS_SYNC); unsigned char rb[16384]; pread(fd, rb, sizeof rb, 0);
    CHECK(all(rb, sizeof rb, 0x77), "file contents untouched"); CHECK(pagefix_fallback > 0, "took the normal path for it");
    close(fd); unlink(path);

    printf("T5 unmapped range falls back to a real mapping\n");
    munmap(b + 32 * K, 4 * K);
    CHECK(pagefix_mmap(b + 33 * K, K, PROT_READ | PROT_WRITE, F, -1, 0) == b + 33 * K, "mapping created"); b[33 * K] = 5; CHECK(b[33 * K] == 5, "and usable");

    if (argc > 1) {
        size_t N = (size_t)atoi(argv[1]) << 20; printf("T6 memory cost of a decommit/recommit cycle over %zu MB\n", N >> 20);
        double f0 = freegb(); unsigned char *r = mmap(NULL, N, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        for (size_t o = 0; o < N; o += K) r[o] = 1;
        for (size_t o = K; o < N; o += 2 * K) pagefix_mmap(r + o, K, PROT_NONE, F, -1, 0);
        for (size_t o = K; o < N; o += 2 * K) { mprotect(r + o, K, PROT_READ | PROT_WRITE); r[o] = 2; }
        printf("  footprint %.2f GB, system free change %+.2f GB (without the fix this costs 3x the footprint)\n", footgb(), freegb() - f0);
    }
    printf("calls %lu, split %lu, in place %lu, fallback %lu, pages zeroed %lu\n%s\n", pagefix_calls, pagefix_split, pagefix_inplace, pagefix_fallback, pagefix_zeroed, fails ? "FAILED" : "ALL TESTS PASSED");
    return fails != 0;
}
