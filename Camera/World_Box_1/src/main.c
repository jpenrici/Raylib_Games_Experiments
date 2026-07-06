/* =============================================================================
 *  CUBE WORLD - DUAL CAMERA STUDY (Raylib / C)
 * =============================================================================
 *
 *  GOAL
 *  ----
 *  A single colored cube represents a "world box". Six faces, six colors,
 *  used purely as a visual reference so you can tell orientation at a glance.
 *
 *  Two independent cameras observe the same world:
 *
 *   CAMERA 1 - "Internal / look-around" camera
 *      Fixed at the origin (0,0,0), the exact center of the cube.
 *      It never translates - it only rotates in place (yaw/pitch), like
 *      turning your head while standing still. Because raylib does not
 *      cull back faces by default, you can see the inside of the cube
 *      walls without any extra work.
 *
 *   CAMERA 2 - "External / orbital-free" camera
 *      Orbits around the cube from the outside, at a variable distance,
 *      always looking at the cube's center. Mouse wheel zooms in/out.
 *
 *  Both cameras are rendered every frame into their own RenderTexture2D.
 *  One is shown full-screen ("main" view), the other is composited as a
 *  small inset in the corner ("Picture-in-Picture", PiP). Press 1 / 2 or
 *  TAB to decide which camera is "main" (and therefore which one receives
 *  keyboard/mouse control).
 *
 *  COORDINATE CONVENTION
 *  ----------------------
 *  This project uses raylib's native convention: Y-UP.
 *      X (red)   -> left/right
 *      Y (green) -> up/down
 *      Z (blue)  -> forward/backward
 *  The ground/reference plane is therefore the XZ plane.
 *
 *  ARCHITECTURE (why the code is split like this)
 *  -----------------------------------------------
 *  main() only orchestrates: init, loop (input -> update -> render -> draw),
 *  cleanup. No heavy logic lives directly in the loop body. Responsibilities:
 *
 *      GenMeshColoredCube()      -> builds the 6-colored-face cube mesh
 *      LoadWorldCubeModel()      -> wraps mesh into a drawable Model
 *      InitInternalCamera() / UpdateInternalCamera()  -> Camera 1 logic
 *      InitExternalCamera() / UpdateExternalCamera()  -> Camera 2 logic
 *      DrawWorldAxes()           -> small RGB axis gizmo at the origin
 *      DrawSceneContents()       -> grid + axes + cube (shared by both views)
 *      main()'s render passes    -> render each camera into its own RenderTexture2D
 *      main()'s composite step   -> draws main view + PiP inset + HUD text
 *
 * ========================================================================== */

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#include <math.h>
#include <stdlib.h>

/* -----------------------------------------------------------------------
 *  CONFIGURATION CONSTANTS
 * ----------------------------------------------------------------------- */

#define SCREEN_WIDTH 1280
#define SCREEN_HEIGHT 720

#define PIP_WIDTH 360 // Picture-in-picture inset width  (px)
#define PIP_HEIGHT 240 // Picture-in-picture inset height (px)
#define PIP_MARGIN 20 // Margin from screen edges
#define PIP_BORDER 3 // Border thickness around the inset

#define CUBE_HALF_SIZE 6.0f // Cube spans [-6,6] on each axis (12 units)
#define GRID_SLICES 12
#define GRID_SPACING 1.0f
#define AXIS_LENGTH 8.0f

#define MOUSE_LOOK_SENSITIVITY 0.0035f // radians per pixel of mouse delta
#define KEY_LOOK_SPEED 2.0f // radians per second (arrow keys)
#define PITCH_LIMIT (89.0f * DEG2RAD)

#define ORBIT_DISTANCE_MIN (CUBE_HALF_SIZE * 1.8f)
#define ORBIT_DISTANCE_MAX 60.0f
#define ORBIT_ZOOM_SPEED 2.0f // units per mouse-wheel notch

/* -----------------------------------------------------------------------
 *  DATA STRUCTURES
 * ----------------------------------------------------------------------- */

typedef struct {
    Camera3D camera;
    float yaw; // radians, rotation around the Y (up) axis
    float pitch; // radians, rotation around the local right axis
} LookaroundCamera; // Camera 1: fixed position, rotates in place

typedef struct {
    Camera3D camera;
    Vector3 target; // point being orbited (cube center)
    float yaw;
    float pitch;
    float distance;
} OrbitalCamera; // Camera 2: orbits target at a variable distance

typedef enum {
    ACTIVE_CAMERA_INTERNAL = 1,
    ACTIVE_CAMERA_EXTERNAL = 2
} ActiveCamera;

/* =========================================================================
 *  WORLD MESH: a cube with a different flat color per face
 * ========================================================================= */

/* Builds a raw Mesh with 24 vertices (4 unique vertices per face - not
 * shared between faces) so each face can have its own flat color and
 * correct outward-facing normal. Indices define 2 triangles per face.
 *
 * Face/axis/color pairing is intentional: it mirrors the axis gizmo colors
 * (X=red family, Y=green family, Z=blue family) so the same color logic
 * you learn from the axes helps you read the cube faces too.
 *
 *      +X face -> RED       -X face -> ORANGE
 *      +Y face -> GREEN     -Y face -> YELLOW
 *      +Z face -> BLUE      -Z face -> PURPLE
 */
static Mesh GenMeshColoredCube(float half)
{
    const int FACE_COUNT = 6;
    const int VERTS_PER_FACE = 4;
    const int TOTAL_VERTS = FACE_COUNT * VERTS_PER_FACE; // 24
    const int TOTAL_TRIS = FACE_COUNT * 2; // 12
    const int TOTAL_INDICES = TOTAL_TRIS * 3; // 36

    // Each face: 4 corner positions (already in CCW order as seen from
    // OUTSIDE the cube), one outward normal, one color.
    typedef struct {
        Vector3 corners[4];
        Vector3 normal;
        Color color;
    } FaceDef;

    FaceDef faces[6] = {
        // +X face (right)
        { .corners = { { half, -half, -half }, { half, half, -half }, { half, half, half }, { half, -half, half } },
            .normal = { 1, 0, 0 },
            .color = RED },
        // -X face (left)
        { .corners = { { -half, -half, half }, { -half, half, half }, { -half, half, -half }, { -half, -half, -half } },
            .normal = { -1, 0, 0 },
            .color = ORANGE },
        // +Y face (top)
        { .corners = { { -half, half, -half }, { -half, half, half }, { half, half, half }, { half, half, -half } },
            .normal = { 0, 1, 0 },
            .color = GREEN },
        // -Y face (bottom)
        { .corners = { { -half, -half, half }, { -half, -half, -half }, { half, -half, -half }, { half, -half, half } },
            .normal = { 0, -1, 0 },
            .color = YELLOW },
        // +Z face (front)
        { .corners = { { -half, -half, half }, { half, -half, half }, { half, half, half }, { -half, half, half } },
            .normal = { 0, 0, 1 },
            .color = BLUE },
        // -Z face (back)
        { .corners = { { half, -half, -half }, { -half, -half, -half }, { -half, half, -half }, { half, half, -half } },
            .normal = { 0, 0, -1 },
            .color = PURPLE },
    };

    Mesh mesh = { 0 };
    mesh.vertexCount = TOTAL_VERTS;
    mesh.triangleCount = TOTAL_TRIS;

    mesh.vertices = (float*)MemAlloc(TOTAL_VERTS * 3 * sizeof(float));
    mesh.normals = (float*)MemAlloc(TOTAL_VERTS * 3 * sizeof(float));
    mesh.texcoords = (float*)MemAlloc(TOTAL_VERTS * 2 * sizeof(float)); // unused, but required by default shader attribute layout
    mesh.colors = (unsigned char*)MemAlloc(TOTAL_VERTS * 4 * sizeof(unsigned char));
    mesh.indices = (unsigned short*)MemAlloc(TOTAL_INDICES * sizeof(unsigned short));

    for (int f = 0; f < FACE_COUNT; f++) {
        for (int v = 0; v < VERTS_PER_FACE; v++) {
            int vi = f * VERTS_PER_FACE + v;

            mesh.vertices[vi * 3 + 0] = faces[f].corners[v].x;
            mesh.vertices[vi * 3 + 1] = faces[f].corners[v].y;
            mesh.vertices[vi * 3 + 2] = faces[f].corners[v].z;

            mesh.normals[vi * 3 + 0] = faces[f].normal.x;
            mesh.normals[vi * 3 + 1] = faces[f].normal.y;
            mesh.normals[vi * 3 + 2] = faces[f].normal.z;

            mesh.texcoords[vi * 2 + 0] = 0.0f;
            mesh.texcoords[vi * 2 + 1] = 0.0f;

            mesh.colors[vi * 4 + 0] = faces[f].color.r;
            mesh.colors[vi * 4 + 1] = faces[f].color.g;
            mesh.colors[vi * 4 + 2] = faces[f].color.b;
            mesh.colors[vi * 4 + 3] = faces[f].color.a;
        }

        // Two triangles per face: (0,1,2) and (0,2,3), using the face's
        // own local vertex order (already CCW as seen from outside).
        int base = f * VERTS_PER_FACE;
        int ii = f * 6;
        mesh.indices[ii + 0] = base + 0;
        mesh.indices[ii + 1] = base + 1;
        mesh.indices[ii + 2] = base + 2;
        mesh.indices[ii + 3] = base + 0;
        mesh.indices[ii + 4] = base + 2;
        mesh.indices[ii + 5] = base + 3;
    }

    UploadMesh(&mesh, false); // false = static mesh (not updated every frame)
    return mesh;
}

/* Wraps the colored cube mesh into a Model with the default raylib
 * material. The default material/shader multiplies vertex color by
 * material tint, which is exactly what we want (flat colored faces,
 * no external textures or lighting setup needed). */
static Model LoadWorldCubeModel(float half)
{
    Mesh mesh = GenMeshColoredCube(half);
    Model model = LoadModelFromMesh(mesh);
    return model;
}

/* =========================================================================
 *  CAMERA 1: INTERNAL LOOK-AROUND CAMERA
 * ========================================================================= */

static void InitInternalCamera(LookaroundCamera* cam)
{
    cam->yaw = 0.0f; // looking toward +Z initially
    cam->pitch = 0.0f;

    cam->camera.position = (Vector3) { 0.0f, 0.0f, 0.0f }; // always the origin
    cam->camera.up = (Vector3) { 0.0f, 1.0f, 0.0f };
    cam->camera.fovy = 70.0f;
    cam->camera.projection = CAMERA_PERSPECTIVE;
    cam->camera.target = (Vector3) { 0.0f, 0.0f, 1.0f }; // recomputed below anyway
}

/* Rebuilds camera.target from yaw/pitch. Since position never changes for
 * this camera, "moving the view" is purely a matter of recomputing the
 * forward direction vector each frame. */
static void UpdateInternalCamera(LookaroundCamera* cam, bool isControlled)
{
    if (isControlled) {
        // Mouse look: hold the RIGHT mouse button to rotate, like a
        // typical 3D editor "look" control. Cursor is hidden while held.
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
            if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))
                DisableCursor();
            Vector2 delta = GetMouseDelta();
            cam->yaw -= delta.x * MOUSE_LOOK_SENSITIVITY;
            cam->pitch -= delta.y * MOUSE_LOOK_SENSITIVITY;
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_RIGHT))
            EnableCursor();

        // Keyboard alternative: arrow keys always work, no button needed.
        float dt = GetFrameTime();
        if (IsKeyDown(KEY_LEFT))
            cam->yaw += KEY_LOOK_SPEED * dt;
        if (IsKeyDown(KEY_RIGHT))
            cam->yaw -= KEY_LOOK_SPEED * dt;
        if (IsKeyDown(KEY_UP))
            cam->pitch += KEY_LOOK_SPEED * dt;
        if (IsKeyDown(KEY_DOWN))
            cam->pitch -= KEY_LOOK_SPEED * dt;

        // Clamp pitch so the camera cannot flip past straight up/down
        // (classic gimbal-lock-looking artifact avoided this way).
        if (cam->pitch > PITCH_LIMIT)
            cam->pitch = PITCH_LIMIT;
        if (cam->pitch < -PITCH_LIMIT)
            cam->pitch = -PITCH_LIMIT;
    }

    // Spherical coordinates -> forward direction vector (Y-up convention).
    Vector3 forward = {
        cosf(cam->pitch) * sinf(cam->yaw),
        sinf(cam->pitch),
        cosf(cam->pitch) * cosf(cam->yaw)
    };
    cam->camera.target = Vector3Add(cam->camera.position, forward);
}

/* =========================================================================
 *  CAMERA 2: EXTERNAL ORBITAL CAMERA
 * ========================================================================= */

static void InitExternalCamera(OrbitalCamera* cam)
{
    cam->target = (Vector3) { 0.0f, 0.0f, 0.0f }; // always orbits the cube center
    cam->yaw = 45.0f * DEG2RAD;
    cam->pitch = 25.0f * DEG2RAD;
    cam->distance = CUBE_HALF_SIZE * 3.0f;

    cam->camera.up = (Vector3) { 0.0f, 1.0f, 0.0f };
    cam->camera.fovy = 45.0f;
    cam->camera.projection = CAMERA_PERSPECTIVE;
}

static void UpdateExternalCamera(OrbitalCamera* cam, bool isControlled)
{
    if (isControlled) {
        // Same "hold right mouse button to rotate" convention as Camera 1,
        // so switching between cameras feels consistent.
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
            if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))
                DisableCursor();
            Vector2 delta = GetMouseDelta();
            cam->yaw -= delta.x * MOUSE_LOOK_SENSITIVITY;
            cam->pitch += delta.y * MOUSE_LOOK_SENSITIVITY;
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_RIGHT))
            EnableCursor();

        float dt = GetFrameTime();
        if (IsKeyDown(KEY_LEFT))
            cam->yaw += KEY_LOOK_SPEED * dt;
        if (IsKeyDown(KEY_RIGHT))
            cam->yaw -= KEY_LOOK_SPEED * dt;
        if (IsKeyDown(KEY_UP))
            cam->pitch += KEY_LOOK_SPEED * dt;
        if (IsKeyDown(KEY_DOWN))
            cam->pitch -= KEY_LOOK_SPEED * dt;

        if (cam->pitch > PITCH_LIMIT)
            cam->pitch = PITCH_LIMIT;
        if (cam->pitch < -PITCH_LIMIT)
            cam->pitch = -PITCH_LIMIT;

        // Mouse wheel zoom (distance from target).
        float wheel = GetMouseWheelMove();
        cam->distance -= wheel * ORBIT_ZOOM_SPEED;
        if (cam->distance < ORBIT_DISTANCE_MIN)
            cam->distance = ORBIT_DISTANCE_MIN;
        if (cam->distance > ORBIT_DISTANCE_MAX)
            cam->distance = ORBIT_DISTANCE_MAX;
    }

    // Spherical coordinates around the target point.
    Vector3 offset = {
        cam->distance * cosf(cam->pitch) * sinf(cam->yaw),
        cam->distance * sinf(cam->pitch),
        cam->distance * cosf(cam->pitch) * cosf(cam->yaw)
    };
    cam->camera.position = Vector3Add(cam->target, offset);
    cam->camera.target = cam->target;
}

/* =========================================================================
 *  SHARED SCENE DRAWING (grid, axis gizmo, world cube)
 * ========================================================================= */

/* Small RGB axis gizmo at the origin. Colors deliberately match the cube
 * face colors along the positive axes (+X red, +Y green, +Z blue), so the
 * gizmo doubles as a legend for reading the cube's faces. */
static void DrawWorldAxes(float length)
{
    DrawLine3D((Vector3) { 0, 0, 0 }, (Vector3) { length, 0, 0 }, RED);
    DrawLine3D((Vector3) { 0, 0, 0 }, (Vector3) { 0, length, 0 }, GREEN);
    DrawLine3D((Vector3) { 0, 0, 0 }, (Vector3) { 0, 0, length }, BLUE);

    // Small tip markers make direction easier to spot from a distance.
    DrawSphere((Vector3) { length, 0, 0 }, 0.15f, RED);
    DrawSphere((Vector3) { 0, length, 0 }, 0.15f, GREEN);
    DrawSphere((Vector3) { 0, 0, length }, 0.15f, BLUE);
}

/* Everything that both cameras should see. Must be called between
 * BeginMode3D()/EndMode3D(). Kept as one function so both render passes
 * (main view and PiP view) always stay visually in sync. */
static void DrawSceneContents(Model worldModel)
{
    DrawGrid(GRID_SLICES, GRID_SPACING); // reference plane, drawn on XZ (Y-up)
    DrawWorldAxes(AXIS_LENGTH);

    // The internal camera sits INSIDE this cube looking at its inner
    // walls. Raylib does not enable backface culling by default, so both
    // the inside and outside of these faces render correctly with zero
    // extra work. We disable it explicitly here anyway, for clarity and
    // so the behavior does not depend on an implicit engine default.
    rlDisableBackfaceCulling();
    DrawModel(worldModel, (Vector3) { 0, 0, 0 }, 1.0f, WHITE);
}

/* =========================================================================
 *  MAIN
 *
 *  Note on the render-to-texture step below: doing this for both cameras
 *  every frame is what makes Picture-in-Picture possible - we end up with
 *  two independent "photographs" of the same scene to composite afterwards.
 * ========================================================================= */

int main(void)
{
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Cube World - Dual Camera Study (Raylib)");
    SetTargetFPS(60);

    Model worldModel = LoadWorldCubeModel(CUBE_HALF_SIZE);

    LookaroundCamera internalCam;
    InitInternalCamera(&internalCam);

    OrbitalCamera externalCam;
    InitExternalCamera(&externalCam);

    // Full-resolution target for whichever camera is currently "main",
    // and a lower-resolution target for the PiP inset (no need to render
    // the inset at full screen resolution - it is drawn small anyway).
    RenderTexture2D mainRT = LoadRenderTexture(SCREEN_WIDTH, SCREEN_HEIGHT);
    RenderTexture2D pipRT = LoadRenderTexture(PIP_WIDTH, PIP_HEIGHT);

    ActiveCamera active = ACTIVE_CAMERA_INTERNAL;

    Rectangle pipRect = {
        SCREEN_WIDTH - PIP_WIDTH - PIP_MARGIN,
        PIP_MARGIN,
        PIP_WIDTH,
        PIP_HEIGHT
    };

    while (!WindowShouldClose()) {
        /* ---------------- INPUT: choose which camera is "main" ---------------- */
        if (IsKeyPressed(KEY_ONE))
            active = ACTIVE_CAMERA_INTERNAL;
        if (IsKeyPressed(KEY_TWO))
            active = ACTIVE_CAMERA_EXTERNAL;
        if (IsKeyPressed(KEY_TAB)) {
            active = (active == ACTIVE_CAMERA_INTERNAL)
                ? ACTIVE_CAMERA_EXTERNAL
                : ACTIVE_CAMERA_INTERNAL;
        }

        /* ---------------- UPDATE: only the "main" camera receives control ------ */
        UpdateInternalCamera(&internalCam, active == ACTIVE_CAMERA_INTERNAL);
        UpdateExternalCamera(&externalCam, active == ACTIVE_CAMERA_EXTERNAL);

        Camera3D mainCameraView = (active == ACTIVE_CAMERA_INTERNAL)
            ? internalCam.camera
            : externalCam.camera;
        Camera3D pipCameraView = (active == ACTIVE_CAMERA_INTERNAL)
            ? externalCam.camera
            : internalCam.camera;

        /* ---------------- RENDER PASS 1: main camera into mainRT --------------- */
        BeginTextureMode(mainRT);
        ClearBackground((Color) { 25, 25, 35, 255 });
        BeginMode3D(mainCameraView);
        DrawSceneContents(worldModel);
        EndMode3D();
        EndTextureMode();

        /* ---------------- RENDER PASS 2: secondary camera into pipRT ----------- */
        BeginTextureMode(pipRT);
        ClearBackground((Color) { 15, 15, 20, 255 });
        BeginMode3D(pipCameraView);
        DrawSceneContents(worldModel);
        EndMode3D();
        EndTextureMode();

        /* ---------------- COMPOSITE: draw both textures to the screen --------- */
        BeginDrawing();
        ClearBackground(BLACK);

        // RenderTexture2D contents are stored bottom-up in OpenGL, so
        // the source rectangle's height must be negative to flip them
        // right-side up when drawn. This trips up almost everyone the
        // first time they use render textures in raylib.
        DrawTextureRec(mainRT.texture,
            (Rectangle) { 0, 0, (float)mainRT.texture.width, -(float)mainRT.texture.height },
            (Vector2) { 0, 0 }, WHITE);

        // Border behind the inset so it reads clearly against the main view.
        DrawRectangle(pipRect.x - PIP_BORDER, pipRect.y - PIP_BORDER,
            pipRect.width + PIP_BORDER * 2, pipRect.height + PIP_BORDER * 2,
            (active == ACTIVE_CAMERA_INTERNAL) ? SKYBLUE : GOLD);

        DrawTextureRec(pipRT.texture,
            (Rectangle) { 0, 0, (float)pipRT.texture.width, -(float)pipRT.texture.height },
            (Vector2) { pipRect.x, pipRect.y }, WHITE);

        /* ---------------- HUD ---------------- */
        const char* mainLabel = (active == ACTIVE_CAMERA_INTERNAL)
            ? "MAIN: Camera 1 (internal / look-around)"
            : "MAIN: Camera 2 (external / orbital)";
        const char* pipLabel = (active == ACTIVE_CAMERA_INTERNAL)
            ? "PiP: Camera 2 (external)"
            : "PiP: Camera 1 (internal)";

        DrawText(mainLabel, 20, 20, 22, RAYWHITE);
        DrawText(pipLabel, (int)pipRect.x, (int)(pipRect.y + pipRect.height + 10), 16, LIGHTGRAY);

        DrawText("1 / 2: select main camera    TAB: swap    Hold RIGHT MOUSE: look/orbit    Arrows: look/orbit    Wheel: zoom (Cam 2)",
            20, SCREEN_HEIGHT - 30, 18, GRAY);

        DrawFPS(SCREEN_WIDTH - 90, SCREEN_HEIGHT - 30);
        EndDrawing();
    }

    UnloadRenderTexture(mainRT);
    UnloadRenderTexture(pipRT);
    UnloadModel(worldModel);
    CloseWindow();

    return 0;
}
