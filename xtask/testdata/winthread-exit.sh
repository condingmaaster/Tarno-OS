/winthread-exit.exe
code=$?
echo winthread-last-return-status=$code
[ "$code" = 42 ] || exit 1
/winthread-exit.exe X
code=$?
echo winthread-last-explicit-status=$code
[ "$code" = 37 ] || exit 1
echo winthread-exit-result=PASS
echo winthread-exit-done
