#!/usr/bin/env bash
# Build + run the full offline verification suite (Path A + Path B).
# No hardware required. Needs: g++ (C++17) and python3.
set -e
cd "$(dirname "$0")"

EX=../examples/OneOpenAir
CL=../src/Libraries/airgradient-client/src
INC="-I$EX -I../src -I$CL -Ihostshim"

echo "== [1/6] C++ Path A framing =="
g++ -std=c++17 -O2 -I"$EX" frame_test.cpp -o frame_test && ./frame_test | tail -1

echo "== [2/6] C++ typed FrameReader (device RX parse) =="
g++ -std=c++17 -O2 -I"$EX" frame_reader_test.cpp -o frame_reader_test && ./frame_reader_test | tail -1

echo "== [3/6] Python Path A reader =="
python3 test_reader.py | tail -1

echo "== [4/6] Python Sensor Hub protocol =="
python3 test_hub.py | tail -1

echo "== [5/6] Build device_sim (real firmware protocol code) + integration test =="
g++ -std=c++17 -O2 $INC \
  device_sim.cpp "$EX/AgWireLink.cpp" "$EX/AirgradientSerialClient.cpp" \
  "$CL/airgradientClient.cpp" -o device_sim
python3 test_integration.py | tail -1

echo "== [6/6] Path A end-to-end (emit_sim -> reader) =="
g++ -std=c++17 -O2 -I"$EX" emit_sim.cpp -o emit_sim
./emit_sim | python3 ag_wired_reader.py --stdin >/dev/null

echo
echo "All verification suites passed."
