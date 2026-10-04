/whello.exe alpha
echo rc-$?-end
/whello.exe | /busybox cat
/whello.exe > /tmp/w.out
/busybox cat /tmp/w.out
echo winc-done
