mkdir -p /tmp/download-web
mkdir -p /tmp/download-web/cgi-bin
cat > /tmp/download-web/cgi-bin/slow <<'EOF'
#!/busybox sh
sleep 20
printf 'Content-Type: application/octet-stream\r\n\r\nlate data'
EOF
chmod +x /tmp/download-web/cgi-bin/slow
/sendfiletest
ln /rg.exe /tmp/download-web/rg.exe
/busybox httpd -f -p 8081 -h /tmp/download-web &
server=$!
thosdesk &
sleep 1
thosdownloads http://127.0.0.1:8081/rg.exe "Windows tool.exe" &
sleep 35
expected=$(/busybox sha256sum /rg.exe | /busybox cut -d' ' -f1)
got=$(/busybox sha256sum "$HOME/Downloads/Windows tool.exe" | /busybox cut -d' ' -f1)
[ "$expected" = "$got" ] && echo downloads-bytes-match
"$HOME/Downloads/Windows tool.exe" --version
/busybox sh /bin/thos-fetch http://127.0.0.1:8081/rg.exe "Windows tool.exe"
echo overwrite-result=$?
/busybox sh /bin/thos-fetch http://127.0.0.1:8081/missing.exe "missing.exe"
echo missing-result=$?
[ ! -e "$HOME/Downloads/missing.exe" ] && echo no-partial-final-file
/busybox sh /bin/thos-fetch http://127.0.0.1:8081/cgi-bin/slow "cancelled.exe" &
transfer=$!
sleep 2
kill "$transfer"
wait "$transfer"
echo cancel-result=$?
[ ! -e "$HOME/Downloads/cancelled.exe" ] && echo no-cancelled-final-file
[ -z "$(/busybox find "$HOME/Downloads" -name '.thos-download.*')" ] && echo temporary-files-clean
mkdir -p "/tmp/Space Program/Program Files/Vendor"
ln /rg.exe "/tmp/Space Program/Program Files/Vendor/Tool.EXE"
tar -cf /tmp/spaces.tar -C "/tmp/Space Program" .
/busybox sh /thos-pkg install /tmp/spaces.tar "space-program"
/busybox sh -c "$(/busybox sed -n 's/^Tool.EXE|//p' /opt/apps.conf) --version"
echo registered-exe-result=$?
kill "$server"
thoswin quit
sleep 1
echo downloads-test-done
