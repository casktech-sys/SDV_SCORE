#!/usr/bin/env bash
# Usage: ./demo.sh [none|crc|stale|freeze]
FAULT=${1:-none}
echo "=== DEMO: fault=${FAULT} ============================================"
./adas_writer --fault="$FAULT" --count=60 &
WPID=$!
sleep 0.3
./ivi_reader
wait $WPID 2>/dev/null
echo "====================================================================="
