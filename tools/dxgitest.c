/* dxgitest.exe: what DXGI and Direct3D 11 report in this prefix. The game logs the same things as "Logging video
 * adapters" and refuses to start when the adapter has no name, no output or no feature level 11.1.
 *   x86_64-w64-mingw32-gcc -O2 -o dxgitest.exe dxgitest.c -ldxgi -ld3d11 -ldxguid -luuid */
#define COBJMACROS
#include <windows.h>
#include <dxgi.h>
#include <d3d11.h>
#include <stdio.h>

int main(void)
{
    IDXGIFactory1 *f = NULL; IDXGIAdapter1 *a; HRESULT hr; UINT i, j;
    HMODULE m;
    char path[MAX_PATH];
    static const D3D_FEATURE_LEVEL want[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };

    hr = CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&f);
    printf("CreateDXGIFactory1 -> %08lx\n", (unsigned long)hr);
    if (FAILED(hr)) return 1;
    for (i = 0; IDXGIFactory1_EnumAdapters1(f, i, &a) != DXGI_ERROR_NOT_FOUND; i++) {
        DXGI_ADAPTER_DESC1 d; IDXGIOutput *o; ID3D11Device *dev = NULL; D3D_FEATURE_LEVEL got = 0;
        memset(&d, 0, sizeof(d));
        hr = IDXGIAdapter1_GetDesc1(a, &d);
        printf("adapter %u: GetDesc1 -> %08lx  '%ls'  vendor %04x device %04x  video memory %llu MB  flags %x  luid %08lx:%08lx\n",
               i, (unsigned long)hr, d.Description, d.VendorId, d.DeviceId, (unsigned long long)(d.DedicatedVideoMemory >> 20), d.Flags,
               (unsigned long)d.AdapterLuid.HighPart, (unsigned long)d.AdapterLuid.LowPart);
        for (j = 0; IDXGIAdapter1_EnumOutputs(a, j, &o) != DXGI_ERROR_NOT_FOUND; j++) {
            DXGI_OUTPUT_DESC od; memset(&od, 0, sizeof(od));
            IDXGIOutput_GetDesc(o, &od);
            printf("   output %u: '%ls'  %ldx%ld  attached %d\n", j, od.DeviceName,
                   od.DesktopCoordinates.right - od.DesktopCoordinates.left, od.DesktopCoordinates.bottom - od.DesktopCoordinates.top, od.AttachedToDesktop);
            IDXGIOutput_Release(o);
            if (j > 8) break;
        }
        if (!j) printf("   no outputs\n");
        hr = D3D11CreateDevice((IDXGIAdapter *)a, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0, want, 3, D3D11_SDK_VERSION, &dev, &got, NULL);
        printf("   D3D11CreateDevice -> %08lx  feature level %x\n", (unsigned long)hr, got);
        if (dev) ID3D11Device_Release(dev);
        IDXGIAdapter1_Release(a);
        if (i > 8) break;
    }
    if (!i) printf("no adapters\n");
    m = GetModuleHandleA("d3d11.dll"); path[0] = 0; if (m) GetModuleFileNameA(m, path, MAX_PATH);
    printf("d3d11.dll loaded from %s%s\n", path, GetProcAddress(m, "DXVK_RegisterAnnotation") || GetModuleHandleA("wined3d.dll") == NULL ? "  (not wined3d)" : "  (wined3d is loaded)");
    m = GetModuleHandleA("dxgi.dll"); path[0] = 0; if (m) GetModuleFileNameA(m, path, MAX_PATH);
    printf("dxgi.dll loaded from %s\n", path);
    printf("wined3d.dll %s, winevulkan.dll %s\n", GetModuleHandleA("wined3d.dll") ? "loaded" : "not loaded", GetModuleHandleA("winevulkan.dll") ? "loaded" : "not loaded");
    return 0;
}
