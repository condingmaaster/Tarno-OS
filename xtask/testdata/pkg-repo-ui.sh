set -e
# Offline fixture of an already unpacked repository package. This verifies
# registration in the shared root, not archive signatures or network fetching.
mkdir -p /opt/root /opt/.db
/busybox dpkg-deb -x /pkgdemo.deb /opt/root
/busybox printf 'usr/bin/pkgdemo\nusr/share/applications/pkgdemo.desktop\n' > /opt/.db/pkgdemo.list
/busybox printf 'pkgdemo|1|pool/pkgdemo.deb|unused||0|demo\n' > /opt/.index
cp /opt/root/usr/bin/pkgdemo /opt/root/usr/bin/unrelated
/busybox printf '[Desktop Entry]\nName=Unrelated\nExec=unrelated\nType=Application\n' > /opt/root/usr/share/applications/unrelated.desktop
/busybox sh /thos-pkg install pkgdemo
/busybox sh /thos-pkg install pkgdemo
test "$(/busybox grep -c '^Pkg Demo|' /opt/apps.conf)" = 1
if /busybox grep -q Unrelated /opt/apps.conf; then exit 1; fi
/busybox sh -c "$(/busybox sed -n 's/^Pkg Demo|//p' /opt/apps.conf)"
echo pkg-repo-ui-done
