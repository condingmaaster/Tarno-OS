thosdesk &
sleep 1
/wininput.exe &
app=$!
wait "$app"
echo wininput-exit:$?
thoswin quit
sleep 1
echo wininput-done
