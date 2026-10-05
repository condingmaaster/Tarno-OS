/busybox sh /hwreport > /dev/null
echo hw-model=$(/busybox grep -m1 "model name" /tmp/hwreport.txt)
echo hw-pci=$(/busybox grep -c " class " /tmp/hwreport.txt)
echo hw-log=$(/busybox grep -c "THOS:" /tmp/hwreport.txt)
echo dmesg-lines=$(/busybox dmesg | /busybox wc -l)
/mapstest
echo hw-done
