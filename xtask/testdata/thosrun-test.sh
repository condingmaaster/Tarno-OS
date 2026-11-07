dir="$HOME/Downloads/launcher-check"
mkdir -p "$dir"
cp /rg.exe "$dir/Windows tool.exe"
chmod 644 "$dir/Windows tool.exe"
/thosrun --windows "$dir/Windows tool.exe" --version
echo run-pe-result=$?
[ -x "$dir/Windows tool.exe" ] && echo run-pe-executable
cp /busybox "$dir/busybox Linux"
chmod 644 "$dir/busybox Linux"
/thosrun "$dir/busybox Linux" echo native-launcher-says-hello
echo run-elf-result=$?
[ -x "$dir/busybox Linux" ] && echo run-elf-executable
printf 'MZ\n/busybox touch /tmp/launcher-bad-script-ran\n' > "$dir/Broken tool.exe"
chmod 644 "$dir/Broken tool.exe"
/thosrun --windows "$dir/Broken tool.exe"
echo run-broken-result=$?
[ ! -x "$dir/Broken tool.exe" ] && echo run-broken-mode-unchanged
[ ! -e /tmp/launcher-bad-script-ran ] && echo run-no-script-fallback
echo thosrun-test-done
