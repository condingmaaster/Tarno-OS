( /busybox sleep 3; /busybox printf "pass\n" | /bin/sudo -S /busybox sh -c "/thosinput type hello-world; /thosinput key enter" ) &
read l
echo got-$l-end
