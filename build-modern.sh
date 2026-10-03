#!/bin/bash
set -e

cd "$( dirname "${BASH_SOURCE[0]}" )"

mkdir -p build-release
cd build-release

cmake \
  -DCMAKE_INSTALL_PREFIX="/usr" \
  -DCMAKE_BUILD_TYPE=Release \
  -DWITH_QT5=TRUE \
  ..

cmake --build . -j"$(nproc)"

echo "Build finished. You can run:"
echo "  ./src/simplescreenrecorder --help"
echo "  ./src/simplescreenrecorder"
