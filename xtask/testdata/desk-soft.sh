/busybox mkdir -p /opt
/busybox cat > /opt/.index <<'X'
tree|2.2.1-1|pool/x.deb|abc| libc6|59392|displays an indented directory tree
htop|3.4.1-5|pool/y.deb|abc| libc6|171412|interactive processes viewer
treesize|1.0|pool/z.deb|abc| libc6|1000|a tree size tool
X
thosdesk &
sleep 1
/busybox cat > /opt/fakepkg <<'X'
#!/busybox sh
echo "downloading $2"
/busybox sleep 4
echo "unpacking $2"
/busybox sleep 4
echo fake-pkg-ran $1 $2
X
/busybox chmod +x /opt/fakepkg
SOFT_QUERY=tree SOFT_AUTO=1 THOS_PKG=/opt/fakepkg thossoft &
sleep 40
thoswin quit
sleep 1
