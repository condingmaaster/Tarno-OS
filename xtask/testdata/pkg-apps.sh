/busybox sh /thos-pkg update
for p in htop jq ripgrep nano w3m; do /busybox sh /thos-pkg install $p; done
echo installed=$(/busybox sh /thos-pkg installed | /busybox tr '\n' ' ')
htop --version
jq --version
rg --version
nano --version
echo '{"a":[1,2,3]}' | jq '.a | add'
export TERM=linux
w3m -dump http://deb.debian.org/ | /busybox head -n 4
echo pkg-apps-done
