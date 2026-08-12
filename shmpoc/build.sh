#!/usr/bin/env bash
set -e
CXX=${CXX:-g++}
FLAGS="-std=c++17 -O2 -Wall -pthread"
$CXX $FLAGS adas_writer.cpp -o adas_writer -lrt
$CXX $FLAGS ivi_reader.cpp  -o ivi_reader  -lrt
echo "built: ./adas_writer  ./ivi_reader"
