#include "pi_plot.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include "../common.h"
#include "../pi_func.h"

#include "pi_builtin.h"

static TTF_Font *get_openFont(int size)
{
    static int ttf_ready;
    if (!ttf_ready)
    {
        if (TTF_Init() == 0)
            ttf_ready = 1;
        else
            return NULL;
    }
    const char *candidates[] = {
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/calibri.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/Library/Fonts/Arial.ttf",
    };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++)
    {
        TTF_Font *f = TTF_OpenFont(candidates[i], size);
        if (f)
            return f;
    }
    return NULL;
}

static void set_drawColor(SDL_Renderer *r, int rgb, Uint8 a)
{
    Uint8 R = (Uint8)((rgb >> 16) & 255);
    Uint8 G = (Uint8)((rgb >> 8) & 255);
    Uint8 B = (Uint8)(rgb & 255);
    SDL_SetRenderDrawColor(r, R, G, B, a);
}

static int palette_color(int series_index)
{
    static int pal[] = {
        0x2e86ab,
        0xa23b72,
        0xf18f01,
        0xc73e1d,
        0x6a994e,
        0xbc4749,
        0x7209b7,
        0x4cc9f0,
    };
    return pal[series_index % (int)(sizeof(pal) / sizeof(pal[0]))];
}

static const int plot_left_margin = 100;
static const int plot_margin = 80;

static PiChart *chart_from(vm_t *vm, Value v0)
{
    if (IS_CHART(v0))
        return AS_CHART(v0);
    if (IS_CONTEXT(v0))
        return (PiChart *)add_obj(vm, new_chart(AS_CONTEXT(v0)));
    return NULL;
}

static double list_min(list_t *lst)
{
    double m = INFINITY;
    int n = LIST_SIZE(lst);
    for (int i = 0; i < n; i++)
    {
        double v = AS_NUM(*(Value *)list_getAt(lst, i));
        if (v < m)
            m = v;
    }
    return m;
}

static double list_max(list_t *lst)
{
    double m = -INFINITY;
    int n = LIST_SIZE(lst);
    for (int i = 0; i < n; i++)
    {
        double v = AS_NUM(*(Value *)list_getAt(lst, i));
        if (v > m)
            m = v;
    }
    return m;
}

static void map_xy(PiChart *chart, double x, double y, int W, int H, int xmargin, int ymargin, int *px, int *py)
{
    double x0 = chart->xmin, x1 = chart->xmax, y0 = chart->ymin, y1 = chart->ymax;
    if (x1 <= x0)
        x1 = x0 + 1e-9;
    if (y1 <= y0)
        y1 = y0 + 1e-9;
    *px = xmargin + (int)((x - x0) / (x1 - x0) * (W - xmargin - ymargin));
    *py = H - ymargin - (int)((y - y0) / (y1 - y0) * (H - 2 * ymargin));
}

static Value make_kindSeries(vm_t *vm, PiChart *chart, const char *kind, list_t *tail)
{
    list_t *parts = list_create(VALUE_SIZE);
    Value vk = NEW_OBJ(add_obj(vm, new_pistring(strdup(kind))));
    list_add(parts, &vk);
    int n = LIST_SIZE(tail);
    for (int i = 0; i < n; i++)
    {
        Value *c = (Value *)list_getAt(tail, i);
        list_add(parts, c);
    }

    // wrap in a list and add to chart->series
    Object *lo = new_list(parts);
    add_obj(vm, lo);
    Value v = NEW_OBJ(lo);
    list_add(chart->series, &v);

    list_free(tail);
    return NEW_OBJ((Object *)chart);
}

enum TextFlags
{
    TEXT_NONE = 0,
    TEXT_CENTER_X = 1 << 0,
    TEXT_CENTER_Y = 1 << 1,
    TEXT_BOLD = 1 << 2,
    TEXT_VERTICAL = 1 << 3
};

static void draw_text(SDL_Renderer *r, TTF_Font *font, const char *text,
                      int x, int y, int rgb, int flags)
{
    if (!font || !text)
        return;

    // Handle font style
    int old_style = TTF_GetFontStyle(font);
    if (flags & TEXT_BOLD)
        TTF_SetFontStyle(font, old_style | TTF_STYLE_BOLD);

    // Color
    SDL_Color fg = {
        (Uint8)((rgb >> 16) & 255),
        (Uint8)((rgb >> 8) & 255),
        (Uint8)(rgb & 255),
        255};

    SDL_Surface *surf = TTF_RenderUTF8_Blended(font, text, fg);
    if (!surf)
        goto cleanup;

    SDL_Surface *final_surf = surf;

    // Handle vertical (rotate 90°)
    if (flags & TEXT_VERTICAL)
    {
        SDL_Surface *rotated = SDL_CreateRGBSurface(0, surf->h, surf->w, 32,
                                                    surf->format->Rmask, surf->format->Gmask,
                                                    surf->format->Bmask, surf->format->Amask);

        if (!rotated)
            goto cleanup;

        if (SDL_MUSTLOCK(surf))
            SDL_LockSurface(surf);
        if (SDL_MUSTLOCK(rotated))
            SDL_LockSurface(rotated);

        Uint32 *src = (Uint32 *)surf->pixels;
        Uint32 *dst = (Uint32 *)rotated->pixels;
        int pitch_src = surf->pitch / 4;
        int pitch_dst = rotated->pitch / 4;

        for (int i = 0; i < surf->w; i++)
        {
            for (int j = 0; j < surf->h; j++)
            {
                dst[i * pitch_dst + (surf->h - 1 - j)] =
                    src[j * pitch_src + i];
            }
        }

        if (SDL_MUSTLOCK(rotated))
            SDL_UnlockSurface(rotated);
        if (SDL_MUSTLOCK(surf))
            SDL_UnlockSurface(surf);

        final_surf = rotated;
        SDL_FreeSurface(surf);
    }

    SDL_Texture *tex = SDL_CreateTextureFromSurface(r, final_surf);
    if (!tex)
        goto cleanup;

    int tw, th;
    SDL_QueryTexture(tex, NULL, NULL, &tw, &th);

    SDL_Rect dst = {x, y, tw, th};

    // Centering
    if (flags & TEXT_CENTER_X)
        dst.x -= tw / 2;
    if (flags & TEXT_CENTER_Y)
        dst.y -= th / 2;

    SDL_RenderCopy(r, tex, NULL, &dst);

    SDL_DestroyTexture(tex);
    SDL_FreeSurface(final_surf);

cleanup:
    TTF_SetFontStyle(font, old_style);
}

static void chart_computeBounds(PiChart *c)
{
    if (c->has_bounds)
        return;

    double xmin = INFINITY, xmax = -INFINITY, ymin = INFINITY, ymax = -INFINITY;
    int ns = list_size(c->series);

    for (int si = 0; si < ns; si++)
    {
        Value *series_val = (Value *)list_getAt(c->series, si);
        if (!series_val || !IS_LIST(*series_val))
            continue;
        PiList *sl = AS_LIST(*series_val);
        list_t *items = sl->items;
        if (LIST_SIZE(items) < 1)
            continue;
        Value *k0 = (Value *)list_getAt(items, 0);
        if (!k0 || !IS_STRING(*k0))
            continue;
        const char *kind = AS_CSTRING(*k0);

        if (!strcmp(kind, "scatter") || !strcmp(kind, "line") || !strcmp(kind, "step"))
        {
            if (LIST_SIZE(items) < 3)
                continue;
            PiList *xl = AS_LIST(*(Value *)list_getAt(items, 1));
            PiList *yl = AS_LIST(*(Value *)list_getAt(items, 2));
            int n = LIST_SIZE(xl->items);
            if (n != LIST_SIZE(yl->items))
                continue;
            for (int i = 0; i < n; i++)
            {
                Value *vx = (Value *)list_getAt(xl->items, i);
                Value *vy = (Value *)list_getAt(yl->items, i);
                if (!IS_NUM(*vx) || !IS_NUM(*vy))
                    continue;
                double x = AS_NUM(*vx), y = AS_NUM(*vy);
                if (x < xmin)
                    xmin = x;
                if (x > xmax)
                    xmax = x;
                if (y < ymin)
                    ymin = y;
                if (y > ymax)
                    ymax = y;
            }
        }
        else if (!strcmp(kind, "bar"))
        {
            if (LIST_SIZE(items) < 3)
                continue;
            PiList *yl = AS_LIST(*(Value *)list_getAt(items, 2));
            int n = LIST_SIZE(yl->items);
            if (n == 0)
                continue;
            xmin = fmin(xmin, 0.0);
            xmax = fmax(xmax, (double)(n - 1));
            ymin = fmin(ymin, 0.0);
            for (int i = 0; i < n; i++)
            {
                Value *vy = (Value *)list_getAt(yl->items, i);
                if (!IS_NUM(*vy))
                    continue;
                double y = AS_NUM(*vy);
                ymin = fmin(ymin, y);
                ymax = fmax(ymax, y);
            }
        }
        else if (!strcmp(kind, "hist"))
        {
            if (LIST_SIZE(items) < 2)
                continue;
            PiList *dl = AS_LIST(*(Value *)list_getAt(items, 1));
            int bins = 10;
            if (LIST_SIZE(items) > 2)
            {
                Value *vb = (Value *)list_getAt(items, 2);
                if (IS_NUM(*vb))
                    bins = (int)AS_NUM(*vb);
            }
            if (bins < 1)
                bins = 1;
            int nd = LIST_SIZE(dl->items);
            if (nd < 1)
                continue;
            double lo = list_min(dl->items);
            double hi = list_max(dl->items);
            if (hi <= lo)
                hi = lo + 1e-9;
            double *counts = (double *)calloc((size_t)bins, sizeof(double));
            if (!counts)
                continue;
            for (int i = 0; i < nd; i++)
            {
                Value *v = (Value *)list_getAt(dl->items, i);
                if (!IS_NUM(*v))
                    continue;
                double x = AS_NUM(*v);
                int b = (int)((x - lo) / (hi - lo) * bins);
                if (b < 0)
                    b = 0;
                if (b >= bins)
                    b = bins - 1;
                counts[b] += 1.0;
            }
            double cmax = 1.0;
            for (int b = 0; b < bins; b++)
                if (counts[b] > cmax)
                    cmax = counts[b];
            free(counts);
            xmin = fmin(xmin, lo);
            xmax = fmax(xmax, hi);
            ymin = fmin(ymin, 0.0);
            ymax = fmax(ymax, cmax);
        }
        else if (!strcmp(kind, "heatmap") || !strcmp(kind, "contour"))
        {
            if (LIST_SIZE(items) < 2)
                continue;
            Value *mv = (Value *)list_getAt(items, 1);
            if (!IS_TENSOR(*mv) || AS_TENSOR(*mv)->ndim != 2)
                continue;
            PiTensor *M = AS_TENSOR(*mv);
            xmin = 0;
            xmax = M->cols > 0 ? M->cols : 1;
            ymin = 0;
            ymax = M->rows > 0 ? M->rows : 1;
        }
    }

    if (xmin <= xmax && ymin <= ymax && isfinite(xmin) && isfinite(xmax) && isfinite(ymin) && isfinite(ymax))
    {
        double dx = (xmax - xmin) * 0.05 + 1e-9;
        double dy = (ymax - ymin) * 0.05 + 1e-9;
        c->xmin = xmin - dx;
        c->xmax = xmax + dx;
        c->ymin = ymin - dy;
        c->ymax = ymax + dy;
    }
    else
    {
        c->xmin = 0;
        c->xmax = 1;
        c->ymin = 0;
        c->ymax = 1;
    }
}

static void format_axisLabel(char *buffer, size_t size, double value)
{
    const double eps = 1e-14;
    if (fabs(value) < eps)
    {
        snprintf(buffer, size, "0");
        return;
    }

    if (fabs(value) < 0.0001 || fabs(value) > 10000)
        snprintf(buffer, size, "%.2e", value);
    else if (fabs(value - round(value)) < 0.0001)
        snprintf(buffer, size, "%.0f", value);
    else
        snprintf(buffer, size, "%.2f", value);
}

static void draw_axisTicks(SDL_Renderer *r, PiChart *chart, TTF_Font *font, int W, int H, int xmargin, int ymargin)
{
    if (!font)
        return;

    set_drawColor(r, 0x000000, 255);

    int num_ticks = 6;
    for (int i = 0; i <= num_ticks; i++)
    {
        double t = (double)i / num_ticks;
        double x_val = chart->xmin + t * (chart->xmax - chart->xmin - 1e-2);
        int px, py0, py1;
        map_xy(chart, x_val, chart->ymin, W, H, xmargin, ymargin, &px, &py0);
        map_xy(chart, x_val, chart->ymin - (chart->ymax - chart->ymin) * 0.02, W, H, xmargin, ymargin, &px, &py1);
        SDL_RenderDrawLine(r, px, py0, px, py0 + 6);

        char label[32];
        format_axisLabel(label, sizeof(label), x_val);
        int tw, th;
        th = 12;
        tw = strlen(label) * 6;
        draw_text(r, font, label, px - tw / 2, py0 + 8, 0x555555, TEXT_BOLD);
    }

    for (int i = 0; i <= num_ticks; i++)
    {
        double t = (double)i / num_ticks;
        double y_val = chart->ymin + t * (chart->ymax - chart->ymin);
        int px0, py, px1;
        map_xy(chart, chart->xmin, y_val, W, H, xmargin, ymargin, &px0, &py);
        map_xy(chart, chart->xmin - (chart->xmax - chart->xmin) * 0.02, y_val, W, H, xmargin, ymargin, &px1, &py);
        SDL_RenderDrawLine(r, px0 - 6, py, px0, py);

        char label[32];
        format_axisLabel(label, sizeof(label), y_val);
        int tw = strlen(label) * 6;
        draw_text(r, font, label, px0 - tw - 8, py, 0x555555, TEXT_CENTER_Y | TEXT_BOLD);
    }
}

static void draw_filledBorderedCircle(SDL_Renderer *r,
                                      int cx, int cy,
                                      int radius,
                                      int fill_color,
                                      int border_color)
{
    // ------------------------
    // FILLED CIRCLE (SPAN FILL)
    // ------------------------
    set_drawColor(r, fill_color, 220);

    int x = 0;
    int y = radius;
    int d = 1 - radius;

    while (x <= y)
    {
        // draw horizontal spans instead of relying on sqrt()
        SDL_RenderDrawLine(r, cx - x, cy + y, cx + x, cy + y);
        SDL_RenderDrawLine(r, cx - x, cy - y, cx + x, cy - y);
        SDL_RenderDrawLine(r, cx - y, cy + x, cx + y, cy + x);
        SDL_RenderDrawLine(r, cx - y, cy - x, cx + y, cy - x);

        x++;

        if (d < 0)
        {
            d += 2 * x + 1;
        }
        else
        {
            y--;
            d += 2 * (x - y) + 1;
        }
    }

    // ------------------------
    // BORDER (MIDPOINT CIRCLE)
    // ------------------------
    set_drawColor(r, border_color, 255);

    x = 0;
    y = radius;
    d = 1 - radius;

    while (x <= y)
    {
        SDL_RenderDrawPoint(r, cx + x, cy + y);
        SDL_RenderDrawPoint(r, cx - x, cy + y);
        SDL_RenderDrawPoint(r, cx + x, cy - y);
        SDL_RenderDrawPoint(r, cx - x, cy - y);
        SDL_RenderDrawPoint(r, cx + y, cy + x);
        SDL_RenderDrawPoint(r, cx - y, cy + x);
        SDL_RenderDrawPoint(r, cx + y, cy - x);
        SDL_RenderDrawPoint(r, cx - y, cy - x);

        if (d < 0)
        {
            d += 2 * x + 3;
        }
        else
        {
            d += 2 * (x - y) + 5;
            y--;
        }

        x++;
    }
}

static void heatmap_rgb_magma(double t, Uint8 *R, Uint8 *G, Uint8 *B)
{
    t = fmax(0.0, fmin(1.0, t));
    double r_stops[] = {0.001462, 0.258228, 0.554815, 0.852315, 0.987154};
    double g_stops[] = {0.000466, 0.118400, 0.330580, 0.651633, 0.907940};
    double b_stops[] = {0.013866, 0.290110, 0.480826, 0.634506, 0.876042};
    double scaled_t = t * 4.0;
    int idx = (int)scaled_t;
    if (idx >= 4)
        idx = 3;
    double frac = scaled_t - idx;
    *R = (Uint8)((r_stops[idx] * (1.0 - frac) + r_stops[idx + 1] * frac) * 255);
    *G = (Uint8)((g_stops[idx] * (1.0 - frac) + g_stops[idx + 1] * frac) * 255);
    *B = (Uint8)((b_stops[idx] * (1.0 - frac) + b_stops[idx + 1] * frac) * 255);
}

static void heatmap_rgb_plasma(double t, Uint8 *R, Uint8 *G, Uint8 *B)
{
    t = fmax(0.0, fmin(1.0, t));
    double r_stops[] = {0.050383, 0.385441, 0.782950, 0.959703, 0.940015};
    double g_stops[] = {0.029803, 0.176449, 0.413126, 0.663855, 0.889322};
    double b_stops[] = {0.527975, 0.708415, 0.873010, 0.946857, 0.954525};
    double scaled_t = t * 4.0;
    int idx = (int)scaled_t;
    if (idx >= 4)
        idx = 3;
    double frac = scaled_t - idx;
    *R = (Uint8)((r_stops[idx] * (1.0 - frac) + r_stops[idx + 1] * frac) * 255);
    *G = (Uint8)((g_stops[idx] * (1.0 - frac) + g_stops[idx + 1] * frac) * 255);
    *B = (Uint8)((b_stops[idx] * (1.0 - frac) + b_stops[idx + 1] * frac) * 255);
}

static void heatmap_rgb_inferno(double t, Uint8 *R, Uint8 *G, Uint8 *B)
{
    t = fmax(0.0, fmin(1.0, t));
    double r_stops[] = {0.001462, 0.285341, 0.680024, 0.942433, 0.988362};
    double g_stops[] = {0.000466, 0.067732, 0.253384, 0.571154, 0.878120};
    double b_stops[] = {0.013866, 0.157302, 0.255235, 0.405365, 0.754281};
    double scaled_t = t * 4.0;
    int idx = (int)scaled_t;
    if (idx >= 4)
        idx = 3;
    double frac = scaled_t - idx;
    *R = (Uint8)((r_stops[idx] * (1.0 - frac) + r_stops[idx + 1] * frac) * 255);
    *G = (Uint8)((g_stops[idx] * (1.0 - frac) + g_stops[idx + 1] * frac) * 255);
    *B = (Uint8)((b_stops[idx] * (1.0 - frac) + b_stops[idx + 1] * frac) * 255);
}

static void heatmap_rgb_viridis(double t, Uint8 *R, Uint8 *G, Uint8 *B)
{
    t = fmax(0.0, fmin(1.0, t));
    double r_stops[] = {0.267004, 0.229739, 0.312916, 0.575593, 0.993248};
    double g_stops[] = {0.004874, 0.322361, 0.632786, 0.815276, 0.906157};
    double b_stops[] = {0.329415, 0.547495, 0.658336, 0.619543, 0.143936};
    double scaled_t = t * 4.0;
    int idx = (int)scaled_t;
    if (idx >= 4)
        idx = 3;
    double frac = scaled_t - idx;
    *R = (Uint8)((r_stops[idx] * (1.0 - frac) + r_stops[idx + 1] * frac) * 255);
    *G = (Uint8)((g_stops[idx] * (1.0 - frac) + g_stops[idx + 1] * frac) * 255);
    *B = (Uint8)((b_stops[idx] * (1.0 - frac) + b_stops[idx + 1] * frac) * 255);
}

static int current_colormap = 0;
static void heatmap_rgb(double t, Uint8 *R, Uint8 *G, Uint8 *B)
{
    switch (current_colormap)
    {
    case 1:
        heatmap_rgb_plasma(t, R, G, B);
        break;
    case 2:
        heatmap_rgb_inferno(t, R, G, B);
        break;
    case 3:
        heatmap_rgb_magma(t, R, G, B);
        break;
    default:
        heatmap_rgb_viridis(t, R, G, B);
    }
}

static void draw_colorBar(SDL_Renderer *r, PiChart *chart, double zmin, double zmax,
                          int W, int H, int margin, int x_pos, int y_top, int y_bottom, int width)
{
    if (zmax <= zmin)
        zmax = zmin + 1e-9;

    int height = y_bottom - y_top;
    if (height <= 0)
        return;

    for (int y = 0; y < height; y++)
    {
        double t = 1.0 - (double)y / height;
        if (t < 0)
            t = 0;
        if (t > 1)
            t = 1;

        Uint8 R, G, B;
        heatmap_rgb(t, &R, &G, &B);
        SDL_SetRenderDrawColor(r, R, G, B, 255);
        SDL_RenderDrawLine(r, x_pos, y_top + y, x_pos + width, y_top + y);
    }

    set_drawColor(r, 0x333333, 255);
    SDL_Rect border = {x_pos, y_top, width, height};
    SDL_RenderDrawRect(r, &border);

    TTF_Font *font = get_openFont(10);
    if (font)
    {
        int num_ticks = 5;
        for (int i = 0; i <= num_ticks; i++)
        {
            double t = (double)i / num_ticks;
            double value = zmin + t * (zmax - zmin);
            int y_pos = y_top + (int)((1.0 - t) * height);
            SDL_RenderDrawLine(r, x_pos + width, y_pos, x_pos + width + 5, y_pos);

            char label[32];
            if (fabs(value) < 0.001 || fabs(value) > 1000)
                snprintf(label, sizeof(label), "%.2e", value);
            else if (fabs(value - round(value)) < 0.0001)
                snprintf(label, sizeof(label), "%.0f", value);
            else
                snprintf(label, sizeof(label), "%.2f", value);

            int tw = strlen(label) * 5;
            draw_text(r, font, label, x_pos + width + 8, y_pos - 5, 0x444444, TEXT_NONE);
        }

        draw_text(r, font, "Value", x_pos + width / 2 - 15, y_top - 20, 0x444444, TEXT_NONE);
    }
}

Value pt_chart(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_CONTEXT(argv[0]))
        return NIL_VAL;
    PiContext *ctx = AS_CONTEXT(argv[0]);
    return NEW_OBJ(add_obj(vm, new_chart(ctx)));
}

Value pt_func(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_CHART(argv[0]))
    {
        vm_error(vm, "func() takes a chart as first argument");
        return NIL_VAL;
    }
    if (argc < 3 || !IS_LIST(argv[1]) || !IS_FUN(argv[2]))
        return NIL_VAL;
    PiChart *chart = AS_CHART(argv[0]);

    PiList *x_list = AS_LIST(argv[1]);
    int num_points = LIST_SIZE(x_list->items);

    if (num_points < 1)
        return NIL_VAL;

    list_t *y_list = list_create(VALUE_SIZE);

    for (int i = 0; i < num_points; i++)
    {
        Value *vx = (Value *)list_getAt(x_list->items, i);
        if (!IS_NUM(*vx))
            continue;

        double x = AS_NUM(*vx);
        Value args[1] = {NEW_NUM(x)};
        Value result = call_func(vm, AS_FUN(argv[2]), 1, args, NEW_NIL());

        if (IS_NUM(result))
        {
            list_add(y_list, &result);
        }
        else
        {
            Value nan_val = NEW_NAN();
            list_add(y_list, &nan_val);
        }
    }

    Object *y_obj = new_list(y_list);
    add_obj(vm, y_obj);
    Value vy_list = NEW_OBJ(y_obj);

    list_t *tail = list_create(VALUE_SIZE);
    list_add(tail, &argv[1]);
    list_add(tail, &vy_list);

    if (argc > 3 && IS_NUM(argv[3]))
    {
        Value vcolor = argv[3];
        list_add(tail, &vcolor);
    }
    return make_kindSeries(vm, chart, "line", tail);
}

Value pt_scatter(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_CHART(argv[0]))
    {
        vm_error(vm, "scatter() takes a chart as first argument");
        return NIL_VAL;
    }
    if (argc < 3 || !IS_LIST(argv[1]) || !IS_LIST(argv[2]))
        return NIL_VAL;
    PiChart *chart = AS_CHART(argv[0]);
    list_t *tail = list_create(VALUE_SIZE);
    list_add(tail, &argv[1]);
    list_add(tail, &argv[2]);
    if (argc > 3)
    {
        if (IS_NUM(argv[3]))
        {
            Value vcolor = argv[3];
            list_add(tail, &vcolor);
        }
        else if (IS_STRING(argv[3]))
        {
            Value vshape = argv[3];
            list_add(tail, &vshape);
        }
    }
    if (argc > 4 && IS_STRING(argv[4]))
    {
        Value vshape = argv[4];
        list_add(tail, &vshape);
    }
    return make_kindSeries(vm, chart, "scatter", tail);
}

Value pt_line(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_CHART(argv[0]))
    {
        vm_error(vm, "line() takes a chart as first argument");
        return NIL_VAL;
    }
    if (argc < 3 || !IS_LIST(argv[1]) || !IS_LIST(argv[2]))
        return NIL_VAL;
    PiChart *chart = AS_CHART(argv[0]);
    list_t *tail = list_create(VALUE_SIZE);
    list_add(tail, &argv[1]);
    list_add(tail, &argv[2]);
    if (argc > 3 && IS_NUM(argv[3]))
    {
        Value vcolor = argv[3];
        list_add(tail, &vcolor);
    }
    return make_kindSeries(vm, chart, "line", tail);
}

Value pt_bar(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_CHART(argv[0]))
    {
        vm_error(vm, "bar() takes a chart as first argument");
        return NIL_VAL;
    }
    if (argc < 3 || !IS_LIST(argv[1]) || !IS_LIST(argv[2]))
        return NIL_VAL;
    PiChart *chart = AS_CHART(argv[0]);
    list_t *tail = list_create(VALUE_SIZE);
    list_add(tail, &argv[1]);
    list_add(tail, &argv[2]);
    if (argc > 3 && IS_NUM(argv[3]))
    {
        Value vcolor = argv[3];
        list_add(tail, &vcolor);
    }
    return make_kindSeries(vm, chart, "bar", tail);
}

Value pt_hist(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_CHART(argv[0]))
    {
        vm_error(vm, "hist() takes a chart as first argument");
        return NIL_VAL;
    }
    if (argc < 2 || !IS_LIST(argv[1]))
        return NIL_VAL;
    PiChart *chart = AS_CHART(argv[0]);
    list_t *tail = list_create(VALUE_SIZE);
    list_add(tail, &argv[1]);
    if (argc > 2 && IS_NUM(argv[2]))
    {
        Value vb = argv[2];
        list_add(tail, &vb);
    }
    if (argc > 3 && IS_NUM(argv[3]))
    {
        Value vcolor = argv[3];
        list_add(tail, &vcolor);
    }
    return make_kindSeries(vm, chart, "hist", tail);
}

Value pt_step(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_CHART(argv[0]))
    {
        vm_error(vm, "step() takes a chart as first argument");
        return NIL_VAL;
    }
    if (argc < 3 || !IS_LIST(argv[1]) || !IS_LIST(argv[2]))
        return NIL_VAL;
    PiChart *chart = AS_CHART(argv[0]);
    list_t *tail = list_create(VALUE_SIZE);
    list_add(tail, &argv[1]);
    list_add(tail, &argv[2]);
    if (argc > 3 && IS_NUM(argv[3]))
    {
        Value vcolor = argv[3];
        list_add(tail, &vcolor);
    }
    return make_kindSeries(vm, chart, "step", tail);
}

Value pt_heatmap(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_CHART(argv[0]))
    {
        vm_error(vm, "heatmap() takes a chart as first argument");
        return NIL_VAL;
    }
    if (argc < 2 || !IS_TENSOR(argv[1]) || AS_TENSOR(argv[1])->ndim != 2)
    {
        vm_error(vm, "heatmap requires a 2d tensor as second argument");
        return NIL_VAL;
    }
    PiChart *chart = AS_CHART(argv[0]);
    list_t *tail = list_create(VALUE_SIZE);
    list_add(tail, &argv[1]);
    return make_kindSeries(vm, chart, "heatmap", tail);
}

Value pt_title(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_CHART(argv[0]))
    {
        vm_error(vm, "title() takes a chart as first argument");
        return NIL_VAL;
    }
    if (argc < 2 || !IS_STRING(argv[1]))
        return NIL_VAL;
    PiChart *chart = AS_CHART(argv[0]);
    if (chart->title)
        free(chart->title);
    chart->title = strdup(AS_CSTRING(argv[1]));
    return NEW_OBJ((Object *)chart);
}

Value pt_xlabel(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_CHART(argv[0]))
    {
        vm_error(vm, "xlabel() takes a chart as first argument");
        return NIL_VAL;
    }
    if (argc < 2 || !IS_STRING(argv[1]))
        return NIL_VAL;
    PiChart *chart = AS_CHART(argv[0]);
    if (chart->xlabel)
        free(chart->xlabel);
    chart->xlabel = strdup(AS_CSTRING(argv[1]));
    return NEW_OBJ((Object *)chart);
}

Value pt_ylabel(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_CHART(argv[0]))
    {
        vm_error(vm, "ylabel() takes a chart as first argument");
        return NIL_VAL;
    }
    if (argc < 2 || !IS_STRING(argv[1]))
        return NIL_VAL;
    PiChart *chart = AS_CHART(argv[0]);
    if (chart->ylabel)
        free(chart->ylabel);
    chart->ylabel = strdup(AS_CSTRING(argv[1]));
    return NEW_OBJ((Object *)chart);
}

Value pt_grid(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_CHART(argv[0]))
    {
        vm_error(vm, "grid() takes a chart as first argument");
        return NIL_VAL;
    }
    if (argc < 2)
        return NIL_VAL;
    PiChart *chart = AS_CHART(argv[0]);
    chart->show_grid = IS_BOOL(argv[1]) ? AS_BOOL(argv[1]) : (IS_NUM(argv[1]) && AS_NUM(argv[1]) != 0.0);
    return NEW_OBJ((Object *)chart);
}

Value pt_axes(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_CHART(argv[0]))
    {
        vm_error(vm, "axes() takes a chart as first argument");
        return NIL_VAL;
    }
    if (argc < 2)
        return NIL_VAL;
    PiChart *chart = AS_CHART(argv[0]);
    chart->show_axes = IS_BOOL(argv[1]) ? AS_BOOL(argv[1]) : (IS_NUM(argv[1]) && AS_NUM(argv[1]) != 0.0);
    return NEW_OBJ((Object *)chart);
}

Value pt_tick(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_CHART(argv[0]))
    {
        vm_error(vm, "tick() takes a chart as first argument");
        return NIL_VAL;
    }
    if (argc < 2)
        return NIL_VAL;
    PiChart *chart = AS_CHART(argv[0]);
    chart->show_ticks = IS_BOOL(argv[1]) ? AS_BOOL(argv[1]) : (IS_NUM(argv[1]) && AS_NUM(argv[1]) != 0.0);
    return NEW_OBJ((Object *)chart);
}

Value pt_legend(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_CHART(argv[0]))
    {
        vm_error(vm, "legend() takes a chart as first argument");
        return NIL_VAL;
    }
    if (argc < 2 || !IS_LIST(argv[1]))
        return NIL_VAL;

    PiChart *chart = AS_CHART(argv[0]);
    list_t *tail = list_create(VALUE_SIZE);
    list_add(tail, &argv[1]);

    double legend_x = -1.0;
    double legend_y = -1.0;

    if (argc >= 3 && IS_NUM(argv[2]))
        legend_x = AS_NUM(argv[2]);
    if (argc >= 4 && IS_NUM(argv[3]))
        legend_y = AS_NUM(argv[3]);

    list_t *pos_list = list_create(VALUE_SIZE);
    Value vx = NEW_NUM(legend_x);
    Value vy = NEW_NUM(legend_y);
    list_add(pos_list, &vx);
    list_add(pos_list, &vy);

    Object *pos_obj = new_list(pos_list);
    add_obj(vm, pos_obj);
    Value pos_val = NEW_OBJ(pos_obj);

    list_add(tail, &pos_val);

    return make_kindSeries(vm, chart, "legend", tail);
}

static void draw_legend(DrawContext *dc, list_t *items,
                        int border_left, int border_top,
                        int border_right, int border_bottom,
                        int has_heatmap, PiTensor *heatmap_matrix,
                        double cell_size, TTF_Font *legend_font)
{
    if (LIST_SIZE(items) < 2)
        return;
    SDL_Renderer *r = dc->r;

    PiList *leg = AS_LIST(*(Value *)list_getAt(items, 1));
    int ln = LIST_SIZE(leg->items);

    double legend_x = -1.0, legend_y = -1.0;
    if (LIST_SIZE(items) >= 3)
    {
        PiList *pos = AS_LIST(*(Value *)list_getAt(items, 2));
        if (LIST_SIZE(pos->items) >= 2)
        {
            Value *px = (Value *)list_getAt(pos->items, 0);
            Value *py = (Value *)list_getAt(pos->items, 1);
            if (IS_NUM(*px))
                legend_x = AS_NUM(*px);
            if (IS_NUM(*py))
                legend_y = AS_NUM(*py);
        }
    }

    int legend_width = 120;
    int legend_height = ln * 22 + 10;
    int legend_x_pos, legend_y_pos;

    if (legend_x >= 0 && legend_y >= 0)
    {
        if (has_heatmap && heatmap_matrix)
        {
            legend_x_pos = border_left + (int)(legend_x * cell_size);
            legend_y_pos = border_top + (int)(legend_y * cell_size);
        }
        else
        {
            int px_tmp, py_tmp;
            map_xy(dc->chart, legend_x, legend_y, dc->W, dc->H, dc->margin, plot_margin, &px_tmp, &py_tmp);
            legend_x_pos = px_tmp;
            legend_y_pos = py_tmp - legend_height / 2;
        }
    }
    else if (legend_x >= 0 && legend_y < 0)
    {
        if (has_heatmap && heatmap_matrix)
        {
            legend_x_pos = border_left + (int)(legend_x * cell_size);
            legend_y_pos = border_top + 10;
        }
        else
        {
            int px_tmp, py_tmp;
            map_xy(dc->chart, legend_x, dc->chart->ymax, dc->W, dc->H, dc->margin, plot_margin, &px_tmp, &py_tmp);
            legend_x_pos = px_tmp;
            legend_y_pos = py_tmp + 10;
        }
    }
    else if (legend_x < 0 && legend_y >= 0)
    {
        if (has_heatmap && heatmap_matrix)
        {
            legend_x_pos = border_right - legend_width - 10;
            legend_y_pos = border_top + (int)(legend_y * cell_size);
        }
        else
        {
            int px_tmp, py_tmp;
            map_xy(dc->chart, dc->chart->xmax, legend_y, dc->W, dc->H, dc->margin, plot_margin, &px_tmp, &py_tmp);
            legend_x_pos = px_tmp - legend_width - 10;
            legend_y_pos = py_tmp - legend_height / 2;
        }
    }
    else
    {
        legend_x_pos = border_right - legend_width - 10;
        legend_y_pos = border_top + 10;
    }

    if (legend_x_pos < border_left + 10)
        legend_x_pos = border_left + 10;
    if (legend_x_pos + legend_width > border_right - 10)
        legend_x_pos = border_right - legend_width - 10;
    if (legend_y_pos < border_top + 10)
        legend_y_pos = border_top + 10;
    if (legend_y_pos + legend_height > border_bottom - 10)
        legend_y_pos = border_bottom - legend_height - 10;

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    set_drawColor(r, 0xffffff, 220);
    SDL_Rect legend_bg = {legend_x_pos - 5, legend_y_pos - 5, legend_width + 10, legend_height + 10};
    SDL_RenderFillRect(r, &legend_bg);
    set_drawColor(r, 0x000000, 255);
    SDL_RenderDrawRect(r, &legend_bg);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    for (int li = 0; li < ln; li++)
    {
        Value *lv = (Value *)list_getAt(leg->items, li);
        if (!IS_STRING(*lv))
            continue;
        int c = palette_color(li);
        set_drawColor(r, c, 255);
        SDL_Rect sq = {legend_x_pos, legend_y_pos + li * 22, 12, 12};
        SDL_RenderFillRect(r, &sq);
        set_drawColor(r, 0x000000, 255);
        SDL_RenderDrawRect(r, &sq);
        if (legend_font)
            draw_text(r, legend_font, AS_CSTRING(*lv),
                      legend_x_pos + 18, legend_y_pos + li * 22 - 2, 0x333333, TEXT_NONE);
    }
}

static int series_color(list_t *items, int default_color)
{
    if (LIST_SIZE(items) >= 4)
    {
        Value *vc = (Value *)list_getAt(items, 3);
        if (vc && IS_NUM(*vc))
            return (int)AS_NUM(*vc);
    }
    return default_color;
}

static char series_shape(list_t *items)
{
    if (LIST_SIZE(items) >= 5)
    {
        Value *vs = (Value *)list_getAt(items, 4);
        if (vs && IS_STRING(*vs))
        {
            const char *s = AS_CSTRING(*vs);
            return s[0];
        }
    }
    if (LIST_SIZE(items) >= 4)
    {
        Value *vs = (Value *)list_getAt(items, 3);
        if (vs && IS_STRING(*vs))
        {
            const char *s = AS_CSTRING(*vs);
            return s[0];
        }
    }
    return 'o';
}

static void draw_scatter(DrawContext *dc, list_t *items)
{
    if (LIST_SIZE(items) < 3)
        return;

    PiList *xl = AS_LIST(*(Value *)list_getAt(items, 1));
    PiList *yl = AS_LIST(*(Value *)list_getAt(items, 2));
    int n = LIST_SIZE(xl->items);

    int col = series_color(items, dc->col);
    char shape = series_shape(items);

    for (int i = 0; i < n; i++)
    {
        Value *vx = (Value *)list_getAt(xl->items, i);
        Value *vy = (Value *)list_getAt(yl->items, i);

        if (!IS_NUM(*vx) || !IS_NUM(*vy))
            continue;

        int px, py;
        map_xy(dc->chart,
               AS_NUM(*vx),
               AS_NUM(*vy),
               dc->W, dc->H,
               dc->margin,
               plot_margin,
               &px, &py);

        if (shape == 'x')
        {
            set_drawColor(dc->r, col, 255);

            int len = 3; // smaller arms (was 6)
            int w = 1;   // thin stroke

            // "\" diagonal
            for (int i = -w; i <= w; i++)
            {
                SDL_RenderDrawLine(dc->r,
                                   px - len, py - len + i,
                                   px + len, py + len + i);
            }

            // "/" diagonal
            for (int i = -w; i <= w; i++)
            {
                SDL_RenderDrawLine(dc->r,
                                   px - len, py + len + i,
                                   px + len, py - len + i);
            }
        }
        else if (shape == 's')
        {
            set_drawColor(dc->r, col, 220);
            SDL_Rect rect = {px - 5, py - 5, 10, 10};
            SDL_RenderFillRect(dc->r, &rect);
            set_drawColor(dc->r, 0x000000, 255);
            SDL_RenderDrawRect(dc->r, &rect);
        }
        else
        {
            draw_filledBorderedCircle(dc->r, px, py, 5, col, 0x000000);
        }
    }
}

static void draw_line(DrawContext *dc, list_t *items)
{
    if (LIST_SIZE(items) < 3)
        return;
    PiList *xl = AS_LIST(*(Value *)list_getAt(items, 1));
    PiList *yl = AS_LIST(*(Value *)list_getAt(items, 2));
    int n = LIST_SIZE(xl->items);
    for (int i = 0; i < n - 1; i++)
    {
        Value *vx0 = (Value *)list_getAt(xl->items, i);
        Value *vy0 = (Value *)list_getAt(yl->items, i);
        Value *vx1 = (Value *)list_getAt(xl->items, i + 1);
        Value *vy1 = (Value *)list_getAt(yl->items, i + 1);
        if (!IS_NUM(*vx0) || !IS_NUM(*vy0) || !IS_NUM(*vx1) || !IS_NUM(*vy1))
            continue;
        int p0x, p0y, p1x, p1y;
        map_xy(dc->chart, AS_NUM(*vx0), AS_NUM(*vy0), dc->W, dc->H, dc->margin, plot_margin, &p0x, &p0y);
        map_xy(dc->chart, AS_NUM(*vx1), AS_NUM(*vy1), dc->W, dc->H, dc->margin, plot_margin, &p1x, &p1y);

        int col = series_color(items, dc->col);
        set_drawColor(dc->r, col, 255);

        // Draw three slightly offset lines to create thickness = 2
        SDL_RenderDrawLine(dc->r, p0x, p0y, p1x, p1y);
        SDL_RenderDrawLine(dc->r, p0x + 1, p0y, p1x + 1, p1y);
        SDL_RenderDrawLine(dc->r, p0x, p0y + 1, p1x, p1y + 1);
    }
}

static void draw_step(DrawContext *dc, list_t *items)
{
    if (LIST_SIZE(items) < 3)
        return;
    PiList *xl = AS_LIST(*(Value *)list_getAt(items, 1));
    PiList *yl = AS_LIST(*(Value *)list_getAt(items, 2));
    int n = LIST_SIZE(xl->items);
    for (int i = 0; i < n - 1; i++)
    {
        Value *vx0 = (Value *)list_getAt(xl->items, i);
        Value *vy0 = (Value *)list_getAt(yl->items, i);
        Value *vx1 = (Value *)list_getAt(xl->items, i + 1);
        Value *vy1 = (Value *)list_getAt(yl->items, i + 1);
        if (!IS_NUM(*vx0) || !IS_NUM(*vy0) || !IS_NUM(*vx1) || !IS_NUM(*vy1))
            continue;
        int p0x, p0y, p1x, p1y, p2x, p2y;
        map_xy(dc->chart, AS_NUM(*vx0), AS_NUM(*vy0), dc->W, dc->H, dc->margin, plot_margin, &p0x, &p0y);
        map_xy(dc->chart, AS_NUM(*vx1), AS_NUM(*vy0), dc->W, dc->H, dc->margin, plot_margin, &p1x, &p1y);
        map_xy(dc->chart, AS_NUM(*vx1), AS_NUM(*vy1), dc->W, dc->H, dc->margin, plot_margin, &p2x, &p2y);

        int col = series_color(items, dc->col);
        set_drawColor(dc->r, col, 255);

        // Horizontal segment
        SDL_RenderDrawLine(dc->r, p0x, p0y, p1x, p1y);
        SDL_RenderDrawLine(dc->r, p0x + 1, p0y, p1x + 1, p1y);
        SDL_RenderDrawLine(dc->r, p0x, p0y + 1, p1x, p1y + 1);

        // Vertical segment
        SDL_RenderDrawLine(dc->r, p1x, p1y, p2x, p2y);
        SDL_RenderDrawLine(dc->r, p1x + 1, p1y, p2x + 1, p2y);
        SDL_RenderDrawLine(dc->r, p1x, p1y + 1, p2x, p2y + 1);
    }
}

static void draw_bar(DrawContext *dc, list_t *items)
{
    if (LIST_SIZE(items) < 3)
        return;
    PiList *xl = AS_LIST(*(Value *)list_getAt(items, 1));
    PiList *yl = AS_LIST(*(Value *)list_getAt(items, 2));
    int n = LIST_SIZE(yl->items);
    (void)xl;
    for (int i = 0; i < n; i++)
    {
        Value *vy = (Value *)list_getAt(yl->items, i);
        if (!IS_NUM(*vy))
            continue;
        double yi = AS_NUM(*vy);
        double xleft = (double)i - 0.4;
        double xright = (double)i + 0.4;
        int ax0, ay0, ax1, ay1, bx0, ay1b;
        map_xy(dc->chart, xleft, 0, dc->W, dc->H, dc->margin, plot_margin, &ax0, &ay0);
        map_xy(dc->chart, xright, 0, dc->W, dc->H, dc->margin, plot_margin, &ax1, &ay1);
        map_xy(dc->chart, xleft, yi, dc->W, dc->H, dc->margin, plot_margin, &bx0, &ay1b);
        SDL_Rect rect = {ax0, ay1b, ax1 - ax0, ay0 - ay1b};
        if (rect.w < 1)
            rect.w = 1;
        if (rect.h < 1)
            rect.h = 1;
        int col = series_color(items, dc->col);
        set_drawColor(dc->r, col, 220);
        SDL_RenderFillRect(dc->r, &rect);
        set_drawColor(dc->r, 0x000000, 255);
        SDL_RenderDrawRect(dc->r, &rect);
    }
}

static void draw_hist(DrawContext *dc, list_t *items)
{
    if (LIST_SIZE(items) < 2)
        return;
    PiList *dl = AS_LIST(*(Value *)list_getAt(items, 1));
    int bins = 10;
    if (LIST_SIZE(items) > 2)
    {
        Value *vb = (Value *)list_getAt(items, 2);
        if (IS_NUM(*vb))
            bins = (int)AS_NUM(*vb);
    }
    if (bins < 1)
        bins = 1;
    int nd = LIST_SIZE(dl->items);
    if (nd < 1)
        return;

    double lo = list_min(dl->items);
    double hi = list_max(dl->items);
    if (hi <= lo)
        hi = lo + 1e-9;

    double *counts = (double *)calloc((size_t)bins, sizeof(double));
    if (!counts)
        return;
    for (int i = 0; i < nd; i++)
    {
        Value *v = (Value *)list_getAt(dl->items, i);
        if (!IS_NUM(*v))
            continue;
        double x = AS_NUM(*v);
        int b = (int)((x - lo) / (hi - lo) * bins);
        if (b < 0)
            b = 0;
        if (b >= bins)
            b = bins - 1;
        counts[b] += 1.0;
    }

    for (int b = 0; b < bins; b++)
    {
        double x0 = lo + (hi - lo) * b / bins;
        double x1 = lo + (hi - lo) * (b + 1) / bins;
        double h = counts[b];
        int ax0, ay0, ax1, bx0, by1;
        map_xy(dc->chart, x0, 0, dc->W, dc->H, dc->margin, plot_margin, &ax0, &ay0);
        map_xy(dc->chart, x1, 0, dc->W, dc->H, dc->margin, plot_margin, &ax1, &ay0);
        map_xy(dc->chart, x0, h, dc->W, dc->H, dc->margin, plot_margin, &bx0, &by1);
        SDL_Rect rect = {ax0, by1, ax1 - ax0, ay0 - by1};
        if (rect.w < 1)
            rect.w = 1;
        if (rect.h < 1)
            rect.h = 1;
        int col = series_color(items, dc->col);
        set_drawColor(dc->r, col, 220);
        SDL_RenderFillRect(dc->r, &rect);
        set_drawColor(dc->r, 0x000000, 255);
        SDL_RenderDrawRect(dc->r, &rect);
    }
    free(counts);
}

static void draw_heatmap(DrawContext *dc, list_t *items,
                         int heatmap_start_x, int heatmap_start_y,
                         int heatmap_width, int heatmap_height,
                         double cell_size,
                         int border_right, int border_top, int border_bottom)
{
    if (LIST_SIZE(items) < 2)
        return;
    Value *mv = (Value *)list_getAt(items, 1);
    if (!IS_TENSOR(*mv) || AS_TENSOR(*mv)->ndim != 2)
        return;
    PiTensor *M = AS_TENSOR(*mv);

    double zmin = INFINITY, zmax = -INFINITY;
    int indices[2];
    for (int row = 0; row < M->rows; row++)
    {
        indices[0] = row;
        for (int col = 0; col < M->cols; col++)
        {
            indices[1] = col;
            double z = tensor_get(M, indices);
            if (z < zmin)
                zmin = z;
            if (z > zmax)
                zmax = z;
        }
    }
    if (zmax <= zmin)
        zmax = zmin + 1e-9;

    for (int row = 0; row < M->rows; row++)
    {
        indices[0] = row;
        for (int col = 0; col < M->cols; col++)
        {
            indices[1] = col;
            double z = tensor_get(M, indices);
            double t = (z - zmin) / (zmax - zmin);
            if (t < 0)
                t = 0;
            if (t > 1)
                t = 1;
            Uint8 Rc, Gc, Bc;
            heatmap_rgb(t, &Rc, &Gc, &Bc);
            SDL_SetRenderDrawColor(dc->r, Rc, Gc, Bc, 255);
            SDL_Rect rect = {
                heatmap_start_x + (int)(col * cell_size),
                heatmap_start_y + (int)(row * cell_size),
                (int)ceil(cell_size),
                (int)ceil(cell_size)};
            SDL_RenderFillRect(dc->r, &rect);
        }
    }

    if (dc->chart->show_grid && cell_size > 10)
    {
        set_drawColor(dc->r, 0xffffff, 180);
        for (int col = 1; col < M->cols; col++)
        {
            int x = heatmap_start_x + (int)(col * cell_size);
            SDL_RenderDrawLine(dc->r, x, heatmap_start_y, x, heatmap_start_y + heatmap_height);
        }
        for (int row = 1; row < M->rows; row++)
        {
            int y = heatmap_start_y + (int)(row * cell_size);
            SDL_RenderDrawLine(dc->r, heatmap_start_x, y, heatmap_start_x + heatmap_width, y);
        }
    }

    int color_bar_x = border_right + 15;
    int color_bar_width = 25;
    draw_colorBar(dc->r, dc->chart, zmin, zmax, dc->W, dc->H, dc->margin,
                  color_bar_x, border_top, border_bottom, color_bar_width);

    TTF_Font *small_font = get_openFont(10);
    if (small_font)
    {
        draw_text(dc->r, small_font, "Value",
                  color_bar_x + color_bar_width / 2,
                  (border_top + border_bottom) / 2, 0x444444, TEXT_VERTICAL | TEXT_BOLD);
        TTF_CloseFont(small_font);
    }
}

Value pt_show(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_CHART(argv[0]))
    {
        vm_error(vm, "show() takes a chart as first argument");
        return NIL_VAL;
    }
    PiChart *chart = AS_CHART(argv[0]);
    PiContext *ctx = chart->ctx;
    SDL_Renderer *r = ctx ? (SDL_Renderer *)ctx->renderer : NULL;

    if (!ctx || !r || ctx->width <= 0 || ctx->height <= 0)
        return NIL_VAL;

    int W = ctx->width, H = ctx->height;
    const int left_margin = plot_left_margin;
    const int margin = plot_margin;

    chart_computeBounds(chart);

    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    SDL_RenderClear(r);

    TTF_Font *title_font = get_openFont(18);
    TTF_Font *label_font = get_openFont(16);
    TTF_Font *tick_font = get_openFont(11);
    TTF_Font *legend_font = get_openFont(12);

    if (chart->title && title_font)
        draw_text(r, title_font, chart->title, W / 2, 15, 0x222222, TEXT_CENTER_X);
    if (chart->xlabel && label_font)
        draw_text(r, label_font, chart->xlabel, W / 2, H - margin + 35, 0x333333, TEXT_CENTER_X);
    if (chart->ylabel && label_font)
        draw_text(r, label_font, chart->ylabel, 25, H / 2, 0x333333, TEXT_VERTICAL | TEXT_CENTER_Y);

    // detect heatmap
    int has_heatmap = 0;
    PiTensor *heatmap_matrix = NULL;
    int heatmap_rows = 0, heatmap_cols = 0;

    for (int si = 0; si < list_size(chart->series); si++)
    {
        Value *sv = (Value *)list_getAt(chart->series, si);
        if (!sv || !IS_LIST(*sv))
            continue;
        list_t *items = AS_LIST(*sv)->items;
        if (LIST_SIZE(items) < 1)
            continue;
        Value *k0 = (Value *)list_getAt(items, 0);
        if (!k0 || !IS_STRING(*k0))
            continue;
        if (!strcmp(AS_CSTRING(*k0), "heatmap"))
        {
            has_heatmap = 1;
            if (LIST_SIZE(items) >= 2)
            {
                Value *mv = (Value *)list_getAt(items, 1);
                if (IS_TENSOR(*mv) && AS_TENSOR(*mv)->ndim == 2)
                {
                    heatmap_matrix = AS_TENSOR(*mv);
                    heatmap_rows = heatmap_matrix->rows;
                    heatmap_cols = heatmap_matrix->cols;
                }
            }
            break;
        }
    }

    // border / plot area
    int border_left, border_top, border_right, border_bottom;
    int heatmap_start_x = 0, heatmap_start_y = 0;
    int heatmap_width = 0, heatmap_height = 0;
    double cell_size = 0;

    if (has_heatmap && heatmap_matrix)
    {
        int plot_width = W - left_margin - margin;
        int plot_height = H - 2 * margin;
        double cw = (double)plot_width / heatmap_cols;
        double ch = (double)plot_height / heatmap_rows;
        cell_size = fmin(cw, ch);
        heatmap_width = (int)(cell_size * heatmap_cols);
        heatmap_height = (int)(cell_size * heatmap_rows);
        heatmap_start_x = left_margin + (plot_width - heatmap_width) / 2;
        heatmap_start_y = margin + (plot_height - heatmap_height) / 2;
        border_left = heatmap_start_x;
        border_top = heatmap_start_y;
        border_right = heatmap_start_x + heatmap_width;
        border_bottom = heatmap_start_y + heatmap_height;
    }
    else
    {
        map_xy(chart, chart->xmin, chart->ymax, W, H, left_margin, margin, &border_left, &border_top);
        map_xy(chart, chart->xmax, chart->ymin, W, H, left_margin, margin, &border_right, &border_bottom);
    }

    // Draw border (single rectangle, all sides same thickness)
    set_drawColor(r, 0x333333, 255);
    SDL_Rect border_rect = {border_left, border_top,
                            border_right - border_left,
                            border_bottom - border_top};
    SDL_RenderDrawRect(r, &border_rect);

    // Removed duplicate axes lines – only the rectangle above is used.

    // ticks
    if (tick_font)
    {
        if (has_heatmap && heatmap_matrix)
        {
            for (int col = 0; col < heatmap_cols; col++)
            {
                int x = heatmap_start_x + (int)((col + 0.5) * cell_size);
                int y = border_bottom;
                SDL_RenderDrawLine(r, x, y, x, y + 5);
                if (heatmap_cols <= 20 || col % (heatmap_cols / 10 + 1) == 0)
                {
                    char label[32];
                    snprintf(label, sizeof(label), "%d", col);
                    draw_text(r, tick_font, label, x - (int)(strlen(label) * 5) / 2, y + 8, 0x555555, TEXT_NONE);
                }
            }
            for (int row = 0; row < heatmap_rows; row++)
            {
                int x = border_left;
                int y = heatmap_start_y + (int)((row + 0.5) * cell_size);
                SDL_RenderDrawLine(r, x - 5, y, x, y);
                if (heatmap_rows <= 20 || row % (heatmap_rows / 10 + 1) == 0)
                {
                    char label[32];
                    snprintf(label, sizeof(label), "%d", row);
                    draw_text(r, tick_font, label, x - (int)(strlen(label) * 5) - 8, y - 6, 0x555555, TEXT_NONE);
                }
            }
        }
        else if (chart->show_ticks)
        {
            draw_axisTicks(r, chart, tick_font, W, H, left_margin, margin);
        }
    }

    // grid – clipped to the *interior* of the border (so it never crosses the border)
    if (chart->show_grid && !has_heatmap)
    {
        // Clip to area exactly one pixel inside the border
        SDL_Rect inner_rect = {
            border_left + 1,
            border_top + 1,
            (border_right - border_left) - 2,
            (border_bottom - border_top) - 2};
        // Only enable clipping if the inner rectangle has positive size
        if (inner_rect.w > 0 && inner_rect.h > 0)
        {
            SDL_RenderSetClipRect(r, &inner_rect);
            set_drawColor(r, 0xe0e0e0, 200);
            for (int g = 1; g < 10; g++)
            {
                double tx = chart->xmin + (chart->xmax - chart->xmin) * g / 10.0;
                double ty = chart->ymin + (chart->ymax - chart->ymin) * g / 10.0;
                int px0, py0, px1, py1;
                map_xy(chart, tx, chart->ymin, W, H, left_margin, margin, &px0, &py0);
                map_xy(chart, tx, chart->ymax, W, H, left_margin, margin, &px1, &py1);
                SDL_RenderDrawLine(r, px0, py0, px1, py1);
                map_xy(chart, chart->xmin, ty, W, H, left_margin, margin, &px0, &py0);
                map_xy(chart, chart->xmax, ty, W, H, left_margin, margin, &px1, &py1);
                SDL_RenderDrawLine(r, px0, py0, px1, py1);
            }
            SDL_RenderSetClipRect(r, NULL);
        }
    }

    // series loop
    int series_index = 0;
    int ns = list_size(chart->series);

    for (int si = 0; si < ns; si++)
    {
        Value *series_val = (Value *)list_getAt(chart->series, si);
        if (!series_val || !IS_LIST(*series_val))
            continue;
        list_t *items = AS_LIST(*series_val)->items;
        if (LIST_SIZE(items) < 1)
            continue;
        Value *k0 = (Value *)list_getAt(items, 0);
        if (!k0 || !IS_STRING(*k0))
            continue;
        const char *kind = AS_CSTRING(*k0);

        DrawContext dc = {r, chart, W, H, left_margin, palette_color(series_index)};

        if (!strcmp(kind, "legend"))
        {
            draw_legend(&dc, items,
                        border_left, border_top, border_right, border_bottom,
                        has_heatmap, heatmap_matrix, cell_size, legend_font);
        }
        else if (!strcmp(kind, "scatter"))
        {
            draw_scatter(&dc, items);
            series_index++;
        }
        else if (!strcmp(kind, "line"))
        {
            draw_line(&dc, items);
            series_index++;
        }
        else if (!strcmp(kind, "step"))
        {
            draw_step(&dc, items);
            series_index++;
        }
        else if (!strcmp(kind, "bar"))
        {
            draw_bar(&dc, items);
            series_index++;
        }
        else if (!strcmp(kind, "hist"))
        {
            draw_hist(&dc, items);
            series_index++;
        }
        else if (!strcmp(kind, "heatmap"))
        {
            draw_heatmap(&dc, items,
                         heatmap_start_x, heatmap_start_y,
                         heatmap_width, heatmap_height,
                         cell_size,
                         border_right, border_top, border_bottom);
            series_index++;
        }
    }

    if (title_font)
        TTF_CloseFont(title_font);
    if (label_font)
        TTF_CloseFont(label_font);
    if (tick_font)
        TTF_CloseFont(tick_font);
    if (legend_font)
        TTF_CloseFont(legend_font);

    SDL_RenderPresent(r);
    return NIL_VAL;
}

static BuiltinFunc plot_funcs[] = {
    {"chart", pt_chart},
    {"func", pt_func},
    {"scatter", pt_scatter},
    {"bar", pt_bar},
    {"line", pt_line},
    {"hist", pt_hist},
    {"step", pt_step},
    {"heatmap", pt_heatmap},
    {"show", pt_show},
    {"title", pt_title},
    {"xlabel", pt_xlabel},
    {"ylabel", pt_ylabel},
    {"tick", pt_tick},
    {"grid", pt_grid},
    {"axes", pt_axes},
    {"legend", pt_legend},
};

static BuiltinConst plot_consts[] = {};
DEFINE_BUILTIN_MODULE(module_plot, "plot", plot_funcs, plot_consts);
