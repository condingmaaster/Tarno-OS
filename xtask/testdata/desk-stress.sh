thosdesk &
sleep 1
thospanel &
sleep 2
i=0
while [ $i -lt 60 ]; do
    thoswin 200 60 60 300 200 W$i &
    A=$!
    thoswin 60 200 60 260 180 X$i &
    B=$!
    sleep 1
    kill $A
    sleep 1
    kill $B
    i=$((i+1))
    echo stress-round-$i
done
sleep 2
echo stress-finished
thoswin quit
sleep 1
