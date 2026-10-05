/busybox sh /thos-pkg update
for p in htop jq ripgrep nano; do /busybox sh /thos-pkg install $p; done
echo installed=$(/busybox sh /thos-pkg installed | /busybox tr '\n' ' ')
htop --version
jq --version
rg --version
nano --version
echo '{"a":[1,2,3]}' | jq '.a | add'
echo pkg-apps-done
