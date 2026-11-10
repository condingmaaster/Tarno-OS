#!/usr/bin/env bash
# Screenshot of a THOS machine's screen over SSH: tools/thos-shot.sh [host] [out.png]
# Reads /dev/fb0 on the machine (the composed desktop), compresses it there and has the machine push it to this computer
# (plain TCP on port 7000; the SSH channel itself is far too slow for megabytes). The Acer's panel is 1366x768 with 5504 bytes per line.
set -euo pipefail
HOST="${1:-10.42.0.212}"; OUT="${2:-$HOME/Bilder/THOS/shot-$(date +%F-%H%M%S).png}"
W="${THOS_W:-1366}"; H="${THOS_H:-768}"; STRIDE="${THOS_STRIDE:-1376}"      # pixels per line in memory
ME="${THOS_ME:-10.42.0.1}"
KEY="$(dirname "$0")/../target/thos-update-key"
SSH=(ssh -i "$KEY" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=20 -o BatchMode=yes "root@$HOST")
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
BYTES=$((STRIDE * 4 * H))
python3 -I - "$TMP/fb.gz" <<'PY' &
import socket, sys
s = socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("0.0.0.0", 7000)); s.listen(1); s.settimeout(150)
c, _ = s.accept(); d = b""
while True:
    b = c.recv(65536)
    if not b: break
    d += b
open(sys.argv[1], "wb").write(d)
PY
LISTEN=$!
sleep 1
printf '#!/busybox sh\n/busybox dd if=/dev/fb0 bs=%d count=1 2>/dev/null | /busybox gzip -1 -c > /tmp/shot.gz\n/busybox nc %s 7000 < /tmp/shot.gz\n' "$BYTES" "$ME" \
  | timeout 60 "${SSH[@]}" '/busybox cat > /tmp/shot.sh; echo saved' >/dev/null 2>&1 || true
(timeout 60 "${SSH[@]}" '/busybox sh /tmp/shot.sh > /dev/null 2>&1 < /dev/null &' >/dev/null 2>&1 &)
wait "$LISTEN"
gzip -dc "$TMP/fb.gz" > "$TMP/fb.raw"
mkdir -p "$(dirname "$OUT")"
ffmpeg -y -loglevel error -f rawvideo -pixel_format bgr0 -video_size "${STRIDE}x${H}" -i "$TMP/fb.raw" -frames:v 1 -update 1 -vf "crop=$W:$H:0:0" "$OUT"
echo "$OUT"
