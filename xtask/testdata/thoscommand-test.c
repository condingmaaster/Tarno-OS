// SPDX-License-Identifier: GPL-2.0-or-later
#include <assert.h>
#include <stdio.h>
#include "thoscommand.h"
int main(void) {
    char p[1024];
    char q[1024];const char *literal="/tmp/Bob's $(touch forbidden); tool.EXE";
    assert(thos_command_quote(literal,q,sizeof q));
    const char *r=q;assert(thos_command_word(&r,p,sizeof p)&&!strcmp(p,literal));
    assert(!thos_command_quote("abcd",q,5));
    assert(thos_command_program("LD_LIBRARY_PATH='/opt/Program Files/lib' '/opt/Program Files/Tool.EXE' --version",p,sizeof p));
    assert(!strcmp(p,"/opt/Program Files/Tool.EXE"));
    assert(thos_command_program("'/tmp/Bob'\\''s tool.exe'",p,sizeof p)&&!strcmp(p,"/tmp/Bob's tool.exe"));
    assert(thos_command_program("A=1 B=2 /bin/test",p,sizeof p)&&!strcmp(p,"/bin/test"));
    assert(thos_command_program("'/tmp/a=b.exe'",p,sizeof p)&&!strcmp(p,"/tmp/a=b.exe"));
    assert(!thos_command_program("'unterminated",p,sizeof p));
    assert(!thos_command_program("A=1",p,sizeof p));
    assert(!thos_command_program("/too-long",p,3));
    puts("command parser: quoted paths, environment assignments and invalid input passed");
}
