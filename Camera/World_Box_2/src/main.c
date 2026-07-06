/* =============================================================================
 *  CUBE WORLD - DUAL CAMERA STUDY (Raylib / C)
 * =============================================================================
 *
 *  GOAL
 *  ----
 *  A single textured cube represents a "world box". Six faces, each sampling
 *  a different rectangle of a shared PNG atlas, used purely as a visual
 *  reference so you can tell orientation at a glance.
 *
 *  Two independent cameras observe the same world:
 *
 *   CAMERA 1 - "Internal / look-around" camera
 *      Fixed at the origin (0,0,0), the exact center of the cube.
 *      It never translates - it only rotates in place (yaw/pitch), like
 *      turning your head while standing still. Because raylib does not
 *      cull back faces by default, you can see the *inside* of the cube
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
 *  The ground/reference plane is therefore the XZ plane (DrawGrid already
 *  draws on XZ by default), not XY.
 *
 *  TEXTURING (PNG ATLAS)
 *  ----------------------
 *  Instead of one solid color per face, each face samples a rectangle out
 *  of a single texture atlas (one image split into a 3x2 grid of cells):
 *
 *          +-------+-------+-------+
 *          |  +X   |  -X   |  +Y   |   row 0
 *          +-------+-------+-------+
 *          |  -Y   |  +Z   |  -Z   |   row 1
 *          +-------+-------+-------+
 *
 *  This demo GENERATES that atlas procedurally at startup (GenPlaceholderAtlasImage)
 *  so it runs immediately with zero external assets. To use your own PNG
 *  atlas instead, just replace the atlas-loading lines in main() with:
 *
 *      Image atlasImg = LoadImage("assets/cube_atlas.png");
 *      Texture2D atlasTexture = LoadTextureFromImage(atlasImg);
 *      UnloadImage(atlasImg);
 *
 *  ...as long as your PNG follows the same 3x2 layout above, nothing else
 *  in the mesh/UV code needs to change - that is the whole point of an atlas:
 *  one texture, one material, one draw call, regardless of how many "logical"
 *  images make it up.
 *
 *  ARCHITECTURE (why the code is split like this)
 *  -----------------------------------------------
 *  main() only orchestrates: init, loop (input -> update -> render -> draw),
 *  cleanup. No heavy logic lives directly in the loop body. Responsibilities:
 *
 *      GenPlaceholderAtlasImage()-> builds the placeholder 3x2 atlas image
 *      GenMeshTexturedCube()     -> builds the cube mesh with per-face UVs
 *      LoadWorldCubeModel()      -> wraps mesh + atlas texture into a Model
 *      InitInternalCamera() / UpdateInternalCamera()  -> Camera 1 logic
 *      InitExternalCamera() / UpdateExternalCamera()  -> Camera 2 logic
 *      DrawWorldAxes()           -> small RGB axis gizmo at the origin
 *      DrawSceneContents()       -> grid + axes + cube (shared by both views)
 *      main()'s render passes    -> render each camera into its own RenderTexture2D
 *      main()'s composite step   -> draws main view + PiP inset + HUD text
 *
 *  BUILD (Linux example)
 *  ----------------------
 *      gcc cube_world_cameras.c -o cube_world_cameras \
 *          -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
 *
 *  (On Windows/macOS, link against raylib per the usual raylib build
 *  instructions for your platform/IDE.)
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
 *
 *  Camera3D (raylib's own struct) stores position/target/up/fovy, but it
 *  has no notion of "yaw" or "pitch". We keep those angles ourselves and
 *  rebuild camera.position / camera.target from them every frame. This is
 *  the standard pattern for custom camera controllers in raylib.
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
 *  WORLD MESH: a cube whose faces sample a PNG texture atlas
 * ========================================================================= */

#define ATLAS_COLS 3
#define ATLAS_ROWS 2

/* Which atlas cell (col,row) each face samples from. Index order below
 * (+X,-X,+Y,-Y,+Z,-Z) matches the face order used in GenMeshTexturedCube(),
 * so the two arrays stay in lockstep. This is the ONLY place that encodes
 * the atlas layout - change it here if you rearrange the grid. */
typedef struct {
    int col, row;
} AtlasCell;
static const AtlasCell FACE_ATLAS_CELLS[6] = {
    { 0, 0 }, // +X
    { 1, 0 }, // -X
    { 2, 0 }, // +Y
    { 0, 1 }, // -Y
    { 1, 1 }, // +Z
    { 2, 1 }, // -Z
};

/* Generates a placeholder 3x2 atlas image entirely in memory (no PNG file
 * needed), so this demo runs out of the box. Each cell gets a solid color
 * (same palette as the previous version) plus a text label, and a dark
 * border INSET from the cell edge. That border is a standard texture-atlas
 * trick: it stops bilinear filtering from "bleeding" a neighboring cell's
 * color into this one at the edges. */
static Image GenPlaceholderAtlasImage(int cellSize)
{
    typedef struct {
        const char* label;
        Color color;
    } CellInfo;
    // Order must match FACE_ATLAS_CELLS layout: row0 = +X,-X,+Y ; row1 = -Y,+Z,-Z
    CellInfo cells[ATLAS_COLS * ATLAS_ROWS] = {
        { "+X", RED },
        { "-X", ORANGE },
        { "+Y", GREEN },
        { "-Y", YELLOW },
        { "+Z", BLUE },
        { "-Z", PURPLE },
    };

    int atlasW = cellSize * ATLAS_COLS;
    int atlasH = cellSize * ATLAS_ROWS;
    Image atlas = GenImageColor(atlasW, atlasH, BLACK);

    int border = cellSize / 16;
    int fontSize = cellSize / 4;

    for (int row = 0; row < ATLAS_ROWS; row++) {
        for (int col = 0; col < ATLAS_COLS; col++) {
            int idx = row * ATLAS_COLS + col;
            int x = col * cellSize;
            int y = row * cellSize;

            // Dark border strip, then the face color inset by `border`
            // pixels on every side (the anti-bleeding margin mentioned above).
            ImageDrawRectangle(&atlas, x, y, cellSize, cellSize, BLACK);
            ImageDrawRectangle(&atlas, x + border, y + border,
                cellSize - 2 * border, cellSize - 2 * border,
                cells[idx].color);

            int textWidth = MeasureText(cells[idx].label, fontSize);
            ImageDrawText(&atlas, cells[idx].label,
                x + (cellSize - textWidth) / 2,
                y + (cellSize - fontSize) / 2,
                fontSize, BLACK);
        }
    }
    return atlas;
}

/* Builds a raw Mesh with 24 vertices (4 unique vertices per face - not
 * shared between faces) so each face can carry its own UV rectangle and
 * correct outward-facing normal. Indices define 2 triangles per face.
 *
 * UV mapping: every face uses the same relative corner pattern - vertex 0
 * is the rectangle's bottom-left in UV space, going CCW to top-left,
 * top-right, bottom-right - just scaled/offset into that face's own cell
 * of the atlas via FACE_ATLAS_CELLS. Swapping the atlas image later (e.g.
 * for hand-painted PNGs) requires no changes here at all. */
static Mesh GenMeshTexturedCube(float half)
{
    const int FACE_COUNT = 6;
    const int VERTS_PER_FACE = 4;
    const int TOTAL_VERTS = FACE_COUNT * VERTS_PER_FACE; // 24
    const int TOTAL_TRIS = FACE_COUNT * 2; // 12
    const int TOTAL_INDICES = TOTAL_TRIS * 3; // 36

    // Each face: 4 corner positions (already in CCW order as seen from
    // OUTSIDE the cube) and one outward normal. Order must match
    // FACE_ATLAS_CELLS above: +X,-X,+Y,-Y,+Z,-Z.
    typedef struct {
        Vector3 corners[4];
        Vector3 normal;
    } FaceDef;

    FaceDef faces[6] = {
        // +X face (right)
        { .corners = { { half, -half, -half }, { half, half, -half }, { half, half, half }, { half, -half, half } },
            .normal = { 1, 0, 0 } },
        // -X face (left)
        { .corners = { { -half, -half, half }, { -half, half, half }, { -half, half, -half }, { -half, -half, -half } },
            .normal = { -1, 0, 0 } },
        // +Y face (top)
        { .corners = { { -half, half, -half }, { -half, half, half }, { half, half, half }, { half, half, -half } },
            .normal = { 0, 1, 0 } },
        // -Y face (bottom)
        { .corners = { { -half, -half, half }, { -half, -half, -half }, { half, -half, -half }, { half, -half, half } },
            .normal = { 0, -1, 0 } },
        // +Z face (front)
        { .corners = { { -half, -half, half }, { half, -half, half }, { half, half, half }, { -half, half, half } },
            .normal = { 0, 0, 1 } },
        // -Z face (back)
        { .corners = { { half, -half, -half }, { -half, -half, -half }, { -half, half, -half }, { half, half, -half } },
            .normal = { 0, 0, -1 } },
    };

    // Relative UV corner pattern shared by every face (matches the corner
    // order used above: v0,v1,v2,v3). Raylib image-space V grows downward,
    // so v0=(0,1) is visually bottom-left, v1=(0,0) top-left, and so on.
    const Vector2 cornerUV[4] = { { 0, 1 }, { 0, 0 }, { 1, 0 }, { 1, 1 } };

    Mesh mesh = { 0 };
    mesh.vertexCount = TOTAL_VERTS;
    mesh.triangleCount = TOTAL_TRIS;

    mesh.vertices = (float*)MemAlloc(TOTAL_VERTS * 3 * sizeof(float));
    mesh.normals = (float*)MemAlloc(TOTAL_VERTS * 3 * sizeof(float));
    mesh.texcoords = (float*)MemAlloc(TOTAL_VERTS * 2 * sizeof(float));
    mesh.indices = (unsigned short*)MemAlloc(TOTAL_INDICES * sizeof(unsigned short));
    // Note: mesh.colors is intentionally left NULL. Raylib's mesh draw path
    // falls back to a default white vertex color when this attribute is
    // absent, so the atlas texture shows through with no tint at all.

    for (int f = 0; f < FACE_COUNT; f++) {
        float u0 = (float)FACE_ATLAS_CELLS[f].col / ATLAS_COLS;
        float u1 = (float)(FACE_ATLAS_CELLS[f].col + 1) / ATLAS_COLS;
        float v0 = (float)FACE_ATLAS_CELLS[f].row / ATLAS_ROWS;
        float v1 = (float)(FACE_ATLAS_CELLS[f].row + 1) / ATLAS_ROWS;

        for (int v = 0; v < VERTS_PER_FACE; v++) {
            int vi = f * VERTS_PER_FACE + v;

            mesh.vertices[vi * 3 + 0] = faces[f].corners[v].x;
            mesh.vertices[vi * 3 + 1] = faces[f].corners[v].y;
            mesh.vertices[vi * 3 + 2] = faces[f].corners[v].z;

            mesh.normals[vi * 3 + 0] = faces[f].normal.x;
            mesh.normals[vi * 3 + 1] = faces[f].normal.y;
            mesh.normals[vi * 3 + 2] = faces[f].normal.z;

            // Map this face's local UV corner (0..1 range) into its
            // rectangle within the shared atlas.
            mesh.texcoords[vi * 2 + 0] = u0 + cornerUV[v].x * (u1 - u0);
            mesh.texcoords[vi * 2 + 1] = v0 + cornerUV[v].y * (v1 - v0);
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

/* Wraps the textured cube mesh into a Model and assigns the atlas texture
 * as its diffuse map. One mesh + one material + one texture = one single
 * DrawModel() call still draws all six differently-textured faces. */
static Model LoadWorldCubeModel(float half, Texture2D atlasTexture)
{
    Mesh mesh = GenMeshTexturedCube(half);
    Model model = LoadModelFromMesh(mesh);
    model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = atlasTexture;
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

    // --- Atlas texture setup ---------------------------------------------
    // Generated procedurally here so the demo needs zero external files.
    // To use real PNGs instead, replace these three lines with:
    //     Image atlasImg = LoadImage("assets/cube_atlas.png");
    //     Texture2D atlasTexture = LoadTextureFromImage(atlasImg);
    //     UnloadImage(atlasImg);
    // as long as the PNG follows the 3x2 layout documented at the top of
    // this file, GenMeshTexturedCube()'s UVs keep working unchanged.
    Image atlasImg = GenPlaceholderAtlasImage(256); // 256px per cell -> 768x512 atlas
    Texture2D atlasTexture = LoadTextureFromImage(atlasImg);
    UnloadImage(atlasImg); // pixel data now lives on the GPU, CPU copy no longer needed

    // Point filtering keeps the placeholder's text/borders crisp; switch to
    // TEXTURE_FILTER_BILINEAR if you swap in smoother, painted textures.
    SetTextureFilter(atlasTexture, TEXTURE_FILTER_POINT);

    Model worldModel = LoadWorldCubeModel(CUBE_HALF_SIZE, atlasTexture);

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
    // UnloadModel() also frees every texture referenced by its materials,
    // so atlasTexture gets released here too - do NOT call UnloadTexture()
    // on it separately, or you'll double-free the same GPU handle.
    UnloadModel(worldModel);
    CloseWindow();

    return 0;
}
