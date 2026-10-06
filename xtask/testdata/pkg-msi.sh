/busybox sh /thos-pkg install http://www.7-zip.org/a/7z2301-x64.msi sevenzip
echo apps=$(/busybox cat /opt/apps.conf | /busybox cut -d'|' -f1 | /busybox tr '\n' ' ')
/busybox find /opt/sevenzip -name '7z.exe'
echo pkg-msi-done
