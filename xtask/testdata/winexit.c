// SPDX-License-Identifier: GPL-2.0-or-later
#include <windows.h>
static unsigned number(const char**s){while(**s==' '||**s=='"')(*s)++;unsigned n=0;while(**s>='0'&&**s<='9'){n=n*10+*(*s)++-'0';}return n;}
void __attribute__((noreturn)) start(void){const char*s=GetCommandLineA();while(*s&&*s!=' ')s++;unsigned code=number(&s),delay=number(&s);if(delay)Sleep(delay);ExitProcess(code);}
