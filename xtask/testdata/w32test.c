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
    int exok = 1;
    f = FindFirstFileExA("C:\\tmp\\w32dir\\A.TXT", FindExInfoBasic, &fd, FindExSearchNameMatch, 0, FIND_FIRST_EX_LARGE_FETCH);
    exok &= f != INVALID_HANDLE_VALUE;
    if (f != INVALID_HANDLE_VALUE) { exok &= strcmp(fd.cFileName, "a.txt") == 0 && fd.nFileSizeLow == 10 && fd.cAlternateFileName[0] == 0; FindClose(f); }
    f = FindFirstFileExA("C:\\tmp\\w32dir\\A.TXT", FindExInfoStandard, &fd, FindExSearchNameMatch, 0, FIND_FIRST_EX_CASE_SENSITIVE);
    exok &= f == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_NOT_FOUND;
    if (f != INVALID_HANDLE_VALUE) FindClose(f);
    CreateDirectoryA("C:\\tmp\\w32dir\\item-one", 0); CreateDirectoryA("C:\\tmp\\w32dir\\item-two", 0);
    h = CreateFileA("C:\\tmp\\w32dir\\item-file", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0); CloseHandle(h);
    int dirs = 0;
    f = FindFirstFileExA("C:\\tmp\\w32dir\\item-*", FindExInfoBasic, &fd, FindExSearchLimitToDirectories, 0, 0);
    if (f != INVALID_HANDLE_VALUE) { do { dirs++; exok &= (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0; } while (FindNextFileA(f, &fd)); exok &= GetLastError() == ERROR_NO_MORE_FILES; FindClose(f); }
    exok &= dirs == 2;
    WIN32_FIND_DATAW fw;
    f = FindFirstFileExW(L"C:\\tmp\\w32dir\\a.txt", FindExInfoBasic, &fw, FindExSearchNameMatch, 0, 7);
    exok &= f != INVALID_HANDLE_VALUE;
    if (f != INVALID_HANDLE_VALUE) { exok &= fw.cFileName[0] == L'a' && fw.cAlternateFileName[0] == 0 && fw.nFileSizeLow == 10; FindClose(f); }
    f = FindFirstFileExA("C:\\tmp\\w32dir\\*", (FINDEX_INFO_LEVELS)2, &fd, FindExSearchNameMatch, 0, 0);
    exok &= f == INVALID_HANDLE_VALUE && GetLastError() == ERROR_INVALID_PARAMETER;
    f = FindFirstFileExW(L"C:\\tmp\\w32dir\\*", FindExInfoStandard, &fw, FindExSearchLimitToDevices, 0, 0);
    exok &= f == INVALID_HANDLE_VALUE && GetLastError() == ERROR_NOT_SUPPORTED;
    f = FindFirstFileExA("C:\\tmp\\w32dir\\*", FindExInfoStandard, &fd, FindExSearchNameMatch, 0, 8);
    exok &= f == INVALID_HANDLE_VALUE && GetLastError() == ERROR_INVALID_PARAMETER;
    f = FindFirstFileExA("C:\\tmp\\w32dir\\*", FindExInfoStandard, &fd, FindExSearchNameMatch, &fd, 0);
    exok &= f == INVALID_HANDLE_VALUE && GetLastError() == ERROR_INVALID_PARAMETER;
    printf("k7 find-ex-%s\n", exok ? "ok" : "bad");
    DeleteFileA("C:\\tmp\\w32dir\\item-file"); RemoveDirectoryA("C:\\tmp\\w32dir\\item-one"); RemoveDirectoryA("C:\\tmp\\w32dir\\item-two");
    MoveFileA("C:\\tmp\\w32dir\\b.log", "C:\\tmp\\w32dir\\c.log");
    printf("k4 moved-%s\n", GetFileAttributesA("C:\\tmp\\w32dir\\c.log") != INVALID_FILE_ATTRIBUTES ? "ok" : "bad");
    DeleteFileA("C:\\tmp\\w32dir\\a.txt"); DeleteFileA("C:\\tmp\\w32dir\\c.log"); RemoveDirectoryA("C:\\tmp\\w32dir");
    printf("k5 gone-%s\n", GetFileAttributesA("C:\\tmp\\w32dir") == INVALID_FILE_ATTRIBUTES ? "ok" : "bad");
    char *p = (char *)LocalAlloc(LPTR, 100); strcpy(p, "heap"); p = (char *)HeapReAlloc(GetProcessHeap(), 0, p, 5000);
    printf("k6 %s\n", p);
    return exok ? 0 : 1;
}
