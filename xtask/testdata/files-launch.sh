mkdir -p /tmp/files-launch
pad=$(/busybox printf '%0140d' 0)
name="Bob's "'$(echo bad) '"$pad.sh"
cat > "/tmp/files-launch/$name" <<'EOF'
#!/busybox sh
echo files-quoted-opened
EOF
thosdesk &
sleep 1
thospanel &
FILES_LOG=1 FILES_START=/tmp/files-launch thosfiles &
sleep 50
thoswin quit
sleep 1
echo files-launch-done
