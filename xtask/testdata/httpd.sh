mkdir -p /tmp/www
echo httpd-says-hello > /tmp/www/index.html
/busybox httpd -p 127.0.0.1:8080 -h /tmp/www
sleep 1
/busybox wget -q -O - http://127.0.0.1:8080/index.html
echo httpd-done
