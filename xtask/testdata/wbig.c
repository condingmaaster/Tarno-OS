/* A broad Win32/CRT exerciser: math, printf formats, strings, wide characters, time, stdio, descriptors, W-APIs. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <time.h>
#include <wchar.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <windows.h>
static int fails;
#define CHECK(name, cond) do { if (cond) printf("ok   %s\n", name); else { printf("FAIL %s\n", name); fails++; } } while (0)
static int cmp(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }
static int nearly(double a, double b) { return fabs(a - b) < 1e-9 * (fabs(b) > 1 ? fabs(b) : 1); }
int main(int argc, char **argv) {
    char buf[256];
    (void)argc; (void)argv;
    /* math */
    CHECK("sin", nearly(sin(1.0), 0.8414709848078965));
    CHECK("cos", nearly(cos(1.0), 0.5403023058681398));
    CHECK("tan", nearly(tan(1.0), 1.5574077246549023));
    CHECK("atan2", nearly(atan2(1.0, 2.0), 0.4636476090008061));
    CHECK("asin/acos", nearly(asin(0.5), 0.5235987755982989) && nearly(acos(0.5), 1.0471975511965979));
    CHECK("sqrt", sqrt(2.0) == 1.4142135623730951);
    CHECK("pow", nearly(pow(2.0, 10.0), 1024.0) && nearly(pow(2.0, 0.5), 1.4142135623730951) && nearly(pow(-2.0, 3.0), -8.0) && nearly(pow(10.0, -2.0), 0.01));
    CHECK("exp/log", nearly(exp(1.0), 2.718281828459045) && nearly(log(10.0), 2.302585092994046) && nearly(log10(1000.0), 3.0));
    CHECK("floor/ceil", floor(-1.5) == -2.0 && ceil(-1.5) == -1.0 && floor(2.7) == 2.0 && ceil(2.1) == 3.0);
    CHECK("fmod", nearly(fmod(10.5, 3.0), 1.5));
    CHECK("sinh/tanh", nearly(sinh(1.0), 1.1752011936438014) && nearly(tanh(0.5), 0.46211715726000974));
    /* printf */
    snprintf(buf, sizeof buf, "%d|%5.2f|%g|%e|%lld|%c|%s|%x", 42, 3.14159, 2.5, 12345.678, 1LL << 40, 'z', "str", 255);
    CHECK("printf", strcmp(buf, "42| 3.14|2.5|1.234568e+04|1099511627776|z|str|ff") == 0);
    snprintf(buf, sizeof buf, "%.3f %.0f %g %g %G", 0.0005, 2.5, 100000.0, 1e-5, 1.5e20);
    CHECK("printf2", strcmp(buf, "0.001 3 100000 1e-05 1.5E+20") == 0 || strcmp(buf, "0.001 2 100000 1e-05 1.5E+20") == 0);
    snprintf(buf, sizeof buf, "[%-8s][%8s][%08.3f][%+d][%#x]", "ab", "cd", -3.14159, 7, 255);
    CHECK("printf3", strcmp(buf, "[ab      ][      cd][-003.142][+7][0xff]") == 0);
    /* strings / stdlib */
    int v[5] = { 5, 3, 9, 1, 7 };
    qsort(v, 5, sizeof v[0], cmp);
    CHECK("qsort", v[0] == 1 && v[4] == 9);
    CHECK("strstr/strrchr", strcmp(strstr("hello world", "wor"), "world") == 0 && strcmp(strrchr("a/b/c", '/'), "/c") == 0);
    CHECK("strtol/strtod", strtol("0x1f", 0, 16) == 31 && nearly(strtod("2.5e3", 0), 2500.0) && atoi("  -17") == -17);
    char *dup = strdup("a,b,c"); char *t = strtok(dup, ","); CHECK("strtok", t && strcmp(t, "a") == 0);
    int a = 0, b = 0; sscanf("12 34", "%d %d", &a, &b); CHECK("sscanf", a + b == 46);
    CHECK("ctype", isalpha('a') && !isdigit('a') && toupper('q') == 'Q' && isspace(' '));
    CHECK("env", getenv("PATH") != 0 && getenv("NOPE_NOT_SET") == 0);
    putenv("MYVAR=hello"); CHECK("putenv", getenv("MYVAR") && strcmp(getenv("MYVAR"), "hello") == 0);
    /* time */
    time_t now = time(0); struct tm *lt = localtime(&now);
    CHECK("localtime", lt && lt->tm_year >= 120);
    struct tm tm0 = { 0 }; tm0.tm_year = 100; tm0.tm_mon = 0; tm0.tm_mday = 1;
    CHECK("mktime", mktime(&tm0) == 946684800);
    strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S %a %b", gmtime(&(time_t){ 86400 * 365 }));
    CHECK("strftime", strcmp(buf, "1971-01-01 00:00:00 Fri Jan") == 0);
    /* wide characters */
    wchar_t w[32]; wcscpy(w, L"wide"); wcscat(w, L"-chars");
    CHECK("wcs", wcslen(w) == 10 && wcscmp(w, L"wide-chars") == 0 && wcschr(w, L'-') && wcsstr(w, L"chars"));
    swprintf(w, 32, L"%ls=%d %ls", L"x", 7, L"wsz");
    CHECK("swprintf", wcscmp(w, L"x=7 wsz") == 0);
    CHECK("wcstol", wcstol(L"123", 0, 10) == 123 && _wcsicmp(L"ABC", L"abc") == 0 && towupper(L'a') == L'A');
    /* stdio + descriptors */
    FILE *f = fopen("C:\\tmp\\wbig.tmp", "w"); fputs("line1\nline2\n", f); fprintf(f, "n=%d\n", 5); fclose(f);
    f = fopen("C:\\tmp\\wbig.tmp", "r"); char l1[32], l2[32]; fgets(l1, 32, f); fgets(l2, 32, f); fclose(f);
    CHECK("stdio", strcmp(l1, "line1\n") == 0 && strcmp(l2, "line2\n") == 0);
    int fd = _open("C:\\tmp\\wbig.tmp", _O_RDONLY | _O_BINARY); char rb[8]; int n = _read(fd, rb, 5); struct _stat64 st; _fstat64(fd, &st);
    CHECK("_open/_read", fd >= 0 && n == 5 && memcmp(rb, "line1", 5) == 0 && st.st_size == 16);
    CHECK("_lseeki64", _lseeki64(fd, 2, SEEK_SET) == 2); _close(fd);
    CHECK("_stat64", _stat64("C:\\tmp\\wbig.tmp", &st) == 0 && (st.st_mode & _S_IFREG) && st.st_size == 16);
    CHECK("access", _access("C:\\tmp\\wbig.tmp", 0) == 0 && _access("C:\\tmp\\nonexistent.zzz", 0) != 0);
    remove("C:\\tmp\\wbig.tmp");
    CHECK("removed", _access("C:\\tmp\\wbig.tmp", 0) != 0);
    /* Win32 W-APIs */
    HANDLE h = CreateFileW(L"C:\\tmp\\wbig2.tmp", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0); DWORD wn = 0; WriteFile(h, "xyz", 3, &wn, 0); CloseHandle(h);
    CHECK("CreateFileW", wn == 3 && GetFileAttributesW(L"C:\\tmp\\wbig2.tmp") != INVALID_FILE_ATTRIBUTES);
    WIN32_FIND_DATAW fd2; HANDLE fh = FindFirstFileW(L"C:\\tmp\\wbig2.*", &fd2);
    CHECK("FindFirstFileW", fh != INVALID_HANDLE_VALUE && wcscmp(fd2.cFileName, L"wbig2.tmp") == 0 && fd2.nFileSizeLow == 3);
    if (fh != INVALID_HANDLE_VALUE) FindClose(fh);
    CHECK("DeleteFileW", DeleteFileW(L"C:\\tmp\\wbig2.tmp") && GetFileAttributesW(L"C:\\tmp\\wbig2.tmp") == INVALID_FILE_ATTRIBUTES);
    wchar_t wpath[MAX_PATH]; CHECK("GetModuleFileNameW", GetModuleFileNameW(0, wpath, MAX_PATH) > 0);
    CHECK("GetCommandLineW", GetCommandLineW() != 0 && wcslen(GetCommandLineW()) > 0);
    DWORD tls = TlsAlloc(); TlsSetValue(tls, (void *)0x1234); CHECK("TLS", TlsGetValue(tls) == (void *)0x1234); TlsFree(tls);
    LONG iv = 5; CHECK("Interlocked", InterlockedIncrement(&iv) == 6 && InterlockedExchangeAdd(&iv, 4) == 6 && iv == 10);
    SYSTEMTIME stm; FILETIME ftm; GetSystemTime(&stm); SystemTimeToFileTime(&stm, &ftm); SYSTEMTIME back; FileTimeToSystemTime(&ftm, &back);
    CHECK("time conversion", back.wYear == stm.wYear && back.wDay == stm.wDay && back.wSecond == stm.wSecond);
    char tp[MAX_PATH]; CHECK("GetTempPathA", GetTempPathA(MAX_PATH, tp) > 0);
    MEMORYSTATUSEX ms; ms.dwLength = sizeof ms; CHECK("GlobalMemoryStatusEx", GlobalMemoryStatusEx(&ms) && ms.ullTotalPhys > 0);
    printf("wbig-done fails=%d\n", fails);
    return fails;
}
