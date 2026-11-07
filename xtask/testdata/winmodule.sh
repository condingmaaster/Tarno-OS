#!/busybox sh
set -eu
rm -f /tmp/module-expected.txt
/winmodule.exe
printf %s 'C:\winmodule.exe' > /tmp/module-expected.txt
/winmodule-launch /winmodule.exe
mkdir -p '/tmp/Größe 😀 Tools'
cp /winmodule.exe '/tmp/Größe 😀 Tools/test.exe'
printf %s 'C:\tmp\Größe 😀 Tools\test.exe' > /tmp/module-expected.txt
/winmodule-launch '/tmp/Größe 😀 Tools/test.exe'
base=/tmp
for i in 1 2 3 4 5 6 7 8 9; do base="$base/long-directory-segment-$i-12345678"; done
mkdir -p "$base"
cp /winmodule.exe "$base/test.exe"
printf 'C:%s' "$base/test.exe" | tr / '\\' > /tmp/module-expected.txt
/winmodule-launch "$base/test.exe"
rm -f /tmp/module-expected.txt
echo winmodule-series-done
