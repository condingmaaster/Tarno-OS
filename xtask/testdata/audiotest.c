// SPDX-License-Identifier: GPL-2.0-or-later
// Plays one second of a 440 Hz tone through /dev/dsp (48 kHz, signed 16-bit, stereo).
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(void) {
    int fd = open("/dev/dsp", O_WRONLY);
    if (fd < 0) { printf("audio FAIL: no /dev/dsp\n"); return 1; }
    int speed = 48000, ch = 2, fmt = 0x10;
    ioctl(fd, 0xC0045005, &fmt);
    ioctl(fd, 0xC0045006, &ch);
    ioctl(fd, 0xC0045002, &speed);
    static short buf[48000 * 2];
    for (int i = 0; i < 48000; i++) {
        short v = (short)(12000.0 * sin(2.0 * 3.14159265358979 * 440.0 * i / 48000.0));
        buf[2 * i] = v; buf[2 * i + 1] = v;
    }
    ssize_t n = write(fd, buf, sizeof buf);
    close(fd);
    printf("audio-played %zd bytes at %d Hz\n", n, speed);
    fflush(stdout);
    return n == (ssize_t)sizeof buf ? 0 : 1;
}
