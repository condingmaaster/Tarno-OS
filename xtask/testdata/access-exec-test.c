// SPDX-License-Identifier: GPL-2.0-or-later
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
static void check(int ok,const char *what){if(!ok){printf("access-exec FAIL: %s errno=%d\n",what,errno);exit(1);}}
int main(void){
 const char *file="/tmp/access-exec-file",*link="/tmp/access-exec-link",*dir="/tmp/access-exec-dir";
 check(getuid()!=0,"fixture must exercise an ordinary user");
 unlink(file);unlink(link);rmdir(dir);int fd=open(file,O_CREAT|O_EXCL|O_WRONLY,0600);check(fd>=0,"create");close(fd);
 check(access(file,F_OK)==0&&access(file,R_OK|W_OK)==0,"read/write/existence unchanged");
 check(access(file,X_OK)==-1&&errno==EACCES,"non-executable file");
 check(chmod(file,0700)==0&&access(file,X_OK)==0,"owner execute");
 check(symlink(file,link)==0&&access(link,X_OK)==0,"executable target through link");
 check(chmod(file,0600)==0&&access(link,X_OK)==-1&&errno==EACCES,"link must follow non-executable target");
 check(chmod(file,0010)==0&&access(file,X_OK)==-1&&errno==EACCES,"owner bits take precedence over matching group");
 check(chmod(file,0600)==0,"reset owner access");
 fd=open(file,O_WRONLY);check(fd>=0&&ftruncate(fd,300*1024)==0,"create large streaming file");close(fd);
 fd=open(file,O_RDONLY);check(fd>=0,"open read-only stream");
 check(fchmod(fd,0700)==0&&access(file,X_OK)==0,"fchmod on a large read-only descriptor changes the inode");
 check(fchmod(fd,0600)==0&&access(file,X_OK)==-1&&errno==EACCES,"stream fchmod removes execute permission");close(fd);
 check(mkdir(dir,0700)==0&&chmod(dir,0600)==0&&access(dir,X_OK)==-1&&errno==EACCES,"directory search denied");
 check(chmod(dir,0700)==0&&access(dir,X_OK)==0,"directory search granted");
 check(access(file,8)==-1&&errno==EINVAL,"unknown mode rejected");
 check(access("/dev/null",X_OK)==-1&&errno==EACCES,"device not executable");
 check(access("/dev/thos-no-such-device",X_OK)==-1&&errno==ENOENT,"missing device remains ENOENT");
 unlink(link);check(symlink("/dev/null",link)==0&&access(link,X_OK)==-1&&errno==EACCES,"device target through link");
 unlink(link);unlink(file);rmdir(dir);puts("access-exec-test-done");return 0;
}
