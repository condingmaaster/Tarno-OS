export PATH=/usr/bin:/bin:/usr/sbin:$PATH
cd /tmp
/usr/bin/gcc -O1 -o /tmp/gcchello /gcchello.c
echo gcc-rc-$?
/tmp/gcchello
cp /src-thoswls.h /tmp/thoswls.h; cp /src-thoswl.h /tmp/thoswl.h; cp /src-thosgfx.h /tmp/thosgfx.h
/usr/bin/gcc -O1 -o /tmp/evtest /src-evtest.c
/tmp/evtest
/usr/bin/gcc -O1 -I/tmp -o /tmp/thosdesk2 /src-thosdesk.c
echo desk-rc-$?
ls -la /tmp/thosdesk2
echo gcc-script-done
