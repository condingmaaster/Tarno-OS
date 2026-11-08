/busybox sh /thos-pkg update
echo index-lines=$(/busybox wc -l < /opt/.index)
/busybox sh /bin/apt search interactive processes
/busybox sh /bin/apt install -y tree
echo installed=$(/busybox sh /thos-pkg installed)
/busybox ls /opt/root/usr/bin
/opt/root/usr/bin/tree -L 1 /opt
tree -L 1 /opt/.db
/busybox sh /bin/apt remove -y tree
echo after-remove=$(/busybox ls /opt/root/usr/bin 2>&1 | /busybox wc -l) installed2=$(/busybox sh /thos-pkg installed | /busybox wc -l)
echo pkg-repo-done
