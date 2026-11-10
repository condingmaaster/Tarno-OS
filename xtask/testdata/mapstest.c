#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    pthread_attr_t a; void *addr; size_t size;
    int r = pthread_getattr_np(pthread_self(), &a);
    int r2 = r == 0 ? pthread_attr_getstack(&a, &addr, &size) : -1;
    printf("maps-test r=%d r2=%d stack_size_ok=%d\n", r, r2, size >= 65536);
    FILE *f = fopen("/proc/self/maps", "r"); char l[200]; int n = 0, rx = 0;
    while (f && fgets(l, sizeof l, f)) { n++; if (strstr(l, "r-xp")) rx++; }
    printf("maps-lines=%d exec-runs=%d\n", n, rx);
    return 0;
}
