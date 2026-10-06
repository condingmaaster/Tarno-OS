export THOS_SLOMO=20
/busybox mkdir -p /tmp/Pictures /opt
/busybox cat > /opt/.index <<'X'
tree|2.2.1-1|pool/x.deb|abc| libc6|59392|displays an indented directory tree
htop|3.4.1-5|pool/y.deb|abc| libc6|171412|interactive processes viewer
X
thosdesk &
sleep 2
thospanel &
sleep 6
FILES_START=/tmp thosfiles &
sleep 14
SOFT_QUERY=tree thossoft &
sleep 14
thosterm -c "echo THOS" &
sleep 14
timeout 24 thosview /red.png &
sleep 30
thoswin quit
sleep 1
