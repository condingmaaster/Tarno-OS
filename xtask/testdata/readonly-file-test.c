// SPDX-License-Identifier: GPL-2.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
int main(void){
 setbuf(stdout,0);
 for(int large=0;large<2;large++){
  const char *path=large?"/tmp/thos-readonly-large":"/tmp/thos-readonly-small";
  size_t size=large?524288:4096;unlink(path);
  int rw=open(path,O_CREAT|O_TRUNC|O_RDWR,0600);assert(rw>=0);
  char block[4096];memset(block,0x5a,sizeof block);
  for(size_t n=0;n<size;n+=sizeof block)assert(write(rw,block,sizeof block)==sizeof block);
  assert(fsync(rw)==0);int ro=open(path,O_RDONLY);assert(ro>=0);
  errno=0;ssize_t result=write(ro,"BAD",3);int error=errno;
  printf("readonly-write large=%d result=%ld errno=%d flags=%d\n",large,(long)result,error,fcntl(ro,F_GETFL));
  assert(result==-1&&error==EBADF);assert((fcntl(ro,F_GETFL)&O_ACCMODE)==O_RDONLY);
  errno=0;assert(pwrite(ro,"BAD",3,0)==-1&&errno==EBADF);
  errno=0;assert(ftruncate(ro,0)==-1&&(errno==EBADF||errno==EINVAL));
  struct stat a,b;assert(fstat(ro,&a)==0&&fstat(rw,&b)==0&&a.st_ino==b.st_ino&&a.st_size==(off_t)size);
  assert(fchmod(ro,0600)==0);assert(lseek(ro,10,SEEK_SET)==10);
  char c;assert(pread(ro,&c,1,20)==1&&c==0x5a&&lseek(ro,0,SEEK_CUR)==10);
  unsigned char *copy=mmap(0,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE,ro,0);assert(copy!=MAP_FAILED&&copy[0]==0x5a);
  copy[0]=0x31;assert(munmap(copy,4096)==0);
  errno=0;assert(mmap(0,4096,PROT_READ|PROT_WRITE,MAP_SHARED,ro,0)==MAP_FAILED&&errno==EACCES);
  int wo=open(path,O_WRONLY);assert(wo>=0);errno=0;assert(read(wo,&c,1)==-1&&errno==EBADF);
  errno=0;assert(mmap(0,4096,PROT_READ,MAP_PRIVATE,wo,0)==MAP_FAILED&&errno==EACCES);close(wo);
  close(ro);close(rw);ro=open(path,O_RDONLY);assert(ro>=0&&read(ro,&c,1)==1&&c==0x5a);close(ro);unlink(path);
  printf("readonly-ok large=%d\n",large);
 }
 puts("readonly-file-test-done: flags, write/pwrite/truncate denial, metadata, private copy and mmap access");return 0;
}
