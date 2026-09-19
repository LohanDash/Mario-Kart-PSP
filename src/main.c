#include <math.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspkernel.h>
#include <psppower.h>

#include "music.h"
#include "sfx.h"

PSP_MODULE_INFO("MKPSP", PSP_MODULE_USER, 0, 1);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

static unsigned int __attribute__((aligned(16))) display_list[262144];

typedef struct {
    unsigned int start, count;
    float min_x, max_x, min_z, max_z;
} CourseChunk;

typedef struct {
    float x, y, z;
} Vec3;
#include "pinball_routes.h"

typedef struct {
    Vec3 a, b, c, normal;
    unsigned short attributes;
    unsigned char type, variant, category, reserved;
    unsigned short padding;
} KclTriangle;

enum { KCL_GRID_SIZE = 16, KCL_GRID_CELLS = 256 };
typedef struct {
    float min_x, max_x, min_z, max_z, cell_width, cell_depth;
    unsigned int offsets[KCL_GRID_CELLS + 1];
    unsigned short *indices;
} KclGrid;

static KclTriangle *pinball_kcl = NULL;
static unsigned int pinball_kcl_count = 0;
static KclTriangle *mansion_kcl = NULL;
static unsigned int mansion_kcl_count = 0;
static KclGrid pinball_kcl_grid = {0};
static KclGrid mansion_kcl_grid = {0};
static KclTriangle *active_kcl = NULL;
static unsigned int active_kcl_count = 0;
static KclGrid *active_kcl_grid = NULL;
static int kcl_debug_enabled = 0;
static int kcl_touched_id = -1;
static unsigned int kcl_touched_type = 0;
static unsigned int kcl_touched_variant = 0;
static float kcl_touched_normal_x = 0.0f;
static float kcl_touched_normal_y = 1.0f;
static float kcl_touched_normal_z = 0.0f;
static float kcl_wall_hit_t = 1.0f;

typedef struct {
    unsigned int color;
    float x, y, z;
} Vertex;

typedef struct {
    float u, v;
    unsigned int color;
    float x, y, z;
} CourseVertex;

typedef struct {
    CourseVertex *vertices;
    unsigned int count;
    unsigned int texture_width;
    unsigned int texture_height;
    unsigned int *texture;
    int texture_format;
    int repeat_texture;
    int swizzled_texture;
    int has_alpha;
    CourseChunk *chunks;
    unsigned int chunk_count;
} CourseModel;

static CourseModel course_model = {0};
static CourseModel collision_model = {0};
enum { MAX_COURSE_MATERIALS = 28 };
static CourseModel course_batches[MAX_COURSE_MATERIALS] = {{0}};
static CourseModel pinball_collision = {0};
static CourseModel pinball_batches[MAX_COURSE_MATERIALS] = {{0}};
static CourseModel mansion_batches[MAX_COURSE_MATERIALS] = {{0}};
enum { MAPOBJ_BOUND, MAPOBJ_DRAM, MAPOBJ_FLIPPER, MAPOBJ_IRON_BALL, MAPOBJ_COUNT };
static CourseModel pinball_mapobjs[MAPOBJ_COUNT] = {{0}};
enum {
    MANSION_CHANDELIER, MANSION_MOVE_TREE, MANSION_PICTURE1,
    MANSION_PICTURE2, MANSION_TERESA, MANSION_MAPOBJ_COUNT
};
static CourseModel mansion_mapobjs[MANSION_MAPOBJ_COUNT] = {{0}};
static unsigned int *mansion_teresa_alt_texture = NULL;
static unsigned int *pinball_flipper_frames[4] = {0};
static unsigned int *pinball_bound_frames[3] = {0};
static unsigned int *pinball_flag_frames[2] = {0};
typedef struct {
    int model; float x, y, z, rotation_y, scale_x, scale_y, scale_z;
} MapObjInstance;
static const MapObjInstance pinball_objects[] = {
    {MAPOBJ_BOUND, -4.49609f,13.18666f,9.35352f,0,6,4.5f,6}, {MAPOBJ_BOUND,2.94141f,13.18666f,9.35352f,0,6,4.5f,6},
    {MAPOBJ_BOUND,-4.83984f,11.28839f,20.11914f,0,6,4.5f,6}, {MAPOBJ_BOUND,3.12891f,11.28839f,20.11914f,0,6,4.5f,6},
    {MAPOBJ_BOUND,-0.77734f,9.73175f,28.94727f,0,6,4.5f,6}, {MAPOBJ_BOUND,-5.30859f,8.93277f,33.47852f,0,6,4.5f,6},
    {MAPOBJ_BOUND,3.75391f,8.93277f,33.47852f,0,6,4.5f,6}, {MAPOBJ_BOUND,-0.69922f,8.93277f,33.47852f,0,6,6,6},
    {MAPOBJ_FLIPPER,-5.46484f,6.69922f,46.13477f,3.1415927f,1,1,1}, {MAPOBJ_FLIPPER,3.91016f,6.69922f,46.13477f,0,1,1,1},
    {MAPOBJ_BOUND,-0.77734f,8.13379f,38.00977f,0,6,4.5f,6}, {MAPOBJ_IRON_BALL,-14.56462f,22.66462f,0.54646f,0,1,1,1},
    {MAPOBJ_BOUND,10.55078f,8.49195f,35.97852f,0,10,5,10}, {MAPOBJ_BOUND,-12.10547f,8.49195f,35.97852f,0,10,5,10},
    {MAPOBJ_IRON_BALL,2.75403f,28.52011f,-32.62933f,0,1,1,1},
    {MAPOBJ_DRAM,22.81641f,6.76172f,49.80664f,0,1,1,1}, {MAPOBJ_DRAM,22.81641f,6.76172f,52.61914f,0,1,1,1},
    {MAPOBJ_DRAM,22.81641f,6.76172f,55.43164f,0,1,1,1}, {MAPOBJ_DRAM,22.81641f,6.76172f,59.33789f,0,1,1,1},
    {MAPOBJ_IRON_BALL,-14.12608f,22.66462f,0.54646f,0,1,1,1}
};
static const MapObjInstance mansion_objects[] = {
    {MANSION_CHANDELIER,-22.41344f,2.47266f,-28.04492f,0,1,1,1},
    /* Keep the graveyard sparse: two isolated walking trees instead of the
       original five-object clump concentrated in a four-metre patch. */
    {MANSION_MOVE_TREE,-36.20000f,2.51172f,15.80000f,0,1,1,1},
    {MANSION_MOVE_TREE,  5.70000f,2.51172f,39.00000f,0,1,1,1},
    {MANSION_PICTURE1,22.85256f,5.36320f,-55.07980f,0,1,1,1},
    {MANSION_PICTURE1,32.06556f,3.64820f,-37.32880f,3.1415927f,1,1,1},
    {MANSION_PICTURE1,15.66056f,3.60820f,-44.43180f,0,1,1,1},
    {MANSION_PICTURE1,18.71556f,9.66720f,-40.89680f,0,1.45f,1.45f,1.45f},
    {MANSION_PICTURE2,33.50456f,5.40320f,-52.52080f,-1.5707963f,1.06f,1.06f,1.06f},
    {MANSION_PICTURE2,19.98456f,3.62820f,-37.34780f,3.1415927f,1.16f,1.16f,1.16f},
    {MANSION_TERESA,-8.32344f,2.51172f,20.15020f,0,1,1,1},
    {MANSION_TERESA,-27.48644f,2.51172f,5.87220f,0,1,1,1},
    {MANSION_TERESA,-25.97644f,2.51172f,27.26520f,0,1,1,1},
    {MANSION_TERESA,6.90156f,2.51172f,37.19820f,0,1,1,1}
};
static CourseModel *active_batches = course_batches;
static CourseModel *active_collision = &collision_model;
static int active_material_count = 22;
static int active_course = 0;
static int course_hidden = 0;
static float active_start_x = 20.69335f, active_start_y = 2.03125f, active_start_z = -26.33985f;
static float active_start_heading = 1.5707963f;
static CourseModel road_model = {0};
static CourseModel kart_models[2] = {{0}};
static CourseModel wheel_models[2][4] = {{{0}}};
static CourseModel mario_model = {0};
static CourseModel mushroom_icons[3] = {{0}};

typedef struct {
    CourseVertex *vertices;
    unsigned int frame_count;
    unsigned int vertices_per_frame;
    unsigned int drive_frames;
    unsigned int texture_width, texture_height;
    unsigned int *texture;
} AnimatedModel;

static AnimatedModel mario_animation = {0};
static AnimatedModel mansion_animations[MANSION_MAPOBJ_COUNT] = {{0}};
static int mansion_ambient_cooldown = 0;

typedef struct {
    Vec3 position;
    float heading;
    float pitch;
    float roll;
    float speed;
    float drift_charge;
    float boost_frames;
    float boost_limit;
    float vertical_speed;
    float wheel_distance;
    float wheel_steering;
    int drifting;
    int drift_direction;
    int airborne;
    int r_was_down;
    int item_count;
    int l_was_down;
    int cannon_state;
    float cannon_time;
    Vec3 cannon_start;
    float camera_y;
    int hop_camera_lock;
    float hop_height;
    int ramp_grace_frames;
    int up_was_down;
    int inertia_frames;
    int secret_jump_frames;
    int off_map_frames;
    int rescue_frames;
    int rescue_total_frames;
    int has_safe_position;
    Vec3 rescue_start;
    Vec3 rescue_target;
    Vec3 last_safe_position;
    float last_safe_heading;
    float push_x;
    float push_z;
    int object_hit_cooldown;
    int pinball_exit_armed;
} Kart;

enum { PINBALL_BALL_COUNT = 3 };
typedef struct {
    Vec3 position;
    Vec3 velocity;
    float spin;
    unsigned int route_point;
    int sound_cooldown;
} PinballBall;

static PinballBall pinball_balls[PINBALL_BALL_COUNT] = {0};

static void kart_add_push(Kart *kart, float x, float z, float limit) {
    float length;
    kart->push_x += x;
    kart->push_z += z;
    length = sqrtf(kart->push_x * kart->push_x + kart->push_z * kart->push_z);
    if (length > limit && length > 0.0001f) {
        kart->push_x *= limit / length;
        kart->push_z *= limit / length;
    }
}

typedef struct {
    float max_speed;
    float acceleration;
    float braking;
    float handling;
    float drift_handling;
} KartPhysics;

static const KartPhysics physics_standard_mr = {0.1171875f, 0.0009041f, 0.0022f, 0.022f, 0.040f};
static const KartPhysics physics_b_dasher = {0.1100f, 0.0008650f, 0.0022f, 0.0205f, 0.0385f};
static const KartPhysics physics_gros_cube = {0.1206703f, 0.0019341f, 0.0035f, 0.065f, 0.075f};

typedef struct {
    unsigned long long countdown_start;
    unsigned long long race_start;
    unsigned long long lap_start;
    unsigned long long finish_time;
    unsigned long long lap_times[3];
    int lap;
    int armed;
    int finished;
    int active;
    int start_boost_armed;
} RaceTiming;

static RaceTiming timing = {0};
static unsigned long long fps_window_start = 0;
static unsigned int fps_frame_count = 0;
static unsigned int fps_display = 0;

typedef enum {
    SCREEN_MAIN_MENU,
    SCREEN_CHARACTER,
    SCREEN_KART,
    SCREEN_MODE,
    SCREEN_COURSE,
    SCREEN_RACE
} Screen;

static int exit_callback(int arg1, int arg2, void *common) {
    (void)arg1; (void)arg2; (void)common;
    sceKernelExitGame();
    return 0;
}

static int callback_thread(SceSize args, void *argp) {
    (void)args; (void)argp;
    int callback = sceKernelCreateCallback("exit", exit_callback, NULL);
    sceKernelRegisterExitCallback(callback);
    sceKernelSleepThreadCB();
    return 0;
}

static void setup_callbacks(void) {
    int thread = sceKernelCreateThread("callbacks", callback_thread, 0x11, 0xFA0, 0, NULL);
    if (thread >= 0) sceKernelStartThread(thread, 0, NULL);
}

static int load_model_file(CourseModel *model, const char *path) {
    struct {
        char magic[4];
        unsigned int count;
        unsigned int stride;
        unsigned int reserved;
    } header;
    FILE *file = fopen(path, "rb");
    size_t bytes;
    if (!file) return 0;
    if (fread(&header, sizeof(header), 1, file) != 1 ||
        (memcmp(header.magic, "MKT2", 4) != 0 && memcmp(header.magic, "MKT3", 4) != 0 &&
         memcmp(header.magic, "MKA4", 4) != 0 && memcmp(header.magic, "MKO4", 4) != 0 &&
         memcmp(header.magic, "MK65", 4) != 0 && memcmp(header.magic, "MK51", 4) != 0) ||
        header.stride != sizeof(CourseVertex) ||
        header.count == 0 || header.count > 100000) {
        fclose(file);
        return 0;
    }
    bytes = header.count * sizeof(CourseVertex);
    model->vertices = (CourseVertex *)memalign(16, bytes);
    if (!model->vertices || fread(model->vertices, 1, bytes, file) != bytes) {
        free(model->vertices);
        model->vertices = NULL;
        fclose(file);
        return 0;
    }
    fclose(file);
    model->count = header.count;
    model->repeat_texture = memcmp(header.magic, "MKT3", 4) == 0 ||
                            memcmp(header.magic, "MKA4", 4) == 0 ||
                            memcmp(header.magic, "MKB4", 4) == 0 ||
                            memcmp(header.magic, "MKO4", 4) == 0 ||
                            memcmp(header.magic, "MK65", 4) == 0 ||
                            memcmp(header.magic, "MK51", 4) == 0;
    model->swizzled_texture = memcmp(header.magic, "MKA4", 4) == 0 ||
                               memcmp(header.magic, "MKB4", 4) == 0 ||
                               memcmp(header.magic, "MKO4", 4) == 0 ||
                               memcmp(header.magic, "MK65", 4) == 0 ||
                               memcmp(header.magic, "MK51", 4) == 0;
    model->has_alpha = memcmp(header.magic, "MKO4", 4) != 0 &&
                       memcmp(header.magic, "MK65", 4) != 0;
    model->texture_format = memcmp(header.magic, "MK65", 4) == 0 ? GU_PSM_5650 :
                            memcmp(header.magic, "MK51", 4) == 0 ? GU_PSM_5551 :
                            GU_PSM_8888;
    model->texture_width = header.reserved >> 16;
    model->texture_height = header.reserved & 0xffff;
    {
        char texture_path[512];
        char *extension;
        strncpy(texture_path, path, sizeof(texture_path) - 1);
        texture_path[sizeof(texture_path) - 1] = 0;
        extension = strrchr(texture_path, '.');
        if (extension) strcpy(extension, ".rgba");
        else strcat(texture_path, ".rgba");
        file = fopen(texture_path, "rb");
        bytes = model->texture_width * model->texture_height *
            (model->texture_format == GU_PSM_8888 ? 4 : 2);
        model->texture = (unsigned int *)memalign(64, bytes);
        if (!file || !model->texture || fread(model->texture, 1, bytes, file) != bytes) {
            if (file) fclose(file);
            free(model->texture);
            model->texture = NULL;
        } else fclose(file);
    }
    sceKernelDcacheWritebackInvalidateAll();
    return 1;
}

static int load_relative_model(CourseModel *model, int argc, char *argv[], const char *relative) {
    char path[512];
    if (argc > 0 && argv && argv[0]) {
        const char *slash = strrchr(argv[0], '/');
        if (!slash) slash = strrchr(argv[0], '\\');
        if (slash) {
            size_t directory_length = (size_t)(slash - argv[0]);
            if (directory_length + 1 + strlen(relative) < sizeof(path)) {
                memcpy(path, argv[0], directory_length);
                path[directory_length] = '/';
                strcpy(path + directory_length + 1, relative);
                if (load_model_file(model, path)) return 1;
            }
        }
    }
    if (load_model_file(model, relative)) return 1;
    snprintf(path, sizeof(path), "ms0:/PSP/GAME/MarioKartPSP/%s", relative);
    return load_model_file(model, path);
}

static int load_kcl_file(const char *path, KclTriangle **destination,
                         unsigned int *destination_count) {
    struct { char magic[4]; unsigned int count; } header;
    FILE *file = fopen(path, "rb");
    size_t bytes;
    if (!file) return 0;
    if (fread(&header, sizeof(header), 1, file) != 1 ||
        memcmp(header.magic, "MKCL", 4) != 0 || !header.count || header.count > 20000) {
        fclose(file); return 0;
    }
    bytes = header.count * sizeof(KclTriangle);
    *destination = (KclTriangle *)memalign(16, bytes);
    if (!*destination || fread(*destination, 1, bytes, file) != bytes) {
        free(*destination); *destination = NULL; fclose(file); return 0;
    }
    fclose(file);
    *destination_count = header.count;
    sceKernelDcacheWritebackInvalidateAll();
    return 1;
}

static int load_relative_kcl(int argc, char *argv[], const char *relative,
                             KclTriangle **destination,
                             unsigned int *destination_count) {
    char path[512];
    if (argc > 0 && argv && argv[0]) {
        const char *slash = strrchr(argv[0], '/');
        if (!slash) slash = strrchr(argv[0], '\\');
        if (slash) {
            size_t length = (size_t)(slash - argv[0]);
            if (length + strlen(relative) + 2 < sizeof(path)) {
                memcpy(path, argv[0], length); path[length] = '/'; strcpy(path+length+1, relative);
                if (load_kcl_file(path, destination, destination_count)) return 1;
            }
        }
    }
    if (load_kcl_file(relative, destination, destination_count)) return 1;
    snprintf(path, sizeof(path), "ms0:/PSP/GAME/MarioKartPSP/%s", relative);
    return load_kcl_file(path, destination, destination_count);
}

static int kcl_grid_coordinate(float value, float minimum, float cell_size) {
    int cell = (int)floorf((value - minimum) / cell_size);
    if (cell < 0) cell = 0;
    if (cell >= KCL_GRID_SIZE) cell = KCL_GRID_SIZE - 1;
    return cell;
}

static void build_kcl_grid(KclGrid *grid, const KclTriangle *triangles,
                           unsigned int triangle_count) {
    unsigned int counts[KCL_GRID_CELLS] = {0};
    unsigned int cursors[KCL_GRID_CELLS];
    unsigned int i, cell, total = 0;
    if (!grid || !triangles || !triangle_count) return;
    grid->min_x = grid->min_z = 100000.0f;
    grid->max_x = grid->max_z = -100000.0f;
    for (i = 0; i < triangle_count; ++i) {
        const KclTriangle *triangle = &triangles[i];
        float min_x = fminf(triangle->a.x, fminf(triangle->b.x, triangle->c.x));
        float max_x = fmaxf(triangle->a.x, fmaxf(triangle->b.x, triangle->c.x));
        float min_z = fminf(triangle->a.z, fminf(triangle->b.z, triangle->c.z));
        float max_z = fmaxf(triangle->a.z, fmaxf(triangle->b.z, triangle->c.z));
        if (min_x < grid->min_x) grid->min_x = min_x;
        if (max_x > grid->max_x) grid->max_x = max_x;
        if (min_z < grid->min_z) grid->min_z = min_z;
        if (max_z > grid->max_z) grid->max_z = max_z;
    }
    grid->min_x -= 0.5f; grid->max_x += 0.5f;
    grid->min_z -= 0.5f; grid->max_z += 0.5f;
    grid->cell_width = (grid->max_x - grid->min_x) / KCL_GRID_SIZE;
    grid->cell_depth = (grid->max_z - grid->min_z) / KCL_GRID_SIZE;
    if (grid->cell_width < 0.001f || grid->cell_depth < 0.001f) return;
    for (i = 0; i < triangle_count; ++i) {
        const KclTriangle *triangle = &triangles[i];
        float min_x = fminf(triangle->a.x, fminf(triangle->b.x, triangle->c.x)) - 0.25f;
        float max_x = fmaxf(triangle->a.x, fmaxf(triangle->b.x, triangle->c.x)) + 0.25f;
        float min_z = fminf(triangle->a.z, fminf(triangle->b.z, triangle->c.z)) - 0.25f;
        float max_z = fmaxf(triangle->a.z, fmaxf(triangle->b.z, triangle->c.z)) + 0.25f;
        int x0 = kcl_grid_coordinate(min_x, grid->min_x, grid->cell_width);
        int x1 = kcl_grid_coordinate(max_x, grid->min_x, grid->cell_width);
        int z0 = kcl_grid_coordinate(min_z, grid->min_z, grid->cell_depth);
        int z1 = kcl_grid_coordinate(max_z, grid->min_z, grid->cell_depth);
        int x, z;
        for (z = z0; z <= z1; ++z)
            for (x = x0; x <= x1; ++x) counts[z * KCL_GRID_SIZE + x]++;
    }
    grid->offsets[0] = 0;
    for (cell = 0; cell < KCL_GRID_CELLS; ++cell) {
        total += counts[cell];
        grid->offsets[cell + 1] = total;
        cursors[cell] = grid->offsets[cell];
    }
    grid->indices = (unsigned short *)malloc(total * sizeof(unsigned short));
    if (!grid->indices) return;
    for (i = 0; i < triangle_count; ++i) {
        const KclTriangle *triangle = &triangles[i];
        float min_x = fminf(triangle->a.x, fminf(triangle->b.x, triangle->c.x)) - 0.25f;
        float max_x = fmaxf(triangle->a.x, fmaxf(triangle->b.x, triangle->c.x)) + 0.25f;
        float min_z = fminf(triangle->a.z, fminf(triangle->b.z, triangle->c.z)) - 0.25f;
        float max_z = fmaxf(triangle->a.z, fmaxf(triangle->b.z, triangle->c.z)) + 0.25f;
        int x0 = kcl_grid_coordinate(min_x, grid->min_x, grid->cell_width);
        int x1 = kcl_grid_coordinate(max_x, grid->min_x, grid->cell_width);
        int z0 = kcl_grid_coordinate(min_z, grid->min_z, grid->cell_depth);
        int z1 = kcl_grid_coordinate(max_z, grid->min_z, grid->cell_depth);
        int x, z;
        for (z = z0; z <= z1; ++z)
            for (x = x0; x <= x1; ++x) {
                cell = (unsigned int)(z * KCL_GRID_SIZE + x);
                grid->indices[cursors[cell]++] = (unsigned short)i;
            }
    }
}

static int kcl_candidate_range(float x, float z, unsigned int *begin,
                               unsigned int *end) {
    int gx, gz;
    unsigned int cell;
    if (!active_kcl || !active_kcl_count) return 0;
    if (!active_kcl_grid || !active_kcl_grid->indices) {
        *begin = 0; *end = active_kcl_count; return 1;
    }
    if (x < active_kcl_grid->min_x || x > active_kcl_grid->max_x ||
        z < active_kcl_grid->min_z || z > active_kcl_grid->max_z) return 0;
    gx = kcl_grid_coordinate(x, active_kcl_grid->min_x, active_kcl_grid->cell_width);
    gz = kcl_grid_coordinate(z, active_kcl_grid->min_z, active_kcl_grid->cell_depth);
    cell = (unsigned int)(gz * KCL_GRID_SIZE + gx);
    *begin = active_kcl_grid->offsets[cell];
    *end = active_kcl_grid->offsets[cell + 1];
    return *begin < *end;
}

static unsigned int kcl_candidate_id(unsigned int candidate) {
    return active_kcl_grid && active_kcl_grid->indices ?
        active_kcl_grid->indices[candidate] : candidate;
}

static unsigned int *load_raw_texture_file(const char *path, size_t bytes) {
    FILE *file=fopen(path,"rb");
    unsigned int *texture;
    if (!file) return NULL;
    texture=(unsigned int *)memalign(64,bytes);
    if (!texture || fread(texture,1,bytes,file)!=bytes) { free(texture); texture=NULL; }
    fclose(file); return texture;
}

static unsigned int *load_relative_raw_texture(int argc,char *argv[],const char *relative,size_t bytes) {
    char path[512]; const char *slash;
    if (argc>0 && argv && argv[0] && (slash=strrchr(argv[0],'/'))!=NULL) {
        size_t length=(size_t)(slash-argv[0]);
        if (length+strlen(relative)+2<sizeof(path)) {
            memcpy(path,argv[0],length); path[length]='/'; strcpy(path+length+1,relative);
            { unsigned int *result=load_raw_texture_file(path,bytes); if (result) return result; }
        }
    }
    { unsigned int *result=load_raw_texture_file(relative,bytes); if (result) return result; }
    snprintf(path,sizeof(path),"ms0:/PSP/GAME/MarioKartPSP/%s",relative);
    return load_raw_texture_file(path,bytes);
}

static void spatialize_course_model_grid(CourseModel *model, int grid,
                                         float origin, float cell_size) {
    enum { MAX_CELLS = 144 };
    unsigned int counts[MAX_CELLS] = {0}, offsets[MAX_CELLS] = {0}, cursors[MAX_CELLS] = {0};
    CourseVertex *ordered;
    CourseChunk *chunks;
    unsigned int triangle, cell, cells, total = 0, chunk_count = 0;
    if (!model->vertices || model->count < 3 || grid < 1 || grid > 12) return;
    cells = (unsigned int)(grid * grid);
    for (triangle = 0; triangle + 2 < model->count; triangle += 3) {
        float x = (model->vertices[triangle].x + model->vertices[triangle+1].x + model->vertices[triangle+2].x) / 3.0f;
        float z = (model->vertices[triangle].z + model->vertices[triangle+1].z + model->vertices[triangle+2].z) / 3.0f;
        int gx = (int)floorf((x - origin) / cell_size), gz = (int)floorf((z - origin) / cell_size);
        if (gx < 0) gx = 0;
        if (gx >= grid) gx = grid-1;
        if (gz < 0) gz = 0;
        if (gz >= grid) gz = grid-1;
        counts[gz * grid + gx] += 3;
    }
    for (cell = 0; cell < cells; ++cell) { offsets[cell] = total; total += counts[cell]; cursors[cell] = offsets[cell]; if (counts[cell]) chunk_count++; }
    ordered = (CourseVertex *)memalign(16, model->count * sizeof(CourseVertex));
    chunks = (CourseChunk *)calloc(chunk_count, sizeof(CourseChunk));
    if (!ordered || !chunks) { free(ordered); free(chunks); return; }
    for (triangle = 0; triangle + 2 < model->count; triangle += 3) {
        float x = (model->vertices[triangle].x + model->vertices[triangle+1].x + model->vertices[triangle+2].x) / 3.0f;
        float z = (model->vertices[triangle].z + model->vertices[triangle+1].z + model->vertices[triangle+2].z) / 3.0f;
        int gx = (int)floorf((x - origin) / cell_size), gz = (int)floorf((z - origin) / cell_size);
        unsigned int destination;
        if (gx < 0) gx = 0;
        if (gx >= grid) gx = grid-1;
        if (gz < 0) gz = 0;
        if (gz >= grid) gz = grid-1;
        cell = (unsigned int)(gz * grid + gx); destination = cursors[cell];
        memcpy(ordered + destination, model->vertices + triangle, 3 * sizeof(CourseVertex)); cursors[cell] += 3;
    }
    {
        unsigned int out = 0;
        for (cell = 0; cell < cells; ++cell) if (counts[cell]) {
            unsigned int j; CourseChunk *chunk = &chunks[out++];
            chunk->start = offsets[cell]; chunk->count = counts[cell];
            chunk->min_x = chunk->min_z = 100000.0f; chunk->max_x = chunk->max_z = -100000.0f;
            for (j = chunk->start; j < chunk->start + chunk->count; ++j) {
                if (ordered[j].x < chunk->min_x) chunk->min_x = ordered[j].x;
                if (ordered[j].x > chunk->max_x) chunk->max_x = ordered[j].x;
                if (ordered[j].z < chunk->min_z) chunk->min_z = ordered[j].z;
                if (ordered[j].z > chunk->max_z) chunk->max_z = ordered[j].z;
            }
        }
    }
    free(model->vertices); model->vertices = ordered; model->chunks = chunks; model->chunk_count = chunk_count;
    sceKernelDcacheWritebackInvalidateAll();
}

static void spatialize_course_model(CourseModel *model) {
    spatialize_course_model_grid(model, 12, -72.0f, 12.0f);
}

static void bound_course_model(CourseModel *model) {
    unsigned int i;
    CourseChunk *bounds;
    if (!model->vertices || !model->count) return;
    bounds = (CourseChunk *)calloc(1, sizeof(CourseChunk));
    if (!bounds) return;
    bounds->start = 0;
    bounds->count = model->count;
    bounds->min_x = bounds->min_z = 100000.0f;
    bounds->max_x = bounds->max_z = -100000.0f;
    for (i = 0; i < model->count; ++i) {
        const CourseVertex *vertex = &model->vertices[i];
        if (vertex->x < bounds->min_x) bounds->min_x = vertex->x;
        if (vertex->x > bounds->max_x) bounds->max_x = vertex->x;
        if (vertex->z < bounds->min_z) bounds->min_z = vertex->z;
        if (vertex->z > bounds->max_z) bounds->max_z = vertex->z;
    }
    model->chunks = bounds;
    model->chunk_count = 1;
}

static int load_animation_file(AnimatedModel *model, const char *path) {
    struct { char magic[4]; unsigned int frames, count, stride, dimensions, drive_frames; } header;
    FILE *file = fopen(path, "rb");
    size_t bytes;
    char texture_path[512], *extension;
    if (!file) return 0;
    if (fread(&header, sizeof(header), 1, file) != 1 || memcmp(header.magic, "MKA5", 4) != 0 ||
        header.stride != sizeof(CourseVertex) || !header.frames || !header.count) {
        fclose(file); return 0;
    }
    bytes = (size_t)header.frames * header.count * sizeof(CourseVertex);
    model->vertices = (CourseVertex *)memalign(16, bytes);
    if (!model->vertices || fread(model->vertices, 1, bytes, file) != bytes) {
        free(model->vertices); model->vertices = NULL; fclose(file); return 0;
    }
    fclose(file);
    model->frame_count = header.frames; model->vertices_per_frame = header.count;
    model->drive_frames = header.drive_frames;
    model->texture_width = header.dimensions >> 16; model->texture_height = header.dimensions & 0xffff;
    strncpy(texture_path, path, sizeof(texture_path)-1); texture_path[sizeof(texture_path)-1]=0;
    extension = strrchr(texture_path, '.'); if (extension) strcpy(extension, ".rgba");
    bytes = model->texture_width * model->texture_height * 4;
    model->texture = (unsigned int *)memalign(64, bytes);
    file = fopen(texture_path, "rb");
    if (!file || !model->texture || fread(model->texture, 1, bytes, file) != bytes) {
        if (file) fclose(file);
        free(model->texture);
        model->texture = NULL;
        return 0;
    }
    fclose(file); sceKernelDcacheWritebackInvalidateAll(); return 1;
}

static int load_relative_animation(AnimatedModel *model, int argc, char *argv[], const char *relative) {
    char path[512]; const char *slash;
    if (argc > 0 && argv && argv[0] && (slash = strrchr(argv[0], '/')) != NULL) {
        size_t length = (size_t)(slash - argv[0]);
        if (length + strlen(relative) + 2 < sizeof(path)) {
            memcpy(path, argv[0], length); path[length]='/'; strcpy(path+length+1, relative);
            if (load_animation_file(model, path)) return 1;
        }
    }
    if (load_animation_file(model, relative)) return 1;
    snprintf(path, sizeof(path), "ms0:/PSP/GAME/MarioKartPSP/%s", relative);
    return load_animation_file(model, path);
}

static void load_game_data(int argc, char *argv[]) {
    int i;
    char relative[128];
    load_relative_model(&collision_model, argc, argv, "data/courses/mario_circuit/collision.mkt");
    spatialize_course_model(&collision_model);
    for (i = 0; i < 22; ++i) {
        snprintf(relative, sizeof(relative), "data/courses/mario_circuit/course_mat_%d.mkt", i);
        load_relative_model(&course_batches[i], argc, argv, relative);
        spatialize_course_model(&course_batches[i]);
    }
    load_relative_kcl(argc, argv, "data/courses/waluigi_pinball/collision.kclp",
                      &pinball_kcl, &pinball_kcl_count);
    build_kcl_grid(&pinball_kcl_grid, pinball_kcl, pinball_kcl_count);
    for (i = 0; i < 26; ++i) {
        snprintf(relative, sizeof(relative), "data/courses/waluigi_pinball/course_mat_%d.mkt", i);
        load_relative_model(&pinball_batches[i], argc, argv, relative);
        spatialize_course_model(&pinball_batches[i]);
    }
    load_relative_kcl(argc, argv, "data/courses/luigis_mansion/collision.kclp",
                      &mansion_kcl, &mansion_kcl_count);
    build_kcl_grid(&mansion_kcl_grid, mansion_kcl, mansion_kcl_count);
    for (i = 0; i < 28; ++i) {
        snprintf(relative, sizeof(relative), "data/courses/luigis_mansion/course_mat_%d.mkt", i);
        load_relative_model(&mansion_batches[i], argc, argv, relative);
        /* Luigi's Mansion is only about two thousand triangles after targeted
           subdivision.  Splitting every one of its 28 materials into a 12x12
           grid created hundreds of tiny GE submissions and also culled broad
           side-floor triangles.  One submission per material is both faster
           on PSP and immune to those disappearing edges. */
        if (i == 7 || i == 11 || i == 23)
            spatialize_course_model_grid(&mansion_batches[i], 4, -80.0f, 40.0f);
        else
            bound_course_model(&mansion_batches[i]);
    }
    load_relative_model(&mansion_mapobjs[MANSION_CHANDELIER], argc, argv,
                        "data/courses/luigis_mansion/mapobj/chandelier/course_mat_0.mkt");
    load_relative_model(&mansion_mapobjs[MANSION_MOVE_TREE], argc, argv,
                        "data/courses/luigis_mansion/mapobj/move_tree/course_mat_0.mkt");
    load_relative_model(&mansion_mapobjs[MANSION_PICTURE1], argc, argv,
                        "data/courses/luigis_mansion/mapobj/picture1/course_mat_0.mkt");
    load_relative_model(&mansion_mapobjs[MANSION_PICTURE2], argc, argv,
                        "data/courses/luigis_mansion/mapobj/picture2/course_mat_0.mkt");
    load_relative_model(&mansion_mapobjs[MANSION_TERESA], argc, argv,
                        "data/courses/luigis_mansion/mapobj/teresa/course_mat_0.mkt");
    load_relative_animation(&mansion_animations[MANSION_CHANDELIER], argc, argv,
        "data/courses/luigis_mansion/mapobj/chandelier/animated.mka");
    load_relative_animation(&mansion_animations[MANSION_MOVE_TREE], argc, argv,
        "data/courses/luigis_mansion/mapobj/move_tree/animated.mka");
    load_relative_animation(&mansion_animations[MANSION_PICTURE1], argc, argv,
        "data/courses/luigis_mansion/mapobj/picture1/animated.mka");
    load_relative_animation(&mansion_animations[MANSION_PICTURE2], argc, argv,
        "data/courses/luigis_mansion/mapobj/picture2/animated.mka");
    mansion_teresa_alt_texture = load_relative_raw_texture(argc, argv,
        "data/courses/luigis_mansion/mapobj/teresa/frame_1.rgba", 64 * 64 * 4);
    load_relative_model(&pinball_mapobjs[MAPOBJ_BOUND], argc, argv, "data/courses/waluigi_pinball/mapobj/bound/course_mat_0.mkt");
    load_relative_model(&pinball_mapobjs[MAPOBJ_DRAM], argc, argv, "data/courses/waluigi_pinball/mapobj/dram/course_mat_0.mkt");
    load_relative_model(&pinball_mapobjs[MAPOBJ_FLIPPER], argc, argv, "data/courses/waluigi_pinball/mapobj/flipper/course_mat_0.mkt");
    load_relative_model(&pinball_mapobjs[MAPOBJ_IRON_BALL], argc, argv, "data/courses/waluigi_pinball/mapobj/iron_ball/course_mat_0.mkt");
    for (i=0;i<4;++i) { snprintf(relative,sizeof(relative),"data/courses/waluigi_pinball/animation/flipper/frame_%d.rgba",i); pinball_flipper_frames[i]=load_relative_raw_texture(argc,argv,relative,32*32*4); }
    for (i=0;i<3;++i) { snprintf(relative,sizeof(relative),"data/courses/waluigi_pinball/animation/bound/frame_%d.rgba",i); pinball_bound_frames[i]=load_relative_raw_texture(argc,argv,relative,32*64*4); }
    for (i=0;i<2;++i) { snprintf(relative,sizeof(relative),"data/courses/waluigi_pinball/animation/flag/frame_%d.rgba",i); pinball_flag_frames[i]=load_relative_raw_texture(argc,argv,relative,128*32*4); }
    load_relative_model(&kart_models[0], argc, argv, "data/karts/standard_mr/standard_body.mkt");
    load_relative_model(&kart_models[1], argc, argv, "data/karts/standard_mr/bdasher_body.mkt");
    for (i = 0; i < 4; ++i) {
        snprintf(relative, sizeof(relative), "data/karts/standard_mr/standard_wheel_%d.mkt", i);
        load_relative_model(&wheel_models[0][i], argc, argv, relative);
        snprintf(relative, sizeof(relative), "data/karts/standard_mr/bdasher_wheel_%d.mkt", i);
        load_relative_model(&wheel_models[1][i], argc, argv, relative);
    }
    load_relative_model(&mario_model, argc, argv, "data/characters/mario/mario.mkt");
    load_relative_animation(&mario_animation, argc, argv, "data/characters/mario/mario_anim.mka");
    for (i = 0; i < 3; ++i) {
        snprintf(relative, sizeof(relative), "data/ui/item_mushroom_%d.mkt", i + 1);
        load_relative_model(&mushroom_icons[i], argc, argv, relative);
    }
}

static void select_course_data(int course) {
    active_course = course;
    if (course == 1) {
        active_batches = pinball_batches;
        active_collision = &pinball_collision;
        active_kcl = pinball_kcl;
        active_kcl_count = pinball_kcl_count;
        active_kcl_grid = &pinball_kcl_grid;
        active_material_count = 26;
        active_start_x = 19.84766f; active_start_y = 5.66797f; active_start_z = 50.97070f;
        active_start_heading = 3.1415927f;
    } else if (course == 2) {
        active_batches = mansion_batches;
        active_collision = &pinball_collision;
        active_kcl = mansion_kcl;
        active_kcl_count = mansion_kcl_count;
        active_kcl_grid = &mansion_kcl_grid;
        active_material_count = 28;
        active_start_x = 21.44531f; active_start_y = 2.51172f; active_start_z = 24.85352f;
        active_start_heading = 3.1415927f;
    } else {
        active_batches = course_batches;
        active_collision = &collision_model;
        active_kcl = NULL;
        active_kcl_count = 0;
        active_kcl_grid = NULL;
        active_material_count = 22;
        active_start_x = 20.69335f; active_start_y = 2.03125f; active_start_z = -26.33985f;
        active_start_heading = 1.5707963f;
    }
}

static enum MusicTrack course_music_track(void) {
    if (active_course == 1) return MUSIC_WALUIGI_PINBALL;
    if (active_course == 2) return MUSIC_LUIGIS_MANSION;
    return MUSIC_MARIO_CIRCUIT;
}

static enum MusicTrack course_final_lap_track(void) {
    if (active_course == 1) return MUSIC_WALUIGI_FINAL_LAP_JINGLE;
    if (active_course == 2) return MUSIC_LUIGIS_FINAL_LAP_JINGLE;
    return MUSIC_FINAL_LAP_JINGLE;
}

static void graphics_init(void) {
    sceGuInit();
    sceGuStart(GU_DIRECT, display_list);
    /* RGB565 halves framebuffer bandwidth and leaves enough EDRAM for all
       frequently used course textures on a PSP-1000. */
    sceGuDrawBuffer(GU_PSM_5650, (void *)0, 512);
    sceGuDispBuffer(480, 272, (void *)0x44000, 512);
    sceGuDepthBuffer((void *)0x88000, 512);
    sceGuOffset(2048 - 240, 2048 - 136);
    sceGuViewport(2048, 2048, 480, 272);
    sceGuDepthRange(65535, 0);
    sceGuScissor(0, 0, 480, 272);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuEnable(GU_DEPTH_TEST);
    /* Clip triangles that cross the camera frustum instead of letting the GE
       reject the whole primitive as the camera moves past one of its edges. */
    sceGuEnable(GU_CLIP_PLANES);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuShadeModel(GU_SMOOTH);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

static int kcl_xz_height(const KclTriangle *triangle, float x, float z, float *height) {
    float denominator = (triangle->b.z-triangle->c.z)*(triangle->a.x-triangle->c.x) +
                        (triangle->c.x-triangle->b.x)*(triangle->a.z-triangle->c.z);
    float wa, wb, wc;
    if (fabsf(denominator) < 0.000001f) return 0;
    wa = ((triangle->b.z-triangle->c.z)*(x-triangle->c.x) +
          (triangle->c.x-triangle->b.x)*(z-triangle->c.z)) / denominator;
    wb = ((triangle->c.z-triangle->a.z)*(x-triangle->c.x) +
          (triangle->a.x-triangle->c.x)*(z-triangle->c.z)) / denominator;
    wc = 1.0f-wa-wb;
    if (wa < -0.001f || wb < -0.001f || wc < -0.001f) return 0;
    *height = wa*triangle->a.y + wb*triangle->b.y + wc*triangle->c.y;
    return 1;
}

static int kcl_ground_height(float x, float z, float current_y, float *height) {
    unsigned int i, begin, end;
    int found = 0, best_id = -1;
    float best = -100000.0f, best_delta = 100000.0f;
    if (!kcl_candidate_range(x, z, &begin, &end)) return 0;
    for (i = begin; i < end; ++i) {
        unsigned int triangle_id = kcl_candidate_id(i);
        const KclTriangle *triangle = &active_kcl[triangle_id];
        float y;
        /* Only explicit KCL floors and driveable boost/jump surfaces. */
        if (triangle->category != 1 && triangle->category != 4) continue;
        if (!kcl_xz_height(triangle, x, z, &y)) continue;
        /* Accept a small upward step so slopes remain driveable, but select
           only the surface closest to the kart.  The selected triangle is
           recorded after the complete search; recording candidates here used
           to leave kcl_touched_* pointing at a different overlapping floor. */
        if (y > current_y + 0.35f || fabsf(y - current_y) >= best_delta) continue;
        best = y;
        best_delta = fabsf(y - current_y);
        best_id = (int)triangle_id;
        found = 1;
    }
    if (found) {
        const KclTriangle *triangle = &active_kcl[best_id];
        *height = best;
        kcl_touched_id = best_id;
        kcl_touched_type = triangle->type;
        kcl_touched_variant = triangle->variant;
        kcl_touched_normal_x = triangle->normal.x;
        kcl_touched_normal_y = triangle->normal.y;
        kcl_touched_normal_z = triangle->normal.z;
    }
    return found;
}

/* Read-only floor query for the chase camera.  Unlike kcl_ground_height this
   must not replace the kart's touched material/type while rendering. */
static int kcl_camera_floor_height(float x, float z, float eye_y, float *height) {
    unsigned int i, begin, end;
    int found = 0;
    float best = -100000.0f, best_delta = 100000.0f;
    if (!kcl_candidate_range(x, z, &begin, &end)) return 0;
    for (i = begin; i < end; ++i) {
        const KclTriangle *triangle = &active_kcl[kcl_candidate_id(i)];
        float y, delta;
        if (triangle->category != 1 && triangle->category != 4) continue;
        if (!kcl_xz_height(triangle, x, z, &y)) continue;
        /* A higher road may cross the same X/Z elsewhere on the course. Only
           accept a surface the camera could currently be touching. */
        if (y > eye_y + 1.0f) continue;
        delta = fabsf(y - eye_y);
        if (delta >= best_delta) continue;
        best = y;
        best_delta = delta;
        found = 1;
    }
    if (found) *height = best;
    return found;
}

static int kcl_point_in_triangle(const KclTriangle *triangle, Vec3 point) {
    Vec3 v0 = {triangle->c.x-triangle->a.x, triangle->c.y-triangle->a.y, triangle->c.z-triangle->a.z};
    Vec3 v1 = {triangle->b.x-triangle->a.x, triangle->b.y-triangle->a.y, triangle->b.z-triangle->a.z};
    Vec3 v2 = {point.x-triangle->a.x, point.y-triangle->a.y, point.z-triangle->a.z};
    float d00=v0.x*v0.x+v0.y*v0.y+v0.z*v0.z, d01=v0.x*v1.x+v0.y*v1.y+v0.z*v1.z;
    float d11=v1.x*v1.x+v1.y*v1.y+v1.z*v1.z, d20=v2.x*v0.x+v2.y*v0.y+v2.z*v0.z;
    float d21=v2.x*v1.x+v2.y*v1.y+v2.z*v1.z, denominator=d00*d11-d01*d01;
    float v, w;
    if (fabsf(denominator) < 0.000001f) return 0;
    v=(d11*d20-d01*d21)/denominator; w=(d00*d21-d01*d20)/denominator;
    return v >= -0.01f && w >= -0.01f && v+w <= 1.01f;
}

static int kcl_prism_contact(unsigned char category, float x, float y, float z, float radius) {
    unsigned int i, begin, end;
    Vec3 point = {x,y,z};
    if (!kcl_candidate_range(x, z, &begin, &end)) return 0;
    for (i=begin; i<end; ++i) {
        unsigned int triangle_id=kcl_candidate_id(i);
        const KclTriangle *triangle=&active_kcl[triangle_id];
        Vec3 projected;
        float distance;
        if (triangle->category != category) continue;
        /* Type 16 is an edge wall: MKDS deliberately ignores it for a kart
           travelling on the ground.  Treating it as an ordinary wall stopped
           the kart at the seams following boost ramps. */
        if (category == 2 && triangle->type == 16) continue;
        distance=(point.x-triangle->a.x)*triangle->normal.x+
                 (point.y-triangle->a.y)*triangle->normal.y+
                 (point.z-triangle->a.z)*triangle->normal.z;
        if (fabsf(distance)>radius) continue;
        projected.x=point.x-triangle->normal.x*distance;
        projected.y=point.y-triangle->normal.y*distance;
        projected.z=point.z-triangle->normal.z*distance;
        if (category == 5) {
            float min_x=fminf(triangle->a.x,fminf(triangle->b.x,triangle->c.x));
            float max_x=fmaxf(triangle->a.x,fmaxf(triangle->b.x,triangle->c.x));
            float min_y=fminf(triangle->a.y,fminf(triangle->b.y,triangle->c.y));
            float max_y=fmaxf(triangle->a.y,fmaxf(triangle->b.y,triangle->c.y));
            /* Trigger volumes collide with the kart sphere, not only with its
               centre.  Expand the KCL rectangle by that sphere radius. */
            if (projected.x >= min_x-radius && projected.x <= max_x+radius &&
                projected.y >= min_y-radius && projected.y <= max_y+radius) {
                kcl_touched_id=(int)triangle_id; kcl_touched_type=triangle->type; kcl_touched_variant=triangle->variant;
                kcl_touched_normal_x=triangle->normal.x;
                kcl_touched_normal_y=triangle->normal.y;
                kcl_touched_normal_z=triangle->normal.z;
                return 1;
            }
            continue;
        }
        if (!kcl_point_in_triangle(triangle, projected)) continue;
        kcl_touched_id=(int)triangle_id; kcl_touched_type=triangle->type; kcl_touched_variant=triangle->variant;
        kcl_touched_normal_x=triangle->normal.x;
        kcl_touched_normal_y=triangle->normal.y;
        kcl_touched_normal_z=triangle->normal.z;
        return 1;
    }
    return 0;
}

static int kcl_wall_sweep(Vec3 from, Vec3 to, float radius) {
    unsigned int i, begin, end;
    float earliest = 2.0f;
    int best_id = -1;
    float best_nx = 0.0f, best_nz = 0.0f;
    Vec3 movement = {to.x - from.x, to.y - from.y, to.z - from.z};
    if (!kcl_candidate_range(to.x, to.z, &begin, &end)) return 0;
    for (i = begin; i < end; ++i) {
        unsigned int triangle_id = kcl_candidate_id(i);
        const KclTriangle *triangle = &active_kcl[triangle_id];
        float from_distance, to_distance, denominator, t, side;
        Vec3 center, projected;
        if (triangle->category != 2 || triangle->type == 16) continue;
        from_distance = (from.x - triangle->a.x) * triangle->normal.x +
                        (from.y - triangle->a.y) * triangle->normal.y +
                        (from.z - triangle->a.z) * triangle->normal.z;
        to_distance = (to.x - triangle->a.x) * triangle->normal.x +
                      (to.y - triangle->a.y) * triangle->normal.y +
                      (to.z - triangle->a.z) * triangle->normal.z;
        denominator = from_distance - to_distance;
        if (from_distance > radius && to_distance <= radius) {
            t = (from_distance - radius) / denominator;
            side = 1.0f;
        } else if (from_distance < -radius && to_distance >= -radius) {
            t = (from_distance + radius) / denominator;
            side = -1.0f;
        } else if (fabsf(from_distance) <= radius) {
            side = from_distance >= 0.0f ? 1.0f : -1.0f;
            if ((to_distance - from_distance) * side >= 0.0f) continue;
            t = 0.0f;
        } else {
            continue;
        }
        if (t < 0.0f || t > 1.0f || t >= earliest) continue;
        center.x = from.x + movement.x * t;
        center.y = from.y + movement.y * t;
        center.z = from.z + movement.z * t;
        {
            float distance = (center.x - triangle->a.x) * triangle->normal.x +
                             (center.y - triangle->a.y) * triangle->normal.y +
                             (center.z - triangle->a.z) * triangle->normal.z;
            projected.x = center.x - triangle->normal.x * distance;
            projected.y = center.y - triangle->normal.y * distance;
            projected.z = center.z - triangle->normal.z * distance;
        }
        if (!kcl_point_in_triangle(triangle, projected)) continue;
        best_id = (int)triangle_id;
        earliest = t;
        best_nx = triangle->normal.x * side;
        best_nz = triangle->normal.z * side;
        if (movement.x * best_nx + movement.z * best_nz > 0.0f) {
            best_nx = -best_nx;
            best_nz = -best_nz;
        }
    }
    if (best_id < 0) return 0;
    kcl_wall_hit_t = earliest;
    kcl_touched_id = best_id;
    kcl_touched_type = active_kcl[best_id].type;
    kcl_touched_variant = active_kcl[best_id].variant;
    kcl_touched_normal_x = best_nx;
    kcl_touched_normal_y = active_kcl[best_id].normal.y;
    kcl_touched_normal_z = best_nz;
    return 1;
}

static int kcl_cannon_sweep(Vec3 from, Vec3 to, float radius) {
    unsigned int i, begin, end;
    if (!kcl_candidate_range(to.x, to.z, &begin, &end)) return 0;
    for (i=begin;i<end;++i) {
        unsigned int triangle_id=kcl_candidate_id(i);
        const KclTriangle *triangle=&active_kcl[triangle_id];
        float from_distance,to_distance,t,denominator,min_x,max_x,min_y,max_y;
        Vec3 point;
        if (triangle->category!=5) continue;
        from_distance=(from.x-triangle->a.x)*triangle->normal.x+
                      (from.y-triangle->a.y)*triangle->normal.y+
                      (from.z-triangle->a.z)*triangle->normal.z;
        to_distance=(to.x-triangle->a.x)*triangle->normal.x+
                    (to.y-triangle->a.y)*triangle->normal.y+
                    (to.z-triangle->a.z)*triangle->normal.z;
        if (fabsf(from_distance)>radius && fabsf(to_distance)>radius &&
            from_distance*to_distance>0.0f) continue;
        denominator=from_distance-to_distance;
        t=fabsf(denominator)>0.000001f ? from_distance/denominator : 0.5f;
        if (t<0.0f) t=0.0f;
        if (t>1.0f) t=1.0f;
        point.x=from.x+(to.x-from.x)*t; point.y=from.y+(to.y-from.y)*t; point.z=from.z+(to.z-from.z)*t;
        min_x=fminf(triangle->a.x,fminf(triangle->b.x,triangle->c.x));
        max_x=fmaxf(triangle->a.x,fmaxf(triangle->b.x,triangle->c.x));
        min_y=fminf(triangle->a.y,fminf(triangle->b.y,triangle->c.y));
        max_y=fmaxf(triangle->a.y,fmaxf(triangle->b.y,triangle->c.y));
        if (point.x<min_x-radius || point.x>max_x+radius ||
            point.y<min_y-radius || point.y>max_y+radius) continue;
        kcl_touched_id=(int)triangle_id; kcl_touched_type=triangle->type; kcl_touched_variant=triangle->variant;
        return 1;
    }
    return 0;
}

#define PINBALL_BALL_RADIUS 0.625f
#define PINBALL_BALL_RENDER_SCALE 4.0f

static void pinball_ball_route(unsigned int index, const Vec3 **route, unsigned int *count) {
    if (index == 0) {
        *route = pinball_ball_route_4;
        *count = sizeof(pinball_ball_route_4) / sizeof(Vec3);
    } else if (index == 1) {
        *route = pinball_ball_route_5;
        *count = sizeof(pinball_ball_route_5) / sizeof(Vec3);
    } else {
        *route = pinball_ball_route_6;
        *count = sizeof(pinball_ball_route_6) / sizeof(Vec3);
    }
}

static float pinball_flipper_rotation(unsigned int index, unsigned long long now,
                                      float *angular_speed) {
    const MapObjInstance *object = &pinball_objects[8 + index];
    float phase = (float)(now % 1800000ULL) * (6.2831853f / 1800000.0f);
    float direction = object->x < 0.0f ? 1.0f : -1.0f;
    if (angular_speed)
        *angular_speed = direction * cosf(phase) * 0.62f * (6.2831853f / 108.0f);
    return object->rotation_y + direction * sinf(phase) * 0.62f;
}

static void reset_pinball_ball(unsigned int index) {
    static const unsigned int starts[PINBALL_BALL_COUNT] = {78, 42, 80};
    const Vec3 *route;
    unsigned int count, start, next;
    float dx, dz, length;
    PinballBall *ball = &pinball_balls[index];
    pinball_ball_route(index, &route, &count);
    start = starts[index] % count;
    next = (start + 1) % count;
    ball->position = route[start];
    ball->position.y += PINBALL_BALL_RADIUS;
    dx = route[next].x - route[start].x;
    dz = route[next].z - route[start].z;
    length = sqrtf(dx * dx + dz * dz);
    ball->velocity.x = length > 0.001f ? dx / length * 0.075f : 0.0f;
    ball->velocity.y = 0.0f;
    ball->velocity.z = length > 0.001f ? dz / length * 0.075f : 0.0f;
    ball->spin = 0.0f;
    ball->route_point = next;
    ball->sound_cooldown = 0;
}

static void reset_pinball_balls(void) {
    unsigned int i;
    for (i = 0; i < PINBALL_BALL_COUNT; ++i) reset_pinball_ball(i);
}

static void pinball_flipper_hits_ball(PinballBall *ball, unsigned int index,
                                      unsigned long long now) {
    const MapObjInstance *object = &pinball_objects[8 + index];
    float angular_speed;
    float rotation = pinball_flipper_rotation(index, now, &angular_speed);
    float direction_x = -cosf(rotation);
    float direction_z = sinf(rotation);
    float relative_x = ball->position.x - object->x;
    float relative_z = ball->position.z - object->z;
    float along = relative_x * direction_x + relative_z * direction_z;
    float closest_x, closest_z, dx, dz, distance_sq, distance, nx, nz;
    float surface_x, surface_z, impulse;
    if (fabsf(ball->position.y - object->y) > 0.72f) return;
    if (along < 0.0f) along = 0.0f;
    if (along > 2.85f) along = 2.85f;
    closest_x = object->x + direction_x * along;
    closest_z = object->z + direction_z * along;
    dx = ball->position.x - closest_x;
    dz = ball->position.z - closest_z;
    distance_sq = dx * dx + dz * dz;
    if (distance_sq >= 0.78f * 0.78f) return;
    distance = sqrtf(fmaxf(distance_sq, 0.000001f));
    nx = dx / distance;
    nz = dz / distance;
    ball->position.x = closest_x + nx * 0.78f;
    ball->position.z = closest_z + nz * 0.78f;
    surface_x = angular_speed * (closest_z - object->z);
    surface_z = -angular_speed * (closest_x - object->x);
    impulse = 0.055f + fabsf(angular_speed) * along * 1.35f;
    ball->velocity.x = surface_x * 0.75f + nx * impulse;
    ball->velocity.z = surface_z * 0.75f + nz * impulse;
    if (ball->velocity.y < 0.035f) ball->velocity.y = 0.035f;
    if (ball->sound_cooldown <= 0) {
        sfx_play(SFX_PINBALL_FLIPPER);
        ball->sound_cooldown = 8;
    }
}

static void pinball_bumpers_hit_ball(PinballBall *ball) {
    unsigned int i;
    for (i = 0; i < sizeof(pinball_objects) / sizeof(pinball_objects[0]); ++i) {
        const MapObjInstance *object = &pinball_objects[i];
        float object_scale, bumper_radius, bottom, top, dx, dz, distance_sq;
        if (object->model != MAPOBJ_BOUND) continue;
        object_scale = fmaxf(object->scale_x, object->scale_z);
        bumper_radius = object_scale * 0.1615f + PINBALL_BALL_RADIUS;
        bottom = object->y - object->scale_y * 0.15625f - PINBALL_BALL_RADIUS;
        top = object->y + object->scale_y * 0.21875f + PINBALL_BALL_RADIUS;
        if (ball->position.y < bottom || ball->position.y > top) continue;
        dx = ball->position.x - object->x;
        dz = ball->position.z - object->z;
        distance_sq = dx * dx + dz * dz;
        if (distance_sq < bumper_radius * bumper_radius && distance_sq > 0.000001f) {
            float distance = sqrtf(distance_sq);
            float nx = dx / distance, nz = dz / distance;
            float dot = ball->velocity.x * nx + ball->velocity.z * nz;
            float correction = fminf(bumper_radius - distance, 0.12f);
            ball->position.x += nx * correction;
            ball->position.z += nz * correction;
            if (dot < 0.0f) {
                ball->velocity.x -= 1.82f * dot * nx;
                ball->velocity.z -= 1.82f * dot * nz;
            }
            ball->velocity.x += nx * 0.045f;
            ball->velocity.z += nz * 0.045f;
            if (ball->velocity.y < 0.018f) ball->velocity.y = 0.018f;
            if (ball->sound_cooldown <= 0) {
                sfx_play(SFX_PINBALL_BUMPER);
                ball->sound_cooldown = 8;
            }
        }
    }
}

static void limit_pinball_ball_speed(PinballBall *ball) {
    float speed = sqrtf(ball->velocity.x * ball->velocity.x +
                        ball->velocity.z * ball->velocity.z);
    if (speed > 0.10f) {
        ball->velocity.x *= 0.10f / speed;
        ball->velocity.z *= 0.10f / speed;
    }
    if (ball->velocity.y > 0.10f) ball->velocity.y = 0.10f;
    if (ball->velocity.y < -0.10f) ball->velocity.y = -0.10f;
}

static void update_pinball_ball(unsigned int index, unsigned long long now) {
    PinballBall *ball = &pinball_balls[index];
    const Vec3 *route;
    unsigned int count;
    unsigned int previous_point;
    float dx, dz, distance_sq, distance, horizontal_speed;
    float segment_x, segment_z, segment_sq, segment_position, support_y;
    int advances = 0;
    if (ball->sound_cooldown > 0) ball->sound_cooldown--;
    pinball_ball_route(index, &route, &count);
    dx = route[ball->route_point].x - ball->position.x;
    dz = route[ball->route_point].z - ball->position.z;
    distance_sq = dx * dx + dz * dz;
    while (distance_sq < 0.85f * 0.85f && advances++ < 3) {
        if (ball->route_point + 1 >= count) {
            reset_pinball_ball(index);
            return;
        }
        ball->route_point++;
        dx = route[ball->route_point].x - ball->position.x;
        dz = route[ball->route_point].z - ball->position.z;
        distance_sq = dx * dx + dz * dz;
    }
    distance = sqrtf(fmaxf(distance_sq, 0.000001f));
    ball->velocity.x += dx / distance * 0.0016f;
    ball->velocity.z += dz / distance * 0.0016f;
    ball->velocity.x *= 0.997f;
    ball->velocity.z *= 0.997f;
    horizontal_speed = sqrtf(ball->velocity.x * ball->velocity.x +
                             ball->velocity.z * ball->velocity.z);
    if (horizontal_speed > 0.10f) {
        ball->velocity.x *= 0.10f / horizontal_speed;
        ball->velocity.z *= 0.10f / horizontal_speed;
        horizontal_speed = 0.10f;
    }
    ball->velocity.y -= 0.0035f;
    ball->position.x += ball->velocity.x;
    ball->position.y += ball->velocity.y;
    ball->position.z += ball->velocity.z;
    previous_point = (ball->route_point + count - 1) % count;
    segment_x = route[ball->route_point].x - route[previous_point].x;
    segment_z = route[ball->route_point].z - route[previous_point].z;
    segment_sq = segment_x * segment_x + segment_z * segment_z;
    if (segment_sq > 0.0001f) {
        segment_position = ((ball->position.x - route[previous_point].x) * segment_x +
                            (ball->position.z - route[previous_point].z) * segment_z) / segment_sq;
        if (segment_position < 0.0f) segment_position = 0.0f;
        if (segment_position > 1.0f) segment_position = 1.0f;
    } else {
        segment_position = 1.0f;
    }
    support_y = route[previous_point].y +
        (route[ball->route_point].y - route[previous_point].y) * segment_position +
        PINBALL_BALL_RADIUS;
    if (ball->position.y <= support_y) {
        ball->position.y = support_y;
        if (ball->velocity.y < -0.018f) ball->velocity.y *= -0.30f;
        else ball->velocity.y = 0.0f;
        ball->velocity.x *= 0.992f;
        ball->velocity.z *= 0.992f;
    }
    pinball_flipper_hits_ball(ball, 0, now);
    pinball_flipper_hits_ball(ball, 1, now);
    pinball_bumpers_hit_ball(ball);
    if (segment_sq > 0.0001f) {
        float anchor_x, anchor_z, lateral_x, lateral_z, lateral_sq;
        segment_position = ((ball->position.x - route[previous_point].x) * segment_x +
                            (ball->position.z - route[previous_point].z) * segment_z) / segment_sq;
        if (segment_position < 0.0f) segment_position = 0.0f;
        if (segment_position > 1.0f) segment_position = 1.0f;
        anchor_x = route[previous_point].x + segment_x * segment_position;
        anchor_z = route[previous_point].z + segment_z * segment_position;
        lateral_x = ball->position.x - anchor_x;
        lateral_z = ball->position.z - anchor_z;
        lateral_sq = lateral_x * lateral_x + lateral_z * lateral_z;
        if (lateral_sq > 0.45f * 0.45f) {
            float lateral = sqrtf(lateral_sq);
            float nx = lateral_x / lateral, nz = lateral_z / lateral;
            float outward = ball->velocity.x * nx + ball->velocity.z * nz;
            ball->position.x = anchor_x + nx * 0.45f;
            ball->position.z = anchor_z + nz * 0.45f;
            if (outward > 0.0f) {
                ball->velocity.x -= 1.35f * outward * nx;
                ball->velocity.z -= 1.35f * outward * nz;
            }
        }
    }
    limit_pinball_ball_speed(ball);
    /* MKDS keeps the IronBall sprite facing the racer.  Its position advances
       and bounces, but the picture itself never rolls. */
    ball->spin = 0.0f;
    if (ball->position.y < -8.0f || fabsf(ball->position.x) > 80.0f ||
        fabsf(ball->position.z) > 110.0f)
        reset_pinball_ball(index);
}

static void pinball_separate_balls(void) {
    unsigned int i, j;
    for (i = 0; i < PINBALL_BALL_COUNT; ++i) {
        for (j = i + 1; j < PINBALL_BALL_COUNT; ++j) {
            PinballBall *a = &pinball_balls[i];
            PinballBall *b = &pinball_balls[j];
            float dx = b->position.x - a->position.x;
            float dy = b->position.y - a->position.y;
            float dz = b->position.z - a->position.z;
            float distance_sq = dx * dx + dy * dy + dz * dz;
            float diameter = PINBALL_BALL_RADIUS * 2.0f;
            if (distance_sq < diameter * diameter && distance_sq > 0.000001f) {
                float distance = sqrtf(distance_sq);
                float nx = dx / distance, ny = dy / distance, nz = dz / distance;
                float overlap = (diameter - distance) * 0.5f;
                float relative = (b->velocity.x - a->velocity.x) * nx +
                                 (b->velocity.y - a->velocity.y) * ny +
                                 (b->velocity.z - a->velocity.z) * nz;
                a->position.x -= nx * overlap; a->position.y -= ny * overlap; a->position.z -= nz * overlap;
                b->position.x += nx * overlap; b->position.y += ny * overlap; b->position.z += nz * overlap;
                if (relative < 0.0f) {
                    float impulse = relative * 0.82f;
                    a->velocity.x += nx * impulse; a->velocity.y += ny * impulse; a->velocity.z += nz * impulse;
                    b->velocity.x -= nx * impulse; b->velocity.y -= ny * impulse; b->velocity.z -= nz * impulse;
                }
            }
        }
    }
}

static void pinball_objects_hit_kart(Kart *kart, unsigned long long now) {
    unsigned int i;
    for (i = 0; i < sizeof(pinball_objects) / sizeof(pinball_objects[0]); ++i) {
        const MapObjInstance *object = &pinball_objects[i];
        float object_scale, bumper_radius, bottom, top, dx, dz, distance_sq;
        if (object->model != MAPOBJ_BOUND) continue;
        object_scale = fmaxf(object->scale_x, object->scale_z);
        bumper_radius = object_scale * 0.1615f + 0.28f;
        bottom = object->y - object->scale_y * 0.15625f - 0.15f;
        top = object->y + object->scale_y * 0.21875f + 0.30f;
        if (kart->position.y + 0.15f < bottom || kart->position.y + 0.15f > top) continue;
        dx = kart->position.x - object->x;
        dz = kart->position.z - object->z;
        distance_sq = dx * dx + dz * dz;
        if (distance_sq < bumper_radius * bumper_radius && distance_sq > 0.000001f) {
            float distance = sqrtf(distance_sq);
            float nx = dx / distance, nz = dz / distance;
            float velocity_x = sinf(kart->heading) * kart->speed;
            float velocity_z = cosf(kart->heading) * kart->speed;
            float impact = velocity_x * nx + velocity_z * nz;
            float overlap = bumper_radius - distance;
            float impulse = 0.030f + fminf(overlap * 0.08f, 0.025f);
            if (impact < 0.0f) impulse += fminf(-impact * 0.70f, 0.070f);
            kart_add_push(kart, nx * impulse, nz * impulse, 0.145f);
            if (impact < -0.025f) {
                kart->speed *= 0.96f;
                kart->vertical_speed = 0.018f;
                kart->airborne = 1;
            }
            if (kart->object_hit_cooldown <= 0) {
                sfx_play(SFX_PINBALL_BUMPER);
                kart->object_hit_cooldown = 7;
            }
        }
    }
    for (i = 0; i < PINBALL_BALL_COUNT; ++i) {
        PinballBall *ball = &pinball_balls[i];
        float dx = ball->position.x - kart->position.x;
        float dz = ball->position.z - kart->position.z;
        float distance_sq = dx * dx + dz * dz;
        float contact = PINBALL_BALL_RADIUS + 0.22f;
        if (fabsf(ball->position.y - (kart->position.y + 0.18f)) < 0.55f &&
            distance_sq < contact * contact && distance_sq > 0.000001f) {
            float distance = sqrtf(distance_sq);
            float nx = dx / distance, nz = dz / distance;
            float overlap = contact - distance;
            float kart_vx = sinf(kart->heading) * kart->speed + kart->push_x;
            float kart_vz = cosf(kart->heading) * kart->speed + kart->push_z;
            float closing = (ball->velocity.x - kart_vx) * nx +
                            (ball->velocity.z - kart_vz) * nz;
            float impulse = 0.014f + fminf(fmaxf(-closing, 0.0f) * 0.72f, 0.080f);
            float correction = fminf(overlap * 0.28f, 0.024f);
            ball->position.x += nx * correction;
            ball->position.z += nz * correction;
            ball->velocity.x += nx * impulse * 0.72f;
            ball->velocity.z += nz * impulse * 0.72f;
            ball->velocity.y += fminf(0.010f + impulse * 0.18f, 0.022f);
            kart_add_push(kart, -nx * impulse * 0.92f,
                          -nz * impulse * 0.92f, 0.155f);
            if (closing < -0.055f) kart->speed *= 0.97f;
            if (ball->sound_cooldown <= 0) {
                sfx_play(SFX_PINBALL_BALL_HIT);
                ball->sound_cooldown = 8;
            }
        }
    }
    for (i = 0; i < 2; ++i) {
        const MapObjInstance *object = &pinball_objects[8 + i];
        float angular_speed;
        float rotation = pinball_flipper_rotation(i, now, &angular_speed);
        float direction_x = -cosf(rotation), direction_z = sinf(rotation);
        float relative_x = kart->position.x - object->x;
        float relative_z = kart->position.z - object->z;
        float along = relative_x * direction_x + relative_z * direction_z;
        float closest_x, closest_z, dx, dz, distance_sq;
        if (fabsf(kart->position.y - object->y) > 0.60f) continue;
        if (along < 0.0f) along = 0.0f;
        if (along > 2.85f) along = 2.85f;
        closest_x = object->x + direction_x * along;
        closest_z = object->z + direction_z * along;
        dx = kart->position.x - closest_x;
        dz = kart->position.z - closest_z;
        distance_sq = dx * dx + dz * dz;
        if (distance_sq < 0.88f * 0.88f && distance_sq > 0.000001f) {
            float distance = sqrtf(distance_sq);
            float nx = dx / distance, nz = dz / distance;
            float surface_x = angular_speed * (closest_z - object->z);
            float surface_z = -angular_speed * (closest_x - object->x);
            float impulse = 0.026f + fabsf(angular_speed) * along * 0.72f;
            if (impulse > 0.105f) impulse = 0.105f;
            kart_add_push(kart, nx * impulse + surface_x * 0.48f,
                          nz * impulse + surface_z * 0.48f, 0.165f);
            if (fabsf(angular_speed) > 0.008f) {
                kart->vertical_speed = 0.030f;
                kart->airborne = 1;
            }
            if (kart->object_hit_cooldown <= 0) {
                sfx_play(SFX_PINBALL_FLIPPER);
                kart->object_hit_cooldown = 7;
            }
        }
    }
}

static void update_pinball_objects(Kart *kart, unsigned long long now) {
    unsigned int i;
    for (i = 0; i < PINBALL_BALL_COUNT; ++i) update_pinball_ball(i, now);
    pinball_separate_balls();
    for (i = 0; i < PINBALL_BALL_COUNT; ++i) limit_pinball_ball_speed(&pinball_balls[i]);
    pinball_objects_hit_kart(kart, now);
}

static void ground_height_range(const CourseModel *ground, unsigned int start, unsigned int count,
                                float x, float z, float current_y,
                                int *found, float *best, float *best_delta) {
    unsigned int i, end = start + count;
    for (i = start; i + 2 < end; i += 3) {
        const CourseVertex *a = &ground->vertices[i];
        const CourseVertex *b = &ground->vertices[i + 1];
        const CourseVertex *c = &ground->vertices[i + 2];
        float denominator = (b->z - c->z) * (a->x - c->x) + (c->x - b->x) * (a->z - c->z);
        float wa, wb, wc, y;
        if (fabsf(denominator) < 0.00001f) continue;
        wa = ((b->z - c->z) * (x - c->x) + (c->x - b->x) * (z - c->z)) / denominator;
        wb = ((c->z - a->z) * (x - c->x) + (a->x - c->x) * (z - c->z)) / denominator;
        wc = 1.0f - wa - wb;
        if (wa < -0.001f || wb < -0.001f || wc < -0.001f) continue;
        y = wa * a->y + wb * b->y + wc * c->y;
        {
            float delta = fabsf(y - current_y);
            if (delta <= 0.65f && delta < *best_delta) {
                *best = y; *best_delta = delta; *found = 1;
            }
        }
    }
}

static int ground_height(float x, float z, float current_y, float *height) {
    unsigned int chunk;
    int found = 0;
    float best = 0.0f, best_delta = 100000.0f;
    const CourseModel *ground;
    if (active_kcl) return kcl_ground_height(x,z,current_y,height);
    ground = active_collision->vertices ? active_collision : &course_model;
    if (!ground->vertices) return 0;
    if (ground->chunks) {
        for (chunk = 0; chunk < ground->chunk_count; ++chunk) {
            const CourseChunk *bounds = &ground->chunks[chunk];
            if (x < bounds->min_x - 0.002f || x > bounds->max_x + 0.002f ||
                z < bounds->min_z - 0.002f || z > bounds->max_z + 0.002f) continue;
            ground_height_range(ground, bounds->start, bounds->count, x, z, current_y,
                                &found, &best, &best_delta);
        }
    } else {
        ground_height_range(ground, 0, ground->count, x, z, current_y,
                            &found, &best, &best_delta);
    }
    if (found) *height = best;
    return found;
}

static int model_surface_at(const CourseModel *model, float x, float z, float y) {
    unsigned int chunk;
    if (!model->vertices) return 0;
    for (chunk = 0; chunk < (model->chunks ? model->chunk_count : 1); ++chunk) {
        unsigned int i, start = model->chunks ? model->chunks[chunk].start : 0;
        unsigned int end = start + (model->chunks ? model->chunks[chunk].count : model->count);
        if (model->chunks) {
            const CourseChunk *bounds = &model->chunks[chunk];
            if (x < bounds->min_x || x > bounds->max_x || z < bounds->min_z || z > bounds->max_z) continue;
        }
        for (i = start; i + 2 < end; i += 3) {
            const CourseVertex *a = &model->vertices[i], *b = &model->vertices[i+1], *c = &model->vertices[i+2];
            float denominator = (b->z-c->z)*(a->x-c->x) + (c->x-b->x)*(a->z-c->z);
            float wa, wb, wc, surface_y;
            if (fabsf(denominator) < 0.00001f) continue;
            wa = ((b->z-c->z)*(x-c->x) + (c->x-b->x)*(z-c->z)) / denominator;
            wb = ((c->z-a->z)*(x-c->x) + (a->x-c->x)*(z-c->z)) / denominator;
            wc = 1.0f-wa-wb;
            if (wa < -0.002f || wb < -0.002f || wc < -0.002f) continue;
            surface_y = wa*a->y + wb*b->y + wc*c->y;
            if (fabsf(surface_y-y) < 0.12f) return 1;
        }
    }
    return 0;
}

static float offroad_speed_factor(float x, float z, float y) {
    /* Material 4 is RoadDirt: it is a legitimate section of Mario Circuit,
       not off-road. Dirt outside the course, sand and grass remain slow. */
    static const unsigned char slow_materials[] = {20, 21};
    unsigned int i;
    /* Native MKDS KCL surface classes. Luigi's Mansion marks its mud/grass as
       type 3, so the slowdown follows the actual collision boundary instead
       of the wider visual grass mesh. Keep the lighter/heavier types ready for
       other converted DS tracks as well. */
    if (active_kcl) {
        if (kcl_touched_type == 2) return 0.70f; /* weak off-road */
        if (kcl_touched_type == 3) return 0.52f; /* off-road */
        if (kcl_touched_type == 5) return 0.36f; /* heavy off-road */
        return 0.0f;
    }
    /* The road wins at shared seams, preventing a one-frame slowdown where
       asphalt and grass triangles meet. */
    if (active_course != 0) return 0.0f;
    if (model_surface_at(&course_batches[19], x, z, y)) return 0.0f;
    for (i = 0; i < sizeof(slow_materials); ++i)
        if (model_surface_at(&course_batches[slow_materials[i]], x, z, y)) return 0.42f;
    return 0.0f;
}

static float point_segment_distance_sq(float px, float pz, float ax, float az, float bx, float bz) {
    float dx = bx - ax, dz = bz - az;
    float length_sq = dx * dx + dz * dz;
    float amount = length_sq > 0.000001f ? ((px - ax) * dx + (pz - az) * dz) / length_sq : 0.0f;
    float x, z;
    if (amount < 0.0f) amount = 0.0f;
    if (amount > 1.0f) amount = 1.0f;
    x = ax + amount * dx; z = az + amount * dz;
    dx = px - x; dz = pz - z;
    return dx * dx + dz * dz;
}

static int hits_wall_range(const CourseModel *collision, unsigned int start, unsigned int count,
                           float x, float y, float z) {
    unsigned int i, end = start + count;
    const float radius_sq = 0.18f * 0.18f;
    for (i = start; i + 2 < end; i += 3) {
        const CourseVertex *a = &collision->vertices[i];
        const CourseVertex *b = &collision->vertices[i + 1];
        const CourseVertex *c = &collision->vertices[i + 2];
        float ux = b->x - a->x, uy = b->y - a->y, uz = b->z - a->z;
        float vx = c->x - a->x, vy = c->y - a->y, vz = c->z - a->z;
        float nx = uy * vz - uz * vy;
        float ny = uz * vx - ux * vz;
        float nz = ux * vy - uy * vx;
        float horizontal = nx * nx + nz * nz;
        float min_y = fminf(a->y, fminf(b->y, c->y));
        float max_y = fmaxf(a->y, fmaxf(b->y, c->y));
        /* Ignore floor/roof faces; keep nearly vertical barrier faces. */
        if (horizontal < ny * ny * 2.0f) continue;
        if (y + 0.35f < min_y || y > max_y + 0.10f) continue;
        if (point_segment_distance_sq(x, z, a->x, a->z, b->x, b->z) < radius_sq ||
            point_segment_distance_sq(x, z, b->x, b->z, c->x, c->z) < radius_sq ||
            point_segment_distance_sq(x, z, c->x, c->z, a->x, a->z) < radius_sq) return 1;
    }
    return 0;
}

static int hits_wall(float x, float y, float z) {
    unsigned int chunk;
    const float radius = 0.18f;
    if (active_kcl)
        /* The kart body is narrower than the old spherical 0.18 shell.  That
           shell made guard rails feel much thicker than their visible model. */
        return kcl_prism_contact(2, x, y + 0.18f, z, 0.135f);
    if (!active_collision->vertices) return 0;
    if (!active_collision->chunks)
        return hits_wall_range(active_collision, 0, active_collision->count, x, y, z);
    for (chunk = 0; chunk < active_collision->chunk_count; ++chunk) {
        const CourseChunk *bounds = &active_collision->chunks[chunk];
        if (x < bounds->min_x - radius || x > bounds->max_x + radius ||
            z < bounds->min_z - radius || z > bounds->max_z + radius) continue;
        if (hits_wall_range(active_collision, bounds->start, bounds->count, x, y, z)) return 1;
    }
    return 0;
}

static int hits_wall_path(float old_x, float old_y, float old_z,
                          float x, float y, float z) {
    if (active_kcl) {
        Vec3 from = {old_x, old_y + 0.18f, old_z};
        Vec3 to = {x, y + 0.18f, z};
        if (kcl_wall_sweep(from, to, 0.135f)) return 1;
    }
    kcl_wall_hit_t = 0.0f;
    return hits_wall(x, y, z);
}

static int pinball_ramp_zone(float x, float z) {
    /* KCL side prisms slightly overlap the four jump ramps.  Recognise their
       complete footprints (plus one kart radius) before testing those walls,
       including the narrow launcher ramp at the end of the starting straight. */
    if (x > 17.70f && x < 22.00f && z > 37.15f && z < 40.25f) return 1;
    if (x >  7.05f && x < 10.30f && z > -63.45f && z < -60.05f) return 1;
    if (x > 13.50f && x < 17.35f && z > -63.15f && z < -59.20f) return 1;
    if (x > -1.10f && x <  3.30f && z > -62.10f && z < -58.10f) return 1;
    if (x > -13.95f && x < -10.10f && z > -9.60f && z < -6.55f) return 1;
    return 0;
}

static void closest_point_on_floor_edge(Vec3 point, Vec3 a, Vec3 b,
                                        Vec3 *closest, float *distance_sq) {
    float dx = b.x - a.x, dz = b.z - a.z;
    float denominator = dx * dx + dz * dz;
    float t = denominator > 0.000001f ?
        ((point.x - a.x) * dx + (point.z - a.z) * dz) / denominator : 0.0f;
    Vec3 candidate;
    float ex, ey, ez, candidate_distance;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    candidate.x = a.x + dx * t;
    candidate.y = a.y + (b.y - a.y) * t;
    candidate.z = a.z + dz * t;
    ex = candidate.x - point.x;
    ey = candidate.y - point.y;
    ez = candidate.z - point.z;
    candidate_distance = ex * ex + ey * ey + ez * ez;
    if (candidate_distance < *distance_sq) {
        *distance_sq = candidate_distance;
        *closest = candidate;
    }
}

static int nearest_pinball_floor(Vec3 point, Vec3 *target) {
    unsigned int i;
    int found = 0;
    float best_distance = 1000000000.0f;
    Vec3 best = point, best_centroid = point;
    for (i = 0; i < active_kcl_count; ++i) {
        const KclTriangle *triangle = &active_kcl[i];
        Vec3 candidate, centroid;
        float distance_sq = 1000000000.0f;
        float height;
        if ((triangle->category != 1 && triangle->category != 4) ||
            triangle->normal.y < 0.45f) continue;
        centroid.x = (triangle->a.x + triangle->b.x + triangle->c.x) / 3.0f;
        centroid.y = (triangle->a.y + triangle->b.y + triangle->c.y) / 3.0f;
        centroid.z = (triangle->a.z + triangle->b.z + triangle->c.z) / 3.0f;
        if (kcl_xz_height(triangle, point.x, point.z, &height)) {
            float dy = height - point.y;
            candidate.x = point.x;
            candidate.y = height;
            candidate.z = point.z;
            distance_sq = dy * dy;
        } else {
            closest_point_on_floor_edge(point, triangle->a, triangle->b,
                                        &candidate, &distance_sq);
            closest_point_on_floor_edge(point, triangle->b, triangle->c,
                                        &candidate, &distance_sq);
            closest_point_on_floor_edge(point, triangle->c, triangle->a,
                                        &candidate, &distance_sq);
        }
        if (distance_sq < best_distance) {
            best_distance = distance_sq;
            best = candidate;
            best_centroid = centroid;
            found = 1;
        }
    }
    if (found) {
        /* Pull the edge result inside the triangle so the recovery never ends
           balanced on the same barrier from which the kart fell. */
        target->x = best.x * 0.78f + best_centroid.x * 0.22f;
        target->y = best.y * 0.78f + best_centroid.y * 0.22f + 0.03f;
        target->z = best.z * 0.78f + best_centroid.z * 0.22f;
    }
    return found;
}

static void update_kart(Kart *kart, const SceCtrlData *pad, const KartPhysics *physics, int noclip) {
    float throttle = (pad->Buttons & (PSP_CTRL_SQUARE | PSP_CTRL_CROSS)) ? physics->acceleration : 0.0f;
    float brake = (pad->Buttons & PSP_CTRL_CIRCLE) ? physics->braking : 0.0f;
    int analog = (int)pad->Lx - 128;
    float steering = 0.0f;
    float old_x = kart->position.x, old_y = kart->position.y, old_z = kart->position.z;
    float movement_heading;
    float floor;
    int r_down = (pad->Buttons & PSP_CTRL_RTRIGGER) != 0;
    int r_pressed = r_down && !kart->r_was_down;
    int l_down = (pad->Buttons & PSP_CTRL_LTRIGGER) != 0;
    int l_pressed = l_down && !kart->l_was_down;
    int up_down = (pad->Buttons & PSP_CTRL_UP) != 0;
    int up_pressed = up_down && !kart->up_was_down;
    int ramp_surface = kcl_touched_type == 18 || kcl_touched_normal_y < 0.92f;
    int ramp_trick_requested = active_course == 1 && !kart->airborne &&
        (r_pressed || up_pressed);
    /* R launches from real ramps; on flat ground it remains the regular hop.
       Up is kept as an alternate trick button on the same surfaces. */
    int ramp_trick_pressed = ramp_trick_requested && ramp_surface;
    int floor_found = 0;
    int floor_is_ramp = 0;
    if (active_kcl) kcl_touched_id = -1;
    if (!noclip && kart->rescue_frames > 0) {
        int elapsed = kart->rescue_total_frames - kart->rescue_frames + 1;
        float t = (float)elapsed / (float)kart->rescue_total_frames;
        float smooth = t * t * (3.0f - 2.0f * t);
        float dx = kart->rescue_target.x - kart->rescue_start.x;
        float dz = kart->rescue_target.z - kart->rescue_start.z;
        float horizontal = sqrtf(dx * dx + dz * dz);
        float arc = 0.65f + horizontal * 0.16f;
        if (arc > 3.0f) arc = 3.0f;
        kart->position.x = kart->rescue_start.x + dx * smooth;
        kart->position.y = kart->rescue_start.y +
            (kart->rescue_target.y - kart->rescue_start.y) * smooth +
            sinf(t * 3.1415927f) * arc;
        kart->position.z = kart->rescue_start.z + dz * smooth;
        if (horizontal > 0.01f) kart->heading = atan2f(dx, dz);
        kart->speed = 0.0f;
        kart->push_x = kart->push_z = 0.0f;
        kart->vertical_speed = 0.0f;
        kart->airborne = 1;
        kart->drifting = 0;
        kart->pitch *= 0.82f;
        kart->roll *= 0.82f;
        kart->camera_y += (kart->position.y - kart->camera_y) * 0.22f;
        if (kart->position.y - kart->camera_y > 1.0f)
            kart->camera_y = kart->position.y - 1.0f;
        if (kart->position.y - kart->camera_y < -1.0f)
            kart->camera_y = kart->position.y + 1.0f;
        kart->rescue_frames--;
        if (kart->rescue_frames <= 0) {
            kart->position = kart->rescue_target;
            kart->camera_y = kart->position.y;
            kart->airborne = 0;
            kart->hop_camera_lock = 0;
            kart->speed = physics->max_speed * 0.55f;
            kart->last_safe_position = kart->position;
            kart->last_safe_heading = kart->heading;
            kart->has_safe_position = 1;
            if (kart->cannon_state == -2) kart->cannon_state = -1;
        }
        kart->r_was_down = r_down;
        kart->l_was_down = l_down;
        kart->up_was_down = up_down;
        return;
    }
    /* Waluigi Pinball: the launcher first accelerates the kart through the
       tube, then its KCL cannon plane applies a real ballistic impulse. */
    if (active_course == 1 && !noclip) {
        if (kart->cannon_state < 0 && kart->position.z > 45.0f) kart->cannon_state = 0;
        /* Enter the launcher as soon as the kart reaches its real KCL ramp.
           Waiting for the floor query at the end of the frame let a narrow
           side wall reject the movement first, which stopped the kart on the
           lip instead of starting the cannon. */
        if (kart->cannon_state == 0 &&
            kart->position.x > 17.75f && kart->position.x < 21.95f &&
            kart->position.z < 40.15f && kart->position.z > 37.25f) {
            kart->cannon_state = 2;
            kart->speed = 0.32f;
            kart->push_x = kart->push_z = 0.0f;
            kart->heading = 3.1415927f;
            kart->airborne = 0;
            kart->vertical_speed = 0.0f;
            kart->hop_height = 0.0f;
            kart->hop_camera_lock = 0;
        }
        if (kart->cannon_state == 2 &&
            kcl_prism_contact(5, kart->position.x, kart->position.y + 0.18f,
                              kart->position.z, 0.70f)) {
            kart->cannon_state = 1;
            kart->drifting = 0;
            kart->airborne = 1;
            kart->speed = 0.65f;
            kart->vertical_speed = 0.29f;
            kart->heading = 3.1415927f;
            kart->camera_y = kart->position.y;
            sfx_play(SFX_PINBALL_LAUNCH);
        }
        if (kart->cannon_state == 1) {
            float landing;
            kart->position.x += sinf(kart->heading)*kart->speed;
            kart->position.z += cosf(kart->heading)*kart->speed;
            kart->vertical_speed -= 0.0006f;
            kart->position.y += kart->vertical_speed;
            kart->pitch = -atan2f(kart->vertical_speed,kart->speed);
            kart->roll *= 0.8f;
            kart->r_was_down = r_down;
            kart->l_was_down = l_down;
            kart->up_was_down = up_down;
            /* The tube climbs about 0.29 unit per frame.  A smoothed camera
               lagged more than two world units behind and pushed Mario to the
               very top of the screen.  Following the cannon height exactly
               keeps the chase camera centred while its X/Z view still moves. */
            kart->camera_y = kart->position.y;
            if (kart->position.z <= -43.0f) {
                kart->cannon_state = -2;
                kart->vertical_speed = -0.180f;
                kart->speed = 0.22f;
                kart->pitch = 0.0f;
                kart->ramp_grace_frames = 48;
                return;
            }
            if (kart->position.z < -35.0f && kart->vertical_speed < 0.0f &&
                kcl_ground_height(kart->position.x,kart->position.z,kart->position.y,&landing) &&
                kart->position.y <= landing+0.25f) {
                kart->cannon_state = -1;
                kart->position.y = landing;
                kart->vertical_speed = 0.0f;
                kart->airborne = 0;
                kart->pitch = 0.0f;
                kart->speed = physics->max_speed;
                kart->inertia_frames = 0;
            }
            return;
        }
    }
    if (analog > 18) {
        steering = (analog - 18) / 109.0f;
        steering *= steering;
    } else if (analog < -18) {
        steering = (analog + 18) / 110.0f;
        steering = -steering * steering;
    }
    if (pad->Buttons & PSP_CTRL_LEFT) steering = 1.0f;
    if (pad->Buttons & PSP_CTRL_RIGHT) steering = -1.0f;
    if (!(pad->Buttons & (PSP_CTRL_LEFT | PSP_CTRL_RIGHT))) steering = -steering;
    if (kart->cannon_state == 2) {
        steering = 0.0f;
        kart->position.x += (19.84766f - kart->position.x) * 0.22f;
    }
    if (kart->secret_jump_frames > 0) steering = 0.0f;
    kart->wheel_steering += (steering - kart->wheel_steering) * 0.25f;


    kart->speed += throttle;
    kart->speed -= brake;
    if (!throttle && !brake && kart->cannon_state != -2 && kart->inertia_frames <= 0)
        kart->speed *= 0.9838867f;
    if (kart->cannon_state == 2) {
        kart->speed = 0.32f;
        kart->heading = 3.1415927f;
        kart->airborne = 0;
        kart->vertical_speed = 0.0f;
        kart->hop_height = 0.0f;
        kart->hop_camera_lock = 0;
    }
    if (kart->secret_jump_frames > 0) {
        /* Keep the shortcut deterministic despite throttle or boost input. */
        kart->speed = 0.32f;
        kart->drifting = 0;
    }
    if (!noclip && l_pressed && kart->item_count > 0) {
        kart->item_count--;
        if (kart->boost_frames < 130.0f) kart->boost_frames = 130.0f;
        if (kart->boost_limit < 1.60f) kart->boost_limit = 1.60f;
        if (kart->speed < physics->max_speed * 1.40f)
            kart->speed = physics->max_speed * 1.40f;
        sfx_play(SFX_ITEM);
        if (physics != &physics_gros_cube) sfx_play((enum SfxId)(SFX_MARIO_0 + (kart->item_count * 3 + 6) % 11));
    }
    /* Mario Kart hop: R first lifts the kart, then holding it with a direction
       starts the drift.  The edge check prevents repeated hops while R is held. */
    if (!noclip && kart->cannon_state != 2 && r_pressed && !ramp_trick_pressed &&
        !kart->airborne && fabsf(kart->speed) > physics->max_speed * 0.18f) {
        kart->vertical_speed = 0.035f;
        kart->airborne = 1;
        kart->hop_camera_lock = 1;
        kart->hop_height = 0.0f;
        sfx_play(SFX_HOP);
    }
    if (!noclip && kart->cannon_state != 2 && !kart->drifting && r_down &&
        fabsf(steering) > 0.18f && fabsf(kart->speed) > physics->max_speed * 0.30f) {
        kart->drifting = 1;
        kart->drift_direction = steering > 0.0f ? 1 : -1;
        kart->drift_charge = 0.0f;
    }
    if (kart->drifting) {
        if (!r_down || noclip) {
            if (kart->drift_charge >= 2.50f) {
                kart->boost_frames = 175.0f;
                kart->boost_limit = 1.58f;
            } else if (kart->drift_charge >= 1.00f) {
                kart->boost_frames = 115.0f;
                kart->boost_limit = 1.45f;
            } else if (kart->drift_charge >= 0.40f) {
                kart->boost_frames = 48.0f;
                kart->boost_limit = 1.32f;
            }
            if (kart->drift_charge >= 0.40f) {
                sfx_play(SFX_DRIFT_BOOST);
                if (physics != &physics_gros_cube)
                    sfx_play((enum SfxId)(SFX_MARIO_0 + ((int)(kart->drift_charge * 7.0f) % 11)));
            }
            kart->drifting = 0;
            kart->drift_charge = 0.0f;
        } else {
            float into_turn = steering * kart->drift_direction;
            /* Full purple needs roughly three seconds of useful steering.
               Countersteering still charges, but much more slowly. */
            kart->drift_charge += 0.0035f + (into_turn > 0.25f ? 0.0105f : 0.0015f);
        }
    }
    if (kart->boost_frames > 0.0f) {
        kart->boost_frames -= 1.0f;
        if (kart->boost_limit < 1.01f) kart->boost_limit = 1.32f;
        kart->speed += physics->acceleration *
            (2.0f + (kart->boost_limit - 1.0f) * 2.0f);
        if (kart->boost_frames <= 0.0f) kart->boost_limit = 1.0f;
    }
    if (kart->secret_jump_frames > 0) kart->speed = 0.32f;
    {
        float speed_limit = physics->max_speed *
            (kart->boost_frames > 0.0f ? fmaxf(kart->boost_limit, 1.32f) : 1.0f);
        float offroad_factor = offroad_speed_factor(
            kart->position.x, kart->position.z, kart->position.y);
        if (!noclip && kart->secret_jump_frames <= 0 &&
            physics != &physics_gros_cube && kart->boost_frames <= 0.0f &&
            offroad_factor > 0.0f) {
            float offroad_limit = physics->max_speed * offroad_factor;
            kart->speed *= 0.94f;
            if (kart->speed > offroad_limit) kart->speed = offroad_limit;
            if (kart->speed < -offroad_limit * 0.55f) kart->speed = -offroad_limit * 0.55f;
        }
        if (kart->cannon_state != 2 && kart->cannon_state != -2 &&
            kart->secret_jump_frames <= 0 &&
            kart->inertia_frames <= 0 && kart->speed > speed_limit) kart->speed = speed_limit;
    }
    if (kart->speed < -physics->max_speed * 0.32f) kart->speed = -physics->max_speed * 0.32f;
    if (kart->drifting) {
        float countersteer = steering * kart->drift_direction;
        float handling_scale = physics->handling / 0.040f;
        float drift_turn;
        if (handling_scale > 1.25f) handling_scale = 1.25f;
        /* Three distinct drift lines: steering into the drift tightens it,
           neutral keeps a medium arc, and countersteering almost straightens
           the kart.  This applies equally to the analog stick and D-pad. */
        if (countersteer > 0.25f) {
            /* Steering into the slide must be tighter than ordinary steering. */
            drift_turn = kart->drift_direction * physics->drift_handling;
        } else {
            drift_turn = kart->drift_direction *
                (0.012f + countersteer * 0.011f) * handling_scale;
        }
        if (drift_turn > 0.075f) drift_turn = 0.075f;
        if (drift_turn < -0.075f) drift_turn = -0.075f;
        kart->heading += drift_turn * (kart->speed / physics->max_speed);
        /* Outside drift: the kart points into the corner while its velocity
           remains diagonally toward the outside. Countersteering reduces it. */
        movement_heading = kart->heading - kart->drift_direction *
            (0.20f - fmaxf(-0.07f, fminf(0.10f, countersteer * 0.10f)));
    } else {
        kart->heading += steering * physics->handling * (kart->speed / physics->max_speed);
        movement_heading = kart->heading;
    }
    kart->position.x += sinf(movement_heading) * kart->speed + kart->push_x;
    kart->position.z += cosf(movement_heading) * kart->speed + kart->push_z;
    if (active_course == 2 && !kart->airborne) {
        unsigned int tree;
        float object_time = (float)(sceKernelGetSystemTimeWide() % 8000000ULL) / 1000000.0f;
        for (tree = 0; tree < sizeof(mansion_objects) / sizeof(mansion_objects[0]); ++tree) {
            const MapObjInstance *object = &mansion_objects[tree];
            if (object->model != MANSION_MOVE_TREE) continue;
            float tree_x = object->x + sinf(object_time * 0.55f + (float)tree) * 0.12f;
            float tree_z = object->z + cosf(object_time * 0.55f + (float)tree) * 0.12f;
            float dx = kart->position.x - tree_x;
            float dz = kart->position.z - tree_z;
            float distance_sq = dx * dx + dz * dz;
            if (distance_sq < 0.25f) {
                float distance = sqrtf(distance_sq);
                if (distance < 0.001f) {
                    dx = -sinf(kart->heading);
                    dz = -cosf(kart->heading);
                    distance = 1.0f;
                }
                /* A walking tree shoves the kart away over subsequent frames;
                   it never rewrites the kart position, avoiding teleportation. */
                kart->push_x += dx / distance * 0.035f;
                kart->push_z += dz / distance * 0.035f;
                kart->speed *= 0.94f;
                if (kart->object_hit_cooldown <= 0) {
                    sfx_play(SFX_MANSION_TREE);
                    kart->object_hit_cooldown = 20;
                }
            }
        }
    }
    kart->push_x *= 0.86f;
    kart->push_z *= 0.86f;
    if (fabsf(kart->push_x) < 0.001f) kart->push_x = 0.0f;
    if (fabsf(kart->push_z) < 0.001f) kart->push_z = 0.0f;
    /* The table exit has its own MKDS-style "ball lost" cue.  Arm it only
       after the kart has actually entered the table so the nearby start line
       cannot trigger it when a race begins. */
    if (active_course == 1) {
        if (kart->position.z < 44.0f && kart->position.y < 15.0f)
            kart->pinball_exit_armed = 1;
        if (kart->pinball_exit_armed && old_z < 48.0f &&
            kart->position.z >= 48.0f && kart->position.x > -12.0f &&
            kart->position.x < 14.0f) {
            sfx_play(SFX_PINBALL_EXIT);
            kart->pinball_exit_armed = 0;
        }
    }
    if (active_course==1 && kart->cannon_state==2 && pinball_kcl) {
        Vec3 previous={old_x,kart->position.y,old_z};
        Vec3 current=kart->position;
        if (kcl_cannon_sweep(previous,current,0.70f)) {
            kart->cannon_state=1;
            kart->drifting=0; kart->airborne=1;
            kart->speed=0.65f; kart->vertical_speed=0.29f; kart->heading=3.1415927f;
            kart->camera_y=kart->position.y;
            sfx_play(SFX_PINBALL_LAUNCH);
            kart->r_was_down=r_down; kart->l_was_down=l_down;
            return;
        }
    }
    /* Keep the phase small and advance it slowly enough to remain readable at
       60 Hz.  Letting this angle grow forever eventually loses float precision
       and made the wheels appear to teleport, just like the old Mario loop. */
    /* Preserve proportional wheel speed but keep the per-frame phase below the
       temporal aliasing threshold (the exact scale produced a wagon-wheel
       illusion and looked reversed on the 60 Hz PSP screen). */
    kart->wheel_distance += kart->speed * 0.15f;
    if (noclip) {
        if (pad->Buttons & PSP_CTRL_LTRIGGER) kart->position.y += 0.08f;
        if (pad->Buttons & PSP_CTRL_RTRIGGER) kart->position.y -= 0.08f;
        kart->r_was_down = r_down;
        kart->l_was_down = l_down;
        kart->up_was_down = up_down;
        kart->pitch *= 0.85f;
        kart->roll *= 0.85f;
        kart->camera_y += (kart->position.y - kart->camera_y) * 0.18f;
        return;
    }
    /* Resolve the driveable surface before walls.  On the old path, the side
       prism at a ramp lip could reverse the kart before type 18 was known. */
    floor_found = ground_height(kart->position.x, kart->position.z,
                                kart->position.y, &floor);
    floor_is_ramp = floor_found && active_course == 1 &&
        (kcl_touched_type == 18 || kcl_touched_normal_y < 0.92f);
    if (active_course == 1 && pinball_ramp_zone(kart->position.x, kart->position.z))
        floor_is_ramp = 1;
    if (ramp_trick_requested && floor_is_ramp) ramp_trick_pressed = 1;
    if (floor_is_ramp && kart->ramp_grace_frames < 12)
        kart->ramp_grace_frames = 12;

    if ((!kart->airborne || (floor_found && floor >= kart->position.y - 0.45f)) &&
        kart->cannon_state != 2 && kart->cannon_state != -2 &&
        kart->ramp_grace_frames <= 0 && !floor_is_ramp &&
        !kart->hop_camera_lock && !ramp_trick_pressed &&
        hits_wall_path(old_x, old_y, old_z,
                       kart->position.x, kart->position.y, kart->position.z)) {
        float step_floor;
        float wall_nx = kcl_touched_normal_x;
        float wall_nz = kcl_touched_normal_z;
        float wall_t = kcl_wall_hit_t;
        int can_step = ground_height(kart->position.x, kart->position.z, old_y, &step_floor) &&
            step_floor > old_y + 0.005f && step_floor <= old_y + 0.30f;
        if (can_step) {
            kart->position.y = step_floor;
        } else if (active_kcl) {
            float length = sqrtf(wall_nx * wall_nx + wall_nz * wall_nz);
            float movement_x = kart->position.x - old_x;
            float movement_z = kart->position.z - old_z;
            float movement_length = sqrtf(movement_x * movement_x + movement_z * movement_z);
            float safe_t, remaining, slide_x, slide_z, into_wall, push_into;
            if (length > 0.001f) {
                wall_nx /= length;
                wall_nz /= length;
            } else if (movement_length > 0.001f) {
                wall_nx = -movement_x / movement_length;
                wall_nz = -movement_z / movement_length;
            }
            if (movement_x * wall_nx + movement_z * wall_nz > 0.0f) {
                wall_nx = -wall_nx;
                wall_nz = -wall_nz;
            }
            if (wall_t < 0.0f) wall_t = 0.0f;
            if (wall_t > 1.0f) wall_t = 1.0f;
            safe_t = wall_t;
            if (movement_length > 0.001f)
                safe_t -= 0.008f / movement_length;
            if (safe_t < 0.0f) safe_t = 0.0f;
            remaining = 1.0f - wall_t;
            slide_x = movement_x * remaining;
            slide_z = movement_z * remaining;
            into_wall = slide_x * wall_nx + slide_z * wall_nz;
            if (into_wall < 0.0f) {
                slide_x -= into_wall * wall_nx;
                slide_z -= into_wall * wall_nz;
            }
            kart->position.x = old_x + movement_x * safe_t + slide_x * 0.97f;
            kart->position.z = old_z + movement_z * safe_t + slide_z * 0.97f;
            push_into = kart->push_x * wall_nx + kart->push_z * wall_nz;
            if (push_into < 0.0f) {
                kart->push_x -= push_into * wall_nx;
                kart->push_z -= push_into * wall_nz;
            }
            if (movement_x * wall_nx + movement_z * wall_nz < -0.020f) {
                kart->speed *= 0.90f;
                kart->drifting = 0;
            } else {
                kart->speed *= 0.98f;
            }
            floor_found = ground_height(kart->position.x, kart->position.z,
                                        kart->position.y, &floor);
            if (kart->object_hit_cooldown <= 0) {
                sfx_play(SFX_WALL_HIT);
                kart->object_hit_cooldown = 6;
            }
        } else {
            kart->position.x = old_x;
            kart->position.z = old_z;
            kart->speed *= -0.20f;
            sfx_play(SFX_WALL_HIT);
            kart->r_was_down = r_down;
            kart->l_was_down = l_down;
            return;
        }
    }
    if (floor_found) {
        if (active_course == 1 && kcl_touched_type == 18 && kart->boost_frames < 100.0f) {
            if (kart->boost_frames <= 0.0f) sfx_play(SFX_PINBALL_DASH);
            kart->boost_frames = 100.0f;
            if (kart->boost_limit < 1.48f) kart->boost_limit = 1.48f;
            if (kart->speed < physics->max_speed * 1.32f)
                kart->speed = physics->max_speed * 1.32f;
        }
        if (active_course == 1 && kart->cannon_state == 0 &&
            kcl_touched_type == 18 && kcl_touched_variant == 3) {
            kart->cannon_state = 2;
            kart->speed = 0.32f;
            kart->heading = 3.1415927f;
            kart->airborne = 0;
            kart->vertical_speed = 0.0f;
            kart->hop_height = 0.0f;
            kart->hop_camera_lock = 0;
        }
        /* Deliberate secret line: hold right on the extreme outer edge and
           reach the final lip of the large rainbow ramp.  Checking the lip in
           the current travel direction prevents an easy trigger halfway up. */
        if (active_course == 1 && kart->secret_jump_frames <= 0 &&
            kcl_touched_type == 18 && kcl_touched_variant == 2 &&
            kart->position.x < -13.10f && kart->position.x > -13.85f &&
            steering < -0.60f &&
            ((cosf(kart->heading) >= 0.0f && kart->position.z > -7.25f) ||
             (cosf(kart->heading) < 0.0f && kart->position.z < -8.92f))) {
            const float target_x = -5.90f;
            const float target_y = 7.70f;
            const float target_z = 40.50f;
            const float shortcut_speed = 0.32f;
            float dx = target_x - kart->position.x;
            float dz = target_z - kart->position.z;
            float distance = sqrtf(dx * dx + dz * dz);
            int frames = (int)(distance / shortcut_speed + 0.5f);
            if (frames < 1) frames = 1;
            kart->position.y = floor + 0.02f;
            kart->heading = atan2f(dx, dz);
            kart->speed = shortcut_speed;
            kart->push_x = kart->push_z = 0.0f;
            kart->vertical_speed = (target_y - kart->position.y +
                0.0046f * frames * (frames + 1) * 0.5f) / frames;
            kart->airborne = 1;
            kart->hop_camera_lock = 1;
            kart->hop_height = 0.0f;
            kart->secret_jump_frames = frames;
            kart->ramp_grace_frames = frames + 20;
            kart->inertia_frames = frames;
            kart->drifting = 0;
            sfx_play(SFX_CANNON);
        }
        if (kart->secret_jump_frames <= 0 && ramp_trick_pressed) {
            float ramp_rise = floor - old_y;
            float launch_speed;
            if (ramp_rise < 0.0f) ramp_rise = 0.0f;
            launch_speed = 0.052f + ramp_rise;
            if (launch_speed > 0.105f) launch_speed = 0.105f;
            kart->vertical_speed = launch_speed;
            kart->airborne = 1;
            kart->hop_camera_lock = 0;
            kart->hop_height = 0.0f;
            kart->ramp_grace_frames = 36;
            if (kart->boost_frames < 120.0f) kart->boost_frames = 120.0f;
            if (kart->boost_limit < 1.58f) kart->boost_limit = 1.58f;
            if (kart->speed < physics->max_speed * 1.42f)
                kart->speed = physics->max_speed * 1.42f;
            sfx_play(SFX_BOOST);
        }
        if (kart->airborne || floor < kart->position.y - 0.10f) {
            kart->airborne = 1;
            /* One world-space ballistic model for hops and ramp jumps.  The
               previous hop branch stored height relative to the floor, so the
               kart followed slopes instead of rising and falling under gravity. */
            kart->vertical_speed -= 0.0046f;
            kart->position.y += kart->vertical_speed;
            if (kart->position.y <= floor) {
                if (kart->vertical_speed > 0.0f) {
                    /* Do not let a rising ramp penetrate an ascending kart;
                       keep its velocity so it leaves the lip ballistically. */
                    kart->position.y = floor + 0.01f;
                } else {
                    kart->position.y = floor;
                    kart->vertical_speed = 0.0f;
                    kart->airborne = 0;
                    kart->secret_jump_frames = 0;
                    kart->hop_height = 0.0f;
                    kart->hop_camera_lock = 0;
                    sfx_play(SFX_LANDING);
                    if (kart->cannon_state == -2) {
                        kart->cannon_state = -1;
                        kart->speed = physics->max_speed;
                        kart->inertia_frames = 0;
                    }
                }
            }
        } else {
            kart->position.y = floor;
            {
                float front_height, rear_height, left_height, right_height;
                float dx = sinf(kart->heading) * 0.30f;
                float dz = cosf(kart->heading) * 0.30f;
                float side_x = cosf(kart->heading) * 0.24f;
                float side_z = -sinf(kart->heading) * 0.24f;
                float target_pitch = 0.0f;
                float target_roll = 0.0f;
                if (ground_height(kart->position.x + dx, kart->position.z + dz, floor, &front_height) &&
                    ground_height(kart->position.x - dx, kart->position.z - dz, floor, &rear_height)) {
                    target_pitch = -atan2f(front_height - rear_height, 0.60f);
                    if (target_pitch > 0.45f) target_pitch = 0.45f;
                    if (target_pitch < -0.45f) target_pitch = -0.45f;
                }
                if (ground_height(kart->position.x - side_x, kart->position.z - side_z, floor, &left_height) &&
                    ground_height(kart->position.x + side_x, kart->position.z + side_z, floor, &right_height)) {
                    target_roll = atan2f(right_height - left_height, 0.48f);
                    if (target_roll > 0.35f) target_roll = 0.35f;
                    if (target_roll < -0.35f) target_roll = -0.35f;
                }
                kart->pitch += (target_pitch - kart->pitch) * 0.18f;
                kart->roll += (target_roll - kart->roll) * 0.18f;
            }
        }
    } else if (active_collision->vertices || course_model.vertices || active_kcl) {
        /* No floor means genuine free fall, not a failed movement. */
        kart->airborne = 1;
        kart->vertical_speed -= 0.0046f;
        kart->position.y += kart->vertical_speed;
    }
    if (floor_found && !kart->airborne) {
        kart->last_safe_position = kart->position;
        kart->last_safe_heading = kart->heading;
        kart->has_safe_position = 1;
        kart->off_map_frames = 0;
    } else if (!noclip && kart->cannon_state != 1 &&
               kart->secret_jump_frames <= 0 && kart->airborne &&
               kart->vertical_speed < -0.020f &&
               (!floor_found || floor < kart->position.y - 1.25f) &&
               (!kart->has_safe_position ||
                kart->position.y < kart->last_safe_position.y - 2.0f)) {
        kart->off_map_frames++;
    } else {
        kart->off_map_frames = 0;
    }
    if (!noclip && kart->off_map_frames >= 18) {
        Vec3 target;
        int found = active_kcl &&
            nearest_pinball_floor(kart->position, &target);
        if (!found && kart->has_safe_position) {
            target = kart->last_safe_position;
            target.y += 0.03f;
            found = 1;
        }
        if (found) {
            float dx = target.x - kart->position.x;
            float dy = target.y - kart->position.y;
            float dz = target.z - kart->position.z;
            float distance = sqrtf(dx * dx + dy * dy + dz * dz);
            int frames = 45 + (int)(distance * 3.0f);
            if (frames > 110) frames = 110;
            kart->rescue_start = kart->position;
            kart->rescue_target = target;
            kart->rescue_total_frames = frames;
            kart->rescue_frames = frames;
            kart->off_map_frames = 0;
            kart->speed = 0.0f;
            kart->vertical_speed = 0.0f;
            kart->drifting = 0;
            kart->hop_camera_lock = 0;
        }
    }
    kart->r_was_down = r_down;
    kart->l_was_down = l_down;
    kart->up_was_down = up_down;
    if (kart->inertia_frames > 0) kart->inertia_frames--;
    if (kart->ramp_grace_frames > 0) kart->ramp_grace_frames--;
    if (kart->secret_jump_frames > 0) kart->secret_jump_frames--;
    if (kart->object_hit_cooldown > 0) kart->object_hit_cooldown--;
    {
        float camera_difference = kart->position.y - kart->camera_y;
        float follow = kart->hop_camera_lock ? 0.075f : 0.18f;
        if (kart->cannon_state == 2) kart->camera_y = kart->position.y;
        else kart->camera_y += camera_difference * follow;
        if (kart->hop_camera_lock) {
            if (kart->position.y - kart->camera_y > 0.70f)
                kart->camera_y = kart->position.y - 0.70f;
            else if (kart->position.y - kart->camera_y < -0.70f)
                kart->camera_y = kart->position.y + 0.70f;
        }
    }
}

static void draw_box(float x, float y, float z, float sx, float sy, float sz, unsigned int color) {
    Vertex *v = (Vertex *)sceGuGetMemory(36 * sizeof(Vertex));
    const float p[8][3] = {
        {-sx,-sy,-sz},{sx,-sy,-sz},{sx,sy,-sz},{-sx,sy,-sz},
        {-sx,-sy, sz},{sx,-sy, sz},{sx,sy, sz},{-sx,sy, sz}
    };
    const unsigned char faces[36] = {
        0,1,2, 0,2,3, 5,4,7, 5,7,6, 4,0,3, 4,3,7,
        1,5,6, 1,6,2, 3,2,6, 3,6,7, 4,5,1, 4,1,0
    };
    int i;
    for (i = 0; i < 36; ++i) {
        int n = faces[i];
        v[i].color = color;
        v[i].x = x + p[n][0]; v[i].y = y + p[n][1]; v[i].z = z + p[n][2];
    }
    sceGumDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, 36, NULL, v);
}

static const unsigned char *glyph(char c) {
    static const unsigned char blank[7] = {0,0,0,0,0,0,0};
    static const unsigned char letters[26][7] = {
        {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},
        {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
        {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},
        {7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
        {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
        {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
        {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
        {17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
        {17,17,10,4,4,4,4},{31,1,2,4,8,16,31}
    };
    static const unsigned char numbers[10][7] = {
        {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
        {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
        {14,17,17,15,1,1,14}
    };
    static const unsigned char colon[7] = {0,4,4,0,4,4,0};
    if (c >= 'A' && c <= 'Z') return letters[c - 'A'];
    if (c >= '0' && c <= '9') return numbers[c - '0'];
    if (c == ':') return colon;
    return blank;
}

static void draw_rect(float x, float y, float w, float h, unsigned int color) {
    Vertex *v = (Vertex *)sceGuGetMemory(2 * sizeof(Vertex));
    v[0].color = color; v[0].x = x;     v[0].y = y;     v[0].z = 0.0f;
    v[1].color = color; v[1].x = x + w; v[1].y = y + h; v[1].z = 0.0f;
    sceGuDrawArray(GU_SPRITES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, NULL, v);
}

static void draw_ui_texture(const CourseModel *texture, float x, float y, float w, float h) {
    CourseVertex *v;
    if (!texture->texture) return;
    v = (CourseVertex *)sceGuGetMemory(6 * sizeof(CourseVertex));
#define UI_VERTEX(I,U,V,X,Y) do { v[I].u=(U); v[I].v=(V); v[I].color=0xffffffff; \
    v[I].x=(X); v[I].y=(Y); v[I].z=0.0f; } while (0)
    /* GU_TRANSFORM_2D bypasses the transform/normalisation stage: UVs are in
       texels, not in the normalised 0..1 coordinates used by our 3D models. */
    UI_VERTEX(0, 0.0f,                         0.0f,                          x,     y);
    UI_VERTEX(1, (float)texture->texture_width,0.0f,                          x + w, y);
    UI_VERTEX(2, (float)texture->texture_width,(float)texture->texture_height,x + w, y + h);
    UI_VERTEX(3, 0.0f,                         0.0f,                          x,     y);
    UI_VERTEX(4, (float)texture->texture_width,(float)texture->texture_height,x + w, y + h);
    UI_VERTEX(5, 0.0f,                         (float)texture->texture_height,x,     y + h);
#undef UI_VERTEX
    sceGuTexMode(texture->texture_format, 0, 0, texture->swizzled_texture ? GU_TRUE : GU_FALSE);
    sceGuTexImage(0, texture->texture_width, texture->texture_height,
                  texture->texture_width, texture->texture);
    sceGuTexFlush();
    sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGBA);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuEnable(GU_BLEND);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 |
                   GU_VERTEX_32BITF | GU_TRANSFORM_2D, 6, NULL, v);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_BLEND);
}

static void draw_text(float x, float y, const char *text, float scale, unsigned int color) {
    while (*text) {
        const unsigned char *rows = glyph(*text++);
        int row, column;
        for (row = 0; row < 7; ++row) {
            for (column = 0; column < 5; ++column) {
                if (rows[row] & (1 << (4 - column)))
                    draw_rect(x + column * scale, y + row * scale, scale, scale, color);
            }
        }
        x += 6.0f * scale;
    }
}

static void draw_centered(float y, const char *text, float scale, unsigned int color) {
    const char *p = text;
    int length = 0;
    while (*p++) ++length;
    draw_text(240.0f - length * 3.0f * scale, y, text, scale, color);
}

static void format_time(char *output, size_t size, unsigned long long microseconds) {
    unsigned long long milliseconds = microseconds / 1000ULL;
    unsigned int minutes = (unsigned int)(milliseconds / 60000ULL);
    unsigned int seconds = (unsigned int)((milliseconds / 1000ULL) % 60ULL);
    unsigned int millis = (unsigned int)(milliseconds % 1000ULL);
    snprintf(output, size, "%u:%02u:%03u", minutes, seconds, millis);
}

static int update_timing(const Kart *kart, unsigned long long now) {
    float dx, dz;
    if (!timing.active || timing.finished) return timing.finished ? 1 : 0;
    if (!timing.race_start) {
        if (now - timing.countdown_start < 3000000ULL) return 0;
        timing.race_start = now;
        timing.lap_start = now;
    }
    dx = kart->position.x - active_start_x;
    dz = kart->position.z - active_start_z;
    if (dx * dx + dz * dz > 100.0f) timing.armed = 1;
    if (timing.armed && dx * dx + dz * dz < 2.25f &&
        sinf(kart->heading) * sinf(active_start_heading) +
        cosf(kart->heading) * cosf(active_start_heading) > 0.35f) {
        timing.lap_times[timing.lap - 1] = now - timing.lap_start;
        timing.armed = 0;
        if (timing.lap >= 3) {
            timing.finished = 1;
            timing.finish_time = now - timing.race_start;
            return 1;
        }
        timing.lap++;
        timing.lap_start = now;
    }
    return 0;
}

static void draw_race_hud(unsigned long long now, const Kart *kart) {
    char value[32];
    char line[48];
    unsigned long long countdown_elapsed;
    unsigned long long fps_elapsed;
    if (!fps_window_start) fps_window_start = now;
    fps_frame_count++;
    fps_elapsed = now - fps_window_start;
    if (fps_elapsed >= 500000ULL) {
        fps_display = (unsigned int)(((unsigned long long)fps_frame_count * 1000000ULL +
                                     fps_elapsed / 2ULL) / fps_elapsed);
        fps_frame_count = 0;
        fps_window_start = now;
    }
    sceGuDisable(GU_DEPTH_TEST);
    draw_rect(8, 8, 72, 42, 0xd0202020);
    if (kart->item_count > 0 && mushroom_icons[kart->item_count - 1].texture) {
        draw_ui_texture(&mushroom_icons[kart->item_count - 1], 12, 13, 64, 32);
    } else {
        if (kart->item_count >= 3) strcpy(line, "3 CHAMPIS");
        else if (kart->item_count == 2) strcpy(line, "2 CHAMPIS");
        else if (kart->item_count == 1) strcpy(line, "CHAMPI");
        else strcpy(line, "VIDE");
        draw_text(12, 22, line, 1.0f, kart->item_count ? 0xffffffff : 0xff808080);
    }
    draw_rect(330, 8, 142, 42, 0xb0202020);
    if (!timing.active) {
        draw_text(338, 18, "COUPE", 2.0f, 0xffffffff);
    } else {
        snprintf(value, sizeof(value), "TOUR %d 3", timing.lap);
        draw_text(338, 13, value, 1.5f, 0xffffffff);
        format_time(value, sizeof(value), timing.finished ? timing.finish_time :
                    (timing.race_start ? now - timing.race_start : 0));
        draw_text(338, 31, value, 1.5f, 0xffffff40);
        countdown_elapsed = now - timing.countdown_start;
        if (!timing.race_start) {
            snprintf(value, sizeof(value), "%d", 3 - (int)(countdown_elapsed / 1000000ULL));
            draw_centered(98, value, 8.0f, 0xffffffff);
        } else if (now - timing.race_start < 700000ULL) {
            draw_centered(98, "GO", 7.0f, 0xff40ff40);
        } else if (timing.finished) {
            int i;
            draw_rect(92, 62, 296, 166, 0xe0202020);
            draw_centered(76, "TERMINE", 4.0f, 0xffffff40);
            format_time(value, sizeof(value), timing.finish_time);
            draw_centered(112, value, 3.0f, 0xffffffff);
            for (i = 0; i < 3; ++i) {
                format_time(value, sizeof(value), timing.lap_times[i]);
                snprintf(line, sizeof(line), "TOUR %d  %s", i + 1, value);
                draw_centered(151.0f + i * 22.0f, line, 1.8f, 0xffffffff);
            }
        }
    }
    if (kart->drifting) {
        unsigned int color = kart->drift_charge >= 2.50f ? 0xffff40d0 :
                             kart->drift_charge >= 1.00f ? 0xff2080ff :
                             kart->drift_charge >= 0.40f ? 0xffffa020 : 0xffffffff;
        draw_text(354, 246, "DRIFT", 1.5f, color);
    } else if (kart->boost_frames > 0.0f) {
        draw_text(354, 246, "TURBO", 1.5f, 0xffffff40);
    }
    if (course_hidden) {
        draw_rect(8, 210, 150, 20, 0xd0202020);
        draw_text(12, 214, "PISTE INVISIBLE", 1.0f, 0xffffff40);
    }
    draw_rect(414, 55, 58, 15, 0xb0202020);
    snprintf(line, sizeof(line), "FPS %u", fps_display);
    draw_text(421, 59, line, 1.0f, fps_display < 30 ? 0xff4040ff : 0xffffffff);
    if (kcl_debug_enabled && active_kcl) {
        draw_rect(8, 238, 230, 28, 0xd0202020);
        if (kcl_touched_id >= 0)
            snprintf(line,sizeof(line),"KCL #%d TYPE %u VAR %u",kcl_touched_id,kcl_touched_type,kcl_touched_variant);
        else strcpy(line,"KCL AUCUN TRIANGLE");
        draw_text(14,247,line,1.2f,0xffffffff);
    }
    sceGuEnable(GU_DEPTH_TEST);
}

static void render_menu(Screen screen, int selection) {
    sceGuStart(GU_DIRECT, display_list);
    sceGuClearColor(0xff602010);
    sceGuClear(GU_COLOR_BUFFER_BIT);
    sceGuDisable(GU_DEPTH_TEST);
    draw_rect(0, 0, 480, 54, 0xffb04020);
    draw_centered(14, "MARIO KART PSP", 4.0f, 0xffffffff);

    if (screen == SCREEN_MAIN_MENU) {
        draw_centered(79, "MENU PRINCIPAL", 2.0f, 0xffe0e0e0);
        draw_rect(128, selection ? 157 : 117, 224, 30, 0xffd05020);
        draw_centered(124, "JOUER", 3.0f, 0xffffffff);
        draw_centered(164, "QUITTER", 3.0f, 0xffffffff);
    } else if (screen == SCREEN_CHARACTER) {
        draw_centered(69, "PERSONNAGE", 3.0f, 0xffffffff);
        draw_rect(118, selection ? 151 : 108, 244, 34, 0xffd05020);
        draw_centered(116, "MARIO", 3.0f, 0xffffffff);
        draw_centered(159, "GROS CUBE", 3.0f, 0xffffffff);
        draw_centered(219, "X VALIDER   O RETOUR", 1.5f, 0xffe0e0e0);
    } else if (screen == SCREEN_KART) {
        draw_centered(78, "KART", 3.0f, 0xffffffff);
        draw_rect(98, selection ? 158 : 112, 284, 34, 0xffd05020);
        draw_centered(120, "STANDARD MR", 2.5f, 0xffffffff);
        draw_centered(166, "B DASHER", 3.0f, 0xffffffff);
        draw_centered(207, "X VALIDER   O RETOUR", 1.5f, 0xffe0e0e0);
    } else if (screen == SCREEN_MODE) {
        draw_centered(69, "MODE", 3.0f, 0xffffffff);
        draw_rect(76, selection ? 151 : 108, 328, 34, 0xffd05020);
        draw_centered(116, "CONTRE LA MONTRE", 2.3f, 0xffffffff);
        draw_centered(159, "COUPE", 3.0f, 0xffffffff);
        draw_centered(219, "X VALIDER   O RETOUR", 1.5f, 0xffe0e0e0);
    } else {
        draw_centered(78, "COURSE", 3.0f, 0xffffffff);
        draw_rect(70, 105 + selection * 40, 340, 30, 0xffd05020);
        draw_centered(110, "MARIO CIRCUIT", 2.0f, selection == 0 ? 0xffffffff : 0xffb0b0b0);
        draw_centered(150, "WALUIGI PINBALL", 2.0f, selection == 1 ? 0xffffffff : 0xffb0b0b0);
        draw_centered(190, "LUIGI'S MANSION", 2.0f, selection == 2 ? 0xffffffff : 0xffb0b0b0);
        draw_centered(230, "X JOUER   O RETOUR", 1.5f, 0xffe0e0e0);
    }
    sceGuEnable(GU_DEPTH_TEST);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
}

static void draw_model(const CourseModel *model) {
    if (!model->vertices) return;
    if (model->texture) {
        sceGuTexMode(model->texture_format, 0, 0, model->swizzled_texture ? GU_TRUE : GU_FALSE);
        sceGuTexImage(0, model->texture_width, model->texture_height,
            model->texture_width, model->texture);
        sceGuTexFlush();
        sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGBA);
        /* Nearest filtering matches the original DS look and is cheaper on
           PSP-1000 when drawing the subdivided course mesh. */
        sceGuTexFilter(GU_NEAREST, GU_NEAREST);
        sceGuTexWrap(model->repeat_texture ? GU_REPEAT : GU_CLAMP,
                     model->repeat_texture ? GU_REPEAT : GU_CLAMP);
        if (model->has_alpha) {
            sceGuAlphaFunc(GU_GREATER, 8, 0xff);
            sceGuEnable(GU_ALPHA_TEST);
        }
        sceGuEnable(GU_TEXTURE_2D);
    }
    sceGumDrawArray(GU_TRIANGLES,
        GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
        model->count, NULL, model->vertices);
    sceGuDisable(GU_TEXTURE_2D);
    if (model->has_alpha) sceGuDisable(GU_ALPHA_TEST);
}

static void draw_iron_ball_model(const CourseModel *model) {
    CourseVertex bl, br, tr, tl, bottom, top;
    CourseVertex *fixed;
    if (!model->vertices || model->count < 6) return;
    bl = model->vertices[0];
    br = model->vertices[1];
    tr = model->vertices[2];
    tl = model->vertices[5];
    bottom = bl;
    bottom.x = (bl.x + br.x) * 0.5f;
    bottom.y = (bl.y + br.y) * 0.5f;
    bottom.z = (bl.z + br.z) * 0.5f;
    bottom.u = 1.0f;
    top = tl;
    top.x = (tl.x + tr.x) * 0.5f;
    top.y = (tl.y + tr.y) * 0.5f;
    top.z = (tl.z + tr.z) * 0.5f;
    top.u = 1.0f;
    bl.u = 0.0f; tl.u = 0.0f;
    br.u = 0.0f; tr.u = 0.0f;
    fixed = (CourseVertex *)sceGuGetMemory(12 * sizeof(CourseVertex));
    fixed[0] = bl;     fixed[1] = bottom; fixed[2] = top;
    fixed[3] = bl;     fixed[4] = top;    fixed[5] = tl;
    fixed[6] = bottom; fixed[7] = br;     fixed[8] = tr;
    fixed[9] = bottom; fixed[10] = tr;    fixed[11] = top;
    if (model->texture) {
        sceGuTexMode(model->texture_format, 0, 0,
                     model->swizzled_texture ? GU_TRUE : GU_FALSE);
        sceGuTexImage(0, model->texture_width, model->texture_height,
                      model->texture_width, model->texture);
        sceGuTexFlush();
        sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGBA);
        sceGuTexFilter(GU_NEAREST, GU_NEAREST);
        sceGuTexWrap(GU_CLAMP, GU_CLAMP);
        if (model->has_alpha) {
            sceGuAlphaFunc(GU_GREATER, 8, 0xff);
            sceGuEnable(GU_ALPHA_TEST);
        }
        sceGuEnable(GU_TEXTURE_2D);
    }
    sceGumDrawArray(GU_TRIANGLES,
        GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
        12, NULL, fixed);
    sceGuDisable(GU_TEXTURE_2D);
    if (model->has_alpha) sceGuDisable(GU_ALPHA_TEST);
}

static void draw_kcl_debug(void) {
    unsigned int i;
    Vertex *vertices;
    if (!kcl_debug_enabled || !active_kcl) return;
    vertices = (Vertex *)sceGuGetMemory(active_kcl_count * 3 * sizeof(Vertex));
    for (i=0; i<active_kcl_count; ++i) {
        const KclTriangle *triangle=&active_kcl[i];
        unsigned int color = triangle->category==1 ? 0x7030ff30 :
                             triangle->category==2 ? 0x703030ff :
                             triangle->category==4 ? 0x7000ffff :
                             triangle->category==5 ? 0x70ff30ff :
                             triangle->category==6 ? 0x700000ff : 0x70808080;
        Vec3 points[3]={triangle->a,triangle->b,triangle->c};
        int j;
        if ((int)i==kcl_touched_id) color=0xc0ffffff;
        for (j=0;j<3;++j) {
            Vertex *out=&vertices[i*3+j]; out->color=color;
            out->x=points[j].x; out->y=points[j].y+0.006f; out->z=points[j].z;
        }
    }
    sceGuDisable(GU_TEXTURE_2D);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);
    sceGumDrawArray(GU_TRIANGLES,GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D,
                    active_kcl_count*3,NULL,vertices);
    sceGuDisable(GU_BLEND);
}

typedef struct {
    ScePspFVector3 eye, forward, right, up;
    float half_x, half_y, near_z;
} CourseClipContext;

static float course_clip_distance(const CourseVertex *vertex,
                                  const CourseClipContext *clip, int plane) {
    float px = vertex->x - clip->eye.x;
    float py = vertex->y - clip->eye.y;
    float pz = vertex->z - clip->eye.z;
    float x = px * clip->right.x + py * clip->right.y + pz * clip->right.z;
    float y = px * clip->up.x + py * clip->up.y + pz * clip->up.z;
    float z = px * clip->forward.x + py * clip->forward.y + pz * clip->forward.z;
    if (plane == 0) return z - clip->near_z;
    if (plane == 1) return clip->half_x * z + x;
    if (plane == 2) return clip->half_x * z - x;
    if (plane == 3) return clip->half_y * z + y;
    return clip->half_y * z - y;
}

static unsigned int lerp_color(unsigned int a, unsigned int b, float t) {
    unsigned int result = 0;
    int shift;
    for (shift = 0; shift < 32; shift += 8) {
        float av = (float)((a >> shift) & 255);
        float bv = (float)((b >> shift) & 255);
        result |= ((unsigned int)(av + (bv - av) * t + 0.5f) & 255) << shift;
    }
    return result;
}

static CourseVertex lerp_course_vertex(const CourseVertex *a,
                                       const CourseVertex *b, float t) {
    CourseVertex out;
    out.u = a->u + (b->u - a->u) * t;
    out.v = a->v + (b->v - a->v) * t;
    out.color = lerp_color(a->color, b->color, t);
    out.x = a->x + (b->x - a->x) * t;
    out.y = a->y + (b->y - a->y) * t;
    out.z = a->z + (b->z - a->z) * t;
    return out;
}

static unsigned int clip_course_triangle(const CourseVertex *triangle,
                                         const CourseClipContext *clip,
                                         CourseVertex *output) {
    CourseVertex buffers[2][10];
    CourseVertex *input = buffers[0], *clipped = buffers[1];
    unsigned int count = 3;
    int plane;
    memcpy(input, triangle, 3 * sizeof(CourseVertex));
    for (plane = 0; plane < 5 && count >= 3; ++plane) {
        unsigned int i, clipped_count = 0;
        CourseVertex previous = input[count - 1];
        float previous_distance = course_clip_distance(&previous, clip, plane);
        for (i = 0; i < count; ++i) {
            CourseVertex current = input[i];
            float current_distance = course_clip_distance(&current, clip, plane);
            int previous_inside = previous_distance >= 0.0f;
            int current_inside = current_distance >= 0.0f;
            if (previous_inside != current_inside) {
                float denominator = previous_distance - current_distance;
                float t = fabsf(denominator) > 0.000001f ?
                    previous_distance / denominator : 0.0f;
                clipped[clipped_count++] = lerp_course_vertex(&previous, &current, t);
            }
            if (current_inside) clipped[clipped_count++] = current;
            previous = current;
            previous_distance = current_distance;
        }
        count = clipped_count;
        {
            CourseVertex *swap = input;
            input = clipped;
            clipped = swap;
        }
    }
    if (count < 3) return 0;
    if (output) {
        unsigned int i;
        for (i = 1; i + 1 < count; ++i) {
            *output++ = input[0];
            *output++ = input[i];
            *output++ = input[i + 1];
        }
    }
    return (count - 2) * 3;
}

static int course_triangle_guard_status(const CourseVertex *triangle,
                                        const CourseClipContext *clip) {
    int i, behind = 0, needs_clip = 0;
    for (i = 0; i < 3; ++i) {
        float px = triangle[i].x - clip->eye.x;
        float py = triangle[i].y - clip->eye.y;
        float pz = triangle[i].z - clip->eye.z;
        float x = px * clip->right.x + py * clip->right.y + pz * clip->right.z;
        float y = px * clip->up.x + py * clip->up.y + pz * clip->up.z;
        float z = px * clip->forward.x + py * clip->forward.y + pz * clip->forward.z;
        /* The GE guard band is roughly 4096x4096 around a 480x272 viewport.
           Only vertices that can actually leave that band (or cross the near
           plane) need CPU clipping; normal triangles stay in their original
           cached vertex stream and cost no copy/allocation. */
        if (z <= clip->near_z) {
            ++behind;
            needs_clip = 1;
        } else if (fabsf(x) > z * 6.8f || fabsf(y) > z * 12.5f) {
            needs_clip = 1;
        }
    }
    if (behind == 3) return 2;
    return needs_clip;
}

static void setup_course_clip(CourseClipContext *clip,
                              const ScePspFVector3 *eye,
                              const ScePspFVector3 *center) {
    float length;
    clip->eye = *eye;
    clip->forward.x = center->x - eye->x;
    clip->forward.y = center->y - eye->y;
    clip->forward.z = center->z - eye->z;
    length = sqrtf(clip->forward.x * clip->forward.x +
                   clip->forward.y * clip->forward.y +
                   clip->forward.z * clip->forward.z);
    if (length < 0.0001f) length = 1.0f;
    clip->forward.x /= length;
    clip->forward.y /= length;
    clip->forward.z /= length;
    clip->right.x = -clip->forward.z;
    clip->right.y = 0.0f;
    clip->right.z = clip->forward.x;
    length = sqrtf(clip->right.x * clip->right.x + clip->right.z * clip->right.z);
    if (length < 0.0001f) length = 1.0f;
    clip->right.x /= length;
    clip->right.z /= length;
    clip->up.x = clip->right.y * clip->forward.z - clip->right.z * clip->forward.y;
    clip->up.y = clip->right.z * clip->forward.x - clip->right.x * clip->forward.z;
    clip->up.z = clip->right.x * clip->forward.y - clip->right.y * clip->forward.x;
    /* 72 degree vertical FOV, widened 10% beyond the visible viewport.  PSP's
       GE does not side-clip triangles, so this small CPU guard prevents a huge
       ground polygon from leaving the hardware guard band and vanishing. */
    clip->half_y = 0.7992f;
    clip->half_x = 1.4104f;
    clip->near_z = 0.205f;
}

static void draw_course_model(const CourseModel *model, const ScePspFVector3 *eye,
                              const ScePspFVector3 *center, float heading,
                              int software_clip) {
    unsigned int i, visible_count = 0;
    unsigned short visible[144];
    float visible_depth[144];
    float forward_x = sinf(heading), forward_z = cosf(heading);
    CourseClipContext clip;
    if (!model->chunks || !model->chunk_count) { draw_model(model); return; }
    if (software_clip) setup_course_clip(&clip, eye, center);
    for (i = 0; i < model->chunk_count && visible_count < 144; ++i) {
        const CourseChunk *chunk = &model->chunks[i];
        float cx = (chunk->min_x + chunk->max_x) * .5f, cz = (chunk->min_z + chunk->max_z) * .5f;
        float rx = (chunk->max_x - chunk->min_x) * .5f, rz = (chunk->max_z - chunk->min_z) * .5f;
        float radius = sqrtf(rx*rx + rz*rz) + 1.5f, dx = cx-eye->x, dz = cz-eye->z;
        float front = dx*forward_x + dz*forward_z, side = fabsf(dx*forward_z - dz*forward_x);
        if (front < -radius || front > 125.0f + radius ||
            side > fmaxf(front,0.0f)*1.45f + radius) continue;
        {
            unsigned int insert = visible_count;
            while (insert > 0 && visible_depth[insert - 1] > front) {
                visible_depth[insert] = visible_depth[insert - 1];
                visible[insert] = visible[insert - 1];
                --insert;
            }
            visible_depth[insert] = front;
            visible[insert] = (unsigned short)i;
            ++visible_count;
        }
    }
    if (!visible_count) return;
    /* Bind/flush the texture only after visibility is known.  Luigi's Mansion
       has 28 materials; skipping off-camera texture state is particularly
       valuable in the graveyard where the house materials are behind Mario. */
    if (model->texture) {
        sceGuTexMode(model->texture_format, 0, 0, model->swizzled_texture ? GU_TRUE : GU_FALSE);
        sceGuTexImage(0, model->texture_width, model->texture_height, model->texture_width, model->texture);
        sceGuTexFlush(); sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGBA); sceGuTexFilter(GU_NEAREST, GU_NEAREST);
        sceGuTexWrap(model->repeat_texture ? GU_REPEAT : GU_CLAMP, model->repeat_texture ? GU_REPEAT : GU_CLAMP);
        if (model->has_alpha) { sceGuAlphaFunc(GU_GREATER, 8, 0xff); sceGuEnable(GU_ALPHA_TEST); }
        sceGuEnable(GU_TEXTURE_2D);
    }
    /* Front-to-back submission lets the depth test reject castle pixels hidden
       by the road, walls and nearer facades before they consume fill-rate. */
    for (i = 0; i < visible_count; ++i) {
        const CourseChunk *chunk = &model->chunks[visible[i]];
        const CourseVertex *source = model->vertices + chunk->start;
        if (software_clip) {
            unsigned int vertex, run_start = 0, run_count = 0;
            for (vertex = 0; vertex + 2 < chunk->count; vertex += 3) {
                int guard_status = course_triangle_guard_status(source + vertex, &clip);
                if (!guard_status) {
                    if (!run_count) run_start = vertex;
                    run_count += 3;
                    continue;
                }
                if (run_count) {
                    sceGumDrawArray(GU_TRIANGLES,
                        GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D,
                        run_count, NULL, source + run_start);
                    run_count = 0;
                }
                if (guard_status == 2) continue;
                {
                    CourseVertex clipped[18];
                    unsigned int output_count = clip_course_triangle(source + vertex, &clip, clipped);
                    if (output_count) {
                        CourseVertex *output = (CourseVertex *)sceGuGetMemory(
                            output_count * sizeof(CourseVertex));
                        memcpy(output, clipped, output_count * sizeof(CourseVertex));
                        sceGumDrawArray(GU_TRIANGLES,
                            GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D,
                            output_count, NULL, output);
                    }
                }
            }
            if (run_count)
                sceGumDrawArray(GU_TRIANGLES,
                    GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D,
                    run_count, NULL, source + run_start);
        } else {
            sceGumDrawArray(GU_TRIANGLES,
                GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D,
                chunk->count, NULL, source);
        }
    }
    sceGuDisable(GU_TEXTURE_2D); if (model->has_alpha) sceGuDisable(GU_ALPHA_TEST);
}

static void draw_animated_model(const AnimatedModel *model, unsigned int frame) {
    if (!model->vertices || !model->texture || !model->frame_count) return;
    frame %= model->frame_count;
    sceGuTexMode(GU_PSM_8888, 0, 0, GU_TRUE);
    sceGuTexImage(0, model->texture_width, model->texture_height,
                  model->texture_width, model->texture);
    sceGuTexFlush(); sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGBA);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST); sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuAlphaFunc(GU_GREATER, 8, 0xff); sceGuEnable(GU_ALPHA_TEST); sceGuEnable(GU_TEXTURE_2D);
    sceGumDrawArray(GU_TRIANGLES,
        GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
        model->vertices_per_frame, NULL,
        model->vertices + (size_t)frame * model->vertices_per_frame);
    sceGuDisable(GU_TEXTURE_2D); sceGuDisable(GU_ALPHA_TEST);
}

static void update_mansion_ambience(const Kart *kart) {
    unsigned int i;
    if (mansion_ambient_cooldown > 0) {
        --mansion_ambient_cooldown;
        return;
    }
    for (i = 0; i < sizeof(mansion_objects) / sizeof(mansion_objects[0]); ++i) {
        const MapObjInstance *object = &mansion_objects[i];
        float dx = kart->position.x - object->x;
        float dz = kart->position.z - object->z;
        float distance_sq = dx * dx + dz * dz;
        if (object->model == MANSION_TERESA && distance_sq < 42.25f) {
            sfx_play(SFX_MANSION_BOO);
            mansion_ambient_cooldown = 210;
            return;
        }
        if (object->model == MANSION_CHANDELIER && distance_sq < 64.0f) {
            sfx_play(SFX_MANSION_CHANDELIER);
            mansion_ambient_cooldown = 180;
            return;
        }
        if ((object->model == MANSION_PICTURE1 || object->model == MANSION_PICTURE2) &&
            distance_sq < 25.0f) {
            sfx_play(SFX_MANSION_PICTURE);
            mansion_ambient_cooldown = 150;
            return;
        }
    }
}

static void render(const Kart *kart, int gros_cube, int selected_kart, unsigned long long now) {
    float camera_distance = kart->cannon_state == 2 ? 0.95f : 1.30f;
    float camera_height = kart->cannon_state == 2 ? 0.58f : 0.85f;
    float camera_look_height = kart->cannon_state == 2 ? 0.06f : 0.12f;
    ScePspFVector3 eye = {
        kart->position.x - sinf(kart->heading) * camera_distance,
        kart->camera_y + camera_height,
        kart->position.z - cosf(kart->heading) * camera_distance
    };
    ScePspFVector3 center = {
        kart->position.x + sinf(kart->heading) * 1.65f,
        kart->position.y + camera_look_height,
        kart->position.z + cosf(kart->heading) * 1.65f
    };
    ScePspFVector3 up = {0.0f, 1.0f, 0.0f};

    /* Camera ground guard for Waluigi Pinball. On steep descents the smoothed
       camera can lag into the higher road behind the kart; that projects huge
       floor triangles outside the PSP guard band and makes whole layers vanish.
       Do not apply it in the air, preserving the hop and ballistic jump view. */
    if (active_kcl && !kart->airborne && !kart->hop_camera_lock &&
        kart->cannon_state != 1) {
        float camera_floor;
        if (kcl_camera_floor_height(eye.x, eye.z, eye.y, &camera_floor) &&
            eye.y < camera_floor + 0.72f)
            eye.y = camera_floor + 0.72f;
    }

    sceGuStart(GU_DIRECT, display_list);
    sceGuClearColor(0xffd08040);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    /* Keep the near plane clear of the chase camera. Luigi's Mansion uses a
       smaller near plane plus a 200-unit far plane: its whole course fits in
       that range, retaining PSP 16-bit Z precision while protecting nearby
       ground from clipping. */
    sceGumPerspective(72.0f, 480.0f / 272.0f,
                      active_course == 1 ? 0.80f : active_course == 2 ? 0.20f : 0.50f,
                      active_course == 0 ? 300.0f : 200.0f);
    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();
    sceGumLookAt(&eye, &center, &up);
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();

    if (!course_hidden && active_batches[0].vertices) {
        int i;
        float castle_dx = kart->position.x - 16.5f;
        float castle_dz = kart->position.z - 6.0f;
        int near_castle = castle_dx * castle_dx + castle_dz * castle_dz < 900.0f;
        int pass, layer;
        /* Opaque geometry first fills the Z buffer. Alpha-tested windows and
           foliage are submitted afterwards, so hidden texels are discarded. */
        for (pass = 0; pass < 2; ++pass)
        for (layer = 0; layer < (active_course == 1 ? 2 : 1); ++layer)
        for (i = 0; i < active_material_count; ++i) {
            unsigned int *saved_texture = active_batches[i].texture;
            float texture_u = 0.0f, texture_v = 0.0f;
            int depth_offset = active_course == 1 &&
                (i == 1 || i == 2 || i == 3 || i == 4 || i == 6 || i == 7);
            if (!!active_batches[i].has_alpha != pass) continue;
            /* Pinball floor markings are separate, nearly coplanar meshes.
               Draw the structural floor first and its skin second; drawing
               them in material-number order allowed the base to erase half
               of an arrow/cannon layer as the camera moved. */
            if (active_course == 1 && !!depth_offset != layer) continue;
            /* The castle's window and roof batches cover a large part of the
               screen.  Their closed meshes have consistent winding, so avoid
               rasterising their invisible back faces on the PSP-1000. */
            int cull_batch = active_course == 0 && (i == 11 || i == 12 || i == 18 ||
                (near_castle && (i == 14 || i == 15 || i == 16 || i == 20)));
            /* Close castle LOD: its translucent ground shadow and tiny window
               overlay create disproportionate overdraw from a side view. */
            if (active_course == 0 && near_castle && i == 0) continue;
            if (cull_batch) {
                sceGuFrontFace(GU_CCW);
                sceGuEnable(GU_CULL_FACE);
            }
            if (active_course == 1) {
                float phase=(float)(now%4000000ULL)/4000000.0f;
                if (i==2 || i==7) texture_v=phase;
                else if (i==3 || i==6) texture_u=phase;
                else if (i==4) texture_v=-phase;
                else if (i==14)
                    texture_v=-(float)(now%750000ULL)/750000.0f;
                if (i==10 && pinball_flag_frames[0])
                    active_batches[i].texture=pinball_flag_frames[((now/16667ULL)%30)>=15 ? 1 : 0];
                sceGuTexOffset(texture_u,texture_v);
            } else if (active_course == 2 && i == 3) {
                float phase = (float)(now % 6000000ULL) / 6000000.0f;
                texture_u = 0.1f - 0.1f * cosf(phase * 6.2831853f);
                sceGuTexOffset(texture_u, 0.0f);
            }
            if (depth_offset) sceGuDepthOffset(64);
            draw_course_model(&active_batches[i], &eye, &center, kart->heading,
                              active_course == 2);
            active_batches[i].texture=saved_texture;
            if (depth_offset) sceGuDepthOffset(0);
            if (active_course == 1 || (active_course == 2 && i == 3))
                sceGuTexOffset(0.0f,0.0f);
            if (cull_batch) sceGuDisable(GU_CULL_FACE);
        }
    } else if (!course_hidden && course_model.vertices) {
        draw_model(&course_model);
    } else if (!course_hidden) {
        draw_box(0, -0.7f, 60, 11, 0.5f, 80, 0xff606060);
        draw_box(-12, 0, 60, 1, 1, 80, 0xffffffff);
        draw_box(12, 0, 60, 1, 1, 80, 0xffffffff);
    }

    if (active_course == 1) {
        unsigned int i;
        int ball_index = 0;
        int flipper_index = 0;
        for (i = 0; i < sizeof(pinball_objects) / sizeof(pinball_objects[0]); ++i) {
            const MapObjInstance *object = &pinball_objects[i];
            CourseModel *model = &pinball_mapobjs[object->model];
            unsigned int *saved_texture;
            float x = object->x, y = object->y, z = object->z;
            float rotation = object->rotation_y;
            float rotation_z = 0.0f;
            ScePspFVector3 translate;
            ScePspFVector3 scale = {object->scale_x, object->scale_y, object->scale_z};
            if (!model->vertices) continue;
            saved_texture=model->texture;
            if (object->model == MAPOBJ_FLIPPER) {
                unsigned int frame=(unsigned int)((now/16667ULL)%33), texture_frame;
                rotation = pinball_flipper_rotation((unsigned int)flipper_index, now, NULL);
                flipper_index++;
                texture_frame=frame<1?0:frame<5?1:frame<9?2:frame<13?1:frame<17?3:frame<22?1:frame<27?2:3;
                if (pinball_flipper_frames[texture_frame]) model->texture=pinball_flipper_frames[texture_frame];
            } else if (object->model == MAPOBJ_IRON_BALL) {
                const PinballBall *ball = &pinball_balls[ball_index];
                x = ball->position.x;
                y = ball->position.y;
                z = ball->position.z;
                rotation = atan2f(eye.x - x, eye.z - z);
                rotation_z = 0.0f;
                scale.x = scale.y = scale.z = PINBALL_BALL_RENDER_SCALE;
                ball_index++;
            } else if (object->model == MAPOBJ_BOUND) {
                unsigned int frame=(unsigned int)((now/16667ULL)%61),texture_frame;
                rotation += (float)(now % 3000000ULL) * (6.2831853f / 3000000.0f);
                texture_frame=frame<1?0:frame<16?1:frame<31?2:frame<46?1:2;
                if (pinball_bound_frames[texture_frame]) model->texture=pinball_bound_frames[texture_frame];
            }
            translate.x = x; translate.y = y; translate.z = z;
            sceGumPushMatrix();
            sceGumTranslate(&translate);
            sceGumRotateY(rotation);
            if (rotation_z != 0.0f) sceGumRotateZ(rotation_z);
            sceGumScale(&scale);
            if (object->model == MAPOBJ_IRON_BALL) draw_iron_ball_model(model);
            else draw_model(model);
            model->texture=saved_texture;
            sceGumPopMatrix();
        }
    }

    if (active_course == 2) {
        unsigned int i;
        float time = (float)(now % 8000000ULL) / 1000000.0f;
        for (i = 0; i < sizeof(mansion_objects) / sizeof(mansion_objects[0]); ++i) {
            const MapObjInstance *object = &mansion_objects[i];
            CourseModel *model = &mansion_mapobjs[object->model];
            AnimatedModel *animation = &mansion_animations[object->model];
            int use_animation = animation->vertices && animation->texture;
            unsigned int *saved_texture;
            float x = object->x, y = object->y, z = object->z;
            float rotation_y = object->rotation_y;
            float rotation_z = 0.0f;
            float dx = x - kart->position.x, dz = z - kart->position.z;
            int cull_object = object->model == MANSION_MOVE_TREE ||
                object->model == MANSION_PICTURE1 || object->model == MANSION_PICTURE2;
            ScePspFVector3 translate;
            ScePspFVector3 scale = {object->scale_x, object->scale_y, object->scale_z};
            /* Object distance culling keeps the extra atmosphere essentially
               free: the PSP never submits objects from the other side of the
               course, while every object near the player remains visible. */
            if ((!model->vertices && !use_animation) || dx * dx + dz * dz > 3025.0f) continue;
            saved_texture = model->texture;
            if (object->model == MANSION_MOVE_TREE) {
                rotation_y = atan2f(eye.x - x, eye.z - z);
            } else if (object->model == MANSION_TERESA) {
                x += sinf(time * 0.85f + (float)i * 1.4f) * 0.42f;
                y += sinf(time * 1.30f + (float)i) * 0.20f + 0.35f;
                z += cosf(time * 0.85f + (float)i * 1.4f) * 0.42f;
                /* The original Boo is a camera-facing sprite, not a character-
                   facing sprite.  Alternate its two native face textures. */
                rotation_y = atan2f(eye.x - x, eye.z - z);
                if (((now / 300000ULL) & 1ULL) && mansion_teresa_alt_texture)
                    model->texture = mansion_teresa_alt_texture;
            }
            translate.x = x; translate.y = y; translate.z = z;
            sceGumPushMatrix();
            sceGumTranslate(&translate);
            sceGumRotateY(rotation_y);
            if (rotation_z != 0.0f) sceGumRotateZ(rotation_z);
            sceGumScale(&scale);
            if (cull_object) {
                sceGuFrontFace(GU_CCW);
                sceGuEnable(GU_CULL_FACE);
            }
            if (use_animation)
                draw_animated_model(animation, (unsigned int)(now / 33333ULL));
            else
                draw_model(model);
            if (cull_object) sceGuDisable(GU_CULL_FACE);
            model->texture = saved_texture;
            sceGumPopMatrix();
        }
    }

    draw_kcl_debug();

    sceGumPushMatrix();
    {
        ScePspFVector3 translate = {kart->position.x, kart->position.y, kart->position.z};
        sceGumTranslate(&translate);
        sceGumRotateY(kart->heading);
        sceGumRotateX(kart->pitch);
        sceGumRotateZ(kart->roll);
    }
    if (gros_cube) {
        draw_box(0, 0.16f, 0, 0.16f, 0.16f, 0.16f, 0xff2020e0);
    } else {
        CourseModel *kart_model = &kart_models[selected_kart ? 1 : 0];
        if (kart_model->vertices) {
            int saved_alpha = kart_model->has_alpha;
            kart_model->has_alpha = 0;
            sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
            sceGuEnable(GU_BLEND);
            draw_model(kart_model);
            sceGuDisable(GU_BLEND);
            kart_model->has_alpha = saved_alpha;
        }
        else draw_box(0, 0.10f, 0, 0.18f, 0.10f, 0.28f, 0xff2020e0);
        {
            int wheel;
            static const float positions[2][4][3] = {
                {{-.171875f,.086975f,-.161255f},{.171875f,.086975f,-.161255f},{-.145630f,.061401f,.163757f},{.145630f,.061401f,.163757f}},
                {{-.233460f,.117249f,-.272644f},{.233460f,.117249f,-.272644f},{-.218384f,.090149f,.196350f},{.218384f,.090149f,.196350f}}
            };
            static const float radii[2][4] = {{.086975f,.086975f,.049316f,.049316f},{.117248f,.117248f,.086975f,.086975f}};
            int model = selected_kart ? 1 : 0;
            for (wheel = 0; wheel < 4; ++wheel) {
                ScePspFVector3 p = {positions[model][wheel][0],positions[model][wheel][1],positions[model][wheel][2]};
                sceGumPushMatrix(); sceGumTranslate(&p);
                if (wheel >= 2) sceGumRotateY(kart->wheel_steering * .38f);
                sceGumRotateX(kart->wheel_distance / radii[model][wheel]);
                draw_model(&wheel_models[model][wheel]); sceGumPopMatrix();
            }
        }
        sceGumPushMatrix();
        {
            ScePspFVector3 driver = {0.0f, 0.18f, -0.03f};
            sceGumTranslate(&driver);
        }
        if (mario_animation.vertices) {
            unsigned int frame;
            if (timing.finished && mario_animation.frame_count > mario_animation.drive_frames)
                frame = mario_animation.drive_frames +
                    (unsigned int)((now / 16667ULL) % (mario_animation.frame_count - mario_animation.drive_frames));
            else {
                float amount = (kart->wheel_steering + 1.0f) * 0.5f;
                if (amount < 0.0f) amount = 0.0f;
                if (amount > 1.0f) amount = 1.0f;
                frame = (unsigned int)(amount * (mario_animation.drive_frames - 1) + 0.5f);
            }
            draw_animated_model(&mario_animation, frame);
        } else if (mario_model.vertices) draw_model(&mario_model);
        else draw_box(0, 0.28f, 0, 0.10f, 0.20f, 0.10f, 0xff2020e0);
        sceGumPopMatrix();
    }
    sceGumPopMatrix();

    draw_race_hud(now, kart);

    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
}

int main(int argc, char *argv[]) {
    Kart kart = {0};
    SceCtrlData pad;
    unsigned int previous_buttons = 0;
    Screen screen = SCREEN_MAIN_MENU;
    int menu_selection = 0;
    int character_selection = 0;
    int selected_gros_cube = 0;
    int kart_selection = 0;
    int mode_selection = 0;
    int selected_mode = 0;
    int course_selection = 0;
    int noclip = 0;
    int race_music_lap = 0;
    setup_callbacks();
    /* PSP games are allowed to use the full 333/166 MHz clocks. */
    scePowerSetClockFrequency(333, 333, 166);
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    load_game_data(argc, argv);
    graphics_init();
    music_init();
    sfx_init();
    music_request(MUSIC_MAIN_MENU);
    while (1) {
        unsigned int pressed;
        sceCtrlReadBufferPositive(&pad, 1);
        pressed = pad.Buttons & ~previous_buttons;
        previous_buttons = pad.Buttons;
        if (screen != SCREEN_RACE) {
            if (pressed & (PSP_CTRL_UP | PSP_CTRL_DOWN)) sfx_play(SFX_MENU_MOVE);
            if (pressed & PSP_CTRL_CROSS) sfx_play(SFX_MENU_CONFIRM);
            if (pressed & PSP_CTRL_CIRCLE) sfx_play(SFX_MENU_BACK);
        }
        if (screen == SCREEN_MAIN_MENU) {
            music_request(MUSIC_MAIN_MENU);
            if (pressed & (PSP_CTRL_UP | PSP_CTRL_DOWN)) menu_selection = !menu_selection;
            if (pressed & PSP_CTRL_CROSS) {
                if (menu_selection) break;
                screen = SCREEN_CHARACTER;
            }
            render_menu(screen, menu_selection);
        } else if (screen == SCREEN_CHARACTER) {
            music_request(MUSIC_SINGLE_MENU);
            if (pressed & (PSP_CTRL_UP | PSP_CTRL_DOWN)) character_selection = !character_selection;
            if (pressed & PSP_CTRL_CROSS) {
                selected_gros_cube = character_selection;
                screen = selected_gros_cube ? SCREEN_MODE : SCREEN_KART;
            }
            if (pressed & PSP_CTRL_CIRCLE) screen = SCREEN_MAIN_MENU;
            render_menu(screen, screen == SCREEN_CHARACTER ? character_selection : 0);
        } else if (screen == SCREEN_KART) {
            music_request(MUSIC_SINGLE_MENU);
            if (pressed & (PSP_CTRL_UP | PSP_CTRL_DOWN)) kart_selection = !kart_selection;
            if (pressed & PSP_CTRL_CROSS) screen = SCREEN_MODE;
            if (pressed & PSP_CTRL_CIRCLE) screen = SCREEN_CHARACTER;
            render_menu(screen, screen == SCREEN_KART ? kart_selection : character_selection);
        } else if (screen == SCREEN_MODE) {
            music_request(MUSIC_SINGLE_MENU);
            if (pressed & (PSP_CTRL_UP | PSP_CTRL_DOWN)) mode_selection = !mode_selection;
            if (pressed & PSP_CTRL_CROSS) { selected_mode = mode_selection; screen = SCREEN_COURSE; }
            if (pressed & PSP_CTRL_CIRCLE) screen = selected_gros_cube ? SCREEN_CHARACTER : SCREEN_KART;
            render_menu(screen, screen == SCREEN_MODE ? mode_selection : 0);
        } else if (screen == SCREEN_COURSE) {
            music_request(MUSIC_SINGLE_MENU);
            if (pressed & PSP_CTRL_UP) course_selection = (course_selection + 2) % 3;
            if (pressed & PSP_CTRL_DOWN) course_selection = (course_selection + 1) % 3;
            if (pressed & PSP_CTRL_CROSS) {
                select_course_data(course_selection);
                course_hidden = 0;
                fps_window_start = 0;
                fps_frame_count = 0;
                fps_display = 0;
                memset(&kart, 0, sizeof(kart));
                if (active_course == 1) reset_pinball_balls();
                kart.position.x = active_start_x; kart.position.y = active_start_y; kart.position.z = active_start_z;
                kart.heading = active_start_heading; kart.speed = 0.0f;
                kart.camera_y = kart.position.y;
                kart.last_safe_position = kart.position;
                kart.last_safe_heading = kart.heading;
                kart.has_safe_position = 1;
                /* Time Trial always starts with its fixed reserve of three
                   mushrooms. Cup items will use their own roulette later. */
                kart.item_count = selected_mode == 0 ? 3 : 0;
                memset(&timing, 0, sizeof(timing));
                timing.active = selected_mode == 0;
                timing.lap = 1;
                timing.countdown_start = sceKernelGetSystemTimeWide();
                race_music_lap = 0;
                mansion_ambient_cooldown = 0;
                music_request(selected_mode == 0 ? MUSIC_TIME_TRIAL_START :
                    course_music_track());
                sfx_play(SFX_RACE_START);
                screen = SCREEN_RACE;
            }
            if (pressed & PSP_CTRL_CIRCLE) screen = SCREEN_MODE;
            if (screen != SCREEN_RACE) render_menu(screen, course_selection);
        } else {
            if (pressed & PSP_CTRL_START) screen = SCREEN_COURSE;
            if (screen == SCREEN_RACE) {
                unsigned long long now = sceKernelGetSystemTimeWide();
                const KartPhysics *race_physics = selected_gros_cube ? &physics_gros_cube :
                    (kart_selection ? &physics_b_dasher : &physics_standard_mr);
                /* Time Trial has a fixed three-mushroom reserve.  Reassert the
                   initial value until the countdown ends so a stale L-trigger
                   state from the menu cannot consume one before the race. */
                if (selected_mode == 0 && !timing.race_start) kart.item_count = 3;
                if (pressed & PSP_CTRL_SELECT) {
                    /* Collision view: hide the rendered course and expose the
                       KCL triangles plus their lower-left debug information. */
                    course_hidden = !course_hidden;
                    kcl_debug_enabled = course_hidden;
                    if (pad.Buttons & PSP_CTRL_LTRIGGER) noclip = !noclip;
                }
                /* Rocket start: the accelerator must be down during the final
                   second of the countdown (2.000000 through 2.999999).  Test
                   the held state rather than only the one-frame edge: X may
                   already be held when that window begins. */
                if (!timing.race_start) {
                    unsigned long long elapsed = now - timing.countdown_start;
                    if (elapsed >= 2000000ULL && elapsed < 3000000ULL &&
                        (pad.Buttons & (PSP_CTRL_SQUARE | PSP_CTRL_CROSS)))
                        timing.start_boost_armed = 1;
                }
                {
                    int was_finished = timing.finished;
                    int old_lap = timing.lap;
                    update_timing(&kart, now);
                    if (!was_finished && timing.finished) sfx_play(SFX_RACE_FINISH);
                    else if (timing.lap == 2 && old_lap < 2) sfx_play(SFX_LAP);
                }
                if (timing.finished) music_request(MUSIC_TIME_TRIAL_RESULTS);
                else if (timing.race_start && race_music_lap == 0) {
                    if (timing.start_boost_armed) {
                        kart.boost_frames = 105.0f;
                        kart.boost_limit = 1.48f;
                        kart.speed = race_physics->max_speed;
                        sfx_play(SFX_BOOST);
                    }
                    music_request(course_music_track());
                    race_music_lap = 1;
                } else if (timing.race_start && timing.lap >= 3 && race_music_lap < 3) {
                    music_request(course_final_lap_track());
                    race_music_lap = 3;
                }
                if ((!timing.active || timing.race_start) && !timing.finished) {
                    update_kart(&kart, &pad, race_physics, noclip);
                }
                if (active_course == 1 && !timing.finished)
                    update_pinball_objects(&kart, now);
                if (active_course == 2 && !timing.finished)
                    update_mansion_ambience(&kart);
                render(&kart, selected_gros_cube, kart_selection, now);
            }
        }
        sfx_set_engine(screen == SCREEN_RACE && timing.race_start && !timing.finished);
        sfx_set_drift(screen == SCREEN_RACE && timing.race_start &&
                      !timing.finished && kart.drifting);
    }
    sfx_shutdown();
    music_shutdown();
    free(course_model.vertices);
    free(course_model.texture);
    free(collision_model.vertices);
    free(collision_model.texture);
    free(collision_model.chunks);
    free(pinball_collision.vertices);
    free(pinball_collision.texture);
    free(pinball_collision.chunks);
    free(pinball_kcl);
    free(mansion_kcl);
    free(pinball_kcl_grid.indices);
    free(mansion_kcl_grid.indices);
    {
        int i;
        for (i = 0; i < MAX_COURSE_MATERIALS; ++i) {
            free(course_batches[i].vertices);
            free(course_batches[i].texture);
            free(course_batches[i].chunks);
            free(pinball_batches[i].vertices);
            free(pinball_batches[i].texture);
            free(pinball_batches[i].chunks);
            free(mansion_batches[i].vertices);
            free(mansion_batches[i].texture);
            free(mansion_batches[i].chunks);
        }
        for (i = 0; i < MAPOBJ_COUNT; ++i) {
            free(pinball_mapobjs[i].vertices);
            free(pinball_mapobjs[i].texture);
            free(pinball_mapobjs[i].chunks);
        }
        for (i = 0; i < MANSION_MAPOBJ_COUNT; ++i) {
            free(mansion_mapobjs[i].vertices);
            free(mansion_mapobjs[i].texture);
            free(mansion_mapobjs[i].chunks);
            free(mansion_animations[i].vertices);
            free(mansion_animations[i].texture);
        }
        free(mansion_teresa_alt_texture);
        for (i=0;i<4;++i) free(pinball_flipper_frames[i]);
        for (i=0;i<3;++i) free(pinball_bound_frames[i]);
        for (i=0;i<2;++i) free(pinball_flag_frames[i]);
    }
    free(road_model.vertices);
    free(road_model.texture);
    {
        int i;
        for (i = 0; i < 2; ++i) {
            free(kart_models[i].vertices);
            free(kart_models[i].texture);
        }
    }
    {
        int i;
        int kart_index;
        for (kart_index = 0; kart_index < 2; ++kart_index)
            for (i = 0; i < 4; ++i) {
                free(wheel_models[kart_index][i].vertices);
                free(wheel_models[kart_index][i].texture);
            }
    }
    free(mario_model.vertices);
    free(mario_model.texture);
    free(mario_animation.vertices);
    free(mario_animation.texture);
    {
        int i;
        for (i = 0; i < 3; ++i) {
            free(mushroom_icons[i].vertices);
            free(mushroom_icons[i].texture);
        }
    }
    sceGuTerm();
    sceKernelExitGame();
    return 0;
}
