thosdesk &
sleep 1
/gdi-font.exe &
app=$!
wait "$app"
echo gdi-font-exit:$?
thoswin quit
sleep 1
echo gdi-font-done
