#!/bin/bash

cd ~/code/node-webrtc
src=./build-linux-arm64/Release/wrtc.node
dst=~/code/main/toeeo-camera-discovery-backend/resources/wrtc/linux-arm64/wrtc.node
tmp="${dst}.tmp.$$"
rm -f "$tmp"
cat "$src" > "$tmp"
chmod 755 "$tmp"
mv -f "$tmp" "$dst"
python3 - <<'PY'   
import pathlib,binascii
p=pathlib.Path("../main/toeeo-camera-discovery-backend/resources/wrtc/linux-arm64/wrtc.node")
b=p.read_bytes()[:16]
print("head",binascii.hexlify(b).decode())
PY