thosdesk &
sleep 1
thospanel &
sleep 1
/winresize.exe &
app=$!
wait "$app"
echo winresize-exit:$?
thoswin quit
sleep 1
echo winresize-done
