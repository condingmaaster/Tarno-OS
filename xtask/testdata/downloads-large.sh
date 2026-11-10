mkdir -p /tmp/download-large-web
/busybox dd if=/dev/zero of=/tmp/download-large-web/large.bin bs=1048576 count=128
/busybox httpd -f -p 8082 -h /tmp/download-large-web &
server=$!
thosdesk &
sleep 1
thospanel &
sleep 1
thosdownloads http://127.0.0.1:8082/large.bin "Large transfer.bin" &
for i in $(seq 1 420); do
    [ -e "$HOME/Downloads/Large transfer.bin" ] && break
    sleep 1
done
/busybox cmp /tmp/download-large-web/large.bin "$HOME/Downloads/Large transfer.bin" && echo downloads-large-bytes-match
sleep 8
kill "$server"
thoswin quit
sleep 1
echo downloads-large-done
