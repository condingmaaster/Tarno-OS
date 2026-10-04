export PATH=/usr/bin:/bin:$PATH
/usr/bin/curl -sS --max-time 40 --cacert /etc/ssl/certs/ca-certificates.crt https://example.com/ | /busybox head -c 300
echo
/usr/bin/curl -sS --max-time 20 -o /dev/null -w "curl-http-code-%{http_code}\n" http://example.com/
echo curl-script-done
