thosdesk &
sleep 1
export XDG_RUNTIME_DIR=/run WAYLAND_DISPLAY=wayland-0
/wlsubclient
status=$?
thoswin quit
sleep 1
echo wayland-subsurface-done:$status
