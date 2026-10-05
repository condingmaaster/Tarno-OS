/busybox sh /thos-pkg update
echo index-lines=$(/busybox wc -l < /opt/.index)
/busybox sh /thos-pkg search "interactive processes"
/busybox sh /thos-pkg install tree
echo installed=$(/busybox sh /thos-pkg installed)
/busybox ls /opt/root/usr/bin
/opt/root/usr/bin/tree -L 1 /opt
tree -L 1 /opt/.db
/busybox sh /thos-pkg remove tree
echo after-remove=$(/busybox ls /opt/root/usr/bin 2>&1 | /busybox wc -l) installed2=$(/busybox sh /thos-pkg installed | /busybox wc -l)
echo pkg-repo-done
