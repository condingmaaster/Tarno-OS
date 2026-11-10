if [ -f /ski32.exe ]; then
    echo skifree-present
    mkdir -p "$HOME/Downloads"
    game="$HOME/Downloads/SkiFree.exe"
    cp /ski32.exe "$game"
    chmod 644 "$game"
    /thosrun --windows "$game"
    echo skifree-status=$?
    [ ! -x "$game" ] && echo skifree-mode-unchanged
fi
echo windows-lightgame-probe-end
