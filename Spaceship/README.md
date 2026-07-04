# Spaceship

A small 2D arcade game built in C with raylib.
Compiles as a native desktop app or as a browser build (via Emscripten), from the same source and the same `CMakeLists.txt`.

## Detail:

This is not a complete game.

## Features

- Native desktop build (Linux)
- Web/WebAssembly build (Emscripten), playable in browser.
- Optional build flags for development/testing:
  - `AUDIO_MUTED` — disable all audio
  - `ONLY_SHAPE` — render shapes instead of images
  - `DRAYLIB_SOURCE_DIR` - path to download the Raylib manual

## Requirements

- CMake 3.21+
- A C11 compiler
- [raylib](https://github.com/raysan5/raylib) 5.5 — fetched automatically if not already installed (see below)
- For the web build: the [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html) (`emcc`, `emcmake`)

## Building — Desktop

```bash
cmake -B build
cmake --build build
./build/game
```

Optional flags:
```bash
cmake -B build -DAUDIO_MUTED=ON -DONLY_SHAPE=ON
```

## Building — Web

```bash
emcmake cmake -B build-web
cmake --build build-web
emrun build-web/index.html   # or serve build-web/ with any static file server
```

This produces `index.html`, `index.js`, `index.wasm`, and `index.data` (game assets bundled from `resources/`) inside `build-web/`.

### Reusing a local raylib checkout

Rebuilding `build-web/` from scratch re-downloads raylib's source each time. To avoid that, clone it once outside the project and point CMake at it:

```bash
git clone --depth 1 --branch 5.5 https://github.com/raysan5/raylib.git /tmp/raylib
emcmake cmake -B build-web -DRAYLIB_SOURCE_DIR=/tmp/raylib
```

## Project structure

```
.
├── CMakeLists.txt   # build configuration (desktop + web)
├── shell.html       # custom Emscripten page template used for the web build
├── src/
│   └── main.c        # game source
└── resources/         # images, fonts, audio
```

## Display

![Game](https://github.com/jpenrici/Raylib_Games_Experiments/blob/main/Spaceship/display/display.png)

## Learn more:

[raylib](https://www.raylib.com) : A simple and easy-to-use library to enjoy videogames programming.