// vulkan-1.dll wrapper for Wine: forwards to winevulkan.dll, advertises the win32 external-memory/semaphore
// extensions Star Citizen 4.10 insists on (unavailable on macOS hosts), strips them at vkCreateDevice,
// and answers their entry points with VK_ERROR_FEATURE_NOT_PRESENT instead of a null-pointer crash.
#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <windows.h>
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static HMODULE wv = NULL; static PFN_vkGetInstanceProcAddr rgipa = NULL; static PFN_vkGetDeviceProcAddr rgdpa = NULL;
static PFN_vkCreateDevice rcreate = NULL; static PFN_vkEnumerateDeviceExtensionProperties redep = NULL;
static FILE *lf = NULL;
static void lg(const char *fmt, ...) { if (!lf) { lf = fopen("C:\\users\\crossover\\sc-offline\\data\\vk1wrap.log", "a"); if (!lf) return; }
    va_list ap; va_start(ap, fmt); vfprintf(lf, fmt, ap); va_end(ap); fputc('\n', lf); fflush(lf); }
static void init(void) { if (wv) return; wv = LoadLibraryA("winevulkan.dll"); if (!wv) { lg("cannot load winevulkan.dll"); return; }
    rgipa = (PFN_vkGetInstanceProcAddr)GetProcAddress(wv, "vkGetInstanceProcAddr"); rgdpa = (PFN_vkGetDeviceProcAddr)GetProcAddress(wv, "vkGetDeviceProcAddr");
    lg("vk1wrap loaded (winevulkan.dll at %p)", (void*)wv); }
static const char *fake[] = { "VK_KHR_external_memory_win32", "VK_KHR_external_semaphore_win32", NULL };
static int is_fake(const char *n) { for (int i = 0; fake[i]; i++) if (!strcmp(n, fake[i])) return 1; return 0; }
static VkResult VKAPI_CALL stub_not_present(void) { lg("stub called for a faked win32 external-memory/semaphore function -> VK_ERROR_FEATURE_NOT_PRESENT"); return VK_ERROR_FEATURE_NOT_PRESENT; }
static VkResult VKAPI_CALL w_edep(VkPhysicalDevice pd, const char *layer, uint32_t *count, VkExtensionProperties *props) {
    init(); uint32_t rn = 0; VkResult r = redep(pd, layer, &rn, NULL); if (r) return r;
    VkExtensionProperties *tmp = (VkExtensionProperties*)calloc(rn + 8, sizeof(*tmp)); uint32_t n = rn; redep(pd, layer, &n, tmp);
    for (int i = 0; fake[i]; i++) { int have = 0; for (uint32_t k = 0; k < n; k++) if (!strcmp(tmp[k].extensionName, fake[i])) have = 1;
        if (!have) { strncpy(tmp[n].extensionName, fake[i], VK_MAX_EXTENSION_NAME_SIZE - 1); tmp[n].specVersion = 1; n++; } }
    static int once = 0; if (!once) { once = 1; lg("device extensions: %u real -> %u advertised", rn, n); }
    if (!props) { *count = n; free(tmp); return VK_SUCCESS; }
    uint32_t give = *count < n ? *count : n; memcpy(props, tmp, give * sizeof(*props)); *count = give; free(tmp); return give < n ? VK_INCOMPLETE : VK_SUCCESS; }
static VkResult VKAPI_CALL w_create_device(VkPhysicalDevice pd, const VkDeviceCreateInfo *ci, const VkAllocationCallbacks *a, VkDevice *out) {
    init(); VkDeviceCreateInfo copy = *ci; const char *kept[256]; uint32_t nk = 0;
    for (uint32_t i = 0; i < ci->enabledExtensionCount && nk < 256; i++) { if (is_fake(ci->ppEnabledExtensionNames[i])) { lg("vkCreateDevice: stripping %s", ci->ppEnabledExtensionNames[i]); continue; } kept[nk++] = ci->ppEnabledExtensionNames[i]; }
    copy.enabledExtensionCount = nk; copy.ppEnabledExtensionNames = kept;
    VkResult r = rcreate(pd, &copy, a, out); lg("vkCreateDevice (%u extensions kept) -> %d", nk, (int)r); return r; }
static PFN_vkVoidFunction VKAPI_CALL w_gdpa(VkDevice dev, const char *name);
PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance inst, const char *name) {
    init(); if (!rgipa) return NULL; PFN_vkVoidFunction f = rgipa(inst, name); if (!name) return f;
    if (!strcmp(name, "vkGetInstanceProcAddr")) return (PFN_vkVoidFunction)vkGetInstanceProcAddr;
    if (!strcmp(name, "vkGetDeviceProcAddr")) { rgdpa = (PFN_vkGetDeviceProcAddr)f; return (PFN_vkVoidFunction)w_gdpa; }
    if (!strcmp(name, "vkEnumerateDeviceExtensionProperties")) { redep = (PFN_vkEnumerateDeviceExtensionProperties)f; return (PFN_vkVoidFunction)w_edep; }
    if (!strcmp(name, "vkCreateDevice")) { rcreate = (PFN_vkCreateDevice)f; return (PFN_vkVoidFunction)w_create_device; }
    if (!strcmp(name, "vkGetMemoryWin32HandleKHR") || !strcmp(name, "vkGetSemaphoreWin32HandleKHR") || !strcmp(name, "vkImportSemaphoreWin32HandleKHR") || !strcmp(name, "vkGetMemoryWin32HandlePropertiesKHR")) return (PFN_vkVoidFunction)stub_not_present;
    return f; }
static PFN_vkVoidFunction VKAPI_CALL w_gdpa(VkDevice dev, const char *name) {
    init(); if (!rgdpa) return NULL; if (name && (!strcmp(name, "vkGetMemoryWin32HandleKHR") || !strcmp(name, "vkGetSemaphoreWin32HandleKHR") || !strcmp(name, "vkImportSemaphoreWin32HandleKHR") || !strcmp(name, "vkGetMemoryWin32HandlePropertiesKHR"))) return (PFN_vkVoidFunction)stub_not_present;
    if (name && !strcmp(name, "vkGetDeviceProcAddr")) return (PFN_vkVoidFunction)w_gdpa;
    return rgdpa(dev, name); }
PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice dev, const char *name) { return w_gdpa(dev, name); }
VkResult VKAPI_CALL vkCreateDevice(VkPhysicalDevice pd, const VkDeviceCreateInfo *ci, const VkAllocationCallbacks *a, VkDevice *out) { init(); if (!rcreate) rcreate = (PFN_vkCreateDevice)GetProcAddress(wv, "vkCreateDevice"); return w_create_device(pd, ci, a, out); }
VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(VkPhysicalDevice pd, const char *layer, uint32_t *count, VkExtensionProperties *props) { init(); if (!redep) redep = (PFN_vkEnumerateDeviceExtensionProperties)GetProcAddress(wv, "vkEnumerateDeviceExtensionProperties"); return w_edep(pd, layer, count, props); }
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r) { if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(h); return TRUE; }
