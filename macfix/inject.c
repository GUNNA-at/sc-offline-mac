/* inject.exe <process name or pid> <dll path>          load a DLL into a process that is already running in this bottle
 * inject.exe <process name or pid> <dll path> unload   call its sco_plugin_unload there, then free it
 * Used to try macfix.dll on a running game without restarting it. */
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static DWORD find_pid(const char *name) {
    DWORD pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 e;
    e.dwSize = sizeof e;
    if (snap == INVALID_HANDLE_VALUE) return 0;
    if (Process32First(snap, &e)) do { if (!_stricmp(e.szExeFile, name)) pid = e.th32ProcessID; } while (Process32Next(snap, &e));
    CloseHandle(snap);
    return pid;
}

/* kernel32's base in the target, so the address of LoadLibraryW there does not rest on an assumption. */
static HMODULE remote_module(HANDLE proc, const char *base_name) {
    HMODULE mods[1024];
    DWORD need = 0;
    if (!EnumProcessModulesEx(proc, mods, sizeof mods, &need, LIST_MODULES_64BIT)) return NULL;
    for (DWORD i = 0; i < need / sizeof(HMODULE) && i < 1024; i++) {
        char n[MAX_PATH] = "";
        if (GetModuleBaseNameA(proc, mods[i], n, sizeof n) && !_stricmp(n, base_name)) return mods[i];
    }
    return NULL;
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 3) { printf("usage: inject.exe <process name or pid> <dll path>\n"); return 2; }
    DWORD pid = (DWORD)atoi(argv[1]);
    if (!pid) pid = find_pid(argv[1]);
    if (!pid) { printf("no process %s\n", argv[1]); return 1; }
    wchar_t path[MAX_PATH];
    if (!MultiByteToWideChar(CP_ACP, 0, argv[2], -1, path, MAX_PATH)) { printf("bad path\n"); return 1; }
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) { printf("no such file: %s\n", argv[2]); return 1; }

    const int unload = argc > 3 && !strcmp(argv[3], "unload");
    HANDLE proc = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
                              FALSE, pid);
    if (!proc) { printf("OpenProcess(%lu) failed: %lu\n", pid, GetLastError()); return 1; }
    HMODULE local_k32 = GetModuleHandleA("kernel32.dll"), remote_k32 = remote_module(proc, "kernel32.dll");
    if (!remote_k32) { printf("kernel32 not found in process %lu (%lu)\n", pid, GetLastError()); return 1; }
    if (unload) {
        const char *base = strrchr(argv[2], '\\');
        base = base ? base + 1 : argv[2];
        HMODULE remote = remote_module(proc, base);
        if (!remote) { printf("%s is not loaded in process %lu\n", base, pid); return 1; }
        HMODULE local = LoadLibraryExA(argv[2], NULL, DONT_RESOLVE_DLL_REFERENCES);      /* mapped only: nothing of it runs here */
        FARPROC fn = local ? GetProcAddress(local, "sco_plugin_unload") : NULL;
        if (!fn) { printf("could not find sco_plugin_unload in %s\n", argv[2]); return 1; }
        HANDLE t1 = CreateRemoteThread(proc, NULL, 0, (LPTHREAD_START_ROUTINE)((char *)remote + ((char *)fn - (char *)local)), NULL, 0, NULL);
        if (!t1) { printf("CreateRemoteThread failed: %lu\n", GetLastError()); return 1; }
        const DWORD w1 = WaitForSingleObject(t1, 8000);
        Sleep(700);                                                                      /* calls still inside the DLL finish */
        const size_t frva = (size_t)((char *)GetProcAddress(local_k32, "FreeLibrary") - (char *)local_k32);
        HANDLE t2 = CreateRemoteThread(proc, NULL, 0, (LPTHREAD_START_ROUTINE)((char *)remote_k32 + frva), remote, 0, NULL);
        DWORD code2 = 0;
        if (t2) { WaitForSingleObject(t2, 8000); GetExitCodeThread(t2, &code2); }
        printf("process %lu: %s stopped (%s) and %s\n", pid, base, w1 == WAIT_OBJECT_0 ? "finished" : "did not finish", code2 ? "freed" : "NOT freed");
        return code2 ? 0 : 1;
    }
    const size_t rva = (size_t)((char *)GetProcAddress(local_k32, "LoadLibraryW") - (char *)local_k32);
    LPTHREAD_START_ROUTINE load = (LPTHREAD_START_ROUTINE)((char *)remote_k32 + rva);

    const size_t bytes = (wcslen(path) + 1) * sizeof(wchar_t);
    void *mem = VirtualAllocEx(proc, NULL, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!mem || !WriteProcessMemory(proc, mem, path, bytes, NULL)) { printf("could not write the path into the process: %lu\n", GetLastError()); return 1; }
    HANDLE th = CreateRemoteThread(proc, NULL, 0, load, mem, 0, NULL);
    if (!th) { printf("CreateRemoteThread failed: %lu\n", GetLastError()); return 1; }
    const DWORD w = WaitForSingleObject(th, 15000);
    DWORD code = 0;
    GetExitCodeThread(th, &code);
    printf("process %lu: LoadLibraryW(%s) %s, returned 0x%08lx%s\n", pid, argv[2], w == WAIT_OBJECT_0 ? "finished" : "did not finish in 15 s", code,
           code ? "" : " (load failed)");
    if (w == WAIT_OBJECT_0) VirtualFreeEx(proc, mem, 0, MEM_RELEASE);
    CloseHandle(th);
    CloseHandle(proc);
    return code ? 0 : 1;
}
