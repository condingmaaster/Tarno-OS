from pathlib import Path
p=Path('xtask/testdata/wargv.c');s=p.read_text();lines=s.splitlines()
for i,l in enumerate(lines):
 if 'CHECK("commandline-unicode-raw-text"' in l:lines[i]=r' CHECK("commandline-unicode-raw-text",equal(api,L"\"/wargv.exe\" \"a b\" \"\" \"Größe 😀\" \"a\\\"b\" \"C:\\dir\\\\\""));'
p.write_text('\n'.join(lines)+'\n')
p=Path('xtask/msvcrtx/kernel32x.c');s=p.read_text();old='''EXPORT const u16 *WINAPI GetCommandLineW(void) {
    static u16 w[1024]; static int init;
    if (!init) { extern const char *WINAPI GetCommandLineA(void); a2w(GetCommandLineA(), w, 1024); init = 1; }
    return w;
}'''
new='''EXPORT u16 *WINAPI GetCommandLineW(void) {
    /* The loader owns a NUL-terminated UTF16 command line for the process
       lifetime. Return that buffer directly: no ANSI conversion, fixed-size
       copy or competing first-call initialization in this shared DLL. */
    unsigned char *peb; __asm__ volatile("mov %%gs:0x60, %0" : "=r"(peb));
    if (!peb) return 0;
    unsigned char *params = *(unsigned char **)(peb + 0x20);
    return params ? *(u16 **)(params + 0x78) : 0;
}'''
assert old in s;s=s.replace(old,new);p.write_text(s)
