f() { /busybox free | /busybox grep Mem | /busybox tr -s ' ' | /busybox cut -d' ' -f4; }
echo soak-free-before=$(f)
i=0
while [ $i -lt 6 ]; do
  /thrtest >/dev/null
  i=$((i+1))
  echo soak-run-$i=$(f)
done
echo soak-free-after=$(f)
