thosdesk &
sleep 1
/gdi-blit.exe &
app=$!
wait "$app"
echo gdi-blit-exit:$?
thoswin quit
sleep 1
echo gdi-blit-done
