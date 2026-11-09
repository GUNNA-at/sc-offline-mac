// libMoltenVK.dylib shim: forwards everything to the real MoltenVK (libMoltenVK.real.dylib next to it)
// and rewrites VkPhysicalDeviceProperties so the Apple GPU looks like a discrete AMD card.
// Reason: Star Citizen 4.10's Vulkan backend finds "no valid Discrete GPU" and rejects the Apple
// integrated device, even though MoltenVK 1.4.2 provides Vulkan 1.3. Mirrors DXVK's AMD spoof.
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <mach-o/dyld.h>
#include <stdarg.h>
#include <time.h>

static void *real = NULL;
static PFN_vkGetInstanceProcAddr real_gipa = NULL;
static PFN_vkGetPhysicalDeviceProperties real_gpdp = NULL;
static PFN_vkGetPhysicalDeviceProperties2 real_gpdp2 = NULL;
static PFN_vkGetPhysicalDeviceProperties2 real_gpdp2khr = NULL;
static int quiet = 0;
static FILE *logf = NULL;
static void slog(const char *fmt, ...) {
    if (!logf) { logf = fopen("/private/tmp/mvkshim.log", "a"); if (!logf) return; setvbuf(logf, NULL, _IOLBF, 0); }
    { time_t t = time(NULL); struct tm tm; localtime_r(&t, &tm); fprintf(logf, "%02d:%02d:%02d ", tm.tm_hour, tm.tm_min, tm.tm_sec); }
    va_list ap; va_start(ap, fmt); vfprintf(logf, fmt, ap); va_end(ap); fputc('\n', logf); fflush(logf);
}

#include <pthread.h>
static void load_real(void) {
    if (real) return;
    Dl_info info; char path[4096] = {0};
    if (dladdr((void*)load_real, &info) && info.dli_fname) {
        strncpy(path, info.dli_fname, sizeof(path)-1);
        char *slash = strrchr(path, '/'); if (slash) *(slash+1) = 0;
        strncat(path, "libMoltenVK.real.dylib", sizeof(path)-strlen(path)-1);
    }
    real = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!real) { fprintf(stderr, "[mvkshim] cannot load %s: %s\n", path, dlerror()); abort(); }
    real_gipa = (PFN_vkGetInstanceProcAddr)dlsym(real, "vkGetInstanceProcAddr");
    quiet = getenv("MVKSHIM_QUIET") != NULL;
    slog("loaded %s (pid %d)", path, getpid());
    /* (Wine's memory layout under Rosetta is handled by winegrid, loaded by ntdll.so at process start: see ../winegrid) */
    /* capture this process's unix stderr (MoltenVK's [mvk-error] text lives there) */
    if (!getenv("MVKSHIM_NO_STDERR_CAPTURE")) { char lp[128]; snprintf(lp, sizeof(lp), "/private/tmp/sc-game-stderr-%d.log", getpid()); if (freopen(lp, "a", stderr)) setvbuf(stderr, NULL, _IOLBF, 0); }
}

static VkResult VKAPI_CALL shim_create_instance(const VkInstanceCreateInfo *ci, const VkAllocationCallbacks *a, VkInstance *out);
static void spoof(VkPhysicalDeviceProperties *p) {
    if (!p) return;
    static int once = 0;
    if (!once) { once = 1; slog("real device '%s' vendor 0x%04x device 0x%04x type %d api %u.%u.%u -> spoof AMD discrete", p->deviceName, p->vendorID, p->deviceID, p->deviceType, VK_API_VERSION_MAJOR(p->apiVersion), VK_API_VERSION_MINOR(p->apiVersion), VK_API_VERSION_PATCH(p->apiVersion)); }
    if (0) { fprintf(stderr, "[mvkshim] real device: '%s' vendor 0x%04x device 0x%04x type %d api %u.%u.%u -> spoofing as discrete AMD\n",
        p->deviceName, p->vendorID, p->deviceID, p->deviceType, VK_API_VERSION_MAJOR(p->apiVersion), VK_API_VERSION_MINOR(p->apiVersion), VK_API_VERSION_PATCH(p->apiVersion)); }
    if (!getenv("MVKSHIM_KEEP_VENDOR")) { p->vendorID = 0x1002; p->deviceID = 0x66af; }
    p->deviceType = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    if (!getenv("MVKSHIM_KEEP_VENDOR")) p->driverVersion = (2u << 22) | (0u << 12) | 283u;   /* AMD-style 2.0.283 */
    if (!getenv("MVKSHIM_KEEP_NAME") && strncmp(p->deviceName, "AMD Radeon VII", 14)) {
        char real[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];   /* the chip's own name as MoltenVK reports it, "Apple M4 Pro" on the test machine: kept in the new name so logs say what is really there */
        snprintf(real, sizeof real, "%s", p->deviceName);
        snprintf(p->deviceName, sizeof(p->deviceName), "AMD Radeon VII (MoltenVK %u.%u on %.180s)", VK_API_VERSION_MAJOR(p->apiVersion), VK_API_VERSION_MINOR(p->apiVersion), real);
    }
}

static void VKAPI_CALL shim_gpdp(VkPhysicalDevice pd, VkPhysicalDeviceProperties *props) {
    real_gpdp(pd, props); spoof(props);
}
static void spoof_chain(VkPhysicalDeviceProperties2 *props) {
    if (!props) return;
    spoof(&props->properties);
    if (getenv("MVKSHIM_KEEP_VENDOR")) return;
    for (VkBaseOutStructure *n = (VkBaseOutStructure*)props->pNext; n; n = n->pNext) {
        if (n->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES) {
            VkPhysicalDeviceDriverProperties *d = (VkPhysicalDeviceDriverProperties*)n;
            d->driverID = VK_DRIVER_ID_AMD_PROPRIETARY;
            snprintf(d->driverName, sizeof(d->driverName), "AMD proprietary driver");
            snprintf(d->driverInfo, sizeof(d->driverInfo), "24.2.1 (MoltenVK underneath)");
        }
    }
}
static void VKAPI_CALL shim_gpdp2(VkPhysicalDevice pd, VkPhysicalDeviceProperties2 *props) { real_gpdp2(pd, props); spoof_chain(props); }
static void VKAPI_CALL shim_gpdp2khr(VkPhysicalDevice pd, VkPhysicalDeviceProperties2 *props) { real_gpdp2khr(pd, props); spoof_chain(props); }

/* ---- video memory size. MoltenVK reports about three quarters of the Mac's RAM as one "video memory" heap (18 GB on a
 * 24 GB machine), and the game sizes its texture pool from that. On a Mac that memory is ordinary RAM, which the game
 * also needs for its own 20+ GB heap. MVKSHIM_VRAM_MB=<MB> makes the shim report a smaller card. ---- */
static PFN_vkGetPhysicalDeviceMemoryProperties real_gpdmp = NULL;
static PFN_vkGetPhysicalDeviceMemoryProperties2 real_gpdmp2 = NULL, real_gpdmp2khr = NULL;
static VkDeviceSize vram_limit(void) {
    static long long mb = -1;
    if (mb < 0) { const char *e = getenv("MVKSHIM_VRAM_MB"); mb = e ? atoll(e) : 0; if (mb < 0) mb = 0; if (mb) slog("video memory reported to the game: %lld MB (MVKSHIM_VRAM_MB)", mb); }
    return (VkDeviceSize)mb << 20;
}
/* Buffers and textures in separate memory blocks. MoltenVK creates one Metal buffer as large as the whole VkDeviceMemory block
 * as soon as any VkBuffer is bound to it, while textures bound to the same block get their own Metal storage anyway: the
 * texture part of a mixed block is allocated twice (mvkshim/vkheap.c: a 256 MB block with a 4 MB buffer and 170 MB of textures
 * costs 466 MB). The game's allocator mixes both in 256 MB blocks. With MVKSHIM_SPLIT_MEMTYPES=1 the shim shows the game one
 * extra memory type, a copy of the GPU-only one, and reports it as the only GPU-only type buffers can use; blocks of the
 * original type then hold textures only and cost nothing by themselves. Allocations of the copy go to the real type. */
static int g_split_src = -1, g_split_clone = -1; static uint32_t g_memtype_flags[VK_MAX_MEMORY_TYPES];
static int split_on(void) { static int v = -1; if (v < 0) { v = getenv("MVKSHIM_SPLIT_MEMTYPES") != NULL; if (v) slog("memory types: buffers and textures get separate GPU-only blocks (MVKSHIM_SPLIT_MEMTYPES)"); } return v; }
static void clamp_heaps(VkPhysicalDeviceMemoryProperties *mp) {
    if (!mp) return;
    VkDeviceSize lim = vram_limit();
    if (lim) for (uint32_t i = 0; i < mp->memoryHeapCount && i < VK_MAX_MEMORY_HEAPS; i++) if (mp->memoryHeaps[i].size > lim) mp->memoryHeaps[i].size = lim;
    if (split_on() && mp->memoryTypeCount < VK_MAX_MEMORY_TYPES) {
        int src = -1;
        for (uint32_t i = 0; i < mp->memoryTypeCount; i++) { VkMemoryPropertyFlags f = mp->memoryTypes[i].propertyFlags;
            if ((f & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) && !(f & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT))) { src = (int)i; break; } }
        if (src >= 0) { mp->memoryTypes[mp->memoryTypeCount] = mp->memoryTypes[src]; g_split_src = src; g_split_clone = (int)mp->memoryTypeCount; mp->memoryTypeCount++; }
    }
    for (uint32_t i = 0; i < mp->memoryTypeCount && i < VK_MAX_MEMORY_TYPES; i++) g_memtype_flags[i] = mp->memoryTypes[i].propertyFlags;
}
static void split_buffer_bits(uint32_t *bits) { if (g_split_clone >= 0 && (*bits & (1u << g_split_src))) { *bits &= ~(1u << g_split_src); *bits |= 1u << g_split_clone; } }
static void clamp_heaps2(VkPhysicalDeviceMemoryProperties2 *mp) {
    if (!mp) return;
    clamp_heaps(&mp->memoryProperties);
    VkDeviceSize lim = vram_limit(); if (!lim) return;
    for (VkBaseOutStructure *n = (VkBaseOutStructure *)mp->pNext; n; n = n->pNext)
        if (n->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT) {
            VkPhysicalDeviceMemoryBudgetPropertiesEXT *b = (VkPhysicalDeviceMemoryBudgetPropertiesEXT *)n;
            for (uint32_t i = 0; i < VK_MAX_MEMORY_HEAPS; i++) if (b->heapBudget[i] > lim) b->heapBudget[i] = lim;
        }
}
static void VKAPI_CALL shim_gpdmp(VkPhysicalDevice pd, VkPhysicalDeviceMemoryProperties *mp) { real_gpdmp(pd, mp); clamp_heaps(mp); }
static void VKAPI_CALL shim_gpdmp2(VkPhysicalDevice pd, VkPhysicalDeviceMemoryProperties2 *mp) { real_gpdmp2(pd, mp); clamp_heaps2(mp); }
static void VKAPI_CALL shim_gpdmp2khr(VkPhysicalDevice pd, VkPhysicalDeviceMemoryProperties2 *mp) { real_gpdmp2khr(pd, mp); clamp_heaps2(mp); }


/* ---- features the engine insists on but Metal/MoltenVK lack: advertise them, then strip them at vkCreateDevice ---- */
static PFN_vkGetPhysicalDeviceFeatures real_gpdf = NULL;
static PFN_vkGetPhysicalDeviceFeatures2 real_gpdf2 = NULL, real_gpdf2khr = NULL;
static PFN_vkCreateDevice real_create_device = NULL;
static int fake_features(void) { return getenv("MVKSHIM_NO_FAKE_FEATURES") == NULL; }
static void bump(VkPhysicalDeviceFeatures *f) {
    if (!f || !fake_features()) return;
    static int once = 0;
    if (!once) { once = 1; slog("features real: geometryShader=%u depthBounds=%u pipelineStatisticsQuery=%u -> advertising all three", f->geometryShader, f->depthBounds, f->pipelineStatisticsQuery); }
    f->geometryShader = VK_TRUE; f->depthBounds = VK_TRUE; f->pipelineStatisticsQuery = VK_TRUE;
}
static void strip(VkPhysicalDeviceFeatures *f, const char *where) {
    if (!f) return;
    if (f->geometryShader || f->depthBounds || f->pipelineStatisticsQuery)
        slog("vkCreateDevice: engine enabled geometryShader=%u depthBounds=%u pipelineStatisticsQuery=%u in %s -> stripping", f->geometryShader, f->depthBounds, f->pipelineStatisticsQuery, where);
    f->geometryShader = VK_FALSE; f->depthBounds = VK_FALSE; f->pipelineStatisticsQuery = VK_FALSE;
}
/* MVKSHIM_NO_TESSELLATION=1: tell the game the GPU has no tessellation shaders. Metal has none either: MoltenVK emulates
 * them with a compute pass per draw plus private scratch buffers sized for the worst case (kept in a pool for good). */
static int no_tess(void) { static int v = -1; if (v < 0) { v = getenv("MVKSHIM_NO_TESSELLATION") != NULL; if (v) slog("features: tessellationShader reported as unsupported (MVKSHIM_NO_TESSELLATION)"); } return v; }
static void VKAPI_CALL shim_gpdf(VkPhysicalDevice pd, VkPhysicalDeviceFeatures *f) { real_gpdf(pd, f); bump(f); if (f && no_tess()) f->tessellationShader = VK_FALSE; }
static void bump_chain(VkPhysicalDeviceFeatures2 *f) {
    if (!f) return; bump(&f->features); if (!fake_features()) return;
    for (VkBaseOutStructure *n = (VkBaseOutStructure*)f->pNext; n; n = n->pNext)
        if (n->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT) {
            static int once = 0; if (!once) { once = 1; slog("features: advertising depthClipEnable (extension faked)"); }
            ((VkPhysicalDeviceDepthClipEnableFeaturesEXT*)n)->depthClipEnable = VK_TRUE; }
}
static void VKAPI_CALL shim_gpdf2(VkPhysicalDevice pd, VkPhysicalDeviceFeatures2 *f) { real_gpdf2(pd, f); bump_chain(f); if (f && no_tess()) f->features.tessellationShader = VK_FALSE; }
static void VKAPI_CALL shim_gpdf2khr(VkPhysicalDevice pd, VkPhysicalDeviceFeatures2 *f) { real_gpdf2khr(pd, f); bump_chain(f); if (f && no_tess()) f->features.tessellationShader = VK_FALSE; }

/* ---- device extensions the engine requires but the host lacks: advertise, then strip at vkCreateDevice ---- */
static PFN_vkEnumerateDeviceExtensionProperties real_edep = NULL;
static const char *fake_exts_default[] = { "VK_EXT_depth_clip_enable", NULL };
static const char *fake_ext(int i) { static char buf[512]; static char *list[16]; static int n = -1;
    if (n < 0) { n = 0; const char *e = getenv("MVKSHIM_FAKE_EXTS");
        if (e) { strncpy(buf, e, sizeof(buf)-1); char *t = strtok(buf, ","); while (t && n < 15) { list[n++] = t; t = strtok(NULL, ","); } }
        else { for (int k = 0; fake_exts_default[k]; k++) list[n++] = (char*)fake_exts_default[k]; } }
    return i < n ? list[i] : NULL; }
static int is_fake(const char *name) { for (int i = 0; fake_ext(i); i++) if (!strcmp(name, fake_ext(i))) return 1; return 0; }
static VkResult VKAPI_CALL shim_edep(VkPhysicalDevice pd, const char *layer, uint32_t *count, VkExtensionProperties *props) {
    uint32_t real_n = 0; VkResult r = real_edep(pd, layer, &real_n, NULL); if (r != VK_SUCCESS) return r;
    VkExtensionProperties *tmp = (VkExtensionProperties*)calloc(real_n + 16, sizeof(*tmp)); uint32_t n = real_n; real_edep(pd, layer, &n, tmp);
    for (int i = 0; fake_ext(i); i++) { int have = 0; for (uint32_t k = 0; k < n; k++) if (!strcmp(tmp[k].extensionName, fake_ext(i))) have = 1;
        if (!have) { strncpy(tmp[n].extensionName, fake_ext(i), VK_MAX_EXTENSION_NAME_SIZE-1); tmp[n].specVersion = 1; n++; } }
    static int once = 0; if (!once) { once = 1; slog("device extensions: %u real -> %u advertised", real_n, n); }
    if (!props) { *count = n; free(tmp); return VK_SUCCESS; }
    uint32_t give = *count < n ? *count : n; memcpy(props, tmp, give * sizeof(*props)); *count = give; free(tmp);
    return give < n ? VK_INCOMPLETE : VK_SUCCESS;
}

static VkResult VKAPI_CALL shim_create_device(VkPhysicalDevice pd, const VkDeviceCreateInfo *ci, const VkAllocationCallbacks *a, VkDevice *out) {
    VkDeviceCreateInfo copy = *ci;
    VkPhysicalDeviceFeatures feats; VkPhysicalDeviceFeatures2 feats2;
    slog("vkCreateDevice: %u extensions, %u queue infos, pEnabledFeatures=%p", ci->enabledExtensionCount, ci->queueCreateInfoCount, (void*)ci->pEnabledFeatures);
    const char *kept[256]; uint32_t nk = 0;
    for (uint32_t i = 0; i < ci->enabledExtensionCount && nk < 256; i++) { const char *e = ci->ppEnabledExtensionNames[i];
        if (is_fake(e)) { slog("   devext: %s (stripped: not really supported)", e); continue; } slog("   devext: %s", e); kept[nk++] = e; }
    copy.enabledExtensionCount = nk; copy.ppEnabledExtensionNames = kept;
    if (ci->pEnabledFeatures) { feats = *ci->pEnabledFeatures; strip(&feats, "pEnabledFeatures"); copy.pEnabledFeatures = &feats; }
    /* unlink any VkPhysicalDeviceDepthClipEnableFeaturesEXT (MoltenVK doesn't know it) by rebuilding the chain head links */
    {
        static VkBaseOutStructure chain_copy[32]; int ci_n = 0; const VkBaseInStructure *prev2 = NULL; int found = 0;
        for (const VkBaseInStructure *n = (const VkBaseInStructure*)ci->pNext; n; n = n->pNext)
            if (n->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT) found = 1;
        if (found) {
            /* copy every link up to and including the one before the depth-clip struct, skipping it */
            VkBaseOutStructure *head = NULL, *tail = NULL;
            for (const VkBaseInStructure *n = (const VkBaseInStructure*)ci->pNext; n; n = n->pNext) {
                if (n->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT) { slog("vkCreateDevice: dropping VkPhysicalDeviceDepthClipEnableFeaturesEXT from pNext"); continue; }
                if (!tail) { head = (VkBaseOutStructure*)n; tail = head; } else { tail->pNext = (VkBaseOutStructure*)n; tail = (VkBaseOutStructure*)n; }   /* mutates engine chain links; one-shot */
            }
            if (tail) tail->pNext = NULL;
            copy.pNext = head;
        }
        (void)chain_copy; (void)ci_n; (void)prev2;
    }
    /* VkPhysicalDeviceFeatures2 may ride in the pNext chain instead */
    const VkBaseInStructure *prev = NULL;
    for (const VkBaseInStructure *n = (const VkBaseInStructure*)copy.pNext; n; prev = n, n = n->pNext) {
        if (n->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2) {
            feats2 = *(const VkPhysicalDeviceFeatures2*)n; strip(&feats2.features, "pNext VkPhysicalDeviceFeatures2");
            if (!prev) copy.pNext = &feats2; else ((VkBaseInStructure*)prev)->pNext = (const VkBaseInStructure*)&feats2;   /* note: mutates engine's chain link; acceptable for a one-shot call */
            break;
        }
    }
    VkResult r = real_create_device(pd, &copy, a, out);
    slog("vkCreateDevice -> %d", (int)r);
    return r;
}


/* ---- graphics-pipeline fallback: Apple's GPU back-end compiler (AGXMetalG16X) crashes on a few of the game's fragment
 * shaders (WaterBoundaryPS, a WaterPS variant, RParticleLightAtlas), which surfaces as VK_ERROR_INITIALIZATION_FAILED from
 * vkCreateGraphicsPipelines and the engine then aborts. When a pipeline fails to build we retry it once with an empty
 * fragment stage and every color write masked off: that pipeline draws nothing, the game keeps running. Logged to
 * /private/tmp/mvkshim.log. MVKSHIM_NO_PIPELINE_FALLBACK=1 disables it. ---- */
#include <pthread.h>
#include <sys/stat.h>
static PFN_vkCreateGraphicsPipelines real_cgp = NULL;
static PFN_vkCreateShaderModule real_csm = NULL;
static PFN_vkGetDeviceProcAddr real_gdpa = NULL;
static PFN_vkDestroyShaderModule real_dsm = NULL;
static pthread_mutex_t stub_lock = PTHREAD_MUTEX_INITIALIZER;
static VkDevice stub_dev[4]; static VkShaderModule stub_mod[4]; static int stub_n = 0;
/* SPIR-V 1.0: "fragment void main() {}" (OpCapability Shader; OpMemoryModel Logical GLSL450; OpEntryPoint Fragment %3 "main";
 * OpExecutionMode %3 OriginUpperLeft; %1 void; %2 fn(void); %3 = OpFunction; OpLabel; OpReturn; OpFunctionEnd) */
static const uint32_t stub_spv[] = { 0x07230203, 0x00010000, 0x00000000, 0x00000005, 0x00000000, 0x00020011, 0x00000001, 0x0003000E, 0x00000000, 0x00000001,
    0x0005000F, 0x00000004, 0x00000003, 0x6E69616D, 0x00000000, 0x00030010, 0x00000003, 0x00000007, 0x00020013, 0x00000001, 0x00030021, 0x00000002, 0x00000001,
    0x00050036, 0x00000001, 0x00000003, 0x00000000, 0x00000002, 0x000200F8, 0x00000004, 0x000100FD, 0x00010038 };
static void resolve_pipeline_fns(void) {
    load_real();
    if (!real_cgp) real_cgp = (PFN_vkCreateGraphicsPipelines)dlsym(real, "vkCreateGraphicsPipelines");
    if (!real_csm) real_csm = (PFN_vkCreateShaderModule)dlsym(real, "vkCreateShaderModule");
    if (!real_gdpa) real_gdpa = (PFN_vkGetDeviceProcAddr)dlsym(real, "vkGetDeviceProcAddr");
    if (!real_dsm) real_dsm = (PFN_vkDestroyShaderModule)dlsym(real, "vkDestroyShaderModule");
}
static VkShaderModule stub_for(VkDevice dev) {
    VkShaderModule m = VK_NULL_HANDLE;
    pthread_mutex_lock(&stub_lock);
    for (int i = 0; i < stub_n; i++) if (stub_dev[i] == dev) { m = stub_mod[i]; break; }
    if (!m && stub_n < 4) {
        VkShaderModuleCreateInfo ci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, sizeof(stub_spv), stub_spv };
        if (real_csm(dev, &ci, NULL, &m) == VK_SUCCESS) { stub_dev[stub_n] = dev; stub_mod[stub_n] = m; stub_n++; slog("pipeline fallback: stub fragment module created for device %p", (void*)dev); }
        else { m = VK_NULL_HANDLE; slog("pipeline fallback: could not create the stub fragment module"); }
    }
    pthread_mutex_unlock(&stub_lock);
    return m;
}
/* ---- remember rejected fragment shaders across launches ----
 * Every shader module's SPIR-V is hashed when it is created. When a pipeline only builds with the empty fragment stage, the
 * hash of its real fragment shader is written to a list on disk with a failure count. A shader that failed in two separate
 * launches is skipped without trying to compile it again (the failing compile stalls the frame for seconds while Apple's
 * compiler crashes and retries, and the engine reads a long stall as a hung GPU). Delete the file to start over.
 * File: $MVKSHIM_BAD_LIST or ~/Library/Caches/mvkshim/bad-fragment-shaders.txt ("<hash hex> <count>" per line). */
#define MODTAB (1u << 17)
static struct { VkShaderModule m; uint64_t h; } modtab[MODTAB];
static pthread_mutex_t mod_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t fnv1a(const void *d, size_t n) { const unsigned char *p = (const unsigned char*)d; uint64_t h = 1469598103934665603ull; for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ull; } return h ? h : 1; }
static uint32_t modslot(VkShaderModule m) { return (uint32_t)((((uint64_t)(uintptr_t)m) * 11400714819323198485ull) >> 47) & (MODTAB - 1); }
static void mod_put(VkShaderModule m, uint64_t h) { pthread_mutex_lock(&mod_lock); uint32_t i = modslot(m); for (uint32_t k = 0; k < MODTAB; k++, i = (i + 1) & (MODTAB - 1)) if (!modtab[i].m || modtab[i].m == m || modtab[i].h == 0) { modtab[i].m = m; modtab[i].h = h; break; } pthread_mutex_unlock(&mod_lock); }
static uint64_t mod_get(VkShaderModule m) { uint64_t h = 0; pthread_mutex_lock(&mod_lock); uint32_t i = modslot(m); for (uint32_t k = 0; k < MODTAB && modtab[i].m; k++, i = (i + 1) & (MODTAB - 1)) if (modtab[i].m == m) { h = modtab[i].h; break; } pthread_mutex_unlock(&mod_lock); return h; }
static void mod_del(VkShaderModule m) { pthread_mutex_lock(&mod_lock); uint32_t i = modslot(m); for (uint32_t k = 0; k < MODTAB && modtab[i].m; k++, i = (i + 1) & (MODTAB - 1)) if (modtab[i].m == m) { modtab[i].h = 0; break; } pthread_mutex_unlock(&mod_lock); }
static struct { uint64_t h; int count; int bumped; } badlist[4096]; static int badlist_n = -1; static char badlist_path[1024];
static void bad_hash_load(void) {
    if (badlist_n >= 0) return; badlist_n = 0;
    const char *e = getenv("MVKSHIM_BAD_LIST"); const char *home = getenv("HOME");
    if (e) snprintf(badlist_path, sizeof badlist_path, "%s", e);
    else { char dir[900]; snprintf(dir, sizeof dir, "%s/Library/Caches/mvkshim", home ? home : "/private/tmp"); mkdir(dir, 0755); snprintf(badlist_path, sizeof badlist_path, "%s/bad-fragment-shaders.txt", dir); }
    FILE *f = fopen(badlist_path, "r"); if (!f) { slog("bad-shader list: %s (new)", badlist_path); return; }
    unsigned long long h; int c; while (badlist_n < 4096 && fscanf(f, "%llx %d", &h, &c) == 2) { badlist[badlist_n].h = h; badlist[badlist_n].count = c; badlist[badlist_n].bumped = 0; badlist_n++; }
    fclose(f); int trusted = 0; for (int i = 0; i < badlist_n; i++) if (badlist[i].count >= 2) trusted++;
    slog("bad-shader list: %d entries, %d trusted (failed in 2+ launches), from %s", badlist_n, trusted, badlist_path);
}
static void bad_hash_save(void) { FILE *f = fopen(badlist_path, "w"); if (!f) return; for (int i = 0; i < badlist_n; i++) fprintf(f, "%016llx %d\n", (unsigned long long)badlist[i].h, badlist[i].count); fclose(f); }
static int bad_hash_trusted(uint64_t h) { if (!h) return 0; int r = 0; pthread_mutex_lock(&mod_lock); bad_hash_load(); for (int i = 0; i < badlist_n; i++) if (badlist[i].h == h) { r = badlist[i].count >= 2; break; } pthread_mutex_unlock(&mod_lock); return r; }
static void bad_hash_record(uint64_t h) {   /* called when a real compile of this fragment shader failed in this launch */
    if (!h) return; pthread_mutex_lock(&mod_lock); bad_hash_load(); int i;
    for (i = 0; i < badlist_n; i++) if (badlist[i].h == h) break;
    if (i == badlist_n) { if (badlist_n >= 4096) { pthread_mutex_unlock(&mod_lock); return; } badlist[i].h = h; badlist[i].count = 0; badlist[i].bumped = 0; badlist_n++; }
    if (!badlist[i].bumped) { badlist[i].bumped = 1; badlist[i].count++; bad_hash_save(); }
    pthread_mutex_unlock(&mod_lock);
}
static VkResult VKAPI_CALL shim_csm(VkDevice dev, const VkShaderModuleCreateInfo *ci, const VkAllocationCallbacks *a, VkShaderModule *out) {
    resolve_pipeline_fns();
    VkResult r = real_csm(dev, ci, a, out);
    if (r == VK_SUCCESS && ci && ci->pCode && ci->codeSize) mod_put(*out, fnv1a(ci->pCode, ci->codeSize));
    return r;
}
static void VKAPI_CALL shim_dsm(VkDevice dev, VkShaderModule m, const VkAllocationCallbacks *a) {
    resolve_pipeline_fns(); if (m) mod_del(m); real_dsm(dev, m, a);
}
static VkShaderModule bad_fs[128]; static int bad_n = 0;      /* fragment modules Apple's compiler is known to reject */
static int fs_known_bad(VkShaderModule m) { for (int i = 0; i < bad_n; i++) if (bad_fs[i] == m) return 1; return 0; }
static void fs_mark_bad(VkShaderModule m) { pthread_mutex_lock(&stub_lock); if (!fs_known_bad(m) && bad_n < 128) bad_fs[bad_n++] = m; pthread_mutex_unlock(&stub_lock); }
static int fallbacks = 0;
static VkResult build_fallback(VkDevice dev, VkPipelineCache cache, const VkGraphicsPipelineCreateInfo *src, const VkAllocationCallbacks *a, VkPipeline *out, int tier, int *had_gs) {
    VkGraphicsPipelineCreateInfo ci = *src;
    VkPipelineShaderStageCreateInfo stages[8]; uint32_t ns = ci.stageCount < 8 ? ci.stageCount : 8; int fs = -1; *had_gs = 0;
    for (uint32_t k = 0; k < ns; k++) { stages[k] = ci.pStages[k]; if (stages[k].stage == VK_SHADER_STAGE_FRAGMENT_BIT) fs = (int)k; if (stages[k].stage == VK_SHADER_STAGE_GEOMETRY_BIT) *had_gs = 1; }
    VkShaderModule stub = fs >= 0 ? stub_for(dev) : VK_NULL_HANDLE;
    if (fs < 0 || !stub) return VK_ERROR_INITIALIZATION_FAILED;
    stages[fs].module = stub; stages[fs].pName = "main"; stages[fs].pSpecializationInfo = NULL;
    VkPipelineColorBlendStateCreateInfo cb; VkPipelineColorBlendAttachmentState att[16];
    if (ci.pColorBlendState) {
        cb = *ci.pColorBlendState; uint32_t na = cb.attachmentCount < 16 ? cb.attachmentCount : 16;
        for (uint32_t k = 0; k < na; k++) { att[k] = cb.pAttachments[k]; att[k].colorWriteMask = 0; }
        cb.pAttachments = att; cb.attachmentCount = na; ci.pColorBlendState = &cb;
    }
    ci.flags &= ~(VkPipelineCreateFlags)VK_PIPELINE_CREATE_DERIVATIVE_BIT; ci.basePipelineHandle = VK_NULL_HANDLE; ci.basePipelineIndex = -1;
    VkPipelineShaderStageCreateInfo keep[2]; uint32_t nk = 0; VkPipelineRasterizationStateCreateInfo rs; VkPipelineInputAssemblyStateCreateInfo ia;
    if (tier == 2) {   /* vertex + empty fragment only, rasterization off: Metal has no geometry stage at all */
        for (uint32_t k = 0; k < ns; k++) if (stages[k].stage == VK_SHADER_STAGE_VERTEX_BIT || stages[k].stage == VK_SHADER_STAGE_FRAGMENT_BIT) keep[nk++] = stages[k];
        if (nk != 2) return VK_ERROR_INITIALIZATION_FAILED;
        if (ci.pRasterizationState) { rs = *ci.pRasterizationState; rs.rasterizerDiscardEnable = VK_TRUE; ci.pRasterizationState = &rs; }
        if (ci.pInputAssemblyState && ci.pInputAssemblyState->topology == VK_PRIMITIVE_TOPOLOGY_PATCH_LIST) { ia = *ci.pInputAssemblyState; ia.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST; ci.pInputAssemblyState = &ia; }
        ci.pStages = keep; ci.stageCount = 2; ci.pTessellationState = NULL;
    } else { ci.pStages = stages; ci.stageCount = ns; }
    *out = VK_NULL_HANDLE;
    return real_cgp(dev, cache, 1, &ci, a, out);
}
static VkResult VKAPI_CALL shim_cgp(VkDevice dev, VkPipelineCache cache, uint32_t n, const VkGraphicsPipelineCreateInfo *cis, const VkAllocationCallbacks *a, VkPipeline *out) {
    resolve_pipeline_fns();
    if (getenv("MVKSHIM_NO_PIPELINE_FALLBACK")) return real_cgp(dev, cache, n, cis, a, out);
    /* pipelines we already know cannot build: skip the doomed compile (Apple's compiler retries for seconds before failing) */
    int doomed = 0, has_gs = 0; VkShaderModule fsmod = VK_NULL_HANDLE;
    if (n == 1) {
        for (uint32_t k = 0; k < cis[0].stageCount; k++) { if (cis[0].pStages[k].stage == VK_SHADER_STAGE_GEOMETRY_BIT) has_gs = 1; if (cis[0].pStages[k].stage == VK_SHADER_STAGE_FRAGMENT_BIT) fsmod = cis[0].pStages[k].module; }
        if (has_gs || (fsmod && (fs_known_bad(fsmod) || bad_hash_trusted(mod_get(fsmod))))) doomed = 1;
    }
    VkResult r = doomed ? VK_ERROR_INITIALIZATION_FAILED : real_cgp(dev, cache, n, cis, a, out);
    if (r == VK_SUCCESS) return r;
    int bad = 0, retried = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (!doomed && n > 1 && out[i] != VK_NULL_HANDLE) continue;           /* in a batch, only the entries left null failed */
        int gs = 0; VkResult r2 = VK_ERROR_INITIALIZATION_FAILED; const char *how = "";
        if (!has_gs) { r2 = build_fallback(dev, cache, &cis[i], a, &out[i], 1, &gs); how = "empty fragment stage, color writes off"; }
        if (r2 != VK_SUCCESS) { r2 = build_fallback(dev, cache, &cis[i], a, &out[i], 2, &gs); how = gs ? "geometry stage dropped, vertex + empty fragment, rasterization off" : "vertex + empty fragment, rasterization off"; }
        if (r2 == VK_SUCCESS) { fallbacks++; VkShaderModule m = VK_NULL_HANDLE; for (uint32_t k = 0; k < cis[i].stageCount; k++) if (cis[i].pStages[k].stage == VK_SHADER_STAGE_FRAGMENT_BIT) m = cis[i].pStages[k].module; if (m && !gs) { fs_mark_bad(m); if (!doomed) bad_hash_record(mod_get(m)); } }
        slog("pipeline %u/%u %s (%d): fallback #%d %s -> %d", i, n, doomed ? "known-bad, compile skipped" : "failed", (int)r, fallbacks, how, (int)r2);
        retried++; if (r2 != VK_SUCCESS) bad = 1;
    }
    if (!retried) return r;
    return bad ? r : VK_SUCCESS;
}
/* ---- GPU memory accounting (diagnostic): what the game keeps on the "video card", which on a Mac is ordinary RAM.
 * One line every 30 s in the log: device memory allocated, textures by size class, render targets, buffers by use.
 * MVKSHIM_NO_GPUMEM_STATS=1 turns it off. ---- */
#include <os/lock.h>
static PFN_vkCreateImage real_cimg = NULL; static PFN_vkDestroyImage real_dimg = NULL;
static PFN_vkCreateBuffer real_cbuf = NULL; static PFN_vkDestroyBuffer real_dbuf = NULL;
static PFN_vkAllocateMemory real_amem = NULL; static PFN_vkFreeMemory real_fmem = NULL;
enum { GK_TEX = 1, GK_RT, GK_BUF_GEOM, GK_BUF_SHADER, GK_BUF_STAGING, GK_BUF_OTHER, GK_MEM };
typedef struct { uint64_t h, bytes, mem; uint8_t kind, bucket, bc; } gm_ent;
typedef struct { uint64_t h, size, buf_bytes, img_bytes; uint32_t nbuf, nimg; uint8_t type; } gm_blk;
#define GB_N (1u << 14)
static gm_blk gm_blks[GB_N]; static uint64_t gm_bufhist_n[5], gm_bufhist_b[5];
#define GM_N (1u << 19)
static gm_ent *gm_tab; static os_unfair_lock gm_lock = OS_UNFAIR_LOCK_INIT;
static uint64_t gm_bytes[8], gm_count[8], gm_tex_b[5], gm_tex_n[5], gm_tex_bc, gm_mem_type[16]; static time_t gm_last;
static int gm_on(void) { static int v = -1; if (v < 0) v = getenv("MVKSHIM_NO_GPUMEM_STATS") == NULL; return v; }
static double gm_bpp(VkFormat f) {
    if (f >= 131 && f <= 134) return 4; if (f >= 135 && f <= 138) return 8; if (f == 139 || f == 140) return 4; if (f >= 141 && f <= 146) return 8;
    if (f >= 9 && f <= 15) return 8; if (f >= 16 && f <= 22) return 16; if (f >= 23 && f <= 36) return 24; if (f >= 37 && f <= 69) return 32;
    if (f >= 70 && f <= 76) return 16; if (f >= 77 && f <= 83) return 32; if (f >= 84 && f <= 90) return 48; if (f >= 91 && f <= 97) return 64;
    if (f >= 98 && f <= 100) return 32; if (f >= 101 && f <= 103) return 64; if (f >= 104 && f <= 106) return 96; if (f >= 107 && f <= 109) return 128;
    if (f == 122 || f == 123) return 32; if (f == 124) return 16; if (f == 125 || f == 126) return 32; if (f == 127) return 8; if (f == 128 || f == 129) return 32; if (f == 130) return 64;
    return 32;
}
static gm_blk *gm_blk_find(uint64_t h, int create) {
    uint32_t i = (uint32_t)((h * 0x9E3779B97F4A7C15ull) >> 50) & (GB_N - 1);
    for (uint32_t k = 0; k < GB_N; k++, i = (i + 1) & (GB_N - 1)) {
        if (gm_blks[i].h == h) return &gm_blks[i];
        if (gm_blks[i].h == 0) { if (!create) return NULL; memset(&gm_blks[i], 0, sizeof gm_blks[i]); gm_blks[i].h = h; return &gm_blks[i]; }
    }
    return NULL;
}
static void gm_report_blocks(void) {
    /* GPU-only blocks (no host-visible flag): a block with at least one buffer costs its full size as a Metal buffer */
    uint64_t n_buf = 0, sz_buf = 0, bytes_buf = 0, img_in_buf = 0, n_img = 0, sz_img = 0, n_empty = 0, hv_n = 0, hv_sz = 0, hv_buf = 0, hv_img = 0;
    for (uint32_t i = 0; i < GB_N; i++) { gm_blk *b = &gm_blks[i]; if (!b->h || b->h == ~0ull) continue;
        if (g_memtype_flags[b->type & 31] & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) { hv_n++; hv_sz += b->size; hv_buf += b->buf_bytes; hv_img += b->img_bytes; continue; }
        if (b->nbuf) { n_buf++; sz_buf += b->size; bytes_buf += b->buf_bytes; img_in_buf += b->img_bytes; }
        else if (b->nimg) { n_img++; sz_img += b->size; } else n_empty++; }
    slog("gpumem blocks: GPU-only with buffers %llu = %llu MB (buffers bound %llu MB, textures bound in the same blocks %llu MB) | GPU-only textures-only %llu = %llu MB (no cost by themselves) | unused %llu | host-visible %llu = %llu MB (buffers %llu MB, textures %llu MB) | live buffers by size: <16K %llu/%llu MB, <256K %llu/%llu MB, <4M %llu/%llu MB, <64M %llu/%llu MB, >=64M %llu/%llu MB",
         (unsigned long long)n_buf, (unsigned long long)(sz_buf >> 20), (unsigned long long)(bytes_buf >> 20), (unsigned long long)(img_in_buf >> 20),
         (unsigned long long)n_img, (unsigned long long)(sz_img >> 20), (unsigned long long)n_empty, (unsigned long long)hv_n, (unsigned long long)(hv_sz >> 20), (unsigned long long)(hv_buf >> 20), (unsigned long long)(hv_img >> 20),
         (unsigned long long)gm_bufhist_n[0], (unsigned long long)(gm_bufhist_b[0] >> 20), (unsigned long long)gm_bufhist_n[1], (unsigned long long)(gm_bufhist_b[1] >> 20), (unsigned long long)gm_bufhist_n[2], (unsigned long long)(gm_bufhist_b[2] >> 20),
         (unsigned long long)gm_bufhist_n[3], (unsigned long long)(gm_bufhist_b[3] >> 20), (unsigned long long)gm_bufhist_n[4], (unsigned long long)(gm_bufhist_b[4] >> 20));
}
static void gm_report(void) {      /* called with the lock held */
    time_t now = time(NULL); if (now - gm_last < 30) return; gm_last = now;
    gm_report_blocks();
    char types[160] = ""; int o = 0; for (int i = 0; i < 16 && o < 140; i++) if (gm_mem_type[i]) o += snprintf(types + o, sizeof types - o, " type%d %llu MB", i, (unsigned long long)(gm_mem_type[i] >> 20));
    slog("gpumem: device memory %llu MB in %llu allocations (%s ) | textures %llu = %llu MB [>=4096: %llu/%llu MB, 2048: %llu/%llu MB, 1024: %llu/%llu MB, 512: %llu/%llu MB, smaller: %llu/%llu MB; block-compressed %llu MB] | render targets %llu = %llu MB | buffers: geometry %llu MB, shader data %llu MB, staging %llu MB, other %llu MB",
         (unsigned long long)(gm_bytes[GK_MEM] >> 20), (unsigned long long)gm_count[GK_MEM], types,
         (unsigned long long)gm_count[GK_TEX], (unsigned long long)(gm_bytes[GK_TEX] >> 20),
         (unsigned long long)gm_tex_n[0], (unsigned long long)(gm_tex_b[0] >> 20), (unsigned long long)gm_tex_n[1], (unsigned long long)(gm_tex_b[1] >> 20),
         (unsigned long long)gm_tex_n[2], (unsigned long long)(gm_tex_b[2] >> 20), (unsigned long long)gm_tex_n[3], (unsigned long long)(gm_tex_b[3] >> 20),
         (unsigned long long)gm_tex_n[4], (unsigned long long)(gm_tex_b[4] >> 20), (unsigned long long)(gm_tex_bc >> 20),
         (unsigned long long)gm_count[GK_RT], (unsigned long long)(gm_bytes[GK_RT] >> 20),
         (unsigned long long)(gm_bytes[GK_BUF_GEOM] >> 20), (unsigned long long)(gm_bytes[GK_BUF_SHADER] >> 20), (unsigned long long)(gm_bytes[GK_BUF_STAGING] >> 20), (unsigned long long)(gm_bytes[GK_BUF_OTHER] >> 20));
}
static void gm_add(uint64_t h, uint64_t bytes, int kind, int bucket, int bc) {
    if (!h) return;
    os_unfair_lock_lock(&gm_lock);
    if (!gm_tab) gm_tab = calloc(GM_N, sizeof(gm_ent));
    if (gm_tab) {
        uint32_t i = (uint32_t)((h * 0x9E3779B97F4A7C15ull) >> 45) & (GM_N - 1);
        for (uint32_t k = 0; k < GM_N; k++, i = (i + 1) & (GM_N - 1)) if (gm_tab[i].h == 0 || gm_tab[i].h == ~0ull) {
            gm_tab[i].h = h; gm_tab[i].bytes = bytes; gm_tab[i].kind = (uint8_t)kind; gm_tab[i].bucket = (uint8_t)bucket; gm_tab[i].bc = (uint8_t)bc;
            gm_bytes[kind] += bytes; gm_count[kind]++;
            if (kind == GK_TEX) { gm_tex_b[bucket] += bytes; gm_tex_n[bucket]++; if (bc) gm_tex_bc += bytes; }
            if (kind == GK_MEM) { gm_mem_type[bucket & 15] += bytes; gm_blk *b = gm_blk_find(h, 1); if (b) { b->size = bytes; b->type = (uint8_t)bucket; } }
            if (kind >= GK_BUF_GEOM && kind <= GK_BUF_OTHER) { int c = bytes < 16384 ? 0 : bytes < 262144 ? 1 : bytes < (4u << 20) ? 2 : bytes < (64u << 20) ? 3 : 4; gm_tab[i].bucket = (uint8_t)c; gm_bufhist_n[c]++; gm_bufhist_b[c] += bytes; }
            break;
        }
    }
    gm_report();
    os_unfair_lock_unlock(&gm_lock);
}
static void gm_del(uint64_t h) {
    if (!h || !gm_tab) return;
    os_unfair_lock_lock(&gm_lock);
    uint32_t i = (uint32_t)((h * 0x9E3779B97F4A7C15ull) >> 45) & (GM_N - 1);
    for (uint32_t k = 0; k < GM_N && gm_tab[i].h; k++, i = (i + 1) & (GM_N - 1)) if (gm_tab[i].h == h) {
        gm_ent e = gm_tab[i]; gm_tab[i].h = ~0ull;
        gm_bytes[e.kind] -= e.bytes; gm_count[e.kind]--;
        if (e.kind == GK_TEX) { gm_tex_b[e.bucket] -= e.bytes; gm_tex_n[e.bucket]--; if (e.bc) gm_tex_bc -= e.bytes; }
        if (e.kind == GK_MEM) { gm_mem_type[e.bucket & 15] -= e.bytes; gm_blk *b = gm_blk_find(h, 0); if (b) b->h = ~0ull; }
        if (e.kind >= GK_BUF_GEOM && e.kind <= GK_BUF_OTHER) { gm_bufhist_n[e.bucket]--; gm_bufhist_b[e.bucket] -= e.bytes; }
        if (e.mem) { gm_blk *b = gm_blk_find(e.mem, 0); if (b) { if (e.kind == GK_TEX || e.kind == GK_RT) { b->img_bytes -= e.bytes; b->nimg--; } else { b->buf_bytes -= e.bytes; b->nbuf--; } } }
        break;
    }
    os_unfair_lock_unlock(&gm_lock);
}
static VkResult VKAPI_CALL shim_cimg(VkDevice dev, const VkImageCreateInfo *ci, const VkAllocationCallbacks *a, VkImage *out) {
    VkResult r = real_cimg(dev, ci, a, out);
    if (r == VK_SUCCESS && ci && out && gm_on()) {
        double px = 0; uint32_t w = ci->extent.width, h = ci->extent.height, d = ci->extent.depth;
        for (uint32_t l = 0; l < ci->mipLevels; l++) { uint32_t lw = w >> l ? w >> l : 1, lh = h >> l ? h >> l : 1, ld = d >> l ? d >> l : 1; px += (double)lw * lh * ld; }
        uint64_t bytes = (uint64_t)(px * ci->arrayLayers * (ci->samples ? ci->samples : 1) * gm_bpp(ci->format) / 8.0);
        int rt = (ci->usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) != 0;
        uint32_t m = w > h ? w : h; int bucket = m >= 4096 ? 0 : m >= 2048 ? 1 : m >= 1024 ? 2 : m >= 512 ? 3 : 4;
        gm_add((uint64_t)*out, bytes, rt ? GK_RT : GK_TEX, bucket, ci->format >= 131 && ci->format <= 146);
    }
    return r;
}
static void VKAPI_CALL shim_dimg(VkDevice dev, VkImage img, const VkAllocationCallbacks *a) { if (gm_on()) gm_del((uint64_t)img); real_dimg(dev, img, a); }
static VkResult VKAPI_CALL shim_cbuf(VkDevice dev, const VkBufferCreateInfo *ci, const VkAllocationCallbacks *a, VkBuffer *out) {
    VkResult r = real_cbuf(dev, ci, a, out);
    if (r == VK_SUCCESS && ci && out && gm_on()) {
        VkBufferUsageFlags u = ci->usage;
        int kind = (u & (VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT)) ? GK_BUF_GEOM
                 : (u & (VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT)) ? GK_BUF_SHADER
                 : (u & ~(VkBufferUsageFlags)(VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)) == 0 ? GK_BUF_STAGING : GK_BUF_OTHER;
        gm_add((uint64_t)*out, ci->size, kind, 0, 0);
    }
    return r;
}
static void VKAPI_CALL shim_dbuf(VkDevice dev, VkBuffer b, const VkAllocationCallbacks *a) { if (gm_on()) gm_del((uint64_t)b); real_dbuf(dev, b, a); }
static VkResult VKAPI_CALL shim_amem(VkDevice dev, const VkMemoryAllocateInfo *ai, const VkAllocationCallbacks *a, VkDeviceMemory *out) {
    VkMemoryAllocateInfo real_ai; VkResult r;
    if (ai && g_split_clone >= 0 && (int)ai->memoryTypeIndex == g_split_clone) { real_ai = *ai; real_ai.memoryTypeIndex = (uint32_t)g_split_src; r = real_amem(dev, &real_ai, a, out); }
    else r = real_amem(dev, ai, a, out);
    if (r == VK_SUCCESS && ai && out && gm_on()) gm_add((uint64_t)*out, ai->allocationSize, GK_MEM, (int)(ai->memoryTypeIndex & 15), 0);
    return r;
}
static void VKAPI_CALL shim_fmem(VkDevice dev, VkDeviceMemory m, const VkAllocationCallbacks *a) { if (gm_on()) gm_del((uint64_t)m); real_fmem(dev, m, a); }
static void gm_bound(uint64_t obj, uint64_t mem) {
    if (!obj || !mem || !gm_tab) return;
    os_unfair_lock_lock(&gm_lock);
    uint32_t i = (uint32_t)((obj * 0x9E3779B97F4A7C15ull) >> 45) & (GM_N - 1);
    for (uint32_t k = 0; k < GM_N && gm_tab[i].h; k++, i = (i + 1) & (GM_N - 1)) if (gm_tab[i].h == obj) {
        gm_blk *b = gm_blk_find(mem, 0);
        if (b && !gm_tab[i].mem) { gm_tab[i].mem = mem; if (gm_tab[i].kind == GK_TEX || gm_tab[i].kind == GK_RT) { b->img_bytes += gm_tab[i].bytes; b->nimg++; } else { b->buf_bytes += gm_tab[i].bytes; b->nbuf++; } }
        break;
    }
    os_unfair_lock_unlock(&gm_lock);
}
static PFN_vkBindBufferMemory real_bbm = NULL; static PFN_vkBindImageMemory real_bim = NULL;
static PFN_vkBindBufferMemory2 real_bbm2 = NULL, real_bbm2khr = NULL; static PFN_vkBindImageMemory2 real_bim2 = NULL, real_bim2khr = NULL;
static VkResult VKAPI_CALL shim_bbm(VkDevice dev, VkBuffer b, VkDeviceMemory m, VkDeviceSize o) { VkResult r = real_bbm(dev, b, m, o); if (r == VK_SUCCESS && gm_on()) gm_bound((uint64_t)b, (uint64_t)m); return r; }
static VkResult VKAPI_CALL shim_bim(VkDevice dev, VkImage i, VkDeviceMemory m, VkDeviceSize o) { VkResult r = real_bim(dev, i, m, o); if (r == VK_SUCCESS && gm_on()) gm_bound((uint64_t)i, (uint64_t)m); return r; }
static VkResult VKAPI_CALL shim_bbm2(VkDevice dev, uint32_t n, const VkBindBufferMemoryInfo *p) { VkResult r = real_bbm2(dev, n, p); if (r == VK_SUCCESS && gm_on()) for (uint32_t i = 0; i < n; i++) gm_bound((uint64_t)p[i].buffer, (uint64_t)p[i].memory); return r; }
static VkResult VKAPI_CALL shim_bbm2khr(VkDevice dev, uint32_t n, const VkBindBufferMemoryInfo *p) { VkResult r = real_bbm2khr(dev, n, p); if (r == VK_SUCCESS && gm_on()) for (uint32_t i = 0; i < n; i++) gm_bound((uint64_t)p[i].buffer, (uint64_t)p[i].memory); return r; }
static VkResult VKAPI_CALL shim_bim2(VkDevice dev, uint32_t n, const VkBindImageMemoryInfo *p) { VkResult r = real_bim2(dev, n, p); if (r == VK_SUCCESS && gm_on()) for (uint32_t i = 0; i < n; i++) gm_bound((uint64_t)p[i].image, (uint64_t)p[i].memory); return r; }
static VkResult VKAPI_CALL shim_bim2khr(VkDevice dev, uint32_t n, const VkBindImageMemoryInfo *p) { VkResult r = real_bim2khr(dev, n, p); if (r == VK_SUCCESS && gm_on()) for (uint32_t i = 0; i < n; i++) gm_bound((uint64_t)p[i].image, (uint64_t)p[i].memory); return r; }
/* buffer memory requirements: with the split on, buffers may not use the original GPU-only type */
static PFN_vkGetBufferMemoryRequirements real_gbmr = NULL; static PFN_vkGetBufferMemoryRequirements2 real_gbmr2 = NULL, real_gbmr2khr = NULL;
static PFN_vkGetDeviceBufferMemoryRequirements real_gdbmr = NULL, real_gdbmrkhr = NULL;
static void VKAPI_CALL shim_gbmr(VkDevice dev, VkBuffer b, VkMemoryRequirements *r) { real_gbmr(dev, b, r); if (r) split_buffer_bits(&r->memoryTypeBits); }
static void VKAPI_CALL shim_gbmr2(VkDevice dev, const VkBufferMemoryRequirementsInfo2 *i, VkMemoryRequirements2 *r) { real_gbmr2(dev, i, r); if (r) split_buffer_bits(&r->memoryRequirements.memoryTypeBits); }
static void VKAPI_CALL shim_gbmr2khr(VkDevice dev, const VkBufferMemoryRequirementsInfo2 *i, VkMemoryRequirements2 *r) { real_gbmr2khr(dev, i, r); if (r) split_buffer_bits(&r->memoryRequirements.memoryTypeBits); }
static void VKAPI_CALL shim_gdbmr(VkDevice dev, const VkDeviceBufferMemoryRequirements *i, VkMemoryRequirements2 *r) { real_gdbmr(dev, i, r); if (r) split_buffer_bits(&r->memoryRequirements.memoryTypeBits); }
static void VKAPI_CALL shim_gdbmrkhr(VkDevice dev, const VkDeviceBufferMemoryRequirements *i, VkMemoryRequirements2 *r) { real_gdbmrkhr(dev, i, r); if (r) split_buffer_bits(&r->memoryRequirements.memoryTypeBits); }
/* ---- swapchain: log what the game asks for; MVKSHIM_PRESENT_MODE=immediate|fifo|mailbox overrides the present mode.
 * FIFO (vertical sync) on a 60 Hz mode snaps the frame rate to 60/n: 30, 20, 15, 12, 10. ---- */
static PFN_vkCreateSwapchainKHR real_csc = NULL;
static VkResult VKAPI_CALL shim_csc(VkDevice dev, const VkSwapchainCreateInfoKHR *ci, const VkAllocationCallbacks *a, VkSwapchainKHR *out) {
    static const char *names[] = { "IMMEDIATE (no sync)", "MAILBOX", "FIFO (vertical sync)", "FIFO_RELAXED" };
    VkSwapchainCreateInfoKHR c = *ci; const char *want = getenv("MVKSHIM_PRESENT_MODE"); int asked = (int)ci->presentMode;
    if (want && !strcasecmp(want, "immediate")) c.presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    else if (want && !strcasecmp(want, "fifo")) c.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    else if (want && !strcasecmp(want, "mailbox")) c.presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
    VkResult r = real_csc(dev, &c, a, out);
    if (r != VK_SUCCESS && c.presentMode != ci->presentMode) { c.presentMode = ci->presentMode; r = real_csc(dev, &c, a, out); }
    slog("swapchain: %ux%u, %u images, game asked for present mode %s, created with %s -> %d", ci->imageExtent.width, ci->imageExtent.height, ci->minImageCount,
         asked >= 0 && asked < 4 ? names[asked] : "?", (int)c.presentMode >= 0 && (int)c.presentMode < 4 ? names[c.presentMode] : "?", r);
    return r;
}
/* ---- frame statistics (diagnostic, MVKSHIM_FRAME_STATS=1): every 5 s one log line with frames per second and, per frame,
 * render passes, draws, and how many of the draws use a tessellation pipeline (with their patch count). ---- */
#include <stdatomic.h>
static int fst_on(void) { static int v = -1; if (v < 0) v = getenv("MVKSHIM_FRAME_STATS") != NULL; return v; }
#define PT_N (1u << 16)
static _Atomic uint64_t pt_tab[PT_N]; static uint8_t pt_cp[PT_N];
static atomic_ulong st_pipes, st_pipes_tess, st_pipes_gs, st_frames, st_draws, st_draws_tess, st_patches, st_passes, st_indirect;
static void pt_add(uint64_t h, int cp) { uint32_t i = (uint32_t)((h * 0x9E3779B97F4A7C15ull) >> 40) & (PT_N - 1);
    for (uint32_t k = 0; k < PT_N; k++, i = (i + 1) & (PT_N - 1)) { uint64_t cur = atomic_load(&pt_tab[i]); if (cur == h) return; if (cur == 0) { atomic_store(&pt_tab[i], h); pt_cp[i] = (uint8_t)(cp > 0 && cp < 255 ? cp : 3); return; } } }
static int pt_get(uint64_t h) { uint32_t i = (uint32_t)((h * 0x9E3779B97F4A7C15ull) >> 40) & (PT_N - 1);
    for (uint32_t k = 0; k < 64; k++, i = (i + 1) & (PT_N - 1)) { uint64_t cur = atomic_load(&pt_tab[i]); if (cur == h) return pt_cp[i]; if (cur == 0) return 0; } return 0; }
#define CBT_N (1u << 13)
static struct { _Atomic uint64_t cb; uint8_t cp; } cbt[CBT_N];
static inline uint32_t cbt_i(uint64_t cb) { return (uint32_t)((cb * 0x9E3779B97F4A7C15ull) >> 51) & (CBT_N - 1); }
static inline int cbt_cp(VkCommandBuffer cb) { uint32_t i = cbt_i((uint64_t)cb); return atomic_load(&cbt[i].cb) == (uint64_t)cb ? cbt[i].cp : 0; }
static PFN_vkCmdBindPipeline real_cbp = NULL; static PFN_vkCmdDraw real_cd = NULL; static PFN_vkCmdDrawIndexed real_cdi = NULL;
static PFN_vkCmdDrawIndirect real_cdn = NULL; static PFN_vkCmdDrawIndexedIndirect real_cdin = NULL;
static PFN_vkCmdBeginRenderPass real_brp = NULL; static PFN_vkCmdBeginRenderPass2 real_brp2 = NULL, real_brp2khr = NULL; static PFN_vkCmdBeginRendering real_br = NULL, real_brkhr = NULL;
static PFN_vkQueuePresentKHR real_qp = NULL;
static void VKAPI_CALL shim_cbp(VkCommandBuffer cb, VkPipelineBindPoint bp, VkPipeline p) {
    if (bp == VK_PIPELINE_BIND_POINT_GRAPHICS) { uint32_t i = cbt_i((uint64_t)cb); atomic_store(&cbt[i].cb, (uint64_t)cb); cbt[i].cp = (uint8_t)pt_get((uint64_t)p); }
    real_cbp(cb, bp, p);
}
static void VKAPI_CALL shim_cd(VkCommandBuffer cb, uint32_t vc, uint32_t ic, uint32_t fv, uint32_t fi) {
    int cp = cbt_cp(cb); atomic_fetch_add_explicit(&st_draws, 1, memory_order_relaxed);
    if (cp) { atomic_fetch_add_explicit(&st_draws_tess, 1, memory_order_relaxed); atomic_fetch_add_explicit(&st_patches, (unsigned long)(vc / cp) * (ic ? ic : 1), memory_order_relaxed); }
    real_cd(cb, vc, ic, fv, fi);
}
static void VKAPI_CALL shim_cdi(VkCommandBuffer cb, uint32_t idx, uint32_t ic, uint32_t fi, int32_t vo, uint32_t fin) {
    int cp = cbt_cp(cb); atomic_fetch_add_explicit(&st_draws, 1, memory_order_relaxed);
    if (cp) { atomic_fetch_add_explicit(&st_draws_tess, 1, memory_order_relaxed); atomic_fetch_add_explicit(&st_patches, (unsigned long)(idx / cp) * (ic ? ic : 1), memory_order_relaxed); }
    real_cdi(cb, idx, ic, fi, vo, fin);
}
static void VKAPI_CALL shim_cdn(VkCommandBuffer cb, VkBuffer b, VkDeviceSize o, uint32_t n, uint32_t st) {
    atomic_fetch_add_explicit(&st_draws, n, memory_order_relaxed); atomic_fetch_add_explicit(&st_indirect, n, memory_order_relaxed); if (cbt_cp(cb)) atomic_fetch_add_explicit(&st_draws_tess, n, memory_order_relaxed);
    real_cdn(cb, b, o, n, st);
}
static void VKAPI_CALL shim_cdin(VkCommandBuffer cb, VkBuffer b, VkDeviceSize o, uint32_t n, uint32_t st) {
    atomic_fetch_add_explicit(&st_draws, n, memory_order_relaxed); atomic_fetch_add_explicit(&st_indirect, n, memory_order_relaxed); if (cbt_cp(cb)) atomic_fetch_add_explicit(&st_draws_tess, n, memory_order_relaxed);
    real_cdin(cb, b, o, n, st);
}
static void VKAPI_CALL shim_brp(VkCommandBuffer cb, const VkRenderPassBeginInfo *i, VkSubpassContents c) { atomic_fetch_add_explicit(&st_passes, 1, memory_order_relaxed); real_brp(cb, i, c); }
static void VKAPI_CALL shim_brp2(VkCommandBuffer cb, const VkRenderPassBeginInfo *i, const VkSubpassBeginInfo *c) { atomic_fetch_add_explicit(&st_passes, 1, memory_order_relaxed); real_brp2(cb, i, c); }
static void VKAPI_CALL shim_brp2khr(VkCommandBuffer cb, const VkRenderPassBeginInfo *i, const VkSubpassBeginInfo *c) { atomic_fetch_add_explicit(&st_passes, 1, memory_order_relaxed); real_brp2khr(cb, i, c); }
static void VKAPI_CALL shim_br(VkCommandBuffer cb, const VkRenderingInfo *i) { atomic_fetch_add_explicit(&st_passes, 1, memory_order_relaxed); real_br(cb, i); }
static void VKAPI_CALL shim_brkhr(VkCommandBuffer cb, const VkRenderingInfo *i) { atomic_fetch_add_explicit(&st_passes, 1, memory_order_relaxed); real_brkhr(cb, i); }
static VkResult VKAPI_CALL shim_qp(VkQueue q, const VkPresentInfoKHR *pi) {
    static time_t last; static os_unfair_lock lk = OS_UNFAIR_LOCK_INIT;
    unsigned long f = atomic_fetch_add(&st_frames, 1) + 1; time_t now = time(NULL);
    if (now - last >= 5 && os_unfair_lock_trylock(&lk)) {
        if (last && f) {
            double fr = (double)atomic_exchange(&st_frames, 0), secs = (double)(now - last);
            unsigned long d = atomic_exchange(&st_draws, 0), dt = atomic_exchange(&st_draws_tess, 0), pa = atomic_exchange(&st_patches, 0), rp = atomic_exchange(&st_passes, 0), in = atomic_exchange(&st_indirect, 0);
            if (fr < 1) fr = 1;
            slog("frames: %.1f fps | per frame: %.0f render passes, %.0f draws (%.0f indirect), %.0f of them tessellated with %.0f patches | pipelines so far: %lu (%lu with tessellation, %lu with a geometry stage)",
                 fr / secs, rp / fr, d / fr, in / fr, dt / fr, pa / fr, atomic_load(&st_pipes), atomic_load(&st_pipes_tess), atomic_load(&st_pipes_gs));
        } else { atomic_store(&st_frames, 0); atomic_store(&st_draws, 0); atomic_store(&st_draws_tess, 0); atomic_store(&st_patches, 0); atomic_store(&st_passes, 0); atomic_store(&st_indirect, 0); }
        last = now; os_unfair_lock_unlock(&lk);
    }
    return real_qp(q, pi);
}
static VkResult VKAPI_CALL shim_cgp(VkDevice dev, VkPipelineCache cache, uint32_t n, const VkGraphicsPipelineCreateInfo *cis, const VkAllocationCallbacks *a, VkPipeline *out);
static VkResult VKAPI_CALL shim_cgp_outer(VkDevice dev, VkPipelineCache cache, uint32_t n, const VkGraphicsPipelineCreateInfo *cis, const VkAllocationCallbacks *a, VkPipeline *out) {
    VkResult r = shim_cgp(dev, cache, n, cis, a, out);
    for (uint32_t i = 0; i < n && cis && out; i++) {
        int tess = 0, gs = 0;
        for (uint32_t k = 0; k < cis[i].stageCount; k++) { if (cis[i].pStages[k].stage & (VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT)) tess = 1; if (cis[i].pStages[k].stage == VK_SHADER_STAGE_GEOMETRY_BIT) gs = 1; }
        atomic_fetch_add(&st_pipes, 1); if (gs) atomic_fetch_add(&st_pipes_gs, 1);
        if (tess) { unsigned long c = atomic_fetch_add(&st_pipes_tess, 1) + 1; if (out[i]) pt_add((uint64_t)out[i], cis[i].pTessellationState ? (int)cis[i].pTessellationState->patchControlPoints : 3);
            if (c <= 5 || c % 100 == 0) slog("pipeline with tessellation stages #%lu (%u control points per patch) -> %d", c, cis[i].pTessellationState ? cis[i].pTessellationState->patchControlPoints : 0, r); }
    }
    return r;
}
static PFN_vkVoidFunction fst_hook(const char *name, PFN_vkVoidFunction f) {
    if (!fst_on()) return NULL;
    if (!strcmp(name, "vkCmdBindPipeline")) { real_cbp = (PFN_vkCmdBindPipeline)f; return (PFN_vkVoidFunction)shim_cbp; }
    if (!strcmp(name, "vkCmdDraw")) { real_cd = (PFN_vkCmdDraw)f; return (PFN_vkVoidFunction)shim_cd; }
    if (!strcmp(name, "vkCmdDrawIndexed")) { real_cdi = (PFN_vkCmdDrawIndexed)f; return (PFN_vkVoidFunction)shim_cdi; }
    if (!strcmp(name, "vkCmdDrawIndirect")) { real_cdn = (PFN_vkCmdDrawIndirect)f; return (PFN_vkVoidFunction)shim_cdn; }
    if (!strcmp(name, "vkCmdDrawIndexedIndirect")) { real_cdin = (PFN_vkCmdDrawIndexedIndirect)f; return (PFN_vkVoidFunction)shim_cdin; }
    if (!strcmp(name, "vkCmdBeginRenderPass")) { real_brp = (PFN_vkCmdBeginRenderPass)f; return (PFN_vkVoidFunction)shim_brp; }
    if (!strcmp(name, "vkCmdBeginRenderPass2")) { real_brp2 = (PFN_vkCmdBeginRenderPass2)f; return (PFN_vkVoidFunction)shim_brp2; }
    if (!strcmp(name, "vkCmdBeginRenderPass2KHR")) { real_brp2khr = (PFN_vkCmdBeginRenderPass2)f; return (PFN_vkVoidFunction)shim_brp2khr; }
    if (!strcmp(name, "vkCmdBeginRendering")) { real_br = (PFN_vkCmdBeginRendering)f; return (PFN_vkVoidFunction)shim_br; }
    if (!strcmp(name, "vkCmdBeginRenderingKHR")) { real_brkhr = (PFN_vkCmdBeginRendering)f; return (PFN_vkVoidFunction)shim_brkhr; }
    if (!strcmp(name, "vkQueuePresentKHR")) { real_qp = (PFN_vkQueuePresentKHR)f; return (PFN_vkVoidFunction)shim_qp; }
    return NULL;
}
static PFN_vkVoidFunction gm_hook(const char *name, PFN_vkVoidFunction f) {
    { PFN_vkVoidFunction g = fst_hook(name, f); if (g) return g; }
    if (!strcmp(name, "vkCreateSwapchainKHR")) { real_csc = (PFN_vkCreateSwapchainKHR)f; return (PFN_vkVoidFunction)shim_csc; }
    if (!strcmp(name, "vkBindBufferMemory"))  { real_bbm = (PFN_vkBindBufferMemory)f; return (PFN_vkVoidFunction)shim_bbm; }
    if (!strcmp(name, "vkBindImageMemory"))   { real_bim = (PFN_vkBindImageMemory)f; return (PFN_vkVoidFunction)shim_bim; }
    if (!strcmp(name, "vkBindBufferMemory2")) { real_bbm2 = (PFN_vkBindBufferMemory2)f; return (PFN_vkVoidFunction)shim_bbm2; }
    if (!strcmp(name, "vkBindBufferMemory2KHR")) { real_bbm2khr = (PFN_vkBindBufferMemory2)f; return (PFN_vkVoidFunction)shim_bbm2khr; }
    if (!strcmp(name, "vkBindImageMemory2"))  { real_bim2 = (PFN_vkBindImageMemory2)f; return (PFN_vkVoidFunction)shim_bim2; }
    if (!strcmp(name, "vkBindImageMemory2KHR")) { real_bim2khr = (PFN_vkBindImageMemory2)f; return (PFN_vkVoidFunction)shim_bim2khr; }
    if (!strcmp(name, "vkGetBufferMemoryRequirements"))  { real_gbmr = (PFN_vkGetBufferMemoryRequirements)f; return (PFN_vkVoidFunction)shim_gbmr; }
    if (!strcmp(name, "vkGetBufferMemoryRequirements2")) { real_gbmr2 = (PFN_vkGetBufferMemoryRequirements2)f; return (PFN_vkVoidFunction)shim_gbmr2; }
    if (!strcmp(name, "vkGetBufferMemoryRequirements2KHR")) { real_gbmr2khr = (PFN_vkGetBufferMemoryRequirements2)f; return (PFN_vkVoidFunction)shim_gbmr2khr; }
    if (!strcmp(name, "vkGetDeviceBufferMemoryRequirements")) { real_gdbmr = (PFN_vkGetDeviceBufferMemoryRequirements)f; return (PFN_vkVoidFunction)shim_gdbmr; }
    if (!strcmp(name, "vkGetDeviceBufferMemoryRequirementsKHR")) { real_gdbmrkhr = (PFN_vkGetDeviceBufferMemoryRequirements)f; return (PFN_vkVoidFunction)shim_gdbmrkhr; }
    if (!strcmp(name, "vkCreateImage"))    { real_cimg = (PFN_vkCreateImage)f; return (PFN_vkVoidFunction)shim_cimg; }
    if (!strcmp(name, "vkDestroyImage"))   { real_dimg = (PFN_vkDestroyImage)f; return (PFN_vkVoidFunction)shim_dimg; }
    if (!strcmp(name, "vkCreateBuffer"))   { real_cbuf = (PFN_vkCreateBuffer)f; return (PFN_vkVoidFunction)shim_cbuf; }
    if (!strcmp(name, "vkDestroyBuffer"))  { real_dbuf = (PFN_vkDestroyBuffer)f; return (PFN_vkVoidFunction)shim_dbuf; }
    if (!strcmp(name, "vkAllocateMemory")) { real_amem = (PFN_vkAllocateMemory)f; return (PFN_vkVoidFunction)shim_amem; }
    if (!strcmp(name, "vkFreeMemory"))     { real_fmem = (PFN_vkFreeMemory)f; return (PFN_vkVoidFunction)shim_fmem; }
    return NULL;
}


static PFN_vkVoidFunction VKAPI_CALL shim_gdpa(VkDevice dev, const char *name) {
    resolve_pipeline_fns();
    PFN_vkVoidFunction f = real_gdpa(dev, name);
    if (!f || !name) return f;
    if (!strcmp(name, "vkCreateGraphicsPipelines")) { real_cgp = (PFN_vkCreateGraphicsPipelines)f; return (PFN_vkVoidFunction)shim_cgp_outer; }
    if (!strcmp(name, "vkGetDeviceProcAddr")) return (PFN_vkVoidFunction)shim_gdpa;
    if (!strcmp(name, "vkCreateShaderModule")) { real_csm = (PFN_vkCreateShaderModule)f; return (PFN_vkVoidFunction)shim_csm; }
    if (!strcmp(name, "vkDestroyShaderModule")) { real_dsm = (PFN_vkDestroyShaderModule)f; return (PFN_vkVoidFunction)shim_dsm; }
    { PFN_vkVoidFunction g = gm_hook(name, f); if (g) return g; }
    return f;
}
PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice dev, const char *name) { return shim_gdpa(dev, name); }
VkResult VKAPI_CALL vkCreateShaderModule(VkDevice dev, const VkShaderModuleCreateInfo *ci, const VkAllocationCallbacks *a, VkShaderModule *out) { return shim_csm(dev, ci, a, out); }
void VKAPI_CALL vkDestroyShaderModule(VkDevice dev, VkShaderModule m, const VkAllocationCallbacks *a) { shim_dsm(dev, m, a); }
VkResult VKAPI_CALL vkCreateGraphicsPipelines(VkDevice dev, VkPipelineCache cache, uint32_t n, const VkGraphicsPipelineCreateInfo *cis, const VkAllocationCallbacks *a, VkPipeline *out) { return shim_cgp_outer(dev, cache, n, cis, a, out); }


/* ---- debug messenger ----
 * The game registers a VK_EXT_debug_utils messenger, and MoltenVK then calls back into the game for every warning, on
 * whichever thread produced it. When the engine rebuilds all pipelines at the start of a level load, fourteen worker
 * threads report the harmless "Metal does not support disabling primitive restart" warning at once and the callback path
 * deadlocks against the main thread (black window at "Loading level megamap"; seen 2026-11-08 18:13 and 2026-11-09 10:33).
 * We accept the messenger but never register it with MoltenVK, so nothing calls back into the game. MoltenVK still prints
 * its messages to stderr (/private/tmp/sc-game-stderr-<pid>.log). MVKSHIM_KEEP_DEBUG_MESSENGER=1 restores the callbacks. */
static int keep_messenger(void) { return getenv("MVKSHIM_KEEP_DEBUG_MESSENGER") != NULL; }
#define SHIM_FAKE_MESSENGER ((VkDebugUtilsMessengerEXT)(uintptr_t)0x5C0FFEE5C0FFEE10ull)
static VkResult VKAPI_CALL shim_create_messenger(VkInstance inst, const VkDebugUtilsMessengerCreateInfoEXT *ci, const VkAllocationCallbacks *a, VkDebugUtilsMessengerEXT *out) {
    if (keep_messenger()) { PFN_vkCreateDebugUtilsMessengerEXT f = (PFN_vkCreateDebugUtilsMessengerEXT)real_gipa(inst, "vkCreateDebugUtilsMessengerEXT"); return f ? f(inst, ci, a, out) : VK_ERROR_EXTENSION_NOT_PRESENT; }
    if (out) *out = SHIM_FAKE_MESSENGER;
    slog("debug messenger: accepted, not registered with MoltenVK (no warning callbacks into the game)");
    return VK_SUCCESS;
}
static void VKAPI_CALL shim_destroy_messenger(VkInstance inst, VkDebugUtilsMessengerEXT m, const VkAllocationCallbacks *a) {
    if (m == SHIM_FAKE_MESSENGER || m == VK_NULL_HANDLE) return;
    PFN_vkDestroyDebugUtilsMessengerEXT f = (PFN_vkDestroyDebugUtilsMessengerEXT)real_gipa(inst, "vkDestroyDebugUtilsMessengerEXT"); if (f) f(inst, m, a);
}
VkResult VKAPI_CALL vkCreateDebugUtilsMessengerEXT(VkInstance inst, const VkDebugUtilsMessengerCreateInfoEXT *ci, const VkAllocationCallbacks *a, VkDebugUtilsMessengerEXT *out) { load_real(); return shim_create_messenger(inst, ci, a, out); }
void VKAPI_CALL vkDestroyDebugUtilsMessengerEXT(VkInstance inst, VkDebugUtilsMessengerEXT m, const VkAllocationCallbacks *a) { load_real(); shim_destroy_messenger(inst, m, a); }

PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance inst, const char *name) {
    load_real();
    PFN_vkVoidFunction f = real_gipa(inst, name);
    if (!f || !name) return f;
    if (!strcmp(name, "vkGetInstanceProcAddr")) return (PFN_vkVoidFunction)vkGetInstanceProcAddr;
    if (!strcmp(name, "vkCreateDebugUtilsMessengerEXT")) return (PFN_vkVoidFunction)shim_create_messenger;
    if (!strcmp(name, "vkDestroyDebugUtilsMessengerEXT")) return (PFN_vkVoidFunction)shim_destroy_messenger;
    if (!strcmp(name, "vkCreateInstance")) return (PFN_vkVoidFunction)shim_create_instance;
    if (!strcmp(name, "vkEnumerateInstanceExtensionProperties") || !strcmp(name, "vkEnumeratePhysicalDevices") || !strcmp(name, "vkCreateDevice") || !strcmp(name, "vkEnumerateDeviceExtensionProperties")) slog("GIPA(%s) -> %p", name, f);
    if (!strcmp(name, "vkGetPhysicalDeviceProperties"))     { real_gpdp = (PFN_vkGetPhysicalDeviceProperties)f; return (PFN_vkVoidFunction)shim_gpdp; }
    if (!strcmp(name, "vkGetPhysicalDeviceProperties2"))    { real_gpdp2 = (PFN_vkGetPhysicalDeviceProperties2)f; return (PFN_vkVoidFunction)shim_gpdp2; }
    if (!strcmp(name, "vkGetPhysicalDeviceProperties2KHR")) { real_gpdp2khr = (PFN_vkGetPhysicalDeviceProperties2)f; return (PFN_vkVoidFunction)shim_gpdp2khr; }
    if (!strcmp(name, "vkGetPhysicalDeviceMemoryProperties"))     { real_gpdmp = (PFN_vkGetPhysicalDeviceMemoryProperties)f; return (PFN_vkVoidFunction)shim_gpdmp; }
    if (!strcmp(name, "vkGetPhysicalDeviceMemoryProperties2"))    { real_gpdmp2 = (PFN_vkGetPhysicalDeviceMemoryProperties2)f; return (PFN_vkVoidFunction)shim_gpdmp2; }
    if (!strcmp(name, "vkGetPhysicalDeviceMemoryProperties2KHR")) { real_gpdmp2khr = (PFN_vkGetPhysicalDeviceMemoryProperties2)f; return (PFN_vkVoidFunction)shim_gpdmp2khr; }
    if (!strcmp(name, "vkGetPhysicalDeviceFeatures"))     { real_gpdf = (PFN_vkGetPhysicalDeviceFeatures)f; return (PFN_vkVoidFunction)shim_gpdf; }
    if (!strcmp(name, "vkGetPhysicalDeviceFeatures2"))    { real_gpdf2 = (PFN_vkGetPhysicalDeviceFeatures2)f; return (PFN_vkVoidFunction)shim_gpdf2; }
    if (!strcmp(name, "vkGetPhysicalDeviceFeatures2KHR")) { real_gpdf2khr = (PFN_vkGetPhysicalDeviceFeatures2)f; return (PFN_vkVoidFunction)shim_gpdf2khr; }
    if (!strcmp(name, "vkCreateDevice"))                  { real_create_device = (PFN_vkCreateDevice)f; return (PFN_vkVoidFunction)shim_create_device; }
    if (!strcmp(name, "vkEnumerateDeviceExtensionProperties")) { real_edep = (PFN_vkEnumerateDeviceExtensionProperties)f; return (PFN_vkVoidFunction)shim_edep; }
    if (!strcmp(name, "vkCreateGraphicsPipelines")) { real_cgp = (PFN_vkCreateGraphicsPipelines)f; return (PFN_vkVoidFunction)shim_cgp_outer; }
    if (!strcmp(name, "vkGetDeviceProcAddr"))       { real_gdpa = (PFN_vkGetDeviceProcAddr)f; return (PFN_vkVoidFunction)shim_gdpa; }
    if (!strcmp(name, "vkCreateShaderModule"))      { real_csm = (PFN_vkCreateShaderModule)f; return (PFN_vkVoidFunction)shim_csm; }
    if (!strcmp(name, "vkDestroyShaderModule"))     { real_dsm = (PFN_vkDestroyShaderModule)f; return (PFN_vkVoidFunction)shim_dsm; }
    { PFN_vkVoidFunction g = gm_hook(name, f); if (g) return g; }
    return f;
}
PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance inst, const char *name) { return vkGetInstanceProcAddr(inst, name); }

// Direct exports some loaders call without going through GIPA
static VkResult VKAPI_CALL shim_create_instance(const VkInstanceCreateInfo *ci, const VkAllocationCallbacks *a, VkInstance *out) {
    load_real();
    if (ci) { slog("vkCreateInstance: api %u.%u, %u extensions, %u layers", ci->pApplicationInfo ? VK_API_VERSION_MAJOR(ci->pApplicationInfo->apiVersion) : 0, ci->pApplicationInfo ? VK_API_VERSION_MINOR(ci->pApplicationInfo->apiVersion) : 0, ci->enabledExtensionCount, ci->enabledLayerCount);
        for (uint32_t i = 0; i < ci->enabledExtensionCount; i++) slog("   ext: %s", ci->ppEnabledExtensionNames[i]); }
    VkResult r = ((PFN_vkCreateInstance)real_gipa(NULL, "vkCreateInstance"))(ci, a, out);
    slog("vkCreateInstance -> %d", (int)r); return r;
}
VkResult VKAPI_CALL vkCreateInstance(const VkInstanceCreateInfo *ci, const VkAllocationCallbacks *a, VkInstance *out) { return shim_create_instance(ci, a, out); }
VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(const char *layer, uint32_t *n, VkExtensionProperties *p) {
    load_real(); return ((PFN_vkEnumerateInstanceExtensionProperties)real_gipa(NULL, "vkEnumerateInstanceExtensionProperties"))(layer, n, p);
}
VkResult VKAPI_CALL vkEnumerateInstanceVersion(uint32_t *v) {
    load_real(); return ((PFN_vkEnumerateInstanceVersion)real_gipa(NULL, "vkEnumerateInstanceVersion"))(v);
}
VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties(uint32_t *n, VkLayerProperties *p) {
    load_real(); return ((PFN_vkEnumerateInstanceLayerProperties)real_gipa(NULL, "vkEnumerateInstanceLayerProperties"))(n, p);
}
void VKAPI_CALL vkGetPhysicalDeviceProperties(VkPhysicalDevice pd, VkPhysicalDeviceProperties *p) {
    load_real(); if (!real_gpdp) real_gpdp = (PFN_vkGetPhysicalDeviceProperties)dlsym(real, "vkGetPhysicalDeviceProperties"); shim_gpdp(pd, p);
}
void VKAPI_CALL vkGetPhysicalDeviceProperties2(VkPhysicalDevice pd, VkPhysicalDeviceProperties2 *p) {
    load_real(); if (!real_gpdp2) real_gpdp2 = (PFN_vkGetPhysicalDeviceProperties2)dlsym(real, "vkGetPhysicalDeviceProperties2"); shim_gpdp2(pd, p);
}
void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice pd, VkPhysicalDeviceMemoryProperties *p) {
    load_real(); if (!real_gpdmp) real_gpdmp = (PFN_vkGetPhysicalDeviceMemoryProperties)dlsym(real, "vkGetPhysicalDeviceMemoryProperties"); shim_gpdmp(pd, p);
}
void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties2(VkPhysicalDevice pd, VkPhysicalDeviceMemoryProperties2 *p) {
    load_real(); if (!real_gpdmp2) real_gpdmp2 = (PFN_vkGetPhysicalDeviceMemoryProperties2)dlsym(real, "vkGetPhysicalDeviceMemoryProperties2"); shim_gpdmp2(pd, p);
}
VkResult VKAPI_CALL vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t *v) {
    load_real(); PFN_vkVoidFunction f = (PFN_vkVoidFunction)dlsym(real, "vk_icdNegotiateLoaderICDInterfaceVersion");
    if (f) return ((VkResult (VKAPI_PTR *)(uint32_t*))f)(v); if (*v > 5) *v = 5; return VK_SUCCESS;
}

void VKAPI_CALL vkGetPhysicalDeviceFeatures(VkPhysicalDevice pd, VkPhysicalDeviceFeatures *f) { load_real(); if (!real_gpdf) real_gpdf = (PFN_vkGetPhysicalDeviceFeatures)dlsym(real, "vkGetPhysicalDeviceFeatures"); shim_gpdf(pd, f); }
void VKAPI_CALL vkGetPhysicalDeviceFeatures2(VkPhysicalDevice pd, VkPhysicalDeviceFeatures2 *f) { load_real(); if (!real_gpdf2) real_gpdf2 = (PFN_vkGetPhysicalDeviceFeatures2)dlsym(real, "vkGetPhysicalDeviceFeatures2"); shim_gpdf2(pd, f); }
VkResult VKAPI_CALL vkCreateDevice(VkPhysicalDevice pd, const VkDeviceCreateInfo *ci, const VkAllocationCallbacks *a, VkDevice *out) { load_real(); if (!real_create_device) real_create_device = (PFN_vkCreateDevice)dlsym(real, "vkCreateDevice"); return shim_create_device(pd, ci, a, out); }

VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(VkPhysicalDevice pd, const char *layer, uint32_t *count, VkExtensionProperties *props) { load_real(); if (!real_edep) real_edep = (PFN_vkEnumerateDeviceExtensionProperties)dlsym(real, "vkEnumerateDeviceExtensionProperties"); return shim_edep(pd, layer, count, props); }
