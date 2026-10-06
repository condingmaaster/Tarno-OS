/busybox mkdir -p /opt /tmp/Documents /tmp/Pictures
/busybox cat > /opt/.index <<'X'
tree|2.2.1-1|pool/x.deb|abc| libc6|59392|displays an indented directory tree
htop|3.4.1-5|pool/y.deb|abc| libc6|171412|interactive processes viewer
ripgrep|14.1.1-1|pool/r.deb|abc| libc6|1399000|recursively search directories for a regex pattern
treesize|1.0|pool/z.deb|abc| libc6|1000|a tree size tool
nano|8.4-1|pool/n.deb|abc| libc6|629000|small, friendly text editor inspired by Pico
X
/busybox cp /blue.jpg /tmp/Pictures/
/busybox cp /red.png /tmp/Pictures/
echo "notes" > /tmp/Documents/notes.txt
thosdesk &
sleep 2
thospanel &
sleep 2
FILES_START=/tmp thosfiles &
sleep 2
SOFT_QUERY=tree thossoft &
sleep 2
thosterm -c "printf '\033[1;34mTHOS\033[0m terminal  \033[32mgreen\033[0m \033[33myellow\033[0m \033[31mred\033[0m \033[36mcyan\033[0m\n'; /busybox ls /tmp" &
sleep 2
thosview /red.png &
sleep 70
thoswin quit
sleep 1
