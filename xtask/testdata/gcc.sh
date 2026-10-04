export PATH=/usr/bin:/bin:/usr/sbin:$PATH
cd /tmp
/usr/bin/gcc -O1 -o /tmp/gcchello /gcchello.c
echo gcc-rc-$?
/tmp/gcchello
echo gcc-script-done
