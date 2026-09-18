#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p host-build
"${CXX:-c++}" -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-omit-frame-pointer -Imain tests/test_refresh_policy.cpp -o host-build/test_refresh_policy
host-build/test_refresh_policy
"${CXX:-c++}" -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-omit-frame-pointer -Imain tests/test_audio_sleep.cpp -o host-build/test_audio_sleep
host-build/test_audio_sleep
"${CXX:-c++}" -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-omit-frame-pointer -Icomponents/frame -Ithird_party components/frame/frame.cpp components/frame/image.cpp tests/test_frame.cpp -o host-build/test_frame
host-build/test_frame
"${PYTHON:-python3}" -c "from PIL import Image; Image.new('RGB', (432,576), 'navy').save('host-build/cache-test.jpg')"
"${CXX:-c++}" -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-omit-frame-pointer -Icomponents/frame -Ithird_party -Itests components/frame/frame.cpp components/frame/image.cpp tests/test_photo_client.cpp -o host-build/test_photo_client
host-build/test_photo_client host-build/cache-test.jpg
"${PYTHON:-python3}" -c "from PIL import Image; Image.new('RGB', (456,656), 'navy').save('host-build/native-test.jpg')"
host-build/test_photo_client host-build/native-test.jpg
"${PYTHON:-python3}" -c "from PIL import Image; Image.new('RGB', (800,480), 'orange').save('host-build/cache-other.jpg')"
"${CXX:-c++}" -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-omit-frame-pointer -Icomponents/frame -Ithird_party -Imain components/frame/frame.cpp components/frame/image.cpp components/frame/photo_cache.cpp tests/test_photo_cache.cpp -o host-build/test_photo_cache
host-build/test_photo_cache host-build/cache-test.jpg host-build/cache-other.jpg
"${CXX:-c++}" -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-omit-frame-pointer -Icomponents/frame -Icomponents/weather components/frame/frame.cpp components/weather/weather.cpp tests/test_weather.cpp $(pkg-config --cflags --libs libcjson) -o host-build/test_weather
host-build/test_weather
"${PYTHON:-python3}" tests/test_images.py
"${PYTHON:-python3}" tests/test_prepare_photo.py
