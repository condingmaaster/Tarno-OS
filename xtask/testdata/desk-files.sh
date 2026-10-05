/busybox mkdir -p /tmp/ft
/busybox cp /pkgdemo.deb /tmp/ft/
thosdesk &
sleep 1
FILES_LOG=1 FILES_START=/tmp/ft thosfiles &
sleep 45
echo opt-$(/busybox ls /opt/pkgdemo/usr/bin 2>&1)
thoswin quit
sleep 1
