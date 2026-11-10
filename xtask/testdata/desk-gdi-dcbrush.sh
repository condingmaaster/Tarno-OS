thosdesk &
sleep 1
/gdi-dcbrush.exe &
app=$!
wait "$app"
echo gdi-dcbrush-exit:$?
thoswin quit
sleep 1
echo gdi-dcbrush-done
