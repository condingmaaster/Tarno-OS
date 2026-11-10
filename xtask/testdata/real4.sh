echo start4
export HOME=/tmp
export GIT_CONFIG_NOSYSTEM=1
cd /tmp
mkdir -p repo
cd repo
/usr/bin/git init -q .
echo hello > a.txt
/usr/bin/git add a.txt
/usr/bin/git -c user.name=t -c user.email=t@t -c gc.auto=0 -c maintenance.auto=false commit -q -m first
/usr/bin/git --no-pager log --oneline
echo real4-done
