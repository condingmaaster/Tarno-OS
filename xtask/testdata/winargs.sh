mkdir -p "$HOME/Downloads"
file="$HOME/Downloads/Quoted tool.exe"
cp /winargv.exe "$file"
chmod 644 "$file"
/thosrun --windows "$file" "" "two words" 'quoted"value' 'C:\folder\' "$(/busybox printf 'tab\tin')" "Bob's "'$(literal)'
echo winargv-values-result=$?
set --
for i in $(seq 1 31);do set -- "$@" x;done
/thosrun --windows "$file" "$@"
echo winargv-count-result=$?
long=$(/busybox printf '%0300d' 0)
/thosrun --windows "$file" "$long"
echo winargv-long-result=$?
echo winargs-test-done
