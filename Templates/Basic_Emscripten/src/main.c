/*
 * Basic Game - Template for Emscripten
 *
 * Controls:
 * - P: Pause / Resume game
 * - R: Restart game / Reinitialize state
 * - Q: Quit game (Desktop only)
 * - SPACE: Restart game (From Game Over screen)
 * - MOUSE LEFT: Interaction trigger (Playing state)
 *
 *  The keyboard, mouse, and other controls are described here.
 *
 * Build-time flags (development / CI):
 *   -DAUDIO_MUTED        silence all audio
 *   -DONLY_SHAPE         hide image if loaded
 *   -DRAYLIB_SOURCE_DIR  path to download the Raylib manual for reuse
 *
 * Build and Run:
 * --------------
 *
 * Desktop:
 *
 *  cmake -B build/ [--DAUDIO_MUTED=ON] [-DONLY_SHAPE=ON] [-DRAYLIB_SOURCE_DIR=/tmp/raylib]
 *  cmake --build build
 *  ./build/game
 *
 * Web - JavaScript/WebAssembly with Emscripten:
 *
 *  emcmake cmake -B build-web/ [--DAUDIO_MUTED=ON] [-DONLY_SHAPE=ON] [-DRAYLIB_SOURCE_DIR=/tmp/raylib]
 *  cmake --build build-web
 *  emrun build-web/index.html   (or serve build-web/ with any static server)
 *
 *  Optional (For publication):
 *
 *      cd build-web
 *      zip -r ../game-web.zip index.html index.js index.wasm index.data
 *
 *  Download manual:
 *
 *      git clone --depth 1 --branch 5.5 https://github.com/raysan5/raylib.git /tmp/raylib
 *
 * Project:
 *
 *  Game
 *  ├── CMakeLists.txt
 *  ├── shell.html        # Emscripten-ready model
 *  ├── resources
 *  │   ├── audio
 *  │   │   ├── music.mp3
 *  │   │   └── audio.mp3
 *  │   ├── fonts
 *  │   │   └── ofl.ttf
 *  │   └── images
 *  │       ├── background.png
 *  │       ├── gameOver.png
 *  │       ├── obstacle.png
 *  │       ├── enemy.png
 *  │       └── player.png
 *  └── src
 *      └── main.c
 *
 */

#include "raylib.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

// --- Emscripten Support ---
#if defined(PLATFORM_WEB)
#include <emscripten/emscripten.h>
#endif

// --- Audio Abstraction Macros ---
#ifdef AUDIO_MUTED
#define PLAY_SOUND(sfx) ((void)0)
#define PLAY_MUSIC(mus) ((void)0)
#define UPDATE_MUSIC(mus) ((void)0)
#define STOP_MUSIC(mus) ((void)0)
#else
#define PLAY_SOUND(sfx) PlaySound(sfx)
#define PLAY_MUSIC(mus) PlayMusicStream(mus)
#define UPDATE_MUSIC(mus) UpdateMusicStream(mus)
#define STOP_MUSIC(mus) StopMusicStream(mus)
#endif

// --- Graphics Mode Options ---
#ifdef ONLY_SHAPE
#define HIDDEN_IMAGE true
#else
#define HIDDEN_IMAGE false
#endif

// --- Game Constants ---
#define SCREEN_TITLE "Raylib Game Template"
#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 600
#define TARGET_FPS 60

// Game Playable Area Layout
#define AREA_X 0
#define AREA_Y 0
#define AREA_W 800
#define AREA_H 600

// Resource File Paths
#define BG_PATH "resources/images/background.png"
#define GAMEOVER_PATH "resources/images/gameOver.png"
#define MUSIC_PATH "resources/audio/music.mp3"
#define ALERT_PATH "resources/audio/audio.mp3"
#define MAIN_FONT_PATH "resources/fonts/ofl.ttf"

// --- Custom Game Types ---
typedef enum {
    STATE_PLAYING,
    STATE_PAUSE,
    STATE_WIN,
    STATE_GAMEOVER,
    STATE_QUIT
} GameState;

typedef struct {
    GameState state;
    int score;
    // Add your game entities here (e.g., Player player, Enemy enemies[10])
} Game;

// --- Function Prototypes ---
static void GameInit(Game* game);
static void GameStartLevel(Game* game);
static void GameFreeLevel(Game* game);
static void GameHandleInput(Game* game);
static void GameUpdate(Game* game);
static void GameRender(const Game* game);
static void GameCheckCollisions(Game* game);
static bool GameLevelComplete(const Game* game);
static void GameQuit(Game* game);

// Core Loop Wrapper
static void UpdateDrawFrame(void);

// UI Rendering Helper
static void DrawOverlay(const char* text, Color bgColor, Color textColor);

// Mathematical Utilities
static bool CheckCircleCollision(Vector2 point, Vector2 center, float radius);
static bool PointInArea(Vector2 v);
static float Vec2Distance(Vector2 a, Vector2 b);

// --- Global Instance State ---
// Required as a static global instance so Emscripten's loop callback can modify state contextually.
static Game gameInstance = { 0 };

// --- Main Execution Entry Point ---
int main(void)
{
    // Initialize window configuration
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_TITLE);

#if !defined(PLATFORM_WEB)
    SetTargetFPS(TARGET_FPS);
    HideCursor();
#endif

    // Boot system and startup primary entities
    GameInit(&gameInstance);

#if defined(PLATFORM_WEB)
    // Emscripten frame delegation (0 FPS uses browser requestAnimationFrame sync)
    emscripten_set_main_loop(UpdateDrawFrame, 0, 1);
#else
    // Native desktop runtime loop execution
    while (!WindowShouldClose() && gameInstance.state != STATE_QUIT) {
        UpdateDrawFrame();
    }

    // Free all resources before shutting down context
    GameQuit(&gameInstance);
#endif

    CloseWindow();
    return 0;
}

// ---------------------------------------------------------------------------
// Game Logic Implementation
// ---------------------------------------------------------------------------

static void GameInit(Game* game)
{
    game->score = 0;
    game->state = STATE_PLAYING;
    GameStartLevel(game);
}

static void GameStartLevel(Game* game)
{
    GameFreeLevel(game);

    // Load level specific items here

    game->state = STATE_PLAYING;
}

static void GameFreeLevel(Game* game)
{
    // Unload textures, fonts, sounds or dynamically allocated level-memory here
    (void)game;
}

static void GameHandleInput(Game* game)
{
    // Toggle state pausing
    if (IsKeyPressed(KEY_P)) {
        if (game->state == STATE_PLAYING) {
            game->state = STATE_PAUSE;
        } else if (game->state == STATE_PAUSE) {
            game->state = STATE_PLAYING;
        }
    }

    // General state re-initialization
    if (IsKeyPressed(KEY_R)) {
        GameInit(game);
    }

    // Application termination request (Desktop Only)
    if (IsKeyPressed(KEY_Q)) {
        game->state = STATE_QUIT;
    }

    // Respawn / Continue from death screen
    if (game->state == STATE_GAMEOVER && IsKeyPressed(KEY_SPACE)) {
        GameInit(game);
    }

    // Gameplay specific mechanics trigger
    if (game->state == STATE_PLAYING && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        Vector2 mousePos = GetMousePosition();
        if (PointInArea(mousePos)) {
            // Process area click action here
        }
    }
}

static void GameUpdate(Game* game)
{
    GameCheckCollisions(game);

    if (GameLevelComplete(game)) {
        GameStartLevel(game);
    }
}

static void GameRender(const Game* game)
{
    // Draw Level Background elements first

    // Draw Actors / Sprites here

    // Render UI / Overlay states
    if (game->state == STATE_PAUSE) {
        DrawOverlay("PAUSED\n\n[P] to resume", (Color) { 0, 0, 0, 160 }, RAYWHITE);
    }

    if (game->state == STATE_GAMEOVER) {
        DrawOverlay("GAME OVER\n\n[SPACE] to restart", (Color) { 0, 0, 0, 200 }, RED);
    }
}

static void GameCheckCollisions(Game* game)
{
    // Perform interaction math and entity hits here
    (void)game;
}

static bool GameLevelComplete(const Game* game)
{
    // Evaluate custom level target rules
    (void)game;
    return false;
}

static void GameQuit(Game* game)
{
    GameFreeLevel(game);
}

// ---------------------------------------------------------------------------
// Frame Processing Controller (Shared Pipeline)
// ---------------------------------------------------------------------------
static void UpdateDrawFrame(void)
{
    // 1. Process System Events
    GameHandleInput(&gameInstance);

    // 2. Perform Physical Transformations
    if (gameInstance.state == STATE_PLAYING) {
        GameUpdate(&gameInstance);
    }

    // 3. Render Canvas Output
    BeginDrawing();
    ClearBackground(DARKGRAY);

    GameRender(&gameInstance);

    EndDrawing();

#if defined(PLATFORM_WEB)
    // Handle specific clean-up routine if the web page signals exit state
    if (gameInstance.state == STATE_QUIT) {
        GameQuit(&gameInstance);
        emscripten_cancel_main_loop();
    }
#endif
}

// ---------------------------------------------------------------------------
// UI Rendering Helpers
// ---------------------------------------------------------------------------
static void DrawOverlay(const char* text, Color bgColor, Color textColor)
{
    // Darken background canvas boundary
    DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, bgColor);

    // Center alignment estimation math
    int fontSize = 30;
    int textWidth = MeasureText(text, fontSize);

    DrawText(text, (SCREEN_WIDTH - textWidth) / 2, (SCREEN_HEIGHT - fontSize) / 2, fontSize, textColor);
}

// ---------------------------------------------------------------------------
// Mathematical & Collision Utilities
// ---------------------------------------------------------------------------
static bool CheckCircleCollision(Vector2 point, Vector2 center, float radius)
{
    return Vec2Distance(point, center) <= radius;
}

static float Vec2Distance(Vector2 a, Vector2 b)
{
    return sqrtf((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
}

static bool PointInArea(Vector2 v)
{
    return (v.x >= AREA_X && v.x <= AREA_X + AREA_W && v.y >= AREA_Y && v.y <= AREA_Y + AREA_H);
}
