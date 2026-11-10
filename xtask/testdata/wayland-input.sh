thosdesk &
sleep 2
export XDG_RUNTIME_DIR=/run WAYLAND_DISPLAY=wayland-0
/wlinputclient
status=$?
thoswin quit
sleep 1
echo wayland-input-exit=$status
echo wayland-input-done
