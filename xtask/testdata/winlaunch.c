// SPDX-License-Identifier: GPL-2.0-or-later
#include <windows.h>
#include <string.h>
int main(int argc,char **argv){
 char inherited[96],path[96];DWORD n=0;
 if(!ReadFile(GetStdHandle(STD_INPUT_HANDLE),inherited,sizeof inherited,&n,0)||!n||n>sizeof inherited||inherited[n-1])return 11;
 if(argc!=3||strcmp(argv[0],inherited)||strcmp(argv[1],inherited))return 12;
 if(!GetModuleFileNameA(0,path,sizeof path)||strcmp(path,argv[2]))return 13;
 return 0;
}
