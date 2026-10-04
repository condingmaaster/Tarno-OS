f() { /busybox free | /busybox grep Mem | /busybox tr -s ' ' | /busybox cut -d' ' -f4; }
export XDG_RUNTIME_DIR=/run
echo soak-free-nodesk=$(f)
thosdesk &
sleep 2
echo soak-free-before=$(f)
i=0
while [ $i -lt 6 ]; do
  /wlclient 5 once >/dev/null
  i=$((i+1))
  echo soak-run-$i=$(f)
done
echo soak-free-after=$(f)
