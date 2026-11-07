thosdesk &
sleep 1
/gdi-brush.exe &
app=$!
wait "$app"
echo gdi-brush-exit:$?
thoswin quit
sleep 1
echo gdi-brush-done
