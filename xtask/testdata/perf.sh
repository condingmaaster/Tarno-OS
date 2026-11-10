/busybox mkdir -p /opt
/busybox touch /tmp/thos-perf
thosdesk &
sleep 2
thospanel &
sleep 1
thosfiles &
sleep 1
if [ "$PERF_ONLY_FILES" != 1 ]; then
    thossoft &
    sleep 1
    thossettings &
fi
sleep 3
echo perfscript ready
if [ "$PERF_ONLY_FILES" = 1 ]; then
    # The host ends the desktop after its last input. This is a fallback if
    # the workload never completes, rather than racing a fixed short timeout.
    sleep 180
else
    sleep 75
fi
echo perfscript done-$((1))
thoswin quit
sleep 1
