#include <unistd.h>
#include <errno.h>
#include <stdio.h>
static int failures;
static void check(const char *path,int want){char *args[]={(char*)path,0};char*env[]={0};errno=0;int r=execve(path,args,env);int got=errno;printf("pefail-%s %s rc=%d errno=%d expected=%d\n",r==-1&&got==want?"ok":"FAIL",path,r,got,want);if(r!=-1||got!=want)failures++;}
int main(void){check("/badpe.exe",ELIBBAD);check("/missing-import.exe",ELIBBAD);check("/oversized.exe",ENOMEM);check("/plain.txt",ENOEXEC);printf("pefail-result=%s\n",failures?"FAIL":"PASS");puts("pefail-done");return failures?1:0;}
