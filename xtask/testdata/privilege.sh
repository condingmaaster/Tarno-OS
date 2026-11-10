set -e
test "$(/busybox id -u)" = 1000
if /bin/sudo -n /busybox true; then exit 90; fi
if /bin/doas -n /busybox true; then exit 91; fi
if /bin/sudo -u somebody /busybox true; then exit 92; fi
if /busybox printf 'wrong\n' | /bin/sudo -S /busybox echo privileged-wrong-password; then exit 93; fi
set +e
/busybox printf 'pass\n' | /bin/sudo -S /busybox sh -c 'id -u; exit 7' > /tmp/priv-output
rc=$?
set -e
test "$rc" = 7
test "$(/busybox cat /tmp/priv-output)" = 0
test "$(/busybox id -u)" = 1000
/busybox printf 'pass\n' | /bin/sudo -S -u root busybox echo 'literal $HOME space' > /tmp/priv-literal
/busybox grep -qxF 'literal $HOME space' /tmp/priv-literal
/busybox printf 'pass\n' | /bin/sudo -S /busybox sh -c 'exec /bin/doas -n /busybox id -u' > /tmp/priv-root-doas
test "$(/busybox cat /tmp/priv-root-doas)" = 0
/busybox printf 'pass\npayload-after-password\n' | /bin/sudo -S /busybox cat > /tmp/priv-input
test "$(/busybox cat /tmp/priv-input)" = payload-after-password
echo privilege-test-done: authenticated-root-stdio-exit-status-parent-unchanged
