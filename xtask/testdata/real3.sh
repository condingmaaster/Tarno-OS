echo start3
/usr/bin/perl -e 'print 6*7, "\n"; my @a = map { $_*2 } 1..5; print "@a\n"; print "perl-ok\n"'
export PYTHONHOME=/usr
/usr/bin/python3 -S -E -c 'print("py", 6*7, sum(range(10)))' 2>/dev/null
echo real3-done
