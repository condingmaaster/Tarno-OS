// SPDX-License-Identifier: GPL-2.0-or-later
#include <unistd.h>
#include <stdio.h>
int main(int argc,char **argv){
 if(argc!=2)return 2;
 char *args[]={"unrelated.exe",0};char *env[]={0};
 execve(argv[1],args,env);perror("winmodule execve");return 3;
}
