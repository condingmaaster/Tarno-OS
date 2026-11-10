// SPDX-License-Identifier: GPL-2.0-or-later
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/sendfile.h>
#include <unistd.h>
int main(void) {
    const char *text="ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    int f=open("/tmp/sendfile-input",O_CREAT|O_TRUNC|O_RDWR,0600);
    assert(f>=0&&write(f,text,26)==26);
    assert(lseek(f,3,SEEK_SET)==3);
    int d=open("/tmp/sendfile-output",O_CREAT|O_TRUNC|O_RDWR,0600);assert(d>=0);
    off_t offset=10;
    assert(sendfile(d,f,&offset,9)==9&&offset==19);
    assert(lseek(f,0,SEEK_CUR)==3);
    assert(sendfile(d,f,0,4)==4&&lseek(f,0,SEEK_CUR)==7);
    char result[20]={0};assert(lseek(d,0,SEEK_SET)==0&&read(d,result,13)==13);
    assert(!memcmp(result,"KLMNOPQRSDEFG",13));
    offset=-1;errno=0;
    assert(sendfile(d,f,&offset,1)==-1&&errno==EINVAL&&offset==-1);
    assert(lseek(f,0,SEEK_CUR)==7);
    offset=99;assert(sendfile(d,f,&offset,0)==0&&offset==99&&lseek(f,0,SEEK_CUR)==7);
    close(d);close(f);puts("sendfile offsets passed");return 0;
}
