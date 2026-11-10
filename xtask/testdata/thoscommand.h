// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stddef.h>
#include <string.h>
/* Quote a literal pathname, including apostrophes and shell metacharacters. */
static int thos_command_quote(const char *input,char *out,size_t cap) {
    size_t n=0;if(cap<3)return 0;out[n++]='\'';
    for(const char *p=input;*p;p++) {
        size_t need=*p=='\''?4:1;
        if(n+need+2>cap){out[0]=0;return 0;}
        if(*p=='\'') {memcpy(out+n,"'\\''",4);n+=4;}
        else out[n++]=*p;
    }
    out[n++]='\'';out[n]=0;return 1;
}
/* Read one shell word without executing it. Handles package launchers with
   quoted paths and VAR='a value'; substitutions are not evaluated here. */
static int thos_command_word(const char **input,char *out,size_t cap) {
    const char *p=*input;size_t n=0;int quote=0;
    while(*p==' '||*p=='\t') p++;
    if(!*p) return 0;
    while(*p && (quote||(*p!=' '&&*p!='\t'))) {
        unsigned char c=*p++;
        if(c=='\\'&&quote!='\''&&*p) c=*p++;
        else if((c=='\''||c=='"')&&!quote) {quote=c;continue;}
        else if(c==quote) {quote=0;continue;}
        if(n+1>=cap) return 0;
        out[n++]=c;
    }
    if(quote) return 0;
    out[n]=0;*input=p;return 1;
}
static int thos_command_program(const char *input,char *out,size_t cap) {
    while(thos_command_word(&input,out,cap)) {
        char *eq=strchr(out,'=');
        if(!eq) return out[0]!=0;
        if(eq==out) return 0;
        for(char *p=out;p<eq;p++) if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||*p=='_'||(p!=out&&*p>='0'&&*p<='9'))) return 1;
    }
    return 0;
}
