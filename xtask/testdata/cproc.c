#include <windows.h>
#include <stdio.h>
static int run(const char *cmd, DWORD *code) {
    STARTUPINFOA si; PROCESS_INFORMATION pi; memset(&si, 0, sizeof si); si.cb = sizeof si;
    char buf[512]; snprintf(buf, sizeof buf, "%s", cmd);
    fflush(stdout);
    if (!CreateProcessA(NULL, buf, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) { printf("cproc FAIL create %s err=%lu\n", cmd, GetLastError()); return 0; }
    DWORD w = WaitForSingleObject(pi.hProcess, 20000);
    GetExitCodeProcess(pi.hProcess, code);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return w == WAIT_OBJECT_0;
}
int main(void) {
    DWORD c1 = 99, c2 = 99;
    int a = run("C:\\whello.exe one two", &c1);
    int b = run("/busybox echo from-elf-child", &c2);
    printf("cproc windows-child waited=%d code=%lu | elf-child waited=%d code=%lu\n", a, c1, b, c2);
    DWORD c3; int missing = CreateProcessA(NULL, "C:\\nonexistent-program.exe", NULL, NULL, FALSE, 0, NULL, NULL, &(STARTUPINFOA){ sizeof(STARTUPINFOA) }, &(PROCESS_INFORMATION){ 0 });
    (void)c3;
    printf("cproc missing program refused=%d\n", !missing);
    puts("cproc-done");
    return 0;
}
