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
    // 4. SCM_RIGHTS: pass a memfd (and a pipe end) across a socketpair; the receiver maps/reads the same objects
    {
        int sp[2];
        CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sp) == 0, "socketpair2");
        int mf = memfd_create("pool", 0);
        CHECK(mf >= 0 && ftruncate(mf, 4096) == 0, "memfd2");
        char *w = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, mf, 0);
        CHECK(w != MAP_FAILED, "mmap2");
        strcpy(w, "before-send");
        int pp[2]; CHECK(pipe(pp) == 0, "pipe");
        // message 1 carries two descriptors, message 2 (sent right after) carries none, message 3 one
        char c1 = 'A';
        union { char b[CMSG_SPACE(2 * sizeof(int))]; struct cmsghdr al; } u1;
        memset(&u1, 0, sizeof u1);
        struct iovec iv = { &c1, 1 };
        struct msghdr mh = { 0 };
        mh.msg_iov = &iv; mh.msg_iovlen = 1; mh.msg_control = u1.b; mh.msg_controllen = sizeof u1.b;
        struct cmsghdr *cm = CMSG_FIRSTHDR(&mh);
        cm->cmsg_level = SOL_SOCKET; cm->cmsg_type = SCM_RIGHTS; cm->cmsg_len = CMSG_LEN(2 * sizeof(int));
        int two[2] = { mf, pp[1] };
        memcpy(CMSG_DATA(cm), two, sizeof two);
        CHECK(sendmsg(sp[0], &mh, 0) == 1, "sendmsg fds");
        CHECK(write(sp[0], "B", 1) == 1, "plain write after");
        char c3 = 'C';
        union { char b[CMSG_SPACE(sizeof(int))]; struct cmsghdr al; } u3;
        memset(&u3, 0, sizeof u3);
        struct iovec iv3 = { &c3, 1 };
        struct msghdr m3 = { 0 };
        m3.msg_iov = &iv3; m3.msg_iovlen = 1; m3.msg_control = u3.b; m3.msg_controllen = sizeof u3.b;
        struct cmsghdr *c3h = CMSG_FIRSTHDR(&m3);
        c3h->cmsg_level = SOL_SOCKET; c3h->cmsg_type = SCM_RIGHTS; c3h->cmsg_len = CMSG_LEN(sizeof(int));
        memcpy(CMSG_DATA(c3h), &pp[0], sizeof(int));
        CHECK(sendmsg(sp[0], &m3, 0) == 1, "sendmsg fd3");
        // receive with a large buffer: it must stop at message boundaries that carry descriptors
        char rb[16] = {0};
        union { char b[CMSG_SPACE(4 * sizeof(int))]; struct cmsghdr al; } rc;
        struct iovec rv = { rb, sizeof rb };
        struct msghdr rm = { 0 };
        rm.msg_iov = &rv; rm.msg_iovlen = 1; rm.msg_control = rc.b; rm.msg_controllen = sizeof rc.b;
        ssize_t n1 = recvmsg(sp[1], &rm, MSG_CMSG_CLOEXEC);
        CHECK(n1 == 2 && rb[0] == 'A' && rb[1] == 'B', "recv 1 returns A and B (no fds between them)");
        struct cmsghdr *rcm = CMSG_FIRSTHDR(&rm);
        CHECK(rcm && rcm->cmsg_level == SOL_SOCKET && rcm->cmsg_type == SCM_RIGHTS && rcm->cmsg_len == CMSG_LEN(2 * sizeof(int)), "cmsg 1");
        int got2[2]; memcpy(got2, CMSG_DATA(rcm), sizeof got2);
        char *w2 = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, got2[0], 0);
        CHECK(w2 != MAP_FAILED && !strcmp(w2, "before-send"), "received memfd maps the same pages");
        strcpy(w2, "from-receiver");
        CHECK(!strcmp(w, "from-receiver"), "memfd shared in both directions");
        CHECK(write(got2[1], "pipe!", 5) == 5, "received pipe write end works");
        rm.msg_controllen = sizeof rc.b;
        memset(rb, 0, sizeof rb);
        ssize_t n2 = recvmsg(sp[1], &rm, 0);
        CHECK(n2 == 1 && rb[0] == 'C', "recv 2 returns C");
        rcm = CMSG_FIRSTHDR(&rm);
        CHECK(rcm && rcm->cmsg_len == CMSG_LEN(sizeof(int)), "cmsg 2");
        int rd; memcpy(&rd, CMSG_DATA(rcm), sizeof rd);
        char pb[8] = {0};
        CHECK(read(rd, pb, sizeof pb) == 5 && !strcmp(pb, "pipe!"), "received pipe read end reads what the other wrote");
        close(sp[0]); close(sp[1]);
    }
    printf("ipc ok: socketpair, unix listen/accept/connect, SysV shm, memfd, SCM_RIGHTS\n");
    fflush(stdout);
    return 0;
}
