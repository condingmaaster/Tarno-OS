# Isolated local diagnostic profile. No production sandbox preferences are changed.
export XDG_RUNTIME_DIR=/run HOME=/tmp LC_ALL=C.UTF-8
export FONTCONFIG_FILE=/etc/fonts/fonts.conf
export GDK_BACKEND=wayland MOZ_ENABLE_WAYLAND=1 WAYLAND_DISPLAY=wayland-0 NO_AT_BRIDGE=1
export GSETTINGS_BACKEND=memory GTK_USE_PORTAL=0
mkdir -p /tmp/browser-download-web /tmp/firefox-download-probe /tmp/Downloads
chmod 700 /tmp/firefox-download-probe
cp /firefox-downloads-test.html /tmp/browser-download-web/index.html
cp /whello.exe /tmp/browser-download-web/windows.exe
cp /busybox /tmp/browser-download-web/native
cat > /tmp/firefox-download-probe/user.js <<'EOF'
user_pref("browser.download.folderList", 2);
user_pref("browser.download.dir", "/tmp/Downloads");
user_pref("browser.download.useDownloadDir", true);
// Low-memory diagnostic profile: avoid unused speculative content processes.
// Keep site isolation and all existing sandbox preferences unchanged.
user_pref("dom.ipc.processPrelaunch.enabled", false);
user_pref("dom.ipc.processCount", 1);
user_pref("dom.ipc.processCount.webIsolated", 1);
user_pref("toolkit.download.loglevel", "Debug");
user_pref("browser.dom.window.dump.enabled", true);
user_pref("devtools.console.stdout.chrome", true);
user_pref("devtools.console.stdout.content", true);
EOF
/busybox httpd -f -vv -p 8083 -h /tmp/browser-download-web &
server=$!
WLS_INPUT_LOG=1 thosdesk -s &
sleep 1
# Session mode starts the existing panel itself.
export MOZ_DISABLE_CONTENT_SANDBOX=1 MOZ_DISABLE_RDD_SANDBOX=1
# Isolate the observed crash-helper registration timeout; diagnostic profile only.
export MOZ_CRASHREPORTER_DISABLE=1
/usr/lib/firefox-esr/firefox-esr --version
/usr/lib/firefox-esr/firefox-esr --no-remote --profile /tmp/firefox-download-probe http://127.0.0.1:8083/index.html &
browser=$!
for i in $(seq 1 240);do
    /busybox cmp -s /whello.exe "/tmp/Downloads/Firefox Tool.exe" && break
    sleep 1
done
if /busybox cmp -s /whello.exe "/tmp/Downloads/Firefox Tool.exe";then echo firefox-download-pe-bytes-ok;else echo firefox-download-pe-failed;fi
for i in $(seq 1 120);do
    /busybox cmp -s /busybox "/tmp/Downloads/busybox Linux" && break
    sleep 1
done
if /busybox cmp -s /busybox "/tmp/Downloads/busybox Linux";then echo firefox-download-elf-bytes-ok;else echo firefox-download-elf-failed;fi
[ ! -x "/tmp/Downloads/Firefox Tool.exe" ] && [ ! -x "/tmp/Downloads/busybox Linux" ] && echo firefox-download-no-auto-exec
FILES_LOG=1 FILES_START=/tmp/Downloads /thosfiles &
for i in $(seq 1 90);do
    [ -x "/tmp/Downloads/Firefox Tool.exe" ] && [ -x "/tmp/Downloads/busybox Linux" ] && break
    sleep 1
done
[ -x "/tmp/Downloads/Firefox Tool.exe" ] && [ -x "/tmp/Downloads/busybox Linux" ] && echo firefox-download-ui-opened
sleep 8
kill "$browser" "$server" 2>/dev/null
thoswin quit
sleep 1
echo firefox-downloads-done
