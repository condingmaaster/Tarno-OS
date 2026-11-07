export XDG_RUNTIME_DIR=/run HOME=/tmp LC_ALL=C.UTF-8
export FONTCONFIG_FILE=/etc/fonts/fonts.conf
export GDK_BACKEND=wayland WAYLAND_DISPLAY=wayland-0 NO_AT_BRIDGE=1 GSETTINGS_BACKEND=memory GTK_USE_PORTAL=0
WLS_INPUT_LOG=1 thosdesk -s &
sleep 1
WAYLAND_DEBUG=client /gtk-input
status=$?
thoswin quit
sleep 1
echo gtk-input-done:$status
