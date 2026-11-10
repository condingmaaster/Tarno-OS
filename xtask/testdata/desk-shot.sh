thosdesk &
sleep 1
thoswin 40 120 220 300 200 Shot &
sleep 30
echo shot-size=$(/busybox wc -c < /tmp/shot-1.png)
/busybox head -c 4 /tmp/shot-1.png | /busybox od -c | /busybox head -n 1
thoswin quit
sleep 1
