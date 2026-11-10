dir=/tmp/files-browser-exe
mkdir -p "$dir"
name="Browser's "'$(touch unintended).EXE'
cp /whello.exe "$dir/$name"
chmod 644 "$dir/$name"
thosdesk &
sleep 1
thospanel &
FILES_LOG=1 FILES_START="$dir" thosfiles &
for i in $(seq 1 60);do [ -x "$dir/$name" ] && break;sleep 1;done
[ -x "$dir/$name" ] && echo files-browser-exe-enabled
sleep 3
[ ! -e unintended ] && echo files-browser-name-literal
thoswin quit
sleep 1
echo files-browser-exe-done
