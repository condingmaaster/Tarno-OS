// SPDX-License-Identifier: GPL-2.0-or-later
// Win32 threads on THOS: CreateThread, CRITICAL_SECTION, events, semaphores, WaitForMultipleObjects.
#include <windows.h>
#include <stdio.h>

static CRITICAL_SECTION cs;
static volatile long counter;
static HANDLE ev, sem;

static DWORD WINAPI worker(LPVOID arg) {
    for (int i = 0; i < 1000; i++) { EnterCriticalSection(&cs); counter++; LeaveCriticalSection(&cs); }
    return (DWORD)(ULONG_PTR)arg;
}
static DWORD WINAPI signaller(LPVOID arg) {
    (void)arg;
    ReleaseSemaphore(sem, 2, 0);
    SetEvent(ev);
    return 0;
}

int main(void) {
    InitializeCriticalSection(&cs);
    HANDLE th[4];
    for (int i = 0; i < 4; i++) th[i] = CreateThread(0, 0, worker, (LPVOID)(ULONG_PTR)i, 0, 0);
    DWORD r = WaitForMultipleObjects(4, th, TRUE, INFINITE);
    printf("t1 counter-%ld wait-%s\n", counter, r == WAIT_OBJECT_0 ? "ok" : "bad");
    ev = CreateEventA(0, TRUE, FALSE, 0);
    sem = CreateSemaphoreA(0, 0, 10, 0);
    HANDLE s = CreateThread(0, 0, signaller, 0, 0, 0);
    DWORD a = WaitForSingleObject(ev, 5000), b = WaitForSingleObject(sem, 5000), c = WaitForSingleObject(sem, 5000);
    WaitForSingleObject(s, 5000);
    printf("t2 event-%s sem1-%s sem2-%s\n", a == WAIT_OBJECT_0 ? "ok" : "bad", b == WAIT_OBJECT_0 ? "ok" : "bad", c == WAIT_OBJECT_0 ? "ok" : "bad");
    HANDLE m = CreateMutexA(0, FALSE, 0);
    DWORD ma = WaitForSingleObject(m, 1000); ReleaseMutex(m);
    printf("t3 mutex-%s\n", ma == WAIT_OBJECT_0 ? "ok" : "bad");
    return 0;
}
