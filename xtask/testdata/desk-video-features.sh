export THOS_SLOMO=20
/busybox mkdir -p /tmp/Pictures
thosdesk &
sleep 2
thospanel &
sleep 3
thossettings &
sleep 14
thosnotify -k 1 "Installed|tree is ready in the Start menu"
sleep 30
thosnotify -k 3 "Installing failed|no network connection"
sleep 60
thoswin quit
sleep 1
