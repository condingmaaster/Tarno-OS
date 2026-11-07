thosdesk &
sleep 1
/winkey.exe &
app=$!
wait "$app"
echo winkey-exit:$?
thoswin quit
sleep 1
echo winkey-done
