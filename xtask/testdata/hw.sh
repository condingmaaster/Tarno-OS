/busybox sh /hwreport > /dev/null
echo hw-model=$(/busybox grep -m1 "model name" /tmp/hwreport.txt)
echo hw-pci=$(/busybox grep -c " class " /tmp/hwreport.txt)
echo hw-log=$(/busybox grep -c "THOS:" /tmp/hwreport.txt)
echo dmesg-lines=$(/busybox dmesg | /busybox wc -l)
echo wlan-present=$(/busybox awk '$1 == "present" {print $2}' /proc/wlaninfo)
echo gpu-acceleration=$(/busybox awk '$1 == "acceleration" {print $2}' /proc/gpuinfo)
echo gpu-adapter=$(/busybox grep '^adapter ' /proc/gpuinfo)
echo wlan-firmware=$(/busybox awk '$1 == "firmware" {print $2}' /proc/wlaninfo)
echo wlan-microcode=$(/busybox awk '$1 == "microcode_bytes" {print $2}' /proc/wlaninfo)
echo gpu-firmware=$(/busybox awk '$1 == "firmware" {print $2}' /proc/gpuinfo)
echo wlan-uploaded=$(/busybox awk '$1 == "firmware_loaded" {print $2}' /proc/wlaninfo)
echo gpu-uploaded=$(/busybox awk '$1 == "firmware_loaded" {print $2}' /proc/gpuinfo)
echo framebuffer-wc=$(/busybox awk '$1 == "framebuffer_wc_configured" {print $2}' /proc/gpuinfo)
echo wlan-core=$(/busybox awk '$1 == "core_state" {print $2}' /proc/wlaninfo)
/mapstest
echo hw-done
