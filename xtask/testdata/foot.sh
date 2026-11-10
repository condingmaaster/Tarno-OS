THOS_PLACE=legacy thosdesk &
sleep 2
export XDG_RUNTIME_DIR=/run
export HOME=/tmp
export FONTCONFIG_FILE=/etc/fonts/fonts.conf
export TERM=foot
export LC_ALL=C.UTF-8
/usr/bin/foot -d warning -f 'DejaVu Sans Mono:size=11' /busybox sh &
sleep 100
echo foot-file=$(/busybox cat /tmp/footok 2>&1)
thoswin quit
sleep 1
