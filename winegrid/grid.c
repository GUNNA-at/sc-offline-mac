/* winegrid: make Wine's memory reservations use the Mac's 16 KB physical pages efficiently under Rosetta.
 *
 * The problem. An x86_64 process on Apple Silicon sees 4 KB pages, the machine's physical pages are 16 KB. Windows
 * programs reserve address space and then commit it piecemeal; Wine implements "reserve" as an anonymous PROT_NONE
 * mmap and "commit" as mprotect. The kernel gives an anonymous mapping its memory object lazily, on the first page
 * fault, sized to the map entry that faults. mprotect on a not-yet-touched reservation first splits the map entry, so
 * every separately committed piece ends up with its OWN memory object, and every object rounds up to whole 16 KB
 * pages. A program that commits its heap one 4 KB page at a time (Star Citizen does this about a million times) pays
 * 16 KB of real memory per 4 KB page: measured 4.00 GB of RAM for 1.00 GB of data (tools: winegrid/kt.c).
 *
 * The fix. Every anonymous PROT_NONE reservation Wine makes is mapped from named memory objects (one per 128 MB)
 * instead, so the object exists before any commit: all pieces share it, neighbouring 4 KB pages share one 16 KB page
 * and the kernel can merge adjacent map entries again (1.00 GB of RAM for 1.00 GB of data, one map entry instead of
 * 262144). Wine's decommit (it maps fresh PROT_NONE memory over the range) is done in place for these reservations:
 * the range is zeroed where it holds data, handed back to the kernel in whole 16 KB pages (madvise MADV_FREE) and
 * protected, so the object stays shared and nothing is leaked behind it.
 *
 * How it gets in. Wine's ntdll.so is given a weak dependency on this library (winegrid/patch-ntdll.py), so it is
 * loaded at process start, before the first reservation. It redirects the mmap and munmap imports of ntdll.so only.
 * It does nothing unless the environment has WINEGRID=1 (set per bottle), and nothing on a process whose pages
 * already match the machine's. WINEGRID_ONLY=<text> limits it to processes whose command line contains <text>.
 * WINEGRID_NO_INPLACE=1 turns the in-place decommit off (decommit then maps a fresh object). Log: WINEGRID_LOG or
 * /private/tmp/winegrid.log. */
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/time.h>
#include <os/lock.h>
#include <crt_externs.h>
#include <dlfcn.h>
#include <pthread.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#define GRID_CHUNK ((uintptr_t)128 << 20)      /* one memory object per 128 MB of reservation, on absolute boundaries */
#define GRID_TAG 240                           /* VM_MEMORY_APPLICATION_SPECIFIC_1: shows up as its own row in footprint */
#ifndef MINCORE_PAGED_OUT
#define MINCORE_PAGED_OUT 0x20
#endif

static size_t g_tp = 4096, g_hp = 16384;       /* task page, host (physical) page */
static int g_on, g_inplace = 1, g_slots;
static os_unfair_lock g_lock = OS_UNFAIR_LOCK_INIT;

typedef struct { uintptr_t s, e; } rng;
static rng *g_tr; static size_t g_n, g_cap;    /* sorted, disjoint ranges that are backed by our named objects */

static unsigned long c_conv, c_conv_fail, c_dec, c_dec_fresh, c_dec_fallback, c_zero_task, c_zero_host, c_untrack, c_calls;
static unsigned long long b_conv, b_dec;
static time_t g_last_stats; static unsigned long g_last_sig;

/* ---- log ---- */
static void glog(const char *fmt, ...)
{
    const char *path = getenv("WINEGRID_LOG"); if (!path || !*path) path = "/private/tmp/winegrid.log";
    char buf[700]; struct timeval tv; gettimeofday(&tv, NULL); struct tm tm; time_t t = tv.tv_sec; localtime_r(&t, &tm);
    int n = snprintf(buf, sizeof buf, "%02d:%02d:%02d [%d] ", tm.tm_hour, tm.tm_min, tm.tm_sec, (int)getpid());
    va_list ap; va_start(ap, fmt); n += vsnprintf(buf + n, sizeof buf - n - 2, fmt, ap); va_end(ap);
    if (n > (int)sizeof buf - 2) n = sizeof buf - 2;
    buf[n++] = '\n';
    int fd = open(path, O_WRONLY | O_APPEND | O_CREAT, 0644); if (fd >= 0) { (void)!write(fd, buf, n); close(fd); }
}
static void stats(int force)
{
    time_t now = time(NULL); unsigned long sig = c_conv + c_dec + c_untrack + c_dec_fresh;
    if (!force && (now - g_last_stats < 60 || sig == g_last_sig)) return;
    g_last_stats = now; g_last_sig = sig;
    glog("reservations given an object: %lu (%.1f GB, %lu failed) | decommits in place: %lu (%.2f GB; zeroed %lu edge pages and %lu whole 16K pages; %lu whole-chunk, %lu fell back) | untracked: %lu | tracked spans: %zu",
         c_conv, b_conv / 1073741824.0, c_conv_fail, c_dec, b_dec / 1073741824.0, c_zero_task, c_zero_host, c_dec_fresh, c_dec_fallback, c_untrack, g_n);
}

/* ---- tracked ranges ---- */
static size_t tr_first(uintptr_t a)            /* first index whose range ends after a */
{
    size_t lo = 0, hi = g_n;
    while (lo < hi) { size_t m = lo + (hi - lo) / 2; if (g_tr[m].e > a) hi = m; else lo = m + 1; }
    return lo;
}
static int tr_room(void)
{
    if (g_n + 1 <= g_cap) return 1;
    size_t nc = g_cap ? g_cap * 2 : 4096; rng *p = realloc(g_tr, nc * sizeof(rng)); if (!p) return 0;
    g_tr = p; g_cap = nc; return 1;
}
static void tr_add(uintptr_t s, uintptr_t e)   /* [s, e) must not overlap anything tracked */
{
    size_t i = tr_first(s);
    int ml = i > 0 && g_tr[i - 1].e == s, mr = i < g_n && g_tr[i].s == e;
    if (ml && mr) { g_tr[i - 1].e = g_tr[i].e; memmove(&g_tr[i], &g_tr[i + 1], (g_n - i - 1) * sizeof(rng)); g_n--; }
    else if (ml) g_tr[i - 1].e = e;
    else if (mr) g_tr[i].s = s;
    else { if (!tr_room()) return; memmove(&g_tr[i + 1], &g_tr[i], (g_n - i) * sizeof(rng)); g_tr[i].s = s; g_tr[i].e = e; g_n++; }
}
static void tr_remove(uintptr_t s, uintptr_t e)
{
    size_t i = tr_first(s);
    while (i < g_n && g_tr[i].s < e) {
        rng r = g_tr[i]; c_untrack++;
        if (r.s < s && r.e > e) {                                   /* hole in the middle: split */
            if (!tr_room()) { g_tr[i].e = s; return; }
            memmove(&g_tr[i + 1], &g_tr[i], (g_n - i) * sizeof(rng)); g_n++;
            g_tr[i].s = r.s; g_tr[i].e = s; g_tr[i + 1].s = e; g_tr[i + 1].e = r.e; return;
        }
        if (r.s < s) { g_tr[i].e = s; i++; }
        else if (r.e > e) { g_tr[i].s = e; return; }
        else { memmove(&g_tr[i], &g_tr[i + 1], (g_n - i - 1) * sizeof(rng)); g_n--; }
    }
}

/* ---- named objects ---- */
static int map_named(uintptr_t s, uintptr_t e)  /* map a fresh, empty named object over [s, e), inaccessible */
{
    memory_object_size_t sz = e - s; mach_port_t port = MACH_PORT_NULL;
    if (mach_make_memory_entry_64(mach_task_self(), &sz, 0, MAP_MEM_NAMED_CREATE | VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE, &port, MACH_PORT_NULL) != KERN_SUCCESS) return 0;
    mach_vm_address_t a = s;
    kern_return_t kr = mach_vm_map(mach_task_self(), &a, e - s, 0, VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE | VM_MAKE_TAG(GRID_TAG), port, 0, FALSE,
                                   VM_PROT_NONE, VM_PROT_ALL, VM_INHERIT_SHARE);
    mach_port_deallocate(mach_task_self(), port);
    return kr == KERN_SUCCESS;
}
/* give [s, e) named objects, one per chunk. have_mapping: the range already is a valid PROT_NONE anonymous mapping, so a
 * failure just leaves it as it is; otherwise fall back to the normal anonymous mapping. Returns 0 only if a piece could
 * not be mapped at all. */
static int convert(uintptr_t s, uintptr_t e, int have_mapping)
{
    int ok = 1;
    for (uintptr_t p = s; p < e; ) {
        uintptr_t q = (p & ~(GRID_CHUNK - 1)) + GRID_CHUNK; if (q > e || q < p) q = e;
        if (map_named(p, q)) { tr_add(p, q); c_conv++; b_conv += q - p; }
        else { c_conv_fail++; if (!have_mapping && mmap((void *)p, q - p, PROT_NONE, MAP_FIXED | MAP_PRIVATE | MAP_ANON, -1, 0) == MAP_FAILED) ok = 0; }
        p = q;
    }
    return ok;
}

/* ---- decommit in place ---- */
static void zero_edge(uintptr_t p, uintptr_t q)                    /* task pages that share their 16 KB page with others */
{
    char vec[16];
    while (p < q) {
        size_t n = (q - p) / g_tp; if (n > sizeof vec) n = sizeof vec;
        if (mincore((void *)p, n * g_tp, vec) != 0) memset(vec, MINCORE_INCORE, n);
        for (size_t i = 0; i < n; i++)
            if (vec[i] & (MINCORE_INCORE | MINCORE_PAGED_OUT)) { memset((void *)(p + i * g_tp), 0, g_tp); c_zero_task++; }
        p += n * g_tp;
    }
}
static int zero_resident(uintptr_t as, uintptr_t ae)                /* whole 16 KB pages: zero the ones in memory */
{
    char vec[4096]; size_t per = g_hp / g_tp;
    for (uintptr_t c = as; c < ae; ) {
        size_t n = (ae - c) / g_tp; if (n > sizeof vec) n = sizeof vec;
        if (mincore((void *)c, n * g_tp, vec) != 0) return 0;
        for (size_t i = 0; i < n; i += per) {
            int in = 0; for (size_t k = 0; k < per; k++) in |= vec[i + k] & MINCORE_INCORE;
            if (in) { memset((void *)(c + i * g_tp), 0, g_hp); c_zero_host++; }
        }
        c += n * g_tp;
    }
    return 1;
}
static int decommit_piece(uintptr_t p, uintptr_t q)
{
    if (mprotect((void *)p, q - p, PROT_READ | PROT_WRITE) != 0) return 0;
    uintptr_t hm = g_hp - 1, as = (p + hm) & ~hm, ae = q & ~hm;
    if (as >= ae) zero_edge(p, q);
    else {
        if (p < as) zero_edge(p, as);
        if (ae < q) zero_edge(ae, q);
        if (!zero_resident(as, ae)) return 0;
        madvise((void *)as, ae - as, MADV_FREE);                    /* pages in memory are zero and droppable, compressed ones are dropped */
    }
    return mprotect((void *)p, q - p, PROT_NONE) == 0;
}
static int decommit(uintptr_t s, uintptr_t e)                       /* [s, e) lies inside one tracked range */
{
    if (!g_inplace) return 0;
    for (uintptr_t p = s; p < e; ) {
        uintptr_t cb = p & ~(GRID_CHUNK - 1), q = cb + GRID_CHUNK; if (q > e || q < p) q = e;
        if (p == cb && q == cb + GRID_CHUNK && map_named(p, q)) c_dec_fresh++;      /* a whole chunk: simply a new object */
        else if (!decommit_piece(p, q)) return 0;
        p = q;
    }
    c_dec++; b_dec += e - s;
    return 1;
}

/* ---- the two redirected calls ---- */
void *winegrid_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
    if (!g_on) return mmap(addr, len, prot, flags, fd, off);
    int anon = (flags & MAP_ANON) && !(flags & MAP_SHARED) && fd == -1;
    int reserve = anon && prot == PROT_NONE && !(flags & MAP_NORESERVE);
    uintptr_t tm = g_tp - 1;
    if ((++c_calls & 255) == 0) { os_unfair_lock_lock(&g_lock); stats(0); os_unfair_lock_unlock(&g_lock); }

    if (!(flags & MAP_FIXED)) {                                     /* the kernel picks the place: it was free, so untracked */
        void *r = mmap(addr, len, prot, flags, fd, off);
        if (r != MAP_FAILED && reserve && len && !((uintptr_t)r & tm) && !(len & tm)) {
            os_unfair_lock_lock(&g_lock);
            tr_remove((uintptr_t)r, (uintptr_t)r + len);
            convert((uintptr_t)r, (uintptr_t)r + len, 1);
            if (len >= ((size_t)1 << 30)) glog("reservation of %.1f GB at %p now has shared objects", len / 1073741824.0, r);
            os_unfair_lock_unlock(&g_lock);
        }
        return r;
    }

    uintptr_t s = (uintptr_t)addr, e = s + len;
    if (reserve && len && !(s & tm) && !(len & tm) && e > s) {      /* a new reservation, or Wine's decommit */
        int ok = 1;
        os_unfair_lock_lock(&g_lock);
        for (uintptr_t cur = s; cur < e; ) {
            size_t i = tr_first(cur); uintptr_t q;
            if (i < g_n && g_tr[i].s <= cur) {                      /* ours already: release the contents in place */
                q = g_tr[i].e < e ? g_tr[i].e : e;
                if (!decommit(cur, q)) { c_dec_fallback++; tr_remove(cur, q); if (!convert(cur, q, 0)) ok = 0; }
            } else {
                q = (i < g_n && g_tr[i].s < e) ? g_tr[i].s : e;
                if (!convert(cur, q, 0)) ok = 0;
                if (q - cur >= ((uintptr_t)1 << 30)) glog("reservation of %.1f GB at %p now has shared objects", (q - cur) / 1073741824.0, (void *)cur);
            }
            cur = q;
        }
        os_unfair_lock_unlock(&g_lock);
        if (!ok) { errno = ENOMEM; return MAP_FAILED; }
        return addr;
    }

    /* anything else placed over the range (committed memory, a file, a release back to Wine's reserve) replaces us */
    os_unfair_lock_lock(&g_lock);
    if (len) tr_remove(s & ~tm, (e + tm) & ~tm);
    void *r = mmap(addr, len, prot, flags, fd, off);
    os_unfair_lock_unlock(&g_lock);
    return r;
}

int winegrid_munmap(void *addr, size_t len)
{
    if (!g_on) return munmap(addr, len);
    uintptr_t tm = g_tp - 1, s = (uintptr_t)addr;
    os_unfair_lock_lock(&g_lock);
    if (len) tr_remove(s & ~tm, (s + len + tm) & ~tm);
    int r = munmap(addr, len);
    os_unfair_lock_unlock(&g_lock);
    return r;
}

/* ---- install: point ntdll.so's imports at the two functions above ---- */
static int rebind(const struct mach_header_64 *mh, intptr_t slide, const char *sym, void *repl)
{
    const struct load_command *lc = (const struct load_command *)(mh + 1);
    const struct segment_command_64 *linkedit = NULL; const struct symtab_command *st = NULL; const struct dysymtab_command *dst = NULL;
    for (uint32_t i = 0; i < mh->ncmds; i++, lc = (const struct load_command *)((const char *)lc + lc->cmdsize)) {
        if (lc->cmd == LC_SEGMENT_64) { const struct segment_command_64 *sg = (const void *)lc; if (!strcmp(sg->segname, SEG_LINKEDIT)) linkedit = sg; }
        else if (lc->cmd == LC_SYMTAB) st = (const void *)lc;
        else if (lc->cmd == LC_DYSYMTAB) dst = (const void *)lc;
    }
    if (!linkedit || !st || !dst || !dst->nindirectsyms) return 0;
    uintptr_t base = (uintptr_t)slide + linkedit->vmaddr - linkedit->fileoff;
    const struct nlist_64 *syms = (const void *)(base + st->symoff); const char *strtab = (const char *)(base + st->stroff);
    const uint32_t *indirect = (const uint32_t *)(base + dst->indirectsymoff);
    int changed = 0;
    lc = (const struct load_command *)(mh + 1);
    for (uint32_t i = 0; i < mh->ncmds; i++, lc = (const struct load_command *)((const char *)lc + lc->cmdsize)) {
        if (lc->cmd != LC_SEGMENT_64) continue;
        const struct segment_command_64 *sg = (const void *)lc;
        if (strcmp(sg->segname, "__DATA") && strcmp(sg->segname, "__DATA_CONST")) continue;
        const struct section_64 *sec = (const struct section_64 *)(sg + 1);
        for (uint32_t j = 0; j < sg->nsects; j++, sec++) {
            uint32_t type = sec->flags & SECTION_TYPE;
            if (type != S_LAZY_SYMBOL_POINTERS && type != S_NON_LAZY_SYMBOL_POINTERS) continue;
            const uint32_t *idx = indirect + sec->reserved1; void **slots = (void **)((uintptr_t)slide + sec->addr);
            for (uint64_t k = 0; k < sec->size / sizeof(void *); k++) {
                uint32_t si = idx[k];
                if (si & (INDIRECT_SYMBOL_ABS | INDIRECT_SYMBOL_LOCAL)) continue;
                if (si >= st->nsyms || strcmp(strtab + syms[si].n_un.n_strx, sym)) continue;
                uintptr_t pg = (uintptr_t)&slots[k] & ~(uintptr_t)(g_tp - 1);
                mprotect((void *)pg, g_tp, PROT_READ | PROT_WRITE);     /* __DATA_CONST is read-only after binding */
                slots[k] = repl; changed++;
            }
        }
    }
    return changed;
}
static void on_image(const struct mach_header *mh, intptr_t slide)
{
    Dl_info di; static const char want[] = "/ntdll.so";
    if (!mh || mh->magic != MH_MAGIC_64 || !dladdr(mh, &di) || !di.dli_fname) return;
    size_t nl = strlen(di.dli_fname), wl = sizeof want - 1;
    if (nl < wl || strcmp(di.dli_fname + nl - wl, want)) return;
    int a = rebind((const struct mach_header_64 *)mh, slide, "_mmap", (void *)winegrid_mmap);
    int b = rebind((const struct mach_header_64 *)mh, slide, "_munmap", (void *)winegrid_munmap);
    g_slots = a + b;
    char ***av = _NSGetArgv(); int *ac = _NSGetArgc();
    glog("on: %d mmap and %d munmap import(s) of ntdll.so redirected (%s)", a, b, av && ac && *ac > 1 && (*av)[1] ? (*av)[1] : "?");
}
static void after_fork_child(void) { g_lock = OS_UNFAIR_LOCK_INIT; }

static int grid_setup(void)                                         /* page sizes; returns 1 when the fix applies here */
{
    vm_size_t hps = 0; host_page_size(mach_host_self(), &hps);
    g_tp = (size_t)getpagesize(); g_hp = (size_t)hps;
    return g_hp > g_tp && g_hp % g_tp == 0 && g_hp / g_tp <= 16;
}
#ifndef GRID_TEST
__attribute__((constructor)) static void grid_init(void)
{
    const char *on = getenv("WINEGRID"); if (!on || !*on || *on == '0') return;
    if (!grid_setup()) return;
    const char *only = getenv("WINEGRID_ONLY");
    if (only && *only) {
        char ***av = _NSGetArgv(); int *ac = _NSGetArgc(); int hit = 0;
        for (int i = 0; av && ac && i < *ac; i++) if ((*av)[i] && strstr((*av)[i], only)) hit = 1;
        if (!hit) return;
    }
    if (getenv("WINEGRID_NO_INPLACE")) g_inplace = 0;
    g_on = 1; g_last_stats = time(NULL);
    pthread_atfork(NULL, NULL, after_fork_child);
    _dyld_register_func_for_add_image(on_image);
}
#else
int winegrid_test_enable(int inplace) { if (!grid_setup()) return 0; g_inplace = inplace; g_on = 1; return 1; }
void winegrid_test_stats(void) { stats(1); }
size_t winegrid_test_spans(void) { return g_n; }
#endif
