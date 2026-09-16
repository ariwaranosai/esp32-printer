#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p host-build
"${CXX:-c++}" -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-omit-frame-pointer -Icomponents/frame -Ithird_party components/frame/frame.cpp components/frame/image.cpp tests/test_frame.cpp -o host-build/test_frame
host-build/test_frame
"${PYTHON:-python3}" tests/test_images.py
