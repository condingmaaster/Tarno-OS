echo start5
cd /tmp
rm -rf w5
mkdir w5
cd w5
printf 'b\na\nc\na\n' > in.txt
echo sort=$(/usr/bin/sort in.txt | tr '\n' ' ')
echo uniq=$(/usr/bin/sort -u in.txt | /usr/bin/wc -l)
echo head=$(/usr/bin/head -n 2 in.txt | tr '\n' ,)
echo tail=$(/usr/bin/tail -n 1 in.txt)
echo cut=$(echo 'x:y:z' | /usr/bin/cut -d: -f2)
echo sha=$(printf abc | /usr/bin/sha256sum | /usr/bin/cut -c1-16)
echo md5=$(printf abc | /usr/bin/md5sum | /usr/bin/cut -c1-8)
echo seq=$(/usr/bin/seq 1 5 | /usr/bin/xargs echo)
echo expr=$(/usr/bin/expr 6 \* 7)
/usr/bin/cp in.txt copy.txt
/usr/bin/mv copy.txt moved.txt
/usr/bin/touch empty
echo find=$(/usr/bin/find . -type f | /usr/bin/sort | tr '\n' ' ')
/usr/bin/diff in.txt moved.txt && echo diff=same
echo stat=$(/usr/bin/stat -c '%s %F' in.txt)
echo du=$(/usr/bin/du -sb in.txt | /usr/bin/cut -f1)
echo b=$(/usr/bin/basename /a/b/c.txt .txt)
echo d=$(/usr/bin/dirname /a/b/c.txt)
echo date=$(/usr/bin/date -u -d @0 +%Y)
echo uname=$(/usr/bin/uname -s)
echo env=$(/usr/bin/env FOO=bar /usr/bin/printenv FOO)
echo tee=$(echo hi | /usr/bin/tee t.out)
printf 'alpha\nbeta\n' > v.txt
/usr/bin/vim.tiny -u NONE -es -c '%s/alpha/GAMMA/' -c 'wq' v.txt
echo vim=$(/usr/bin/head -n 1 v.txt)
echo real5-done
