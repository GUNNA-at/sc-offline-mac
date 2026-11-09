/* peek.exe <process name> <hex address> [qwords]: print memory of a running process in this bottle (read-only). */
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 3) return 2;
    DWORD pid = 0; HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0); PROCESSENTRY32 e; e.dwSize = sizeof e;
    if (Process32First(snap, &e)) do { if (!_stricmp(e.szExeFile, argv[1])) pid = e.th32ProcessID; } while (Process32Next(snap, &e));
    HANDLE p = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!p) { printf("no process\n"); return 1; }
    for (int a = 2; a + 1 <= argc; a += 2) {
        unsigned long long addr = strtoull(argv[a], NULL, 16); int n = a + 1 < argc ? atoi(argv[a + 1]) : 1;
        for (int i = 0; i < n; i++) { unsigned long long v = 0; SIZE_T got = 0;
            if (ReadProcessMemory(p, (void *)(addr + 8ull * i), &v, 8, &got)) printf("%llx: %016llx\n", addr + 8ull * i, v); else printf("%llx: unreadable (%lu)\n", addr + 8ull * i, GetLastError()); }
    }
    return 0;
}
