/busybox sh /thos-pkg install /pkgdemo.deb
/busybox sh /thos-pkg install /pkgtool.tar.gz
/busybox sh /thos-pkg install /pkgzip.zip
/busybox sh /thos-pkg install /pkgdemo.deb
echo list=$(/busybox sh /thos-pkg list)
/busybox cat /opt/apps.conf
/busybox sh -c "$(/busybox sed -n 's/^Pkg Demo|//p' /opt/apps.conf)"
/busybox sh -c "$(/busybox sed -n 's/^pkgtool|//p' /opt/apps.conf)"
/busybox sh -c "$(/busybox sed -n 's/^pkgzip|//p' /opt/apps.conf)"
/busybox sh /thos-pkg remove pkgdemo
echo after-remove=$(/busybox sh /thos-pkg list) apps=$(/busybox cat /opt/apps.conf | /busybox wc -l)
echo pkg-done
echo aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa > /tmp/tr.txt
echo bb > /tmp/tr.txt
echo cc >> /tmp/tr.txt
echo trunc=$(/busybox cat /tmp/tr.txt | /busybox tr '\n' ,)
echo shebang=$(/thos-pkg list | /busybox wc -l) path=$(thos-pkg list | /busybox wc -l)
