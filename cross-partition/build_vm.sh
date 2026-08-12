#!/usr/bin/env bash
# Build the cross-partition POC for the ivshmem (two-VM) transport.
# The -DXPC_IVSHMEM flag is the ONLY difference from the WSL2 shm build.
set -e
cd "$(dirname "$0")"
g++ -std=c++17 -O2 -Wall -DXPC_IVSHMEM adas_writer.cpp -o adas_writer
g++ -std=c++17 -O2 -Wall -DXPC_IVSHMEM ivi_reader.cpp  -o ivi_reader
echo "built (ivshmem / -DXPC_IVSHMEM):  ./adas_writer   ./ivi_reader"
echo
echo "  guest1 (writer):  sudo ./adas_writer --count=200"
echo "  guest2 (reader):  sudo ./ivi_reader"
echo "  fault demos    :  sudo ./adas_writer --fault=crc|stale|freeze --count=200"
