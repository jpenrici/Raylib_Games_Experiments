#!/bin/bash
# A simple pipeline to prepare the output for the web version.

if [[ $EUID -eq 0 ]]; then
  log_error "It is not recommended to run this script as root!"
  exit 1
fi

rm -rf "./build-web"

if [[ ! -d "/tmp/raylib" ]]; then
  git clone --depth 1 --branch 5.5 https://github.com/raysan5/raylib.git /tmp/raylib
fi

if [[ -d "/tmp/raylib" ]]; then
  emcmake cmake -B build-web/ -DRAYLIB_SOURCE_DIR=/tmp/raylib
fi

if [[ -d "./build-web" ]]; then
  cmake --build build-web
fi

if [[ -f "./build-web/index.html" && -f "./build-web/index.js" && -f "./build-web/index.wasm" && -f "./build-web/index.data" ]]; then
  cd "./build-web"
  zip -r ../Spaceship-web.zip index.html index.js index.wasm index.data
fi

echo ""
echo "Run to test:"
echo "  emrun build-web/index.html"

exit 0
