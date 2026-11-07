// SPDX-License-Identifier: GPL-2.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <unistd.h>
#define CHUNK (1024*1024)
int main(void) {
    char *b=malloc(CHUNK);assert(b);memset(b,'F',CHUNK);
    int f=open("/tmp/full-filler",O_CREAT|O_TRUNC|O_RDWR,0600);assert(f>=0);
    struct statvfs base,before,after;assert(statvfs("/",&base)==0);unsigned chunks=0;off_t failed_pos=0;
    for(;;) {
        assert(statvfs("/",&before)==0);off_t pos=lseek(f,0,SEEK_CUR);errno=0;
        ssize_t n=write(f,b,CHUNK);
        if(n<0) {
            assert(errno==ENOSPC);assert(lseek(f,0,SEEK_CUR)==pos);
            assert(statvfs("/",&after)==0&&after.f_bfree==before.f_bfree);
            assert(pread(f,b,16,pos-16)==16);for(int i=0;i<16;i++)assert(b[i]=='F');
            failed_pos=pos;
            break;
        }
        assert(n==CHUNK&&++chunks<1024);
        if(chunks%64==0){printf("disk-full progress: %u MiB\n",chunks);fflush(stdout);}
    }
    assert(ftruncate(f,0)==0);assert(statvfs("/",&after)==0&&after.f_bfree==base.f_bfree);
    assert(lseek(f,0,SEEK_CUR)==failed_pos&&lseek(f,0,SEEK_SET)==0);
    assert(write(f,"after-full",10)==10&&fsync(f)==0);assert(lseek(f,0,SEEK_SET)==0&&read(f,b,10)==10&&!memcmp(b,"after-full",10));
    close(f);unlink("/tmp/full-filler");free(b);puts("disk-full-test-done: failed allocation leaves free count, old contents and fd offset unchanged; space reclaimed and writing recovers");return 0;
}
