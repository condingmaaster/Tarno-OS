thosdesk &
sleep 1
/gdi-pen.exe &
app=$!
wait "$app"
echo gdi-pen-exit:$?
thoswin quit
sleep 1
echo gdi-pen-done
