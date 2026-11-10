#!/bin/bash
set -eu
cd /home/router/Projekte/Tarno-OS
for case_name in gdi-blit-test gdi-lifetime-test gdi-brush-test desk-winresize-test desk-winfocus-panel-test winlookup-test windows-snake-probe asus-installer-probe; do
 bash /tmp/thos-one-case-2026-10-16.sh target/thos-blit-qa-2026-10-17.img "$case_name" > "docs/thos/logs/2026-10-17/pages-${case_name}-runner.log" 2>&1
 case "$case_name" in
  gdi-blit-test) tag=gdi-blit ;;
  gdi-lifetime-test) tag=gdi-lifetime ;;
  gdi-brush-test) tag=gdi-brush ;;
  desk-winresize-test) tag=winresize ;;
  desk-winfocus-panel-test) tag=winfocus ;;
  winlookup-test) tag=winlookup ;;
  windows-snake-probe) tag=windows-snake ;;
  asus-installer-probe) tag=asus-installer ;;
 esac
 if [ -f "target/$tag-serial.log" ]; then cp "target/$tag-serial.log" "docs/thos/logs/2026-10-17/pages-${case_name}-serial.log"; fi
 if [ -f "target/$tag-complete.ppm" ]; then convert "target/$tag-complete.ppm" "docs/thos/screenshots/2026-10-17/pages-${case_name}.png"; fi
 printf '%s PASS\n' "$case_name"
done
