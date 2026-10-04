f() { /busybox free | /busybox grep Mem | /busybox tr -s ' ' | /busybox cut -d' ' -f4; }
export XDG_RUNTIME_DIR=/run HOME=/tmp FONTCONFIG_FILE=/etc/fonts/fonts.conf LC_ALL=C.UTF-8 TERM=foot
thosdesk &
sleep 2
echo soak-free-before=$(f)
i=0
while [ $i -lt 6 ]; do
  /usr/bin/foot -d warning -f 'DejaVu Sans Mono:size=11' /busybox sh -c 'sleep 2'
  i=$((i+1))
  echo soak-run-$i=$(f)
done
echo soak-free-after=$(f)
