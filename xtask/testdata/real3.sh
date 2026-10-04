echo start3
/usr/bin/perl -e 'print 6*7, "\n"; my @a = map { $_*2 } 1..5; print "@a\n"; print "perl-ok\n"'
/usr/bin/perl -e 'use POSIX qw(floor); use List::Util qw(sum max); print "xs:", floor(3.7), ":", sum(1..10), ":", max(3,9,4), "\n"'
export PYTHONHOME=/usr
/usr/bin/python3 -S -E -c 'print("py", 6*7, sum(range(10)))' 2>/dev/null
echo real3-done
