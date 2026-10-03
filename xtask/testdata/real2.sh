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
echo real2-done
