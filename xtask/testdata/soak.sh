f() { /busybox free | /busybox grep Mem | /busybox tr -s ' ' | /busybox cut -d' ' -f4; }
export XDG_RUNTIME_DIR=/run HOME=/tmp FONTCONFIG_FILE=/etc/fonts/fonts.conf LC_ALL=C.UTF-8
export GDK_BACKEND=wayland NO_AT_BRIDGE=1 GSETTINGS_BACKEND=memory GTK_USE_PORTAL=0
echo soak-free-nodesk=$(f)
thosdesk &
sleep 2
echo soak-free-before=$(f)
i=0
while [ $i -lt 6 ]; do
  /gtkhello 4
  i=$((i+1))
  echo soak-run-$i=$(f)
done
sleep 2
echo soak-free-after=$(f)
thoswin quit
sleep 2
echo soak-free-quit=$(f)
