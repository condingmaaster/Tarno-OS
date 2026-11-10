thosdesk &
sleep 1
/winframe.exe &
app=$!
wait "$app"
echo winframe-exit:$?
thoswin quit
sleep 1
echo winframe-done
