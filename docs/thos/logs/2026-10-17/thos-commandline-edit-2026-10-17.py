from pathlib import Path
p=Path('xtask/testdata/wargv.c')
s=p.read_text();s=s.replace('WCHAR**argv;WCHAR**env;} result[8];','WCHAR**argv;WCHAR**env;WCHAR*raw;} result[8];')
s=s.replace('result[i].status=__wgetmainargs(', 'result[i].raw=GetCommandLineW();result[i].status=__wgetmainargs(')
old=' CHECK("raw-wide-command-initialized",_wcmdln&&_wcmdln[0]);CHECK("initial-env-not-eagerly-built",__winitenv==0);'
new=''' ULONG_PTR peb;__asm__ volatile("movq %%gs:0x60,%0":"=r"(peb));ULONG_PTR params=*(ULONG_PTR*)(peb+0x20);
 WCHAR*raw=*(WCHAR**)(params+0x78);unsigned units=*(unsigned short*)(params+0x70)/2;
 CHECK("raw-wide-command-initialized",_wcmdln&&_wcmdln[0]);CHECK("initial-env-not-eagerly-built",__winitenv==0);
 SetLastError(56789);WCHAR*api=GetCommandLineW();
 CHECK("commandline-system-buffer",api==raw&&api==_wcmdln&&raw[units]==0);
 CHECK("commandline-preserves-last-error",GetLastError()==56789);
 CHECK("commandline-unicode-raw-text",equal(api,L"/wargv.exe \\\"a b\\\" \\\"\\\" \\\"Größe 😀\\\" \\\"a\\\\\\\"b\\\" C:\\\\dir\\\\"));
 typedef WCHAR*(WINAPI*getcmd)(void);getcmd dynamic=(getcmd)GetProcAddress(GetModuleHandleA("kernel32.dll"),"GetCommandLineW");
 CHECK("commandline-dynamic-export",dynamic&&dynamic()==raw);
'''
assert old in s;s=s.replace(old,new)
old=' ULONG_PTR peb;__asm__ volatile("movq %%gs:0x60,%0":"=r"(peb));ULONG_PTR params=*(ULONG_PTR*)(peb+0x20);WCHAR**slot='
assert old in s;s=s.replace(old,' WCHAR**slot=')
s=s.replace('CHECK("shared-complete-cache",same);','CHECK("shared-complete-cache",same);int raw_same=1;for(unsigned i=0;i<8;i++)if(result[i].raw!=raw)raw_same=0;CHECK("commandline-eight-thread-identity",raw_same);')
old=' char tiny[1]={0x55};'
new=''' /* Controlled private process metadata exercises long UTF16 and pointer
    lifetime separately from the loader's current 255-byte launch limit.
    Applications must not normally modify their system command line. */
 static WCHAR longraw[1537];for(unsigned i=0;i<1536;i++)longraw[i]=(WCHAR)(0x400+i%128);longraw[1300]=0xd83d;longraw[1301]=0xde00;longraw[1536]=0;
 WCHAR**cmdslot=(WCHAR**)(params+0x78);unsigned short*lengths=(unsigned short*)(params+0x70);unsigned short oldlen=lengths[0],oldmax=lengths[1];*cmdslot=longraw;lengths[0]=3072;lengths[1]=3074;
 api=GetCommandLineW();CHECK("commandline-long-metadata-without-truncation",api==longraw&&equal(api,longraw)&&api[1536]==0);
 *cmdslot=raw;lengths[0]=oldlen;lengths[1]=oldmax;CHECK("commandline-restored-metadata-no-cache",GetCommandLineW()==raw);
 char tiny[1]={0x55};'''
assert old in s;s=s.replace(old,new);p.write_text(s)
