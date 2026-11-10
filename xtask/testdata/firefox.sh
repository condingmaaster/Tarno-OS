thosdesk &
sleep 2
export XDG_RUNTIME_DIR=/run HOME=/tmp LC_ALL=C.UTF-8
export FONTCONFIG_FILE=/etc/fonts/fonts.conf
export GDK_BACKEND=wayland MOZ_ENABLE_WAYLAND=1 WAYLAND_DISPLAY=wayland-0 NO_AT_BRIDGE=1
export GSETTINGS_BACKEND=memory GTK_USE_PORTAL=0
export MOZ_LOG=SharedMemory:5
/usr/lib/firefox-esr/firefox-esr --version
mkdir -p /tmp/firefox-probe
# Isolated offline diagnostic only, not the production browser configuration.
export MOZ_DISABLE_CONTENT_SANDBOX=1 MOZ_DISABLE_RDD_SANDBOX=1
/usr/lib/firefox-esr/firefox-esr --no-remote --profile /tmp/firefox-probe file:///firefox-test.html &
browser=$!
sleep "${THOS_FIREFOX_WAIT:-110}"
kill "$browser" 2>/dev/null
thoswin quit
sleep 1
echo firefox-test-done
