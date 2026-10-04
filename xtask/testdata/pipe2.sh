cd /tmp
mkdir -p w6
cd w6
printf "b\na\n" > in.txt
: > empty
i=0
while [ $i -lt 40 ]; do x=$(/usr/bin/find . -type f | /usr/bin/sort | tr "\n" " "); i=$((i+1)); done
echo pipe2-done-$i
