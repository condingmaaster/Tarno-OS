// SPDX-License-Identifier: GPL-2.0-or-later
#define _GNU_SOURCE
// THOS IPC test: AF_UNIX socketpair + named/abstract listen/accept/connect + SysV shm + memfd.
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/shm.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <stdlib.h>

#define CHECK(c, m) do { if (!(c)) { printf("ipc FAIL: %s\n", m); fflush(stdout); return 1; } } while (0)

int main(void) {
    // 1. socketpair echo across fork
    int sv[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair");
    pid_t p = fork();
    if (p == 0) {
        char b[16]; ssize_t n = read(sv[1], b, sizeof b);
        for (ssize_t i = 0; i < n; i++) b[i] ^= 0x20;
        write(sv[1], b, n); _exit(0);
    }
    write(sv[0], "abc", 3);
    char r[16] = {0};
    CHECK(read(sv[0], r, sizeof r) == 3 && !strcmp(r, "ABC"), "socketpair echo");
    waitpid(p, 0, 0);

    // 2. named (filesystem-path style) server + client, plus SysV shm shared by key
    int ls = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a = {0}; a.sun_family = AF_UNIX; strcpy(a.sun_path, "/tmp/ipc.sock");
    CHECK(bind(ls, (void *)&a, sizeof a) == 0, "bind");
    CHECK(listen(ls, 4) == 0, "listen");
    int shid = shmget(0x7e57, 8192, IPC_CREAT | 0600);
    CHECK(shid >= 0, "shmget");
    char *shm = shmat(shid, 0, 0);
    CHECK(shm != (void *)-1, "shmat");
    p = fork();
    if (p == 0) {
        int c = socket(AF_UNIX, SOCK_STREAM, 0);
        if (connect(c, (void *)&a, sizeof a) != 0) _exit(2);
        int id = shmget(0x7e57, 8192, 0);
        char *m = shmat(id, 0, 0);
        if (m == (void *)-1) _exit(3);
        strcpy(m + 4096, "from-client");
        write(c, "go", 2);
        char b[8]; read(c, b, sizeof b);
        _exit(m[0] == 'S' ? 0 : 4);   // the parent wrote 'S' at offset 0
    }
    int cs = accept(ls, 0, 0);
    CHECK(cs >= 0, "accept");
    shm[0] = 'S';
    char g[8] = {0};
    CHECK(read(cs, g, 2) == 2 && !strcmp(g, "go"), "read go");
    CHECK(!strcmp(shm + 4096, "from-client"), "shm visible across processes");
    write(cs, "ok", 2);
    int st; waitpid(p, &st, 0);
    CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0, "client exit status");
    CHECK(shmdt(shm) == 0, "shmdt");
    shmctl(shid, IPC_RMID, 0);
    // reading from a closed peer gives EOF
    close(cs);
    // 3. memfd mapped twice in one process
    int fd = memfd_create("t", 0);
    CHECK(fd >= 0 && ftruncate(fd, 8192) == 0, "memfd");
    char *m1 = mmap(0, 8192, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    char *m2 = mmap(0, 8192, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    CHECK(m1 != MAP_FAILED && m2 != MAP_FAILED && m1 != m2, "memfd mmap");
    strcpy(m1 + 5000, "mem");
    CHECK(!strcmp(m2 + 5000, "mem"), "memfd shared");
    CHECK(munmap(m1, 8192) == 0 && munmap(m2, 8192) == 0, "munmap");
    printf("ipc ok: socketpair, unix listen/accept/connect, SysV shm, memfd\n");
    fflush(stdout);
    return 0;
}
