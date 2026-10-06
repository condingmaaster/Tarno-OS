export THOS_SLOMO=20
/busybox mkdir -p /tmp/Pictures
thosdesk &
sleep 2
thospanel &
sleep 8
FILES_START=/tmp thosfiles &
sleep 16
thosterm -c "echo THOS" &
sleep 16
timeout 20 thosview /red.png &
sleep 36
thoswin quit
sleep 1
