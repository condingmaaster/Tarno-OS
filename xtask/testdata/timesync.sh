n=0
until /busybox printf "pass\n" | /bin/sudo -S /thos-ntp; do n=$((n+1)); [ $n -ge 5 ] && break; /busybox sleep 4; done
/busybox printf "pass\n" | /bin/sudo -S /busybox date -s "2026-11-09 05:00:00"
/busybox date
echo timesync-end
