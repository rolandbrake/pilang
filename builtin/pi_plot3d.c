#include "pi_plot3d.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include "../pi_func.h"
#include "../pi_object.h"

#include "pi_builtin.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*
    pi_plot3d.c
    ------------
    A small 3D plotting backend for Pilang.

    Main design decision:
    - Use a stable orthographic camera by default.
    - Normalize x, y, z into the same logical cube before projection.
    - Project axes, grid, surface, mesh, and wireframe through the same function.

    This avoids the old visual bug where the apparent 3D perspective looked skewed
    and the axes did not feel like they belonged to the same coordinate system.

    Grid placement:
    - The XY base grid lives at z = zmin (the floor).
    - The XZ grid lives at y = rows-1 (the front wall), matching the front X axis.
    - The YZ grid lives at x = 0 (the back-left wall), so it sits behind the
      surface instead of cutting through the front of it.
*/

static const int PLOT3D_MARGIN = 18;
static const int PLOT3D_AXIS = 0x222222;
static const int PLOT3D_X_AXIS = 0x222222;
static const int PLOT3D_Y_AXIS = 0x222222;
static const int PLOT3D_Z_AXIS = 0x222222;
static const int PLOT3D_GRID = 0xe2e2e2;
static const int PLOT3D_TEXT = 0x111111;
static const int PLOT3D_TICK = 0x222222;
static const double PLOT3D_DATA_INSET = 0.86;

static void set_drawColor(SDL_Renderer *r, int rgb, Uint8 a)
{
    SDL_SetRenderDrawColor(r,
                           (Uint8)((rgb >> 16) & 255),
                           (Uint8)((rgb >> 8) & 255),
                           (Uint8)(rgb & 255),
                           a);
}

static TTF_Font *get_openFont(int size)
{
    static int ttf_ready = 0;

    if (!ttf_ready)
    {
        if (TTF_Init() == 0)
            ttf_ready = 1;
        else
            return NULL;
    }

    const char *candidates[] = {
        "release/VeraMono.ttf",
        "VeraMono.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/calibri.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/Library/Fonts/Arial.ttf",
    };

    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++)
    {
        TTF_Font *font = TTF_OpenFont(candidates[i], size);
        if (font)
            return font;
    }

    return NULL;
}

static void draw_text_angle(SDL_Renderer *r, TTF_Font *font, const char *text,
                            int x, int y, int rgb, int center_x, int center_y,
                            double angle)
{
    if (!font || !text)
        return;

    int old_style = TTF_GetFontStyle(font);
    TTF_SetFontStyle(font, old_style | TTF_STYLE_BOLD);

    SDL_Color color = {
        (Uint8)((rgb >> 16) & 255),
        (Uint8)((rgb >> 8) & 255),
        (Uint8)(rgb & 255),
        255,
    };

    SDL_Surface *surface = TTF_RenderUTF8_Blended(font, text, color);
    if (!surface)
        return;

    SDL_Texture *texture = SDL_CreateTextureFromSurface(r, surface);
    if (texture)
    {
        SDL_Rect dst = {x, y, surface->w, surface->h};
        if (center_x)
            dst.x -= surface->w / 2;
        if (center_y)
            dst.y -= surface->h / 2;

        SDL_RenderCopyEx(r, texture, NULL, &dst, angle, NULL, SDL_FLIP_NONE);
        SDL_DestroyTexture(texture);
    }

    SDL_FreeSurface(surface);
    TTF_SetFontStyle(font, old_style);
}

static void draw_text_at(SDL_Renderer *r, TTF_Font *font, const char *text,
                         int x, int y, int rgb, int center_x, int center_y)
{
    draw_text_angle(r, font, text, x, y, rgb, center_x, center_y, 0.0);
}

static void format_tick(char *buffer, size_t size, double value)
{
    if (fabs(value) < 1e-9)
        snprintf(buffer, size, "0");
    else if (fabs(value - round(value)) < 1e-6)
        snprintf(buffer, size, "%.0f", value);
    else
        snprintf(buffer, size, "%.2f", value);
}

static void heat_rgb(double t, Uint8 *R, Uint8 *G, Uint8 *B)
{
    if (t < 0.0)
        t = 0.0;
    if (t > 1.0)
        t = 1.0;

    double stops[][3] = {
        {68, 1, 84},
        {59, 82, 139},
        {33, 145, 140},
        {94, 201, 98},
        {253, 231, 37},
    };

    double scaled = t * 4.0;
    int i = (int)floor(scaled);
    if (i < 0)
        i = 0;
    if (i > 3)
        i = 3;

    double f = scaled - i;
    *R = (Uint8)(stops[i][0] + (stops[i + 1][0] - stops[i][0]) * f);
    *G = (Uint8)(stops[i][1] + (stops[i + 1][1] - stops[i][1]) * f);
    *B = (Uint8)(stops[i][2] + (stops[i + 1][2] - stops[i][2]) * f);
}

static int heat_color(double t)
{
    Uint8 R, G, B;
    heat_rgb(t, &R, &G, &B);
    return ((int)R << 16) | ((int)G << 8) | (int)B;
}

static int blend_rgb(int a, int b, double t)
{
    if (t < 0.0)
        t = 0.0;
    if (t > 1.0)
        t = 1.0;

    int ar = (a >> 16) & 255;
    int ag = (a >> 8) & 255;
    int ab = a & 255;
    int br = (b >> 16) & 255;
    int bg = (b >> 8) & 255;
    int bb = b & 255;

    int rr = (int)(ar + (br - ar) * t);
    int rg = (int)(ag + (bg - ag) * t);
    int rb = (int)(ab + (bb - ab) * t);

    return (rr << 16) | (rg << 8) | rb;
}

static int clamp_int(int value, int lo, int hi)
{
    if (value < lo)
        return lo;
    if (value > hi)
        return hi;
    return value;
}

static void draw_thick_line(SDL_Renderer *r, int x0, int y0, int x1, int y1, int thickness)
{
    if (thickness <= 1)
    {
        SDL_RenderDrawLine(r, x0, y0, x1, y1);
        return;
    }

    double dx = (double)x1 - x0;
    double dy = (double)y1 - y0;
    double len = sqrt(dx * dx + dy * dy);
    if (len < 1e-9)
        return;

    double nx = -dy / len;
    double ny = dx / len;
    int half = thickness / 2;

    for (int i = -half; i <= half; i++)
    {
        int ox = (int)lrint(nx * i);
        int oy = (int)lrint(ny * i);
        SDL_RenderDrawLine(r, x0 + ox, y0 + oy, x1 + ox, y1 + oy);
    }
}

static Value make_plot3dSeries(vm_t *vm, PiChart3D *chart, const char *kind, list_t *tail)
{
    list_t *parts = list_create(VALUE_SIZE);
    Value kind_value = NEW_OBJ(add_obj(vm, new_pistring(strdup(kind))));
    list_add(parts, &kind_value);

    for (int i = 0; i < LIST_SIZE(tail); i++)
    {
        Value *item = (Value *)list_getAt(tail, i);
        list_add(parts, item);
    }

    Object *series = new_list(parts);
    add_obj(vm, series);

    Value series_value = NEW_OBJ(series);
    list_add(chart->series, &series_value);

    list_free(tail);
    return NEW_OBJ((Object *)chart);
}

static Value make_plot3dTensorSeries(vm_t *vm, int argc, Value *argv,
                                     const char *kind,
                                     const char *chart_error,
                                     const char *tensor_error)
{
    if (argc < 1 || !IS_CHART3D(argv[0]))
    {
        vm_error(vm, chart_error);
        return NIL_VAL;
    }

    if (argc < 2 || !IS_TENSOR(argv[1]) || AS_TENSOR(argv[1])->ndim != 2)
    {
        vm_error(vm, tensor_error);
        return NIL_VAL;
    }

    PiChart3D *chart = AS_CHART3D(argv[0]);
    list_t *tail = list_create(VALUE_SIZE);
    list_add(tail, &argv[1]);

    for (int i = 2; i < argc; i++)
    {
        if (IS_NUM(argv[i]) || IS_BOOL(argv[i]))
        {
            Value option = argv[i];
            list_add(tail, &option);
        }
    }

    return make_plot3dSeries(vm, chart, kind, tail);
}

Value pt3d_chart(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_CONTEXT(argv[0]))
    {
        vm_error(vm, "plot3d.chart() takes a draw context");
        return NIL_VAL;
    }

    return NEW_OBJ(add_obj(vm, new_chart3d(AS_CONTEXT(argv[0]))));
}

Value pt3d_surface(vm_t *vm, int argc, Value *argv)
{
    return make_plot3dTensorSeries(vm, argc, argv, "surface",
                                   "surface() takes a plot3d chart as first argument",
                                   "surface requires a 2d tensor as second argument");
}

Value pt3d_mesh(vm_t *vm, int argc, Value *argv)
{
    return make_plot3dTensorSeries(vm, argc, argv, "mesh",
                                   "mesh() takes a plot3d chart as first argument",
                                   "mesh requires a 2d tensor as second argument");
}

Value pt3d_wireframe(vm_t *vm, int argc, Value *argv)
{
    return make_plot3dTensorSeries(vm, argc, argv, "wireframe",
                                   "wireframe() takes a plot3d chart as first argument",
                                   "wireframe requires a 2d tensor as second argument");
}

Value pt3d_scatter(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_CHART3D(argv[0]))
    {
        vm_error(vm, "scatter() takes a plot3d chart as first argument");
        return NIL_VAL;
    }

    int valid_lists = argc >= 4 && IS_LIST(argv[1]) && IS_LIST(argv[2]) && IS_LIST(argv[3]);
    int valid_tensor = argc >= 2 && IS_TENSOR(argv[1]) && AS_TENSOR(argv[1])->ndim == 2 && AS_TENSOR(argv[1])->cols >= 3;

    if (!valid_lists && !valid_tensor)
    {
        vm_error(vm, "scatter requires x, y, z lists or an Nx3 tensor");
        return NIL_VAL;
    }

    PiChart3D *chart = AS_CHART3D(argv[0]);
    list_t *tail = list_create(VALUE_SIZE);

    int option_start = 0;
    if (valid_lists)
    {
        list_add(tail, &argv[1]);
        list_add(tail, &argv[2]);
        list_add(tail, &argv[3]);
        option_start = 4;
    }
    else
    {
        list_add(tail, &argv[1]);
        option_start = 2;
    }

    for (int i = option_start; i < argc; i++)
    {
        if (IS_NUM(argv[i]))
        {
            Value option = argv[i];
            list_add(tail, &option);
        }
    }

    return make_plot3dSeries(vm, chart, "scatter", tail);
}

static void chart3d_setLabel(char **dst, Value value)
{
    if (*dst)
        free(*dst);
    *dst = strdup(AS_CSTRING(value));
}

Value pt3d_title(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_CHART3D(argv[0]) || !IS_STRING(argv[1]))
    {
        vm_error(vm, "title() takes a plot3d chart and string");
        return NIL_VAL;
    }

    chart3d_setLabel(&AS_CHART3D(argv[0])->title, argv[1]);
    return argv[0];
}

Value pt3d_xlabel(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_CHART3D(argv[0]) || !IS_STRING(argv[1]))
    {
        vm_error(vm, "xlabel() takes a plot3d chart and string");
        return NIL_VAL;
    }

    chart3d_setLabel(&AS_CHART3D(argv[0])->xlabel, argv[1]);
    return argv[0];
}

Value pt3d_ylabel(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_CHART3D(argv[0]) || !IS_STRING(argv[1]))
    {
        vm_error(vm, "ylabel() takes a plot3d chart and string");
        return NIL_VAL;
    }

    chart3d_setLabel(&AS_CHART3D(argv[0])->ylabel, argv[1]);
    return argv[0];
}

Value pt3d_zlabel(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_CHART3D(argv[0]) || !IS_STRING(argv[1]))
    {
        vm_error(vm, "zlabel() takes a plot3d chart and string");
        return NIL_VAL;
    }

    chart3d_setLabel(&AS_CHART3D(argv[0])->zlabel, argv[1]);
    return argv[0];
}

Value pt3d_grid(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_CHART3D(argv[0]))
    {
        vm_error(vm, "grid() takes a plot3d chart and boolean");
        return NIL_VAL;
    }

    AS_CHART3D(argv[0])->show_grid = IS_BOOL(argv[1]) ? AS_BOOL(argv[1]) : (IS_NUM(argv[1]) && AS_NUM(argv[1]) != 0.0);
    return argv[0];
}

Value pt3d_view(vm_t *vm, int argc, Value *argv)
{
    if (argc < 3 || !IS_CHART3D(argv[0]) || !IS_NUM(argv[1]) || !IS_NUM(argv[2]))
    {
        vm_error(vm, "view() takes a plot3d chart, azimuth, and elevation");
        return NIL_VAL;
    }

    PiChart3D *chart = AS_CHART3D(argv[0]);
    chart->azimuth = AS_NUM(argv[1]);
    chart->elevation = AS_NUM(argv[2]);

    if (argc > 3 && IS_NUM(argv[3]))
        chart->distance = AS_NUM(argv[3]);

    return argv[0];
}

static int subplot_rect(int canvas_w, int canvas_h, int rows, int cols, int index, SDL_Rect *out)
{
    if (!out || rows <= 0 || cols <= 0 || index < 1 || index > rows * cols)
        return 0;

    int idx = index - 1;
    int row = idx / cols;
    int col = idx % cols;
    int min_side = canvas_w < canvas_h ? canvas_w : canvas_h;
    int gap = (rows > 1 || cols > 1) ? clamp_int(min_side / 160, 2, 6) : 0;

    int cell_w = (canvas_w - gap * (cols + 1)) / cols;
    int cell_h = (canvas_h - gap * (rows + 1)) / rows;
    if (cell_w <= 0 || cell_h <= 0)
        return 0;

    out->x = gap + col * (cell_w + gap);
    out->y = gap + row * (cell_h + gap);
    out->w = cell_w;
    out->h = cell_h;
    return 1;
}

static int chart3d_has_subplot(PiChart3D *chart)
{
    return chart && (chart->subplot_rows > 1 || chart->subplot_cols > 1);
}

Value pt3d_subplot(vm_t *vm, int argc, Value *argv)
{
    if (argc < 4 || !IS_CHART3D(argv[0]) || !IS_NUM(argv[1]) || !IS_NUM(argv[2]) || !IS_NUM(argv[3]))
    {
        vm_error(vm, "subplot() takes a plot3d chart, rows, columns, and 1-based index");
        return NIL_VAL;
    }

    int rows = (int)AS_NUM(argv[1]);
    int cols = (int)AS_NUM(argv[2]);
    int index = (int)AS_NUM(argv[3]);

    if (rows < 1 || cols < 1 || index < 1 || index > rows * cols)
    {
        vm_error(vm, "subplot() index must be inside rows * columns");
        return NIL_VAL;
    }

    PiChart3D *chart = AS_CHART3D(argv[0]);
    chart->subplot_rows = rows;
    chart->subplot_cols = cols;
    chart->subplot_index = index;
    return argv[0];
}

typedef struct
{
    double x, y, z;
} Vec3;

typedef struct
{
    double x, y;
    double depth;
} Point2D;

typedef struct
{
    double azimuth;
    double elevation;
    double sx, sy, sz;
    int cx, cy;
    double scale;
} Projector3D;

typedef struct
{
    PiTensor *M;
    double zmin, zmax;
    double xmin, xmax;
    double ymin, ymax;
    double rows, cols;
} SurfaceData;

static Vec3 vec3(double x, double y, double z)
{
    Vec3 v;
    v.x = x;
    v.y = y;
    v.z = z;
    return v;
}

static double vec_dot(Vec3 a, Vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static Vec3 rotate_z(Vec3 p, double a)
{
    double c = cos(a);
    double s = sin(a);
    return vec3(p.x * c - p.y * s,
                p.x * s + p.y * c,
                p.z);
}

static Vec3 rotate_x(Vec3 p, double a)
{
    double c = cos(a);
    double s = sin(a);
    return vec3(p.x,
                p.y * c - p.z * s,
                p.y * s + p.z * c);
}

static void tensor_minmax(PiTensor *M, double *zmin, double *zmax)
{
    *zmin = INFINITY;
    *zmax = -INFINITY;

    int idx[2];
    for (int r = 0; r < M->rows; r++)
    {
        idx[0] = r;
        for (int c = 0; c < M->cols; c++)
        {
            idx[1] = c;
            double z = tensor_get(M, idx);
            if (!isfinite(z))
                continue;

            if (z < *zmin)
                *zmin = z;
            if (z > *zmax)
                *zmax = z;
        }
    }

    if (!isfinite(*zmin) || !isfinite(*zmax))
    {
        *zmin = 0.0;
        *zmax = 1.0;
    }

    if (*zmax <= *zmin)
        *zmax = *zmin + 1e-9;
}

static double tensor_at(PiTensor *M, int row, int col)
{
    int idx[2] = {row, col};
    return tensor_get(M, idx);
}

static Vec3 normalize_data_point(SurfaceData *data, double x, double y, double z)
{
    double nx = 0.0;
    double ny = 0.0;
    double nz = 0.0;

    if (data->xmax > data->xmin)
        nx = ((x - data->xmin) / (data->xmax - data->xmin)) * 2.0 - 1.0;

    if (data->ymax > data->ymin)
        ny = ((y - data->ymin) / (data->ymax - data->ymin)) * 2.0 - 1.0;

    nz = ((z - data->zmin) / (data->zmax - data->zmin)) * 2.0 - 1.0;

    /* z is visually compressed a little so labels and axes do not explode. */
    nz *= 0.72;

    return vec3(nx, ny, nz);
}

static Point2D project_vec(Projector3D *p, Vec3 v)
{
    /*
        Explicit 3D-to-2D orthographic projection.

        This replaces the previous rotate_z + rotate_x path because it was hard
        to reason about and made the z-axis appear flipped for some views.

        Screen convention:
        - larger screen y goes downward
        - larger data z must always move upward on screen
        - x and y rotate around z by azimuth
        - elevation tilts the ground plane down
    */
    double ca = cos(p->azimuth);
    double sa = sin(p->azimuth);
    double ce = cos(p->elevation);
    double se = sin(p->elevation);

    double sx = v.x * ca - v.y * sa;
    double sy = v.x * sa * se + v.y * ca * se - v.z * ce;
    double depth = v.x * sa * ce + v.y * ca * ce + v.z * se;

    Point2D out;
    out.x = p->cx + sx * p->scale;
    out.y = p->cy + sy * p->scale;
    out.depth = depth;
    return out;
}

static Point2D project_data(Projector3D *p, SurfaceData *data, double x, double y, double z)
{
    return project_vec(p, normalize_data_point(data, x, y, z));
}

static Point2D project_plot_data(Projector3D *p, SurfaceData *data, double x, double y, double z)
{
    Vec3 v = normalize_data_point(data, x, y, z);
    v.x *= PLOT3D_DATA_INSET;
    v.y *= PLOT3D_DATA_INSET;
    v.z *= PLOT3D_DATA_INSET;
    return project_vec(p, v);
}

static Projector3D make_projector(PiChart3D *chart, SDL_Rect plot)
{
    Projector3D p;

    /*
        Defaults are intentionally isometric-like.
        The user may override with plot3d.view(chart, azimuth, elevation).
    */
    double az = chart->azimuth;
    double el = chart->elevation;

    if (fabs(az) < 1e-9 && fabs(el) < 1e-9)
    {
        az = -45.0;
        el = 32.0;
    }

    p.azimuth = az * M_PI / 180.0;
    p.elevation = el * M_PI / 180.0;

    p.cx = plot.x + plot.w / 2;
    /* True visual center. Keep enough bottom margin for Y/X ticks and labels. */
    p.cy = plot.y + (int)(plot.h * 0.50);
    p.scale = fmin(plot.w * 0.30, plot.h * 0.35);

    p.sx = 1.0;
    p.sy = 1.0;
    p.sz = 1.0;

    return p;
}

typedef struct
{
    Point2D p[4];
    double zavg;
    double depth;
    int color;
} Face3D;

static int compare_faces_back_to_front(const void *a, const void *b)
{
    const Face3D *fa = (const Face3D *)a;
    const Face3D *fb = (const Face3D *)b;

    if (fa->depth < fb->depth)
        return -1;
    if (fa->depth > fb->depth)
        return 1;
    return 0;
}

static void fill_triangle(SDL_Renderer *r, int x0, int y0, int x1, int y1, int x2, int y2)
{
    if (y1 < y0)
    {
        int tx = x0, ty = y0;
        x0 = x1;
        y0 = y1;
        x1 = tx;
        y1 = ty;
    }
    if (y2 < y0)
    {
        int tx = x0, ty = y0;
        x0 = x2;
        y0 = y2;
        x2 = tx;
        y2 = ty;
    }
    if (y2 < y1)
    {
        int tx = x1, ty = y1;
        x1 = x2;
        y1 = y2;
        x2 = tx;
        y2 = ty;
    }

    if (y0 == y2)
        return;

    for (int y = y0; y <= y2; y++)
    {
        double a = (double)(y - y0) / (double)(y2 - y0);
        double xb = x0 + (x2 - x0) * a;
        double xs;

        if (y < y1)
            xs = y1 == y0 ? x1 : x0 + (x1 - x0) * ((double)(y - y0) / (double)(y1 - y0));
        else
            xs = y2 == y1 ? x1 : x1 + (x2 - x1) * ((double)(y - y1) / (double)(y2 - y1));

        int xa = (int)lrint(xs);
        int xb_i = (int)lrint(xb);

        if (xa > xb_i)
        {
            int t = xa;
            xa = xb_i;
            xb_i = t;
        }

        SDL_RenderDrawLine(r, xa, y, xb_i, y);
    }
}

static void draw_face(SDL_Renderer *r, Face3D *f)
{
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    set_drawColor(r, f->color, 238);

    fill_triangle(r,
                  (int)lrint(f->p[0].x), (int)lrint(f->p[0].y),
                  (int)lrint(f->p[1].x), (int)lrint(f->p[1].y),
                  (int)lrint(f->p[2].x), (int)lrint(f->p[2].y));

    fill_triangle(r,
                  (int)lrint(f->p[0].x), (int)lrint(f->p[0].y),
                  (int)lrint(f->p[2].x), (int)lrint(f->p[2].y),
                  (int)lrint(f->p[3].x), (int)lrint(f->p[3].y));

    set_drawColor(r, 0x000000, 45);
    SDL_RenderDrawLine(r, (int)f->p[0].x, (int)f->p[0].y, (int)f->p[1].x, (int)f->p[1].y);
    SDL_RenderDrawLine(r, (int)f->p[1].x, (int)f->p[1].y, (int)f->p[2].x, (int)f->p[2].y);
    SDL_RenderDrawLine(r, (int)f->p[2].x, (int)f->p[2].y, (int)f->p[3].x, (int)f->p[3].y);
    SDL_RenderDrawLine(r, (int)f->p[3].x, (int)f->p[3].y, (int)f->p[0].x, (int)f->p[0].y);

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

static double edge_value(Point2D a, Point2D b, double x, double y)
{
    return (x - a.x) * (b.y - a.y) - (y - a.y) * (b.x - a.x);
}

static void draw_depth_triangle(SDL_Renderer *r, double *zbuf, int W, int H,
                                Point2D a, Point2D b, Point2D c, int color)
{
    double area = edge_value(a, b, c.x, c.y);
    if (fabs(area) < 1e-9)
        return;

    int min_x = (int)floor(fmin(a.x, fmin(b.x, c.x)));
    int max_x = (int)ceil(fmax(a.x, fmax(b.x, c.x)));
    int min_y = (int)floor(fmin(a.y, fmin(b.y, c.y)));
    int max_y = (int)ceil(fmax(a.y, fmax(b.y, c.y)));

    if (min_x < 0)
        min_x = 0;
    if (min_y < 0)
        min_y = 0;
    if (max_x >= W)
        max_x = W - 1;
    if (max_y >= H)
        max_y = H - 1;

    set_drawColor(r, color, 255);

    for (int y = min_y; y <= max_y; y++)
    {
        for (int x = min_x; x <= max_x; x++)
        {
            double px = x + 0.5;
            double py = y + 0.5;
            double w0 = edge_value(b, c, px, py) / area;
            double w1 = edge_value(c, a, px, py) / area;
            double w2 = edge_value(a, b, px, py) / area;

            if (w0 < -1e-9 || w1 < -1e-9 || w2 < -1e-9)
                continue;

            double depth = a.depth * w0 + b.depth * w1 + c.depth * w2;
            int zi = y * W + x;
            if (depth <= zbuf[zi])
                continue;

            zbuf[zi] = depth;
            SDL_RenderDrawPoint(r, x, y);
        }
    }
}

static void draw_depth_face(SDL_Renderer *r, double *zbuf, int W, int H, Face3D *f)
{
    draw_depth_triangle(r, zbuf, W, H, f->p[0], f->p[1], f->p[2], f->color);
    draw_depth_triangle(r, zbuf, W, H, f->p[0], f->p[2], f->p[3], f->color);
}

static void draw_axis_line(SDL_Renderer *r, Point2D a, Point2D b, int color, int thickness)
{
    set_drawColor(r, color, 255);
    draw_thick_line(r,
                    (int)lrint(a.x), (int)lrint(a.y),
                    (int)lrint(b.x), (int)lrint(b.y),
                    thickness);
}

static void draw_tick_mark(SDL_Renderer *r, Point2D p, Point2D axis_start)
{
    double dx = p.x - axis_start.x;
    double dy = p.y - axis_start.y;
    double len = sqrt(dx * dx + dy * dy);
    if (len < 1e-9)
        return;

    dx /= len;
    dy /= len;
    double nx = -dy;
    double ny = dx;

    SDL_RenderDrawLine(r,
                       (int)lrint(p.x - nx * 4.0),
                       (int)lrint(p.y - ny * 4.0),
                       (int)lrint(p.x + nx * 4.0),
                       (int)lrint(p.y + ny * 4.0));
}

static void offset_away_from_center(Point2D p, Point2D center, double distance, int *x, int *y)
{
    double dx = p.x - center.x;
    double dy = p.y - center.y;
    double len = sqrt(dx * dx + dy * dy);

    if (len < 1e-9)
    {
        *x = (int)lrint(p.x);
        *y = (int)lrint(p.y);
        return;
    }

    dx /= len;
    dy /= len;

    *x = (int)lrint(p.x + dx * distance);
    *y = (int)lrint(p.y + dy * distance);
}

static void draw_tick_on_axis(SDL_Renderer *r, Point2D p, Point2D axis_a, Point2D axis_b)
{
    double dx = axis_b.x - axis_a.x;
    double dy = axis_b.y - axis_a.y;
    double len = sqrt(dx * dx + dy * dy);

    if (len < 1e-9)
        return;

    dx /= len;
    dy /= len;

    double nx = -dy;
    double ny = dx;

    SDL_RenderDrawLine(r,
                       (int)lrint(p.x - nx * 5.0),
                       (int)lrint(p.y - ny * 5.0),
                       (int)lrint(p.x + nx * 5.0),
                       (int)lrint(p.y + ny * 5.0));
}

static void fill_quad(SDL_Renderer *r, Point2D a, Point2D b, Point2D c, Point2D d)
{
    fill_triangle(r,
                  (int)lrint(a.x), (int)lrint(a.y),
                  (int)lrint(b.x), (int)lrint(b.y),
                  (int)lrint(c.x), (int)lrint(c.y));

    fill_triangle(r,
                  (int)lrint(a.x), (int)lrint(a.y),
                  (int)lrint(c.x), (int)lrint(c.y),
                  (int)lrint(d.x), (int)lrint(d.y));
}

static void draw_axis_grid(SDL_Renderer *r, PiChart3D *chart,
                           Projector3D *proj, SurfaceData *data)
{
    if (!chart->show_grid)
        return;

    const int tick_count = 6;

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    /*
        Draw the three axis planes that surround the surface. The two side
        panes sit on the walls at y = 0 (XZ) and x = cols - 1 (YZ), sharing
        the corner (x = cols - 1, y = 0).

        - XY base plane at z = zmin (the floor under the surface)
        - XZ side plane at y = 0
        - YZ side plane at x = cols - 1, shared by the Y and Z axes, with
          Y to the right of Z.

        Each plane is first filled with a very light gray pane, then a
        slightly darker gray grid is drawn on top so the panels read as
        soft, filled reference panels (like matplotlib's pane fill) rather
        than a bare set of gray strokes on white.
    */
    Point2D xy_a = project_data(proj, data, data->xmin, data->ymin, data->zmin);
    Point2D xy_b = project_data(proj, data, data->xmax, data->ymin, data->zmin);
    Point2D xy_c = project_data(proj, data, data->xmax, data->ymax, data->zmin);
    Point2D xy_d = project_data(proj, data, data->xmin, data->ymax, data->zmin);

    Point2D xz_a = project_data(proj, data, data->xmin, data->ymin, data->zmin);
    Point2D xz_b = project_data(proj, data, data->xmax, data->ymin, data->zmin);
    Point2D xz_c = project_data(proj, data, data->xmax, data->ymin, data->zmax);
    Point2D xz_d = project_data(proj, data, data->xmin, data->ymin, data->zmax);

    Point2D yz_a = project_data(proj, data, data->xmax, data->ymin, data->zmin);
    Point2D yz_b = project_data(proj, data, data->xmax, data->ymax, data->zmin);
    Point2D yz_c = project_data(proj, data, data->xmax, data->ymax, data->zmax);
    Point2D yz_d = project_data(proj, data, data->xmax, data->ymin, data->zmax);

    set_drawColor(r, 0xf2f2f2, 255);
    fill_quad(r, xy_a, xy_b, xy_c, xy_d);
    fill_quad(r, xz_a, xz_b, xz_c, xz_d);
    fill_quad(r, yz_a, yz_b, yz_c, yz_d);

    set_drawColor(r, PLOT3D_GRID, 255);

    for (int i = 0; i <= tick_count; i++)
    {
        double t = (double)i / (double)tick_count;
        double x = data->xmin + (data->xmax - data->xmin) * t;
        double y = data->ymin + (data->ymax - data->ymin) * t;
        double z = data->zmin + (data->zmax - data->zmin) * t;

        /* XY base grid (floor) */
        Point2D xy_x0 = project_data(proj, data, x, data->ymin, data->zmin);
        Point2D xy_x1 = project_data(proj, data, x, data->ymax, data->zmin);
        Point2D xy_y0 = project_data(proj, data, data->xmin, y, data->zmin);
        Point2D xy_y1 = project_data(proj, data, data->xmax, y, data->zmin);

        SDL_RenderDrawLine(r, (int)lrint(xy_x0.x), (int)lrint(xy_x0.y),
                              (int)lrint(xy_x1.x), (int)lrint(xy_x1.y));
        SDL_RenderDrawLine(r, (int)lrint(xy_y0.x), (int)lrint(xy_y0.y),
                              (int)lrint(xy_y1.x), (int)lrint(xy_y1.y));

        /* XZ side grid, at y = 0. */
        Point2D xz_x0 = project_data(proj, data, x, data->ymin, data->zmin);
        Point2D xz_x1 = project_data(proj, data, x, data->ymin, data->zmax);
        Point2D xz_z0 = project_data(proj, data, data->xmin, data->ymin, z);
        Point2D xz_z1 = project_data(proj, data, data->xmax, data->ymin, z);

        SDL_RenderDrawLine(r, (int)lrint(xz_x0.x), (int)lrint(xz_x0.y),
                              (int)lrint(xz_x1.x), (int)lrint(xz_x1.y));
        SDL_RenderDrawLine(r, (int)lrint(xz_z0.x), (int)lrint(xz_z0.y),
                              (int)lrint(xz_z1.x), (int)lrint(xz_z1.y));

        /* YZ side grid, at x = cols - 1. Shared by Y and Z axes, with Y to
           the right of Z. */
        Point2D yz_y0 = project_data(proj, data, data->xmax, y, data->zmin);
        Point2D yz_y1 = project_data(proj, data, data->xmax, y, data->zmax);
        Point2D yz_z0 = project_data(proj, data, data->xmax, data->ymin, z);
        Point2D yz_z1 = project_data(proj, data, data->xmax, data->ymax, z);

        SDL_RenderDrawLine(r, (int)lrint(yz_y0.x), (int)lrint(yz_y0.y),
                              (int)lrint(yz_y1.x), (int)lrint(yz_y1.y));
        SDL_RenderDrawLine(r, (int)lrint(yz_z0.x), (int)lrint(yz_z0.y),
                              (int)lrint(yz_z1.x), (int)lrint(yz_z1.y));
    }

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}


static void draw_axis_plain(SDL_Renderer *r, Point2D a, Point2D b, int color)
{
    /* Thin neutral axes, no arrowheads. */
    draw_axis_line(r, a, b, color, 1);
}

static void draw_axis_tick_label(SDL_Renderer *r, TTF_Font *font,
                                 Point2D p, double nx, double ny,
                                 const char *text, int color)
{
    /*
        nx, ny is a fixed, pre-computed outward screen-space direction for
        this axis (NOT derived from the local tangent of the axis line at
        p). Using a fixed outward direction keeps every tick mark and label
        pointing the same way along an axis, regardless of where the tick
        sits, and keeps them perpendicular-looking and outside the plot
        area instead of drifting back toward the data.
    */
    set_drawColor(r, color, 255);
    SDL_RenderDrawLine(r,
                       (int)lrint(p.x - nx * 4.0),
                       (int)lrint(p.y - ny * 4.0),
                       (int)lrint(p.x + nx * 4.0),
                       (int)lrint(p.y + ny * 4.0));

    if (font && text)
    {
        int lx = (int)lrint(p.x + nx * 18.0);
        int ly = (int)lrint(p.y + ny * 18.0);
        draw_text_at(r, font, text, lx, ly, color, 1, 1);
    }
}

static void draw_axes_ticks_labels(SDL_Renderer *r, TTF_Font *font,
                                   PiChart3D *chart, Projector3D *proj,
                                   SurfaceData *data)
{
    /*
        X is drawn on the front floor edge (high y). Y is drawn on the
        back floor edge (x = 0), and Z is drawn on the back-left vertical
        edge (x = 0, y = 0), matching the YZ grid plane drawn at x = 0 in
        draw_axis_grid().

        All ticks are projected from real 3D positions on those axes.
        This keeps tick labels, tick marks, and axis lines consistent.

        Tick marks and axis-name labels use a FIXED outward screen-space
        direction per axis (computed once, below) rather than a per-point
        tangent normal. This keeps every tick perpendicular-looking and
        keeps every label outside the plot area, regardless of where along
        the axis it sits.
    */
    Point2D O = project_data(proj, data, data->xmin, data->ymin, data->zmin);
    Point2D X0 = project_data(proj, data, data->xmin, data->ymax, data->zmin);
    Point2D X = project_data(proj, data, data->xmax, data->ymax, data->zmin);
    Point2D Y = project_data(proj, data, data->xmin, data->ymax, data->zmin);
    Point2D Z = project_data(proj, data, data->xmin, data->ymin, data->zmax);

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    draw_axis_plain(r, X0, X, PLOT3D_X_AXIS);
    draw_axis_plain(r, O, Y, PLOT3D_Y_AXIS);
    draw_axis_plain(r, O, Z, PLOT3D_Z_AXIS);

    /*
        Fixed outward directions, derived from the screen-space center of
        the plotted cube. "Outward" means away from the cube's screen
        center, so ticks/labels always point away from the data toward
        the empty margin.
    */
    Point2D center = project_data(proj, data,
                                  (data->xmin + data->xmax) * 0.5,
                                  (data->ymin + data->ymax) * 0.5,
                                  (data->zmin + data->zmax) * 0.5);

    double xnx, xny, ynx, yny, znx, zny;

    /* X axis direction (X0 -> X); outward = perpendicular, away from center. */
    {
        double dx = X.x - X0.x, dy = X.y - X0.y;
        double len = sqrt(dx * dx + dy * dy);
        dx /= len; dy /= len;
        double n1x = -dy, n1y = dx;
        double mx = (X0.x + X.x) * 0.5, my = (X0.y + X.y) * 0.5;
        double toward = (mx - center.x) * n1x + (my - center.y) * n1y;
        if (toward < 0) { n1x = -n1x; n1y = -n1y; }
        xnx = n1x; xny = n1y;
    }

    /* Y axis direction (O -> Y); outward = perpendicular, away from center. */
    {
        double dx = Y.x - O.x, dy = Y.y - O.y;
        double len = sqrt(dx * dx + dy * dy);
        dx /= len; dy /= len;
        double n1x = -dy, n1y = dx;
        double mx = (O.x + Y.x) * 0.5, my = (O.y + Y.y) * 0.5;
        double toward = (mx - center.x) * n1x + (my - center.y) * n1y;
        if (toward < 0) { n1x = -n1x; n1y = -n1y; }
        ynx = n1x; yny = n1y;
    }

    /* Z axis direction (O -> Z); outward = perpendicular, away from center. */
    {
        double dx = Z.x - O.x, dy = Z.y - O.y;
        double len = sqrt(dx * dx + dy * dy);
        dx /= len; dy /= len;
        double n1x = -dy, n1y = dx;
        double mx = (O.x + Z.x) * 0.5, my = (O.y + Z.y) * 0.5;
        double toward = (mx - center.x) * n1x + (my - center.y) * n1y;
        if (toward < 0) { n1x = -n1x; n1y = -n1y; }
        znx = n1x; zny = n1y;
    }

    char label[64];
    const int tick_count = 4;

    for (int i = 0; i <= tick_count; i++)
    {
        double t = (double)i / (double)tick_count;

        double xv = data->xmin + (data->xmax - data->xmin) * t;
        Point2D px = project_data(proj, data, xv, data->ymax, data->zmin);
        format_tick(label, sizeof(label), xv);
        draw_axis_tick_label(r, font, px, xnx, xny, label, PLOT3D_X_AXIS);

        double yv = data->ymin + (data->ymax - data->ymin) * t;
        Point2D py = project_data(proj, data, data->xmin, yv, data->zmin);
        format_tick(label, sizeof(label), yv);
        draw_axis_tick_label(r, font, py, ynx, yny, label, PLOT3D_Y_AXIS);

        double zv = data->zmin + (data->zmax - data->zmin) * t;
        Point2D pz = project_data(proj, data, data->xmin, data->ymin, zv);
        format_tick(label, sizeof(label), zv);
        draw_axis_tick_label(r, font, pz, znx, zny, label, PLOT3D_Z_AXIS);
    }

    if (font)
    {
        const char *xl = chart->xlabel ? chart->xlabel : "X";
        const char *yl = chart->ylabel ? chart->ylabel : "Y";
        const char *zl = chart->zlabel ? chart->zlabel : "Z";

        /* Midpoints of each axis, in data space, pushed further outward
           than ticks (using the same fixed outward directions) so the
           axis name clears the tick labels. */
        double midx = (data->xmin + data->xmax) * 0.5;
        double midy = (data->ymin + data->ymax) * 0.5;
        double midz = data->zmin + (data->zmax - data->zmin) * 0.5;

        Point2D Xm = project_data(proj, data, midx, data->ymax, data->zmin);
        Point2D Ym = project_data(proj, data, data->xmin, midy, data->zmin);
        Point2D Zm = project_data(proj, data, data->xmin, data->ymin, midz);

        draw_text_at(r, font, xl,
                     (int)lrint(Xm.x + xnx * 40.0),
                     (int)lrint(Xm.y + xny * 40.0),
                     PLOT3D_X_AXIS, 1, 1);

        draw_text_at(r, font, yl,
                     (int)lrint(Ym.x + ynx * 40.0),
                     (int)lrint(Ym.y + yny * 40.0),
                     PLOT3D_Y_AXIS, 1, 1);

        draw_text_angle(r, font, zl,
                        (int)lrint(Zm.x + znx * 46.0),
                        (int)lrint(Zm.y + zny * 46.0),
                        PLOT3D_Z_AXIS, 1, 1, -90.0);
    }

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

typedef struct
{
    int line_color;
    int low_color;
    int high_color;
    int custom_heatmap;
    int show_colorbar;
} Plot3DSeriesStyle;

static Plot3DSeriesStyle series_style(list_t *items, int fallback)
{
    Plot3DSeriesStyle style;
    style.line_color = fallback;
    style.low_color = heat_color(0.0);
    style.high_color = heat_color(1.0);
    style.custom_heatmap = 0;
    style.show_colorbar = 0;

    int color_count = 0;
    for (int i = 2; i < LIST_SIZE(items); i++)
    {
        Value *cv = (Value *)list_getAt(items, i);
        if (cv && IS_NUM(*cv))
        {
            int color = (int)AS_NUM(*cv);
            if (color_count == 0)
            {
                style.line_color = color;
                style.low_color = color;
            }
            else if (color_count == 1)
            {
                style.high_color = color;
                style.custom_heatmap = 1;
            }
            color_count++;
        }
        else if (cv && IS_BOOL(*cv))
        {
            style.show_colorbar = AS_BOOL(*cv);
        }
    }

    return style;
}

static int heatmap_style_color(Plot3DSeriesStyle *style, double t)
{
    if (style->custom_heatmap)
        return blend_rgb(style->low_color, style->high_color, t);
    return heat_color(t);
}

static void draw_colorbar(SDL_Renderer *r, TTF_Font *font,
                          SurfaceData *data, Plot3DSeriesStyle *style)
{
    int W = 0;
    int H = 0;
    if (SDL_GetRendererOutputSize(r, &W, &H) != 0 || W <= 0 || H <= 0)
        return;

    int bar_w = 14;
    int right_margin = 74;
    int max_h = H - 96;
    if (max_h < 40)
        max_h = H > 20 ? H - 20 : H;

    int bar_h = (int)lrint(H * 0.42);
    if (bar_h < 120)
        bar_h = 120;
    if (bar_h > max_h)
        bar_h = max_h;
    if (bar_h < 2)
        return;

    int x = W - right_margin;
    int y = (H - bar_h) / 2;
    if (x < W / 2)
        x = W - 48;
    if (y < 48)
        y = 48;

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    for (int i = 0; i < bar_h; i++)
    {
        double t = 1.0 - (double)i / (double)(bar_h - 1);
        set_drawColor(r, heatmap_style_color(style, t), 255);
        SDL_RenderDrawLine(r, x, y + i, x + bar_w, y + i);
    }

    set_drawColor(r, 0x222222, 255);
    SDL_Rect border = {x, y, bar_w, bar_h};
    SDL_RenderDrawRect(r, &border);

    char label[64];
    const int tick_count = 4;
    for (int i = 0; i <= tick_count; i++)
    {
        double t = (double)i / (double)tick_count;
        int ty = y + (int)lrint((1.0 - t) * (double)(bar_h - 1));
        double value = data->zmin + (data->zmax - data->zmin) * t;

        set_drawColor(r, 0x222222, 255);
        SDL_RenderDrawLine(r, x - 4, ty, x + bar_w + 4, ty);

        format_tick(label, sizeof(label), value);
        draw_text_at(r, font, label, x + bar_w + 10, ty, PLOT3D_TEXT, 0, 1);
    }

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

typedef struct
{
    double x, y, z;
    Point2D p;
    int color;
    int radius;
} ScatterPoint3D;

static int compare_scatter_back_to_front(const void *a, const void *b)
{
    const ScatterPoint3D *pa = (const ScatterPoint3D *)a;
    const ScatterPoint3D *pb = (const ScatterPoint3D *)b;

    if (pa->p.depth < pb->p.depth)
        return -1;
    if (pa->p.depth > pb->p.depth)
        return 1;
    return 0;
}

static void draw_filled_circle(SDL_Renderer *r, int cx, int cy, int radius, int fill, int border)
{
    if (radius < 1)
        radius = 1;

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    set_drawColor(r, fill, 230);
    for (int dy = -radius; dy <= radius; dy++)
    {
        int dx = (int)floor(sqrt((double)(radius * radius - dy * dy)));
        SDL_RenderDrawLine(r, cx - dx, cy + dy, cx + dx, cy + dy);
    }

    set_drawColor(r, border, 180);
    for (int a = 0; a < 360; a += 12)
    {
        double rad = (double)a * M_PI / 180.0;
        int x = cx + (int)lrint(cos(rad) * radius);
        int y = cy + (int)lrint(sin(rad) * radius);
        SDL_RenderDrawPoint(r, x, y);
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

static int scatter_count(list_t *items)
{
    if (LIST_SIZE(items) >= 4 &&
        IS_LIST(*(Value *)list_getAt(items, 1)) &&
        IS_LIST(*(Value *)list_getAt(items, 2)) &&
        IS_LIST(*(Value *)list_getAt(items, 3)))
    {
        PiList *xl = AS_LIST(*(Value *)list_getAt(items, 1));
        PiList *yl = AS_LIST(*(Value *)list_getAt(items, 2));
        PiList *zl = AS_LIST(*(Value *)list_getAt(items, 3));
        int n = LIST_SIZE(xl->items);
        if (LIST_SIZE(yl->items) < n)
            n = LIST_SIZE(yl->items);
        if (LIST_SIZE(zl->items) < n)
            n = LIST_SIZE(zl->items);
        return n;
    }

    if (LIST_SIZE(items) >= 2 && IS_TENSOR(*(Value *)list_getAt(items, 1)))
    {
        PiTensor *T = AS_TENSOR(*(Value *)list_getAt(items, 1));
        if (T->ndim == 2 && T->cols >= 3)
            return T->rows;
    }

    return 0;
}

static int scatter_point_at(list_t *items, int i, double *x, double *y, double *z)
{
    if (LIST_SIZE(items) >= 4 &&
        IS_LIST(*(Value *)list_getAt(items, 1)) &&
        IS_LIST(*(Value *)list_getAt(items, 2)) &&
        IS_LIST(*(Value *)list_getAt(items, 3)))
    {
        PiList *xl = AS_LIST(*(Value *)list_getAt(items, 1));
        PiList *yl = AS_LIST(*(Value *)list_getAt(items, 2));
        PiList *zl = AS_LIST(*(Value *)list_getAt(items, 3));
        Value *vx = (Value *)list_getAt(xl->items, i);
        Value *vy = (Value *)list_getAt(yl->items, i);
        Value *vz = (Value *)list_getAt(zl->items, i);

        if (!vx || !vy || !vz || !IS_NUM(*vx) || !IS_NUM(*vy) || !IS_NUM(*vz))
            return 0;

        *x = AS_NUM(*vx);
        *y = AS_NUM(*vy);
        *z = AS_NUM(*vz);
        return isfinite(*x) && isfinite(*y) && isfinite(*z);
    }

    if (LIST_SIZE(items) >= 2 && IS_TENSOR(*(Value *)list_getAt(items, 1)))
    {
        PiTensor *T = AS_TENSOR(*(Value *)list_getAt(items, 1));
        if (T->ndim != 2 || T->cols < 3)
            return 0;

        *x = tensor_get(T, (int[]){i, 0});
        *y = tensor_get(T, (int[]){i, 1});
        *z = tensor_get(T, (int[]){i, 2});
        return isfinite(*x) && isfinite(*y) && isfinite(*z);
    }

    return 0;
}

static int scatter_option_start(list_t *items)
{
    if (LIST_SIZE(items) >= 4 && IS_LIST(*(Value *)list_getAt(items, 1)))
        return 4;
    return 2;
}

static int scatter_color(list_t *items, int fallback)
{
    int start = scatter_option_start(items);
    if (LIST_SIZE(items) > start)
    {
        Value *cv = (Value *)list_getAt(items, start);
        if (cv && IS_NUM(*cv))
            return (int)AS_NUM(*cv);
    }
    return fallback;
}

static int scatter_radius(list_t *items, int fallback)
{
    int start = scatter_option_start(items);
    if (LIST_SIZE(items) > start + 1)
    {
        Value *sv = (Value *)list_getAt(items, start + 1);
        if (sv && IS_NUM(*sv))
        {
            int radius = (int)lrint(AS_NUM(*sv));
            if (radius > 0)
                return radius;
        }
    }
    return fallback;
}

static int scatter_data_bounds(list_t *items, SurfaceData *data)
{
    int n = scatter_count(items);
    if (n <= 0)
        return 0;

    data->M = NULL;
    data->rows = 2.0;
    data->cols = 2.0;
    data->xmin = INFINITY;
    data->xmax = -INFINITY;
    data->ymin = INFINITY;
    data->ymax = -INFINITY;
    data->zmin = INFINITY;
    data->zmax = -INFINITY;

    for (int i = 0; i < n; i++)
    {
        double x, y, z;
        if (!scatter_point_at(items, i, &x, &y, &z))
            continue;

        if (x < data->xmin)
            data->xmin = x;
        if (x > data->xmax)
            data->xmax = x;
        if (y < data->ymin)
            data->ymin = y;
        if (y > data->ymax)
            data->ymax = y;
        if (z < data->zmin)
            data->zmin = z;
        if (z > data->zmax)
            data->zmax = z;
    }

    if (!isfinite(data->xmin) || !isfinite(data->xmax) ||
        !isfinite(data->ymin) || !isfinite(data->ymax) ||
        !isfinite(data->zmin) || !isfinite(data->zmax))
        return 0;

    if (data->xmax <= data->xmin)
        data->xmax = data->xmin + 1e-9;
    if (data->ymax <= data->ymin)
        data->ymax = data->ymin + 1e-9;
    if (data->zmax <= data->zmin)
        data->zmax = data->zmin + 1e-9;

    return 1;
}

static void draw_scatter3d(SDL_Renderer *r, TTF_Font *font,
                           PiChart3D *chart, Projector3D *proj,
                           list_t *items)
{
    SurfaceData data;
    if (!scatter_data_bounds(items, &data))
        return;

    int n = scatter_count(items);
    ScatterPoint3D *points = (ScatterPoint3D *)malloc(sizeof(ScatterPoint3D) * (size_t)n);
    if (!points)
        return;

    int count = 0;
    for (int i = 0; i < n; i++)
    {
        double x, y, z;
        if (!scatter_point_at(items, i, &x, &y, &z))
            continue;

        points[count].x = x;
        points[count].y = y;
        points[count].z = z;
        points[count].p = project_plot_data(proj, &data, x, y, z);
        count++;
    }

    qsort(points, (size_t)count, sizeof(ScatterPoint3D), compare_scatter_back_to_front);

    int color = scatter_color(items, 0x2e86ab);
    int radius = scatter_radius(items, 5);

    draw_axis_grid(r, chart, proj, &data);

    for (int i = 0; i < count; i++)
    {
        double t = (points[i].z - data.zmin) / (data.zmax - data.zmin);
        int shaded = blend_rgb(color, 0xffffff, 0.10 + 0.24 * t);
        draw_filled_circle(r,
                           (int)lrint(points[i].p.x),
                           (int)lrint(points[i].p.y),
                           radius,
                           shaded,
                           0x111111);
    }

    draw_axes_ticks_labels(r, font, chart, proj, &data);
    free(points);
}

static int plot3d_palette_color(int index)
{
    static const int colors[] = {
        0x2563eb, 0xdc2626, 0x16a34a, 0xf59e0b,
        0x7c3aed, 0x0891b2, 0xdb2777, 0x4b5563,
    };
    return colors[index % (int)(sizeof(colors) / sizeof(colors[0]))];
}

static int collect_scatter_bounds(PiChart3D *chart, SurfaceData *data)
{
    data->M = NULL;
    data->rows = 2.0;
    data->cols = 2.0;
    data->xmin = INFINITY;
    data->xmax = -INFINITY;
    data->ymin = INFINITY;
    data->ymax = -INFINITY;
    data->zmin = INFINITY;
    data->zmax = -INFINITY;

    int found = 0;
    for (int si = 0; si < list_size(chart->series); si++)
    {
        Value *series_val = (Value *)list_getAt(chart->series, si);
        if (!series_val || !IS_LIST(*series_val))
            continue;

        list_t *items = AS_LIST(*series_val)->items;
        if (LIST_SIZE(items) < 1)
            continue;

        Value *kind_val = (Value *)list_getAt(items, 0);
        if (!kind_val || !IS_STRING(*kind_val) || strcmp(AS_CSTRING(*kind_val), "scatter") != 0)
            continue;

        int n = scatter_count(items);
        for (int i = 0; i < n; i++)
        {
            double x, y, z;
            if (!scatter_point_at(items, i, &x, &y, &z))
                continue;

            if (x < data->xmin)
                data->xmin = x;
            if (x > data->xmax)
                data->xmax = x;
            if (y < data->ymin)
                data->ymin = y;
            if (y > data->ymax)
                data->ymax = y;
            if (z < data->zmin)
                data->zmin = z;
            if (z > data->zmax)
                data->zmax = z;
            found = 1;
        }
    }

    if (!found || !isfinite(data->xmin) || !isfinite(data->xmax) ||
        !isfinite(data->ymin) || !isfinite(data->ymax) ||
        !isfinite(data->zmin) || !isfinite(data->zmax))
        return 0;

    if (data->xmax <= data->xmin)
        data->xmax = data->xmin + 1e-9;
    if (data->ymax <= data->ymin)
        data->ymax = data->ymin + 1e-9;
    if (data->zmax <= data->zmin)
        data->zmax = data->zmin + 1e-9;

    return 1;
}

static void draw_all_scatter3d(SDL_Renderer *r, TTF_Font *font,
                               PiChart3D *chart, Projector3D *proj)
{
    SurfaceData data;
    if (!collect_scatter_bounds(chart, &data))
        return;

    int total = 0;
    for (int si = 0; si < list_size(chart->series); si++)
    {
        Value *series_val = (Value *)list_getAt(chart->series, si);
        if (!series_val || !IS_LIST(*series_val))
            continue;
        list_t *items = AS_LIST(*series_val)->items;
        if (LIST_SIZE(items) < 1)
            continue;
        Value *kind_val = (Value *)list_getAt(items, 0);
        if (kind_val && IS_STRING(*kind_val) && strcmp(AS_CSTRING(*kind_val), "scatter") == 0)
            total += scatter_count(items);
    }

    if (total <= 0)
        return;

    ScatterPoint3D *points = (ScatterPoint3D *)malloc(sizeof(ScatterPoint3D) * (size_t)total);
    if (!points)
        return;

    int count = 0;
    int scatter_series_index = 0;
    for (int si = 0; si < list_size(chart->series); si++)
    {
        Value *series_val = (Value *)list_getAt(chart->series, si);
        if (!series_val || !IS_LIST(*series_val))
            continue;

        list_t *items = AS_LIST(*series_val)->items;
        if (LIST_SIZE(items) < 1)
            continue;

        Value *kind_val = (Value *)list_getAt(items, 0);
        if (!kind_val || !IS_STRING(*kind_val) || strcmp(AS_CSTRING(*kind_val), "scatter") != 0)
            continue;

        int color = scatter_color(items, plot3d_palette_color(scatter_series_index));
        int radius = scatter_radius(items, 5);
        int n = scatter_count(items);

        for (int i = 0; i < n; i++)
        {
            double x, y, z;
            if (!scatter_point_at(items, i, &x, &y, &z))
                continue;

            points[count].x = x;
            points[count].y = y;
            points[count].z = z;
            points[count].p = project_plot_data(proj, &data, x, y, z);
            points[count].color = color;
            points[count].radius = radius;
            count++;
        }

        scatter_series_index++;
    }

    qsort(points, (size_t)count, sizeof(ScatterPoint3D), compare_scatter_back_to_front);

    draw_axis_grid(r, chart, proj, &data);

    for (int i = 0; i < count; i++)
    {
        double t = (points[i].z - data.zmin) / (data.zmax - data.zmin);
        int shaded = blend_rgb(points[i].color, 0xffffff, 0.10 + 0.24 * t);
        draw_filled_circle(r,
                           (int)lrint(points[i].p.x),
                           (int)lrint(points[i].p.y),
                           points[i].radius,
                           shaded,
                           0x111111);
    }

    draw_axes_ticks_labels(r, font, chart, proj, &data);
    free(points);
}

static void draw_surface(SDL_Renderer *r, Projector3D *proj, SurfaceData *data,
                         Plot3DSeriesStyle *style)
{
    int face_count = (data->M->rows - 1) * (data->M->cols - 1);
    if (face_count <= 0)
        return;

    int W = 0;
    int H = 0;
    if (SDL_GetRendererOutputSize(r, &W, &H) != 0 || W <= 0 || H <= 0)
        return;

    size_t pixels = (size_t)W * (size_t)H;
    double *zbuf = (double *)malloc(sizeof(double) * pixels);
    if (!zbuf)
        return;

    for (size_t i = 0; i < pixels; i++)
        zbuf[i] = -DBL_MAX;

    for (int row = 0; row < data->M->rows - 1; row++)
    {
        for (int col = 0; col < data->M->cols - 1; col++)
        {
            double z00 = tensor_at(data->M, row, col);
            double z10 = tensor_at(data->M, row, col + 1);
            double z11 = tensor_at(data->M, row + 1, col + 1);
            double z01 = tensor_at(data->M, row + 1, col);

            if (!isfinite(z00) || !isfinite(z10) || !isfinite(z11) || !isfinite(z01))
                continue;

            Face3D f;
            f.p[0] = project_plot_data(proj, data, col, row, z00);
            f.p[1] = project_plot_data(proj, data, col + 1, row, z10);
            f.p[2] = project_plot_data(proj, data, col + 1, row + 1, z11);
            f.p[3] = project_plot_data(proj, data, col, row + 1, z01);
            f.zavg = (z00 + z10 + z11 + z01) * 0.25;
            f.depth = (f.p[0].depth + f.p[1].depth + f.p[2].depth + f.p[3].depth) * 0.25;

            double t = (f.zavg - data->zmin) / (data->zmax - data->zmin);
            f.color = heatmap_style_color(style, t);

            draw_depth_face(r, zbuf, W, H, &f);
        }
    }

    free(zbuf);
}

static void draw_wire_grid(SDL_Renderer *r, Projector3D *proj, SurfaceData *data,
                           Plot3DSeriesStyle *style, int colored_by_height,
                           int thickness)
{
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    for (int row = 0; row < data->M->rows; row++)
    {
        for (int col = 0; col < data->M->cols - 1; col++)
        {
            double z0 = tensor_at(data->M, row, col);
            double z1 = tensor_at(data->M, row, col + 1);
            if (!isfinite(z0) || !isfinite(z1))
                continue;

            if (colored_by_height)
            {
                double t = (((z0 + z1) * 0.5) - data->zmin) / (data->zmax - data->zmin);
                set_drawColor(r, heatmap_style_color(style, t), 245);
            }
            else
            {
                double t = (((z0 + z1) * 0.5) - data->zmin) / (data->zmax - data->zmin);
                int shaded = blend_rgb(style->line_color, 0xffffff, 0.18 + 0.20 * t);
                set_drawColor(r, shaded, 255);
            }

            Point2D p0 = project_plot_data(proj, data, col, row, z0);
            Point2D p1 = project_plot_data(proj, data, col + 1, row, z1);
            draw_thick_line(r, (int)p0.x, (int)p0.y, (int)p1.x, (int)p1.y, thickness);
        }
    }

    for (int col = 0; col < data->M->cols; col++)
    {
        for (int row = 0; row < data->M->rows - 1; row++)
        {
            double z0 = tensor_at(data->M, row, col);
            double z1 = tensor_at(data->M, row + 1, col);
            if (!isfinite(z0) || !isfinite(z1))
                continue;

            if (colored_by_height)
            {
                double t = (((z0 + z1) * 0.5) - data->zmin) / (data->zmax - data->zmin);
                set_drawColor(r, heatmap_style_color(style, t), 245);
            }
            else
            {
                double t = (((z0 + z1) * 0.5) - data->zmin) / (data->zmax - data->zmin);
                int shaded = blend_rgb(style->line_color, 0xffffff, 0.18 + 0.20 * t);
                set_drawColor(r, shaded, 255);
            }

            Point2D p0 = project_plot_data(proj, data, col, row, z0);
            Point2D p1 = project_plot_data(proj, data, col, row + 1, z1);
            draw_thick_line(r, (int)p0.x, (int)p0.y, (int)p1.x, (int)p1.y, thickness);
        }
    }

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

static void draw_surface_like(SDL_Renderer *r, TTF_Font *font,
                              PiChart3D *chart, Projector3D *proj,
                              list_t *items, int mode)
{
    if (LIST_SIZE(items) < 2)
        return;

    Value *mv = (Value *)list_getAt(items, 1);
    if (!IS_TENSOR(*mv) || AS_TENSOR(*mv)->ndim != 2)
        return;

    PiTensor *M = AS_TENSOR(*mv);
    if (M->rows < 2 || M->cols < 2)
        return;

    SurfaceData data;
    data.M = M;
    data.rows = (double)M->rows;
    data.cols = (double)M->cols;
    data.xmin = 0.0;
    data.xmax = data.cols - 1.0;
    data.ymin = 0.0;
    data.ymax = data.rows - 1.0;
    tensor_minmax(M, &data.zmin, &data.zmax);

    Plot3DSeriesStyle style = series_style(items, 0x2e86ab);

    draw_axis_grid(r, chart, proj, &data);

    /*
        mode 0: surface  -> filled, shaded quads with depth-tested visibility
        mode 1: mesh     -> wireframe colored by height (heatmap-style)
        mode 2: wireframe -> wireframe in a single base color, shaded by height
    */
    if (mode == 0)
    {
        draw_surface(r, proj, &data, &style);
    }
    else if (mode == 1)
    {
        draw_wire_grid(r, proj, &data, &style, 1, 1);
    }
    else
    {
        draw_wire_grid(r, proj, &data, &style, 0, 1);
    }

    draw_axes_ticks_labels(r, font, chart, proj, &data);

    if (style.show_colorbar && mode != 2)
        draw_colorbar(r, font, &data, &style);
}


/*
    Interactive 3D view control
    ---------------------------
    Hold the left mouse button and move the mouse to rotate the 3D plot.

    This version intentionally uses SDL_GetMouseState() instead of SDL_PollEvent()
    so plot3d.show() does not consume SDL events that the rest of the engine/editor
    may still need to process.

    Important:
    - plot3d.show(chart) must be called every frame from the draw/update loop.
    - A one-time call will draw the chart once, but there will be no continuous
      mouse-driven redraw.
*/
static void plot3d_handle_mouse(PiChart3D *chart, SDL_Rect viewport)
{
    static PiChart3D *active_chart = NULL;
    static int dragging = 0;
    static int last_x = 0;
    static int last_y = 0;

    if (!chart)
        return;

    int x = 0;
    int y = 0;
    Uint32 buttons = SDL_GetMouseState(&x, &y);
    int left_down = (buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
    int inside = x >= viewport.x && x < viewport.x + viewport.w &&
                 y >= viewport.y && y < viewport.y + viewport.h;

    if (left_down && inside && (!dragging || active_chart != chart))
    {
        dragging = 1;
        active_chart = chart;
        last_x = x;
        last_y = y;

        /*
            The renderer treats azimuth=0 and elevation=0 as "use default view".
            When the user starts dragging from that default state, make the default
            concrete so rotation continues naturally from the visible angle.
        */
        if (fabs(chart->azimuth) < 1e-9 && fabs(chart->elevation) < 1e-9)
        {
            chart->azimuth = -45.0;
            chart->elevation = 32.0;
        }

        return;
    }

    if (!left_down)
    {
        if (active_chart == chart)
            active_chart = NULL;
        dragging = 0;
        return;
    }

    if (dragging && active_chart == chart)
    {
        int dx = x - last_x;
        int dy = y - last_y;

        const double sensitivity = 0.6;

        chart->azimuth -= (double)dx * sensitivity;
        chart->elevation += (double)dy * sensitivity;

        if (chart->elevation > 89.0)
            chart->elevation = 89.0;
        if (chart->elevation < -89.0)
            chart->elevation = -89.0;

        last_x = x;
        last_y = y;
    }
}

static void plot3d_render(PiChart3D *chart, int present)
{
    PiContext *ctx = chart->ctx;
    SDL_Renderer *r = ctx ? (SDL_Renderer *)ctx->renderer : NULL;

    if (!ctx || !r || ctx->width <= 0 || ctx->height <= 0)
        return;

    SDL_Rect old_viewport;
    SDL_RenderGetViewport(r, &old_viewport);

    SDL_Rect viewport;
    if (!subplot_rect(ctx->width, ctx->height,
                      chart->subplot_rows, chart->subplot_cols, chart->subplot_index,
                      &viewport))
        return;

    plot3d_handle_mouse(chart, viewport);

    int is_subplot = chart3d_has_subplot(chart);
    if (is_subplot && chart->subplot_index == 1)
    {
        SDL_RenderSetViewport(r, NULL);
        SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
        SDL_RenderClear(r);
    }

    SDL_RenderSetViewport(r, &viewport);

    int W = viewport.w;
    int H = viewport.h;

    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    if (is_subplot)
    {
        SDL_Rect clear_rect = {0, 0, W, H};
        SDL_RenderFillRect(r, &clear_rect);
    }
    else
    {
        SDL_RenderClear(r);
    }

    int min_side = W < H ? W : H;
    int margin = clamp_int((int)lrint((double)min_side * 0.06), PLOT3D_MARGIN, 60);
    if (W - 2 * margin < 120 || H - 2 * margin < 120)
        margin = clamp_int(min_side / 14, 12, 34);

    SDL_Rect plot = {
        margin,
        margin,
        W - 2 * margin,
        H - 2 * margin,
    };

    /* No gray plot rectangle and no plot border. The 3D scene is drawn directly on the white canvas. */

    Projector3D projector = make_projector(chart, plot);
    int tick_size = clamp_int(min_side / 34, 8, 11);
    int title_size = clamp_int(min_side / 22, 11, 18);
    TTF_Font *tick_font = get_openFont(tick_size);

    int has_scatter = 0;
    for (int si = 0; si < list_size(chart->series); si++)
    {
        Value *series_val = (Value *)list_getAt(chart->series, si);
        if (!series_val || !IS_LIST(*series_val))
            continue;

        list_t *items = AS_LIST(*series_val)->items;
        if (LIST_SIZE(items) < 1)
            continue;

        Value *kind_val = (Value *)list_getAt(items, 0);
        if (!kind_val || !IS_STRING(*kind_val))
            continue;

        const char *kind = AS_CSTRING(*kind_val);

        if (!strcmp(kind, "surface"))
            draw_surface_like(r, tick_font, chart, &projector, items, 0);
        else if (!strcmp(kind, "mesh"))
            draw_surface_like(r, tick_font, chart, &projector, items, 1);
        else if (!strcmp(kind, "wireframe"))
            draw_surface_like(r, tick_font, chart, &projector, items, 2);
        else if (!strcmp(kind, "scatter"))
            has_scatter = 1;
    }

    if (has_scatter)
        draw_all_scatter3d(r, tick_font, chart, &projector);

    TTF_Font *title_font = get_openFont(title_size);
    if (chart->title && title_font)
        draw_text_at(r, title_font, chart->title, W / 2, margin > 36 ? 24 : 12, PLOT3D_TEXT, 1, 1);

    if (title_font)
        TTF_CloseFont(title_font);
    if (tick_font)
        TTF_CloseFont(tick_font);

    SDL_RenderSetViewport(r, &old_viewport);

    if (present)
        SDL_RenderPresent(r);
}

void pt3d_redraw_context(PiContext *ctx)
{
    if (!ctx || !ctx->active_plot3d)
        return;

    plot3d_render(ctx->active_plot3d, 0);
}

Value pt3d_show(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_CHART3D(argv[0]))
    {
        vm_error(vm, "show() takes a plot3d chart");
        return NIL_VAL;
    }

    PiChart3D *chart = AS_CHART3D(argv[0]);
    if (chart->ctx)
        chart->ctx->active_plot3d = chart;

    plot3d_render(chart, 1);
    return NIL_VAL;
}

static BuiltinFunc plot3d_funcs[] = {
    {"chart", pt3d_chart},
    {"surface", pt3d_surface},
    {"mesh", pt3d_mesh},
    {"wireframe", pt3d_wireframe},
    {"scatter", pt3d_scatter},
    {"show", pt3d_show},
    {"title", pt3d_title},
    {"xlabel", pt3d_xlabel},
    {"ylabel", pt3d_ylabel},
    {"zlabel", pt3d_zlabel},
    {"grid", pt3d_grid},
    {"view", pt3d_view},
    {"subplot", pt3d_subplot},
};

static BuiltinConst plot3d_consts[] = {};

DEFINE_BUILTIN_MODULE(module_plot3d, "plot3d", plot3d_funcs, plot3d_consts);
