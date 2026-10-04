echo start2
/usr/bin/gawk 'BEGIN { print "gawk", 6*7 }'
echo "a b c" | /usr/bin/gawk '{ print $2 }'
echo "2^10" | /usr/bin/bc
echo '{"k":[1,2,3]}' | /usr/bin/jq -M '.k | add'
mkdir -p /tmp/tt
echo hello > /tmp/tt/f.txt
/usr/bin/tar cf /tmp/t.tar -C /tmp tt
/usr/bin/tar tf /tmp/t.tar
printf 'all:\n\t@echo make-works\n' > /tmp/Makefile
/usr/bin/make -f /tmp/Makefile
echo data > /tmp/s1
ln -s /tmp/s1 /tmp/s2
cat /tmp/s2
/busybox readlink /tmp/s2
echo viasym > /tmp/s2
cat /tmp/s1
ln -s s1 /tmp/s3
cat /tmp/s3
mkdir /tmp/dd
ln -s /tmp/dd /tmp/dl
echo inlinkeddir > /tmp/dl/f
cat /tmp/dd/f
rm /tmp/s2
cat /tmp/s1
echo real2-done
