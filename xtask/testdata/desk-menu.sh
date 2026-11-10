thosdesk &
sleep 1
thospanel &
sleep 60
echo conf-desktop=$(cat /tmp/desktop.conf) panel=$(cat /tmp/panel.conf)
thoswin quit
sleep 1
