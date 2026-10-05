/busybox sh /thos-pkg update
echo index-lines=$(/busybox wc -l < /opt/.index)
/busybox sh /thos-pkg search "interactive processes"
/busybox sh /thos-pkg install tree
echo installed=$(/busybox sh /thos-pkg installed)
/busybox ls /opt/root/usr/bin
/opt/root/usr/bin/tree -L 1 /opt
echo pkg-repo-done
