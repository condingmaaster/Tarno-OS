thosdesk &
sleep 1
/gdi-alpha.exe &
app=$!
wait "$app"
echo gdi-alpha-exit:$?
thoswin quit
sleep 1
echo gdi-alpha-done
