/* sleeper.exe: a process that does nothing for a while (a dummy target to test inject.exe on). */
#include <windows.h>
#include <stdio.h>
int main(void) { setvbuf(stdout, NULL, _IONBF, 0); printf("sleeper pid %lu\n", GetCurrentProcessId()); Sleep(25000); printf("sleeper done, macfix.dll %s\n", GetModuleHandleA("macfix.dll") ? "is loaded" : "is NOT loaded"); return 0; }
