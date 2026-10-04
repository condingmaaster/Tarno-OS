#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv) {
    char *p = malloc(32);
    strcpy(p, "hello from win32");
    printf("%s %d %s\n", p, argc, argc > 1 ? argv[1] : "-");
    free(p);
    return 7;
}
