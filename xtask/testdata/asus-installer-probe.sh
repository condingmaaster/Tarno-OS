# Official installer diagnostic, in an offline guest without USB passthrough.
if [ -f /ArmouryCrateInstaller.exe ]; then
    echo asus-installer-present
    /thosrun --windows /ArmouryCrateInstaller.exe
    echo asus-installer-status=$?
else
    echo asus-installer-missing
fi
echo asus-installer-probe-end
