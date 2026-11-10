thosdesk &
sleep 2
export XDG_RUNTIME_DIR=/run
export HOME=/tmp
export FONTCONFIG_FILE=/etc/fonts/fonts.conf
export LC_ALL=C.UTF-8
export GDK_BACKEND=wayland NO_AT_BRIDGE=1 GSETTINGS_BACKEND=memory GTK_USE_PORTAL=0 GDK_DEBUG=0
/gtkhello 70 &
sleep 100
thoswin quit
sleep 1
