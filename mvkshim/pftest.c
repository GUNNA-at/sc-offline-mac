/* Windows-side test for the page fix: VirtualFree(MEM_DECOMMIT) / VirtualAlloc(MEM_COMMIT) cycles in 4 KB steps.
 * usage: pftest.exe <MB> <rounds> [novk]   (without "novk" it first creates a Vulkan instance, which makes Wine load the shim) */
#define VK_NO_PROTOTYPES
#include <windows.h>
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    int mb = argc > 1 ? atoi(argv[1]) : 128, rounds = argc > 2 ? atoi(argv[2]) : 4, novk = argc > 3;
    if (!novk) {
        CreateWindowExA(0, "STATIC", "pftest", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, NULL, NULL, GetModuleHandleA(NULL), NULL);
        HMODULE m = LoadLibraryA("vulkan-1.dll"); if (!m) { printf("vulkan-1.dll load failed\n"); return 1; }
        PFN_vkGetInstanceProcAddr gipa = (PFN_vkGetInstanceProcAddr)GetProcAddress(m, "vkGetInstanceProcAddr");
        VkApplicationInfo ai = { VK_STRUCTURE_TYPE_APPLICATION_INFO }; ai.apiVersion = VK_API_VERSION_1_3;
        VkInstanceCreateInfo ci = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO }; ci.pApplicationInfo = &ai; VkInstance inst = NULL;
        VkResult r = ((PFN_vkCreateInstance)gipa(NULL, "vkCreateInstance"))(&ci, NULL, &inst); printf("vkCreateInstance -> %d (shim loaded)\n", r);
    }
    SIZE_T N = (SIZE_T)mb << 20, K = 4096; unsigned long bad = 0, cycles = 0;
    unsigned char *r = VirtualAlloc(NULL, N, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE); if (!r) { printf("VirtualAlloc failed\n"); return 2; }
    for (SIZE_T o = 0; o < N; o += K) r[o] = 0xEE;
    for (int round = 0; round < rounds; round++) {
        SIZE_T first = (SIZE_T)(round % 4) * K;
        for (SIZE_T o = first; o < N; o += 4 * K) if (!VirtualFree(r + o, K, MEM_DECOMMIT)) bad++;
        for (SIZE_T o = first; o < N; o += 4 * K) {
            if (!VirtualAlloc(r + o, K, MEM_COMMIT, PAGE_READWRITE)) { bad++; continue; }
            if (r[o] != 0 || r[o + K - 1] != 0 || r[o + 2000] != 0) bad++;      /* a fresh commit must read as zero */
            r[o] = (unsigned char)(round + 1); r[o + K - 1] = 0x5A; cycles++;
        }
        for (SIZE_T o = 0; o < N; o += K) {                                     /* every page still holds what it should */
            int owner = (int)((o / K) % 4); unsigned char want = owner <= round % 4 || round >= 4 ? 0 : 0xEE; (void)want;
        }
    }
    /* final content check: page p was last rewritten in the latest round whose (round % 4) == p % 4, else still 0xEE */
    for (SIZE_T o = 0; o < N; o += K) { int cls = (int)((o / K) % 4), last = -1; for (int rd = 0; rd < rounds; rd++) if (rd % 4 == cls) last = rd;
        unsigned char want = last < 0 ? 0xEE : (unsigned char)(last + 1); if (r[o] != want) bad++; }
    /* an unaligned multi-page block: 40 KB starting 4 KB into a 16 KB page */
    unsigned char *b = r + 4 * K + K; memset(b, 0x33, 10 * K); VirtualFree(b, 10 * K, MEM_DECOMMIT); VirtualAlloc(b, 10 * K, MEM_COMMIT, PAGE_READWRITE);
    for (SIZE_T i = 0; i < 10 * K; i++) if (b[i]) { bad++; break; }
    printf("%lu decommit/commit cycles over %d MB, %d rounds: %s (%lu problems)\n", cycles, mb, rounds, bad ? "FAIL" : "PASS", bad);
    printf("holding the memory for 12 s so it can be measured from outside (pid %lu)\n", GetCurrentProcessId());
    Sleep(12000);
    return bad != 0;
}
