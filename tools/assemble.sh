#!/usr/bin/env bash
# Builds the full firmware source tree in ./work:
#   1. Downloads the pinned upstream OpenIris-ESPIDF commit.
#   2. Copies every file from ./overlay on top of it (Venti changes).
#   3. Selects the Venti N4R2 board profile.
# Afterwards, build with ESP-IDF v5.4.2 from ./work:  idf.py build && idf.py merge-bin -f raw
set -euo pipefail

cd "$(dirname "$0")/.."
source ./upstream.env

rm -rf work upstream.zip
curl -fsSL -o upstream.zip "https://github.com/${UPSTREAM_REPO}/archive/${UPSTREAM_SHA}.zip"
unzip -q upstream.zip
mv "${UPSTREAM_REPO#*/}-${UPSTREAM_SHA}" work
rm upstream.zip

cp -R overlay/. work/

cd work
python3 tools/switchBoardType.py --board "${BOARD}" --clear-wifi
echo "Assembled ${UPSTREAM_REPO}@${UPSTREAM_SHA} + overlay for board ${BOARD} in ./work"
