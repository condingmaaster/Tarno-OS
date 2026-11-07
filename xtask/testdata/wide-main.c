// SPDX-License-Identifier: GPL-2.0-or-later
#include <stdio.h>
#include <wchar.h>
int wmain(int argc,wchar_t**argv,wchar_t**env){int ok=argc==3&&!wcscmp(argv[1],L"Größe 😀")&&!wcscmp(argv[2],L"a b")&&argv[argc]==0&&env&&env[0]==0;puts(ok?"wide-main-result=PASS":"wide-main-result=FAIL");puts("wide-main-done");return ok?0:1;}
