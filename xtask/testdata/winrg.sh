echo hello-needle-line > /tmp/rgtest.txt
echo other line >> /tmp/rgtest.txt
/rg.exe --version
/rg.exe --color never needle /tmp/rgtest.txt
echo rg-rc=$?
echo winrg-done
