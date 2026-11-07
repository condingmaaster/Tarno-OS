if [ "$1" = panel ]; then thosdesk -s & else thosdesk & fi
sleep 1
/winfocus.exe &
app=$!
wait "$app"
echo winfocus-exit:$?
thoswin quit
sleep 1
echo winfocus-done
