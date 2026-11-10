// SPDX-License-Identifier: GPL-2.0-or-later
#include <stdio.h>
static int same(const char *a,const char *b){while(*a&&*a==*b){a++;b++;}return *a==*b;}
int main(int argc,char **argv){
    if(argc==32){for(int i=1;i<32;i++)if(!same(argv[i],"x"))return 20; if(argv[32])return 21;puts("winargv maximum-pointers-ok");return 0;}
    const char *expected[]={"","two words","quoted\"value","C:\\folder\\","tab\tin","Bob's $(literal)"};
    if(argc!=7||argv[7]){printf("winargv wrong count=%d\n",argc);return 2;}
    for(int i=1;i<7;i++)if(!same(argv[i],expected[i-1])){printf("winargv wrong argument=%d\n",i);return i+2;}
    puts("winargv count7 values-preserved");return 0;
}
