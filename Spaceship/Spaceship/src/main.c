/*
 * A simple spaceship game.
 *
 * Controls:
 *   WASD or Arrow keys move the spaceship
 *   Spacebar shoots
 *   N restarts after time runs out
 *   ESC exits the game
 *
 * Build-time flags:
 *   -DAUDIO_MUTED  silence all audio (development / CI)
 *   -DONLY_SHAPE   hide image if loaded (development / CI)
 *
 * Build and Run:
 * --------------
 *
 * Desktop:
 *
 *  cmake -B build/ [--DAUDIO_MUTED=ON] [-DONLY_SHAPE=ON]
 *  cmake --build build
 *  ./build/game
 *
 * Web - JavaScript/WebAssembly with Emscripten:
 *
 *  emcmake cmake -B build-web
 *  cmake --build build-web
 *  emrun build-web/game.html   (or serve build-web/ with any static server)
 *
 * Project:
 *
 *  Spaceship
 *  ├── CMakeLists*.txt
 *  ├── resources
 *  │   ├── audio
 *  │   │   ├── alert.mp3
 *  │   │   ├── explosion.mp3
 *  │   │   ├── music.mp3
 *  │   │   └── shot.mp3
 *  │   ├── fonts
 *  │   │   ├── NotoSans-Black.ttf
 *  │   │   └── OFL.txt
 *  │   └── images
 *  │       ├── background.png
 *  │       ├── bullet.png
 *  │       ├── gameOver.png
 *  │       ├── meteor.png
 *  │       └── spaceship.png
 *  └── src
 *      └── main.c
 *
 */

#include "raylib.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Emscripten
// ---------------------------------------------------------------------------

#if defined(PLATFORM_WEB)
#include <emscripten/emscripten.h>
#endif

// ---------------------------------------------------------------------------
// Audio mute
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Graphics
// ---------------------------------------------------------------------------

#ifdef ONLY_SHAPE
#define HIDDEN_IMAGE true
#else
#define HIDDEN_IMAGE false
#endif

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

#define SCREEN_TITLE "Spaceship"
#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 600
#define TIMER_FPS 60

// Background
#define BG_PATH "resources/images/background.png"

// Player  (sheet: 1 row × 6 cols, 127×127 px each)
#define PLAYER_PATH "resources/images/spaceship.png"
#define PLAYER_WIDTH 127
#define PLAYER_HEIGHT 127
#define PLAYER_ROWS 1
#define PLAYER_COLS 6
#define PLAYER_DISPLAY_W 64
#define PLAYER_DISPLAY_H 64
#define PLAYER_MOVE_H 4
#define PLAYER_MOVE_V 4
#define PLAYER_ENERGY_MIN 0
#define PLAYER_ENERGY_MAX 10

// Bullet  (sheet: 1 row × 2 cols, 127×127 px each)
// frame 0 = projectile,  frame 1 = explosion
#define BULLET_PATH "resources/images/bullet.png"
#define BULLET_WIDTH 127
#define BULLET_HEIGHT 127
#define BULLET_ROWS 1
#define BULLET_COLS 2
#define BULLET_DISPLAY_W 32
#define BULLET_DISPLAY_H 32
#define BULLET_SPEED 10
#define BULLET_FRAME_NORMAL 0 // flying
#define BULLET_FRAME_EXPLODE 1 // hit animation
#define BULLET_EXPLODE_TICKS 20 // frames the explosion stays visible

// Meteor  (sheet: 1 row × 2 cols, 96×96 px each)
// frame 0 = red/target,  frame 1 = grey/distant
#define OBSTACLE_PATH "resources/images/meteor.png"
#define OBSTACLE_WIDTH 96
#define OBSTACLE_HEIGHT 96
#define OBSTACLE_ROWS 1
#define OBSTACLE_COLS 2
#define METEOR_FRAME_TARGET 0 // active layer, collidable
#define METEOR_FRAME_DISTANT 1 // background layer, decorative

// Target meteors (collision layer)
#define TARGET_COUNT 8
#define TARGET_DISPLAY_W 64
#define TARGET_DISPLAY_H 64

// Distant meteors (background layer — smaller, slower)
#define DISTANT_COUNT 14
#define DISTANT_DISPLAY_W 40
#define DISTANT_DISPLAY_H 40

// Game Over image
#define GAMEOVER_PATH "resources/images/gameOver.png"

// Fallback colors
#define COLOR_PLAYER_FALLBACK ((Color) { 202, 14, 227, 255 })
#define COLOR_BULLET_FALLBACK ((Color) { 0, 200, 80, 255 })
#define COLOR_TARGET_FALLBACK ((Color) { 180, 40, 40, 255 })
#define COLOR_DISTANT_FALLBACK ((Color) { 100, 100, 100, 180 })

// Audio
#define MUSIC_PATH "resources/audio/music.mp3"
#define ALERT_PATH "resources/audio/alert.mp3"
#define EXPLOSION_PATH "resources/audio/explosion.mp3"
#define SHOT_PATH "resources/audio/shot.mp3"

// Font
#define FONT1 "resources/fonts/NotoSans-Black.ttf"

// HUD
#define HUD_FONT_SIZE 20
#define ENERGY_BAR_W 80
#define ENERGY_BAR_H 10
#define GAME_DURATION 1800 // frames (30 s @ 60 fps)

// Score
#define SCORE_HIT 10
#define SCORE_COLLISION -5

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

typedef struct {
    unsigned srcX, srcY;
    unsigned srcW, srcH;
    unsigned width, height;
} Sprite;

typedef struct {
    Texture2D texture;
    Sprite* frames;
    unsigned count;
    bool loaded;
} SpriteSheet;

typedef struct {
    Texture2D texture;
    float scrollOffset;
    bool loaded;
} ScrollBackground;

typedef struct {
    SpriteSheet sheet;
    Vector2 pos;
    unsigned energy;
    unsigned frame;
    int animTimer;
} Player;

// Bullet states
typedef enum {
    BULLET_IDLE, // not fired
    BULLET_FLYING, // moving up
    BULLET_EXPLODING // hit something — playing explosion frame
} BulletState;

typedef struct {
    SpriteSheet sheet;
    Vector2 pos;
    BulletState state;
    int explodeTimer; // counts down from BULLET_EXPLODE_TICKS
} Bullet;

// Generic obstacle — used for both target and distant meteors
typedef struct {
    Vector2 pos;
    float angle;
    float rotSpeed;
    float speed;
    bool active;
} Obstacle;

typedef struct {
    int timer;
    int score;
    int hits;
    int shots;
    int collisions;
    unsigned energy;
} HUD;

typedef struct {
    Texture2D texture;
    bool imageLoaded;
} GameOverImage;

typedef enum {
    STATE_PLAYING,
    STATE_PAUSE,
    STATE_WIN,
    STATE_GAMEOVER,
    STATE_QUIT
} GameState;

typedef struct {
    Player spaceship;
    Bullet bullet;

    // Two meteor layers — share the same meteorSheet texture
    Obstacle targets[TARGET_COUNT]; // collidable
    Obstacle extras[DISTANT_COUNT]; // decorative

    SpriteSheet meteorSheet; // ONE texture for both layers

    ScrollBackground background;

    HUD hud;
    Font font;
    bool fontLoaded;
    Music music;
    Sound sfxShot;
    Sound sfxExplosion;
    Sound sfxAlert;
    bool audioLoaded;

    GameOverImage gameOver;
    GameState state;
} Game;

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------

static void GameInit(Game* game, int screenW, int screenH);
static void GameHandleInput(Game* game);
static void GameUpdate(Game* game);
static void GamePlay(Game* game);
static void GameRender(const Game* game);
static void GameCheckCollisions(Game* game);
static void GameQuit(Game* game);
static void UpdateDrawFrame(void);

void PrepareSheet(SpriteSheet* sheet, const char* path, unsigned rows, unsigned cols, unsigned frameW, unsigned frameH);
void DrawSprite(const SpriteSheet* sheet, unsigned frame, float x, float y);
void DrawSpriteEx(const SpriteSheet* sheet, unsigned frame, float x, float y, float angle, Color tint);
void DrawSpriteFallback(const SpriteSheet* sheet, unsigned frame, float x, float y, Color color);
void ResizeSprite(SpriteSheet* sheet, unsigned newW, unsigned newH);
void UnloadSheet(SpriteSheet* sheet);

void LoadAllAssets(Game* game);
void UnloadAllAssets(Game* game);

void InitScroll(ScrollBackground* bg, const char* path);
void UpdateScroll(ScrollBackground* bg, float speed);
void DrawScrollBackground(const ScrollBackground* bg);

static void DrawHud(const Game* game);
static void DrawGameOver(const Game* game, int screenW, int screenH);
static void DrawPauseOverlay(void);

static void SpawnTarget(Obstacle* m, int screenW, int screenH);
static void SpawnDistant(Obstacle* m, int screenW, int screenH);
static void ResetBullet(Bullet* b);

// ---------------------------------------------------------------------------
// Global game state
// ---------------------------------------------------------------------------
// Needs to live outside main() so the Emscripten main-loop callback
// (UpdateDrawFrame), which takes no arguments, can reach it too.
static Game game = { 0 };

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(void)
{
    // Window
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_TITLE);

#if !defined(PLATFORM_WEB)
    SetTargetFPS(TIMER_FPS);
    HideCursor();
#endif

    // Initialize
    GameInit(&game, SCREEN_WIDTH, SCREEN_HEIGHT);

#if defined(PLATFORM_WEB)
    emscripten_set_main_loop(UpdateDrawFrame, 0, 1);
#else
    // Game Loop
    while (!WindowShouldClose() && game.state != STATE_QUIT) {
        UpdateDrawFrame();
    }

    // Exit
    GameQuit(&game);
#endif // Emscripten

    CloseWindow();

    return 0;
}

// ---------------------------------------------------------------------------
// One tick: events + update + render.
// Shared between the desktop while(...) loop and the Emscripten callback.
// ---------------------------------------------------------------------------

static void UpdateDrawFrame(void)
{
    // Events
    GameHandleInput(&game);

    // Update
    if (game.state == STATE_PLAYING)
        GameUpdate(&game);

    // Render
    GameRender(&game);

#if defined(PLATFORM_WEB)
    if (game.state == STATE_QUIT) {
        GameQuit(&game);
        emscripten_cancel_main_loop();
    }
#endif
}

// ---------------------------------------------------------------------------
// Init / Quit
// ---------------------------------------------------------------------------

static void GameInit(Game* game, int screenW, int screenH)
{
    UnloadAllAssets(game);
    memset(game, 0, sizeof(*game));
    LoadAllAssets(game);

    // ---- Player ----
    game->spaceship.pos.x = (float)(screenW / 2 - PLAYER_DISPLAY_W / 2);
    game->spaceship.pos.y = (float)(screenH / 2 - PLAYER_DISPLAY_H / 2);
    game->spaceship.energy = PLAYER_ENERGY_MAX;

    // ---- Bullet ----
    ResetBullet(&game->bullet);

    // ---- Target meteors (collidable) ----
    for (int i = 0; i < TARGET_COUNT; i++) {
        SpawnTarget(&game->targets[i], screenW, screenH);
        game->targets[i].pos.y = (float)GetRandomValue(-screenH, 0);
    }

    // ---- Distant meteors (decorative) ----
    for (int i = 0; i < DISTANT_COUNT; i++) {
        SpawnDistant(&game->extras[i], screenW, screenH);
        game->extras[i].pos.y = (float)GetRandomValue(-screenH, 0);
    }

    // ---- HUD ----
    game->hud.timer = GAME_DURATION;
    game->hud.energy = PLAYER_ENERGY_MAX;

    game->state = STATE_PLAYING;

    PLAY_MUSIC(game->music);
}

static void GameQuit(Game* game)
{
    UnloadAllAssets(game);
}

// ---------------------------------------------------------------------------
// Assets
// ---------------------------------------------------------------------------

void LoadAllAssets(Game* game)
{
    // Background
    InitScroll(&game->background, BG_PATH);

    // Player
    PrepareSheet(&game->spaceship.sheet, PLAYER_PATH, PLAYER_ROWS, PLAYER_COLS, PLAYER_WIDTH, PLAYER_HEIGHT);
    ResizeSprite(&game->spaceship.sheet, PLAYER_DISPLAY_W, PLAYER_DISPLAY_H);

    // Bullet
    PrepareSheet(&game->bullet.sheet, BULLET_PATH, BULLET_ROWS, BULLET_COLS, BULLET_WIDTH, BULLET_HEIGHT);
    ResizeSprite(&game->bullet.sheet, BULLET_DISPLAY_W, BULLET_DISPLAY_H);

    // Meteors — ONE shared sheet for both layers
    PrepareSheet(&game->meteorSheet, OBSTACLE_PATH, OBSTACLE_ROWS, OBSTACLE_COLS, OBSTACLE_WIDTH, OBSTACLE_HEIGHT);

    // Game Over
    game->gameOver.texture = LoadTexture(GAMEOVER_PATH);
    game->gameOver.imageLoaded = (game->gameOver.texture.id != 0);

    // Font
    game->font = LoadFontEx(FONT1, HUD_FONT_SIZE, NULL, 0);
    game->fontLoaded = (game->font.texture.id != 0);

    // Audio
    if (!IsAudioDeviceReady())
        InitAudioDevice();

    game->music = LoadMusicStream(MUSIC_PATH);
    game->sfxShot = LoadSound(SHOT_PATH);
    game->sfxExplosion = LoadSound(EXPLOSION_PATH);
    game->sfxAlert = LoadSound(ALERT_PATH);
    game->audioLoaded = true;
}

void UnloadAllAssets(Game* game)
{
    UnloadSheet(&game->spaceship.sheet);
    UnloadSheet(&game->bullet.sheet);
    UnloadSheet(&game->meteorSheet);

    if (game->background.loaded) {
        UnloadTexture(game->background.texture);
        game->background.loaded = false;
    }
    if (game->gameOver.imageLoaded) {
        UnloadTexture(game->gameOver.texture);
        game->gameOver.imageLoaded = false;
    }
    if (game->fontLoaded) {
        UnloadFont(game->font);
        game->fontLoaded = false;
    }
    if (game->audioLoaded) {
        STOP_MUSIC(game->music);
        UnloadMusicStream(game->music);
        UnloadSound(game->sfxShot);
        UnloadSound(game->sfxExplosion);
        UnloadSound(game->sfxAlert);
        game->audioLoaded = false;
    }
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

static void GameHandleInput(Game* game)
{
    if (IsKeyPressed(KEY_P) || IsKeyPressed(KEY_ESCAPE)) {
        if (game->state == STATE_PLAYING)
            game->state = STATE_PAUSE;
        else if (game->state == STATE_PAUSE)
            game->state = STATE_PLAYING;
    }

    if (IsKeyPressed(KEY_R))
        GameInit(game, SCREEN_WIDTH, SCREEN_HEIGHT);

    if (IsKeyPressed(KEY_Q))
        game->state = STATE_QUIT;

    if (game->state == STATE_GAMEOVER && IsKeyPressed(KEY_N))
        GameInit(game, SCREEN_WIDTH, SCREEN_HEIGHT);

    if (game->state != STATE_PLAYING)
        return;

    Player* ship = &game->spaceship;

    if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) {
        ship->pos.y -= PLAYER_MOVE_V;
        if (ship->pos.y < 0)
            ship->pos.y = 0;
    }
    if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) {
        ship->pos.y += PLAYER_MOVE_V;
        if (ship->pos.y > SCREEN_HEIGHT - PLAYER_DISPLAY_H)
            ship->pos.y = SCREEN_HEIGHT - PLAYER_DISPLAY_H;
    }
    if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) {
        ship->pos.x -= PLAYER_MOVE_H;
        if (ship->pos.x < 0)
            ship->pos.x = 0;
    }
    if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) {
        ship->pos.x += PLAYER_MOVE_H;
        if (ship->pos.x > SCREEN_WIDTH - PLAYER_DISPLAY_W)
            ship->pos.x = SCREEN_WIDTH - PLAYER_DISPLAY_W;
    }

    // Fire — only when bullet is idle
    if (IsKeyPressed(KEY_SPACE) && game->bullet.state == BULLET_IDLE) {
        Bullet* b = &game->bullet;
        b->state = BULLET_FLYING;
        b->pos.x = ship->pos.x + PLAYER_DISPLAY_W / 2.0f - BULLET_DISPLAY_W / 2.0f;
        b->pos.y = ship->pos.y - BULLET_DISPLAY_H;
        game->hud.shots++;
        PLAY_SOUND(game->sfxShot);
    }
}

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------

static void GameUpdate(Game* game)
{
    UPDATE_MUSIC(game->music);

    game->hud.timer--;
    if (game->hud.timer <= 0) {
        game->hud.timer = 0;
        game->state = STATE_GAMEOVER;
        return;
    }

    // Background scrolls upward — negative speed
    UpdateScroll(&game->background, -2.0f);

    GamePlay(game);

    game->hud.energy = game->spaceship.energy;

    if (game->spaceship.energy == PLAYER_ENERGY_MIN) {
        game->state = STATE_GAMEOVER;
        PLAY_SOUND(game->sfxAlert);
    }
}

static void GamePlay(Game* game)
{
    // ---- Player animation ----
    Player* ship = &game->spaceship;
    if (++ship->animTimer >= 8) {
        ship->animTimer = 0;
        ship->frame = (ship->frame + 1) % (unsigned)PLAYER_COLS;
    }

    // ---- Bullet ----
    Bullet* b = &game->bullet;
    if (b->state == BULLET_FLYING) {
        b->pos.y -= BULLET_SPEED;
        if (b->pos.y + BULLET_DISPLAY_H < 0)
            ResetBullet(b);
    } else if (b->state == BULLET_EXPLODING) {
        b->explodeTimer--;
        if (b->explodeTimer <= 0)
            ResetBullet(b);
    }

    // ---- Target meteors (collidable) ----
    for (int i = 0; i < TARGET_COUNT; i++) {
        Obstacle* m = &game->targets[i];
        m->pos.y += m->speed;
        m->angle += m->rotSpeed;
        if (m->angle >= 360.0f)
            m->angle -= 360.0f;
        if (m->pos.y > SCREEN_HEIGHT + TARGET_DISPLAY_H)
            SpawnTarget(m, SCREEN_WIDTH, SCREEN_HEIGHT);
    }

    // ---- Distant meteors (decorative) ----
    for (int i = 0; i < DISTANT_COUNT; i++) {
        Obstacle* m = &game->extras[i];
        m->pos.y += m->speed;
        m->angle += m->rotSpeed;
        if (m->angle >= 360.0f)
            m->angle -= 360.0f;
        if (m->pos.y > SCREEN_HEIGHT + DISTANT_DISPLAY_H)
            SpawnDistant(m, SCREEN_WIDTH, SCREEN_HEIGHT);
    }

    GameCheckCollisions(game);
}

static void GameCheckCollisions(Game* game)
{
    Player* ship = &game->spaceship;

    Rectangle shipRect = {
        ship->pos.x + 8.0f,
        ship->pos.y + 8.0f,
        (float)(PLAYER_DISPLAY_W - 16),
        (float)(PLAYER_DISPLAY_H - 16)
    };

    for (int i = 0; i < TARGET_COUNT; i++) {
        Obstacle* m = &game->targets[i];

        Rectangle mRect = {
            m->pos.x + 4.0f,
            m->pos.y + 4.0f,
            (float)(TARGET_DISPLAY_W - 8),
            (float)(TARGET_DISPLAY_H - 8)
        };

        // ---- Bullet vs target meteor ----
        if (game->bullet.state == BULLET_FLYING) {
            Rectangle bRect = {
                game->bullet.pos.x,
                game->bullet.pos.y,
                (float)BULLET_DISPLAY_W,
                (float)BULLET_DISPLAY_H
            };
            if (CheckCollisionRecs(bRect, mRect)) {
                // Bullet switches to explosion at impact position
                game->bullet.state = BULLET_EXPLODING;
                game->bullet.explodeTimer = BULLET_EXPLODE_TICKS;

                SpawnTarget(m, SCREEN_WIDTH, SCREEN_HEIGHT);
                game->hud.hits++;
                game->hud.score += SCORE_HIT;
                PLAY_SOUND(game->sfxExplosion);
                continue;
            }
        }

        // ---- Ship vs target meteor ----
        if (CheckCollisionRecs(shipRect, mRect)) {
            SpawnTarget(m, SCREEN_WIDTH, SCREEN_HEIGHT);
            game->hud.collisions++;
            game->hud.score += SCORE_COLLISION;
            if (ship->energy > PLAYER_ENERGY_MIN)
                ship->energy--;
            PLAY_SOUND(game->sfxAlert);
        }
    }
    // Distant meteors intentionally excluded from collision checks
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

// Helper: draw a meteor from the shared sheet at a specific fixed frame,
// with per-instance position, rotation, and custom display size.
static void DrawMeteor(const SpriteSheet* sheet, unsigned fixedFrame,
    float x, float y, float angle, unsigned dispW, unsigned dispH, Color tint)
{
    if (fixedFrame >= sheet->count)
        return;

    if (!sheet->loaded || HIDDEN_IMAGE) {
        DrawSpriteFallback(sheet, 0, x, y, fixedFrame == 0 ? COLOR_TARGET_FALLBACK : COLOR_DISTANT_FALLBACK);
        return;
    }

    const Sprite* s = &sheet->frames[fixedFrame];
    float hw = (float)dispW / 2.0f;
    float hh = (float)dispH / 2.0f;

    Rectangle src = { (float)s->srcX, (float)s->srcY, (float)s->srcW, (float)s->srcH };
    Rectangle dst = { x + hw, y + hh, (float)dispW, (float)dispH };
    Vector2 origin = { hw, hh };

    DrawTexturePro(sheet->texture, src, dst, origin, angle, tint);
}

static void GameRender(const Game* game)
{
    BeginDrawing();
    ClearBackground(BLACK);

    // 1) Scrolling background
    DrawScrollBackground(&game->background);

    // 2) Distant meteors — smaller, behind everything
    for (int i = 0; i < DISTANT_COUNT; i++) {
        const Obstacle* m = &game->extras[i];
        DrawMeteor(&game->meteorSheet, METEOR_FRAME_DISTANT,
            m->pos.x, m->pos.y, m->angle,
            DISTANT_DISPLAY_W, DISTANT_DISPLAY_H,
            (Color) { 180, 180, 180, 160 }); // dimmed grey tint
    }

    // 3) Target meteors — full size, active layer
    for (int i = 0; i < TARGET_COUNT; i++) {
        const Obstacle* m = &game->targets[i];
        DrawMeteor(&game->meteorSheet, METEOR_FRAME_TARGET,
            m->pos.x, m->pos.y, m->angle,
            TARGET_DISPLAY_W, TARGET_DISPLAY_H, WHITE);
    }

    // 4a) Bullet
    const Bullet* b = &game->bullet;
    if (b->state == BULLET_FLYING) {
        if (b->sheet.loaded && !HIDDEN_IMAGE)
            DrawSprite(&b->sheet, BULLET_FRAME_NORMAL, b->pos.x, b->pos.y);
        else
            DrawSpriteFallback(&b->sheet, BULLET_FRAME_NORMAL,
                b->pos.x, b->pos.y, COLOR_BULLET_FALLBACK);
    } else if (b->state == BULLET_EXPLODING) {
        if (b->sheet.loaded && !HIDDEN_IMAGE)
            DrawSprite(&b->sheet, BULLET_FRAME_EXPLODE, b->pos.x, b->pos.y);
        else
            DrawSpriteFallback(&b->sheet, BULLET_FRAME_EXPLODE,
                b->pos.x, b->pos.y, (Color) { 255, 140, 0, 255 });
    }

    // 4b) Player spaceship
    if (game->spaceship.sheet.loaded && !HIDDEN_IMAGE)
        DrawSprite(&game->spaceship.sheet, game->spaceship.frame,
            game->spaceship.pos.x, game->spaceship.pos.y);
    else
        DrawSpriteFallback(&game->spaceship.sheet, game->spaceship.frame,
            game->spaceship.pos.x, game->spaceship.pos.y,
            COLOR_PLAYER_FALLBACK);

    // 5) HUD
    DrawHud(game);

    // Overlays
    if (game->state == STATE_GAMEOVER)
        DrawGameOver(game, SCREEN_WIDTH, SCREEN_HEIGHT);
    else if (game->state == STATE_PAUSE)
        DrawPauseOverlay();

    // Bottom hint bar
    DrawRectangle(0, SCREEN_HEIGHT - 24, SCREEN_WIDTH, 24, (Color) { 0, 0, 0, 180 });
    DrawText("Use WASD or arrows to move, Space to shot and P to pause and R to restart.",
        10, SCREEN_HEIGHT - 19, 14, LIGHTGRAY);

    EndDrawing();
}

// ---------------------------------------------------------------------------
// HUD
// ---------------------------------------------------------------------------

static void DrawHud(const Game* game)
{
    const HUD* h = &game->hud;
    int x = 10, y = 10, sz = HUD_FONT_SIZE;
    char buf[64];

    snprintf(buf, sizeof(buf), "Timer: %d", h->timer);
    DrawText(buf, x, y, sz, RAYWHITE);
    y += sz + 2;
    snprintf(buf, sizeof(buf), "Score : %d", h->score);
    DrawText(buf, x, y, sz, RAYWHITE);
    y += sz + 2;
    snprintf(buf, sizeof(buf), "Hits : %d", h->hits);
    DrawText(buf, x, y, sz, RAYWHITE);
    y += sz + 2;
    snprintf(buf, sizeof(buf), "Shots : %d", h->shots);
    DrawText(buf, x, y, sz, RAYWHITE);
    y += sz + 2;
    snprintf(buf, sizeof(buf), "Collisions: %d", h->collisions);
    DrawText(buf, x, y, sz, RAYWHITE);
    y += sz + 2;
    snprintf(buf, sizeof(buf), "Energy: %u", h->energy);
    DrawText(buf, x, y, sz, RAYWHITE);
    y += sz + 4;

    float ratio = (float)h->energy / (float)PLAYER_ENERGY_MAX;
    Color barCol = (ratio > 0.5f) ? GREEN : (ratio > 0.25f) ? ORANGE
                                                            : RED;
    DrawRectangle(x, y, ENERGY_BAR_W, ENERGY_BAR_H, DARKGRAY);
    DrawRectangle(x, y, (int)(ENERGY_BAR_W * ratio), ENERGY_BAR_H, barCol);
    DrawRectangleLines(x, y, ENERGY_BAR_W, ENERGY_BAR_H, WHITE);

    snprintf(buf, sizeof(buf), "Position: %.0f,%.0f",
        game->spaceship.pos.x, game->spaceship.pos.y);
    int pw = MeasureText(buf, 14);
    DrawText(buf, SCREEN_WIDTH - pw - 10, SCREEN_HEIGHT - 40, 14, WHITE);

    DrawText("Top Down", SCREEN_WIDTH - 110, 30, 20, WHITE);
}

// ---------------------------------------------------------------------------
// Game Over
// ---------------------------------------------------------------------------

static void DrawGameOver(const Game* game, int screenW, int screenH)
{
    if (game->gameOver.imageLoaded && !HIDDEN_IMAGE) {
        int x = (screenW - game->gameOver.texture.width) / 2;
        int y = (screenH - game->gameOver.texture.height) / 2;
        DrawTexture(game->gameOver.texture, x, y, WHITE);
    } else {
        int pw = 360, ph = 140;
        int px = (screenW - pw) / 2, py = (screenH - ph) / 2;
        DrawRectangle(px, py, pw, ph, (Color) { 0, 0, 0, 200 });
        DrawRectangleLines(px, py, pw, ph, RED);
        const char* msg = "GAME OVER";
        const char* hint = "Press N for a new game";
        DrawText(msg, px + (pw - MeasureText(msg, 48)) / 2, py + 18, 48, RED);
        DrawText(hint, px + (pw - MeasureText(hint, 20)) / 2, py + ph - 34, 20, LIGHTGRAY);
    }
}

// ---------------------------------------------------------------------------
// Pause overlay
// ---------------------------------------------------------------------------

static void DrawPauseOverlay(void)
{
    DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, (Color) { 0, 0, 0, 160 });
    const char* msg = "PAUSED  [Esc to resume]";
    int sz = 36;
    DrawText(msg,
        (SCREEN_WIDTH - MeasureText(msg, sz)) / 2,
        (SCREEN_HEIGHT - sz) / 2,
        sz, RAYWHITE);
}

// ---------------------------------------------------------------------------
// Scrolling background
// ---------------------------------------------------------------------------

void InitScroll(ScrollBackground* bg, const char* path)
{
    bg->texture = LoadTexture(path);
    bg->loaded = (bg->texture.id != 0);
    bg->scrollOffset = 0.0f;
}

void UpdateScroll(ScrollBackground* bg, float speed)
{
    float h = (float)bg->texture.height;
    if (h <= 0.0f)
        return;
    bg->scrollOffset += speed;
    // Wrap in both directions
    if (bg->scrollOffset >= h)
        bg->scrollOffset -= h;
    if (bg->scrollOffset < 0)
        bg->scrollOffset += h;
}

void DrawScrollBackground(const ScrollBackground* bg)
{
    if (!bg->loaded || HIDDEN_IMAGE) {
        DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, (Color) { 10, 10, 30, 255 });
        return;
    }

    float off = bg->scrollOffset;
    float th = (float)bg->texture.height;
    float tw = (float)bg->texture.width;
    float sw = (float)SCREEN_WIDTH;
    float sh = (float)SCREEN_HEIGHT;

    // Tile 1
    Rectangle src1 = { 0, off, tw, th - off };
    Rectangle dst1 = { 0, 0, sw, sh * ((th - off) / th) };
    DrawTexturePro(bg->texture, src1, dst1, (Vector2) { 0, 0 }, 0.0f, WHITE);

    // Tile 2 (wrap-around)
    if (off > 0.0f) {
        Rectangle src2 = { 0, 0, tw, off };
        Rectangle dst2 = { 0, dst1.height, sw, sh * (off / th) };
        DrawTexturePro(bg->texture, src2, dst2, (Vector2) { 0, 0 }, 0.0f, WHITE);
    }
}

// ---------------------------------------------------------------------------
// Sprite helpers
// ---------------------------------------------------------------------------

void PrepareSheet(SpriteSheet* sheet, const char* path,
    unsigned rows, unsigned cols,
    unsigned frameW, unsigned frameH)
{
    unsigned count = rows * cols;
    sheet->frames = (Sprite*)malloc(sizeof(Sprite) * (size_t)count);
    sheet->count = count;
    sheet->loaded = false;

    if (!sheet->frames) {
        fprintf(stderr, "[sprite] malloc failed for %s\n", path);
        return;
    }

    unsigned idx = 0;
    for (unsigned r = 0; r < rows; ++r) {
        for (unsigned c = 0; c < cols; ++c) {
            Sprite* s = &sheet->frames[idx++];
            s->srcX = c * frameW;
            s->srcY = r * frameH;
            s->srcW = frameW;
            s->srcH = frameH;
            s->width = frameW;
            s->height = frameH;
        }
    }

    sheet->texture = LoadTexture(path);
    sheet->loaded = (sheet->texture.id != 0);
    if (!sheet->loaded)
        fprintf(stderr, "[sprite] could not load: %s\n", path);
}

void DrawSprite(const SpriteSheet* sheet, unsigned frame, float x, float y)
{
    if (!sheet->loaded || frame >= sheet->count)
        return;
    const Sprite* s = &sheet->frames[frame];
    Rectangle src = { (float)s->srcX, (float)s->srcY, (float)s->srcW, (float)s->srcH };
    Rectangle dst = { x, y, (float)s->width, (float)s->height };
    DrawTexturePro(sheet->texture, src, dst, (Vector2) { 0, 0 }, 0.0f, WHITE);
}

void DrawSpriteEx(const SpriteSheet* sheet, unsigned frame,
    float x, float y, float angle, Color tint)
{
    if (!sheet->loaded || frame >= sheet->count)
        return;
    const Sprite* s = &sheet->frames[frame];
    float hw = (float)s->width / 2.0f;
    float hh = (float)s->height / 2.0f;
    Rectangle src = { (float)s->srcX, (float)s->srcY, (float)s->srcW, (float)s->srcH };
    Rectangle dst = { x + hw, y + hh, (float)s->width, (float)s->height };
    Vector2 origin = { hw, hh };
    DrawTexturePro(sheet->texture, src, dst, origin, angle, tint);
}

void DrawSpriteFallback(const SpriteSheet* sheet, unsigned frame,
    float x, float y, Color color)
{
    if (!sheet->frames || frame >= sheet->count)
        return;
    const Sprite* s = &sheet->frames[frame];
    DrawRectangle((int)x, (int)y, (int)s->width, (int)s->height, color);
    DrawRectangleLines((int)x, (int)y, (int)s->width, (int)s->height, BLACK);
}

void ResizeSprite(SpriteSheet* sheet, unsigned newW, unsigned newH)
{
    for (unsigned i = 0; i < sheet->count; i++) {
        if (newW > 0)
            sheet->frames[i].width = newW;
        if (newH > 0)
            sheet->frames[i].height = newH;
    }
}

void UnloadSheet(SpriteSheet* sheet)
{
    if (sheet->frames) {
        free(sheet->frames);
        sheet->frames = NULL;
    }
    sheet->count = 0;
    if (sheet->loaded) {
        UnloadTexture(sheet->texture);
        sheet->loaded = false;
    }
}

// ---------------------------------------------------------------------------
// Meteor / bullet helpers
// ---------------------------------------------------------------------------

static void SpawnTarget(Obstacle* m, int screenW, int screenH)
{
    (void)screenH;
    m->pos.x = (float)GetRandomValue(0, screenW - TARGET_DISPLAY_W);
    m->pos.y = (float)GetRandomValue(-TARGET_DISPLAY_H * 4, -TARGET_DISPLAY_H);
    m->angle = (float)GetRandomValue(0, 359);
    m->rotSpeed = (float)GetRandomValue(1, 3);
    m->speed = (float)GetRandomValue(2, 5);
    m->active = true;
}

static void SpawnDistant(Obstacle* m, int screenW, int screenH)
{
    (void)screenH;
    m->pos.x = (float)GetRandomValue(0, screenW - DISTANT_DISPLAY_W);
    m->pos.y = (float)GetRandomValue(-DISTANT_DISPLAY_H * 6, -DISTANT_DISPLAY_H);
    m->angle = (float)GetRandomValue(0, 359);
    m->rotSpeed = (float)GetRandomValue(0, 1); // slower rotation
    m->speed = (float)GetRandomValue(1, 3); // slower movement
    m->active = true;
}

static void ResetBullet(Bullet* b)
{
    b->state = BULLET_IDLE;
    b->pos = (Vector2) { 0, 0 };
    b->explodeTimer = 0;
}
