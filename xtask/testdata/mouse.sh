thosdesk &
/busybox sleep 5
echo mouse-desk-up
/busybox sleep 6
/busybox printf "pass\n" | /bin/sudo -S /thosinput move 640 300
echo mouse-moved
/busybox sleep 6
thoswin quit
/busybox sleep 1
echo mouse-end
