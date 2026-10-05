
/busybox wget -O /tmp/readme https://deb.debian.org/debian/README
echo wget-rc=$?
/busybox wc -c /tmp/readme
echo https-done
