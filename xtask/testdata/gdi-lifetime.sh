i=0
while [ "$i" -lt 40 ]; do
 /gdi-lifetime.exe || { echo gdi-lifetime-result=FAIL; echo gdi-lifetime-done; exit 1; }
 i=$((i+1))
done
echo gdi-lifetime-cycles:$i
echo gdi-lifetime-result=PASS
echo gdi-lifetime-done
