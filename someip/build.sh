#!/usr/bin/env bash
set -e; cd "$(dirname "$0")"
g++ -std=c++17 -O2 -Wall adas_service.cpp -o adas_service
g++ -std=c++17 -O2 -Wall ivi_client.cpp  -o ivi_client
echo "built: ./adas_service   ./ivi_client"
echo "run (control plane over the private link):"
echo "  guest1:  ./adas_service 192.168.100.1"
echo "  guest2:  ./ivi_client   192.168.100.1 10"
