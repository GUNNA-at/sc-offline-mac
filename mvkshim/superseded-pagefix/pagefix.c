/* pagefix: stop Wine's 4 KB-granular memory release from multiplying physical memory use under Rosetta.
 *
 * An x86_64 process on Apple Silicon sees 4 KB pages, but the machine's physical pages are 16 KB. Wine implements
 * VirtualFree(MEM_DECOMMIT) (and view release) as mmap(addr, len, PROT_NONE, MAP_FIXED|MAP_ANON): the range gets a NEW
 * anonymous object. When the range does not start and end on 16 KB boundaries, the partial pages at its edges are now
 * split between two objects, and the next time that piece is committed and touched the kernel allocates a whole new
 * 16 KB page for it. A program that frees and reuses memory in 4 KB steps (Star Citizen's allocator has dedicated
 * decommit threads) ends up using a multiple of its nominal footprint: 3x in tools/pagetax, about 1.5x in the game.
 *
 * Fix: for anonymous MAP_FIXED remaps, remap only the 16 KB-aligned interior. The partial head and tail pieces are
 * zeroed (only if they hold data) and re-protected in place, which is what a fresh anonymous mapping would read as.
 * Pieces the kernel does not report as anonymous memory (unmapped or file-backed) take the normal path.
 *
 * pagefix_install() rebinds the `mmap` import of Wine's ntdll.so to pagefix_mmap(). MVKSHIM_NO_PAGEFIX=1 disables it. */
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

static size_t pf_host_page = 0, pf_task_page = 0;
static int pf_enabled = 0;
unsigned long pagefix_calls, pagefix_split, pagefix_inplace, pagefix_fallback, pagefix_zeroed;

/* mincore() bits (sys/mman.h): the kernel says per task page whether it is resident, compressed, and anonymous */
#ifndef MINCORE_ANONYMOUS
#define MINCORE_ANONYMOUS 0x80
#endif
#ifndef MINCORE_PAGED_OUT
#define MINCORE_PAGED_OUT 0x20
#endif

/* Give [p, q) (at most three task pages inside one 16 KB page) the contents and protection a fresh anonymous mapping
 * would have, without remapping it. Only for memory the kernel reports as anonymous: a file-backed or unmapped piece
 * returns 0 and takes the normal path. One mincore, two mprotects and a memset: about 2 microseconds. */
static int pf_piece_inplace(uintptr_t p, uintptr_t q, int prot)
{
    char vec[8]; size_t n = (q - p) / pf_task_page;
    if (n == 0 || n > 4 || mincore((void *)p, q - p, vec) != 0) return 0;      /* ENOMEM: not mapped */
    int data = 0;
    for (size_t i = 0; i < n; i++) {
        if (!(vec[i] & MINCORE_ANONYMOUS)) return 0;                            /* file-backed: never write to it */
        if (vec[i] & (MINCORE_INCORE | MINCORE_PAGED_OUT)) data = 1;            /* has contents that must read as zero */
    }
    if (data || prot != PROT_NONE) {
        if (mprotect((void *)p, q - p, PROT_READ | PROT_WRITE) != 0) return 0;
        for (size_t i = 0; i < n; i++)
            if (vec[i] & (MINCORE_INCORE | MINCORE_PAGED_OUT)) { memset((void *)(p + i * pf_task_page), 0, pf_task_page); pagefix_zeroed++; }
        if (prot == (PROT_READ | PROT_WRITE)) return 1;
    }
    return mprotect((void *)p, q - p, prot) == 0;
}

void *pagefix_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
    pagefix_calls++;
    if (!pf_enabled || !(flags & MAP_FIXED) || !(flags & MAP_ANON) || (flags & MAP_SHARED) || fd != -1 || !len)
        return mmap(addr, len, prot, flags, fd, off);
    uintptr_t s = (uintptr_t)addr, e = s + len, hm = pf_host_page - 1;
    if (!(s & hm) && !(e & hm)) return mmap(addr, len, prot, flags, fd, off);          /* already 16 KB aligned */
    if ((s & (pf_task_page - 1)) || (e & (pf_task_page - 1))) return mmap(addr, len, prot, flags, fd, off);
    uintptr_t as = (s + hm) & ~hm, ae = e & ~hm;                                       /* aligned interior [as, ae) */
    pagefix_split++;
    if (as > ae) {                                                                     /* inside a single 16 KB page */
        if (pf_piece_inplace(s, e, prot)) { pagefix_inplace++; return addr; }
        pagefix_fallback++; return mmap(addr, len, prot, flags, fd, off);
    }
    if (s < as) {
        if (pf_piece_inplace(s, as, prot)) pagefix_inplace++;
        else { pagefix_fallback++; if (mmap((void *)s, as - s, prot, flags, fd, off) == MAP_FAILED) return MAP_FAILED; }
    }
    if (as < ae && mmap((void *)as, ae - as, prot, flags, fd, off) == MAP_FAILED) return MAP_FAILED;
    if (ae < e) {
        if (pf_piece_inplace(ae, e, prot)) pagefix_inplace++;
        else { pagefix_fallback++; if (mmap((void *)ae, e - ae, prot, flags, fd, off) == MAP_FAILED) return MAP_FAILED; }
    }
    return addr;
}

/* decide whether the fix applies: only when the task's pages are smaller than the machine's */
int pagefix_init(void)
{
    vm_size_t hps = 0; host_page_size(mach_host_self(), &hps);
    pf_task_page = (size_t)getpagesize(); pf_host_page = (size_t)hps;
    if (pf_host_page < pf_task_page) pf_host_page = pf_task_page;
    pf_enabled = pf_host_page > pf_task_page && !getenv("MVKSHIM_NO_PAGEFIX");
    return pf_enabled;
}

/* point one image's import of `sym` (with leading underscore) at `repl`; returns the number of slots changed */
static int pf_rebind_image(const struct mach_header_64 *mh, intptr_t slide, const char *sym, void *repl)
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
                uintptr_t pg = (uintptr_t)&slots[k] & ~(uintptr_t)(pf_task_page - 1);
                mprotect((void *)pg, pf_task_page, PROT_READ | PROT_WRITE);     /* __DATA_CONST is read-only after binding */
                slots[k] = repl; changed++;
            }
        }
    }
    return changed;
}

/* returns the number of import slots redirected (0 = nothing to do or not applicable) */
int pagefix_install(const char *image_suffix)
{
    if (!pagefix_init()) return 0;
    int changed = 0; size_t sl = strlen(image_suffix);
    for (uint32_t i = 0; i < _dyld_image_count(); i++) {
        const char *name = _dyld_get_image_name(i); if (!name) continue;
        size_t nl = strlen(name);
        if (nl < sl || strcmp(name + nl - sl, image_suffix)) continue;
        const struct mach_header *mh = _dyld_get_image_header(i);
        if (!mh || mh->magic != MH_MAGIC_64) continue;
        changed += pf_rebind_image((const struct mach_header_64 *)mh, _dyld_get_image_vmaddr_slide(i), "_mmap", (void *)pagefix_mmap);
    }
    return changed;
}
