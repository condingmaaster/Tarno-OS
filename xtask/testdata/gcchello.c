#include <stdio.h>
#include <string.h>
int main(void) {
    char buf[64];
    snprintf(buf, sizeof buf, "gcc-built-%d-%s", 6 * 7, "on-thos");
    puts(buf);
    return (int)strlen(buf) == 17 ? 0 : 1;
}
