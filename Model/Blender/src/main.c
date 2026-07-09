#include "raylib.h"

#define SCREEN_TITLE "Blender - Raylib"
#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 600

#define TIMER_FPS 60 // target frames per second

int main(void)
{
    // Window
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_TITLE);
    SetTargetFPS(TIMER_FPS);

    // Camera
    Camera3D camera = {0};
    camera.position = (Vector3){0.0f, 10.f, 10.f};
    camera.target = (Vector3){0.0f, 0.0f, 0.0f};
    camera.up = (Vector3){0.0f, 1.0f, 0.0f};
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    // Model
    Model m = LoadModel("assets/tractor.glb");

    // Color
    Color blenderOrange = (Color){ 242, 116, 32, 255 };

    // Game Loop
    while (!WindowShouldClose()) {

        // Update
        UpdateCamera(&camera, CAMERA_ORBITAL);

        // Render
        BeginDrawing();
        {
            ClearBackground(DARKGRAY);

            BeginMode3D(camera);
            {
            DrawModel(m, (Vector3){0.0f, 0.0f, 0.0f}, 1.0f, WHITE);
            DrawModelWires(m, (Vector3){0.0f, 0.0f, 0.0f}, 1.0f , blenderOrange );
            DrawGrid(10, 1.0f);
            }
            EndMode3D();

            DrawText("Tractor", 10, 10, 20, GREEN);
        }
        EndDrawing();
    }

    // Exit
    UnloadModel(m);
    CloseWindow();

    return 0;
}
