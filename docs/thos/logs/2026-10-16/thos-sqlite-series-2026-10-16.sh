#!/bin/bash
set -eu
cd /home/router/Projekte/Tarno-OS
for variant in before after; do
 image=target/thos-keyboard-normal-qa-2026-10-16.img
 if [ "$variant" = after ]; then image=target/thos-record-locks-qa-2026-10-16.img; fi
 result=0
 bash /tmp/thos-one-case-2026-10-16.sh "$image" sqlite-lock-test > "/tmp/thos-sqlite-$variant-2026-10-16.log" 2>&1 || result=$?
 cp target/sqlite-lock-serial.log "docs/thos/logs/2026-10-16/sqlite-$variant-serial.log"
 cp "/tmp/thos-sqlite-$variant-2026-10-16.log" "docs/thos/logs/2026-10-16/sqlite-$variant-case.log"
 echo "$variant exit=$result"
 tail -2 "/tmp/thos-sqlite-$variant-2026-10-16.log"
 if [ "$variant" = before ]; then test "$result" -ne 0; else test "$result" = 0; fi
done
