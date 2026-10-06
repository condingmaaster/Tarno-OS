/busybox mkdir -p /opt
printf 'alpha\nbeta\ngamma\n' > /opt/note.txt
thosdesk &
sleep 1
thosedit /opt/note.txt &
sleep 25
echo note-line1=$(/busybox head -n 1 /opt/note.txt)
echo note-line2=$(/busybox sed -n 2p /opt/note.txt)
thoswin quit
sleep 1
