THOS_PLACE=legacy thosdesk &
sleep 2
export XDG_RUNTIME_DIR=/run
echo wi-interfaces=$(/usr/bin/wayland-info 2>&1 | /busybox grep -c '^interface:')
wlclient 6 &
sleep 100
thoswin quit
sleep 1
