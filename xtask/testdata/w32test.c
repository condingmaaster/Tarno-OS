// SPDX-License-Identifier: GPL-2.0-or-later
// Win32 API test: the kernel32 calls THOS's kernel32x.dll adds (time, files, directories, FindFirstFile, memory).
#include <windows.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    DWORD t0 = GetTickCount();
    LARGE_INTEGER pc, pf; QueryPerformanceCounter(&pc); QueryPerformanceFrequency(&pf);
    SYSTEMTIME st; GetSystemTime(&st);
    printf("k1 year-%s tick-%s qpc-%s pid-%s\n", st.wYear >= 2025 ? "ok" : "bad", t0 > 0 ? "ok" : "bad", (pc.QuadPart > 0 && pf.QuadPart > 0) ? "ok" : "bad", GetCurrentProcessId() ? "ok" : "bad");
    CreateDirectoryA("C:\\tmp\\w32dir", 0);
    HANDLE h = CreateFileA("C:\\tmp\\w32dir\\a.txt", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    DWORD n; WriteFile(h, "0123456789", 10, &n, 0); CloseHandle(h);
    h = CreateFileA("C:\\tmp\\w32dir\\b.log", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    WriteFile(h, "xyz", 3, &n, 0); CloseHandle(h);
    h = CreateFileA("C:\\tmp\\w32dir\\a.txt", GENERIC_READ, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    DWORD sz = GetFileSize(h, 0);
    SetFilePointer(h, 4, 0, FILE_BEGIN);
    char buf[8] = {0}; ReadFile(h, buf, 3, &n, 0); CloseHandle(h);
    printf("k2 size-%lu read-%s attr-%s\n", (unsigned long)sz, buf, GetFileAttributesA("C:\\tmp\\w32dir") == FILE_ATTRIBUTE_DIRECTORY ? "dir" : "bad");
    WIN32_FIND_DATAA fd; int count = 0; unsigned long total = 0;
    HANDLE f = FindFirstFileA("C:\\tmp\\w32dir\\*.txt", &fd);
    if (f != INVALID_HANDLE_VALUE) { do { count++; total += fd.nFileSizeLow; } while (FindNextFileA(f, &fd)); FindClose(f); }
    printf("k3 found-%d bytes-%lu name-%s\n", count, total, count ? fd.cFileName : "-");
    MoveFileA("C:\\tmp\\w32dir\\b.log", "C:\\tmp\\w32dir\\c.log");
    printf("k4 moved-%s\n", GetFileAttributesA("C:\\tmp\\w32dir\\c.log") != INVALID_FILE_ATTRIBUTES ? "ok" : "bad");
    DeleteFileA("C:\\tmp\\w32dir\\a.txt"); DeleteFileA("C:\\tmp\\w32dir\\c.log"); RemoveDirectoryA("C:\\tmp\\w32dir");
    printf("k5 gone-%s\n", GetFileAttributesA("C:\\tmp\\w32dir") == INVALID_FILE_ATTRIBUTES ? "ok" : "bad");
    char *p = (char *)LocalAlloc(LPTR, 100); strcpy(p, "heap"); p = (char *)HeapReAlloc(GetProcessHeap(), 0, p, 5000);
    printf("k6 %s\n", p);
    return 0;
}
