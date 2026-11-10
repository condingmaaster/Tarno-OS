thosdesk &
sleep 1
SETTINGS_PAGE=4 thossettings &
sleep 30
echo theme-accent=$(/busybox sed -n 1p $HOME/.thos-theme)
echo theme-wall=$(/busybox sed -n 2p $HOME/.thos-theme)
thoswin quit
sleep 1
