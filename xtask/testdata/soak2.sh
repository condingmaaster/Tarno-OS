export XDG_RUNTIME_DIR=/run HOME=/tmp LC_ALL=C.UTF-8 GDK_BACKEND=wayland NO_AT_BRIDGE=1 GSETTINGS_BACKEND=memory
f() { /busybox free | /busybox grep Mem | /busybox tr -s ' ' | /busybox cut -d' ' -f4; }
echo s2-start=$(f)
i=0; while [ $i -lt 5 ]; do /busybox true; i=$((i+1)); done
echo s2-after-true=$(f)
i=0; while [ $i -lt 5 ]; do /gtkhello 1 >/dev/null 2>&1; i=$((i+1)); done
echo s2-after-gtk-nodisplay=$(f)
echo s2-done
