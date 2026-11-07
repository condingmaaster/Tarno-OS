thosdesk &
sleep 1
/winwide.exe &
app=$!
wait "$app"
echo winwide-exit:$?
thoswin quit
sleep 1
echo winwide-done
