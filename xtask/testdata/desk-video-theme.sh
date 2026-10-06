export THOS_SLOMO=20
/busybox mkdir -p /tmp/Pictures
thosdesk &
sleep 2
thospanel &
sleep 3
SETTINGS_PAGE=4 thossettings &
sleep 100
thoswin quit
sleep 1
