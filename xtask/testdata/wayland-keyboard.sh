thosdesk &
sleep 2
export XDG_RUNTIME_DIR=/run WAYLAND_DISPLAY=wayland-0
/wlkeyboardclient
status=$?
thoswin quit
sleep 1
echo wayland-keyboard-exit=$status
echo wayland-keyboard-done
