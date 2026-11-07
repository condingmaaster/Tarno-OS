# Original MIT Win32 Snake source, cross-compiled as x64; offline guest only.
if [ -f /Snake64.exe ]; then
    echo windows-snake-present
    sha256sum /Snake64.exe
    /thosrun --windows /Snake64.exe
    echo windows-snake-status=$?
else
    echo windows-snake-missing
fi
echo windows-snake-probe-end
