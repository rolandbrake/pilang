#include "pi_plot.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include "../common.h"
#include "pi_builtin.h"

#ifndef NIL_VAL
#define NIL_VAL NEW_NIL()
#define TRUE_VAL NEW_BOOL(true)
#define FALSE_VAL NEW_BOOL(false)
#endif

static SDL_Renderer *renderer_of(PiContext *ctx)
{
    return ctx ? (SDL_Renderer *)ctx->renderer : NULL;
}

static TTF_Font *try_open_font(int size)
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

static void set_draw_color(SDL_Renderer *r, int rgb, Uint8 a)
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

static void map_xy(PiChart *chart, double x, double y, int W, int H, int margin, int *px, int *py)
{
    double x0 = chart->xmin, x1 = chart->xmax, y0 = chart->ymin, y1 = chart->ymax;
    if (x1 <= x0)
        x1 = x0 + 1e-9;
    if (y1 <= y0)
        y1 = y0 + 1e-9;
    *px = margin + (int)((x - x0) / (x1 - x0) * (W - 2 * margin));
    *py = H - margin - (int)((y - y0) / (y1 - y0) * (H - 2 * margin));
}

static void chart_append_series(vm_t *vm, PiChart *chart, list_t *parts)
{
    Object *lo = new_list(parts);
    add_obj(vm, lo);
    Value v = NEW_OBJ(lo);
    list_add(chart->series, &v);
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
    chart_append_series(vm, chart, parts);
    list_free(tail);
    return NEW_OBJ((Object *)chart);
}

static void chart_compute_bounds(PiChart *c)
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
            if (!IS_MATRIX(*mv))
                continue;
            PiMatrix *M = AS_MATRIX(*mv);
            xmin = 0;
            xmax = M->cols > 0 ? M->cols : 1;
            ymin = 0;
            ymax = M->rows > 0 ? M->rows : 1;
        }
        else if (!strcmp(kind, "quiver") || !strcmp(kind, "streamplot"))
        {
            if (LIST_SIZE(items) < 3)
                continue;
            Value *u = (Value *)list_getAt(items, 1);
            Value *vv = (Value *)list_getAt(items, 2);
            if (!IS_MATRIX(*u) || !IS_MATRIX(*vv))
                continue;
            PiMatrix *U = AS_MATRIX(*u);
            xmin = 0;
            xmax = U->cols > 0 ? U->cols : 1;
            ymin = 0;
            ymax = U->rows > 0 ? U->rows : 1;
        }
        else if (!strcmp(kind, "surface") || !strcmp(kind, "mesh") || !strcmp(kind, "wireframe"))
        {
            if (LIST_SIZE(items) < 2)
                continue;
            Value *mv = (Value *)list_getAt(items, 1);
            if (!IS_MATRIX(*mv))
                continue;
            PiMatrix *M = AS_MATRIX(*mv);
            xmin = 0;
            xmax = M->cols > 0 ? M->cols - 1 : 1;
            ymin = 0;
            ymax = M->rows > 0 ? M->rows - 1 : 1;
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

static void draw_text(SDL_Renderer *r, TTF_Font *font, const char *text, int x, int y, int rgb)
{
    if (!font || !text)
        return;
    Uint8 R = (Uint8)((rgb >> 16) & 255);
    Uint8 G = (Uint8)((rgb >> 8) & 255);
    Uint8 B = (Uint8)(rgb & 255);
    SDL_Color fg = {R, G, B, 255};
    SDL_Surface *surf = TTF_RenderUTF8_Blended(font, text, fg);
    if (!surf)
        return;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(r, surf);
    SDL_FreeSurface(surf);
    if (!tex)
        return;
    int tw, th;
    SDL_QueryTexture(tex, NULL, NULL, &tw, &th);
    SDL_Rect dst = {x, y, tw, th};
    SDL_RenderCopy(r, tex, NULL, &dst);
    SDL_DestroyTexture(tex);
}

static void draw_centered_text(SDL_Renderer *r, TTF_Font *font, const char *text, int center_x, int y, int rgb)
{
    if (!font || !text)
        return;
    Uint8 R = (Uint8)((rgb >> 16) & 255);
    Uint8 G = (Uint8)((rgb >> 8) & 255);
    Uint8 B = (Uint8)(rgb & 255);
    SDL_Color fg = {R, G, B, 255};
    SDL_Surface *surf = TTF_RenderUTF8_Blended(font, text, fg);
    if (!surf)
        return;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(r, surf);
    SDL_FreeSurface(surf);
    if (!tex)
        return;
    int tw, th;
    SDL_QueryTexture(tex, NULL, NULL, &tw, &th);
    SDL_Rect dst = {center_x - tw / 2, y, tw, th};
    SDL_RenderCopy(r, tex, NULL, &dst);
    SDL_DestroyTexture(tex);
}

static void draw_rotated_text(SDL_Renderer *r, TTF_Font *font, const char *text, int x, int y, int rgb)
{
    // For y-label, we'll use a simpler approach - just draw it horizontally
    // For proper rotation, would need SDL_ttf's rotated rendering or surface manipulation
    if (!font || !text)
        return;
    Uint8 R = (Uint8)((rgb >> 16) & 255);
    Uint8 G = (Uint8)((rgb >> 8) & 255);
    Uint8 B = (Uint8)(rgb & 255);
    SDL_Color fg = {R, G, B, 255};
    SDL_Surface *surf = TTF_RenderUTF8_Blended(font, text, fg);
    if (!surf)
        return;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(r, surf);
    SDL_FreeSurface(surf);
    if (!tex)
        return;
    int tw, th;
    SDL_QueryTexture(tex, NULL, NULL, &tw, &th);
    SDL_Rect dst = {x, y - th / 2, tw, th};
    SDL_RenderCopy(r, tex, NULL, &dst);
    SDL_DestroyTexture(tex);
}

// Add these helper functions at the top with other helper functions

static void draw_vertical_text(SDL_Renderer *r, TTF_Font *font, const char *text, int x, int y, int rgb)
{
    if (!font || !text)
        return;

    Uint8 R = (Uint8)((rgb >> 16) & 255);
    Uint8 G = (Uint8)((rgb >> 8) & 255);
    Uint8 B = (Uint8)(rgb & 255);
    SDL_Color fg = {R, G, B, 255};

    // Render text to surface
    SDL_Surface *surf = TTF_RenderUTF8_Blended(font, text, fg);
    if (!surf)
        return;

    // Create texture from surface
    SDL_Texture *tex = SDL_CreateTextureFromSurface(r, surf);
    if (!tex)
    {
        SDL_FreeSurface(surf);
        return;
    }

    int tw, th;
    SDL_QueryTexture(tex, NULL, NULL, &tw, &th);

    // Create a rotated surface (90 degrees clockwise for vertical text)
    // For vertical text reading bottom to top, we rotate -90 degrees
    SDL_Surface *rotated = SDL_CreateRGBSurface(0, th, tw, surf->format->BitsPerPixel,
                                                surf->format->Rmask, surf->format->Gmask,
                                                surf->format->Bmask, surf->format->Amask);
    if (!rotated)
    {
        SDL_DestroyTexture(tex);
        SDL_FreeSurface(surf);
        return;
    }

    // Lock surfaces for pixel manipulation
    SDL_LockSurface(surf);
    SDL_LockSurface(rotated);

    // Rotate the surface (90 degrees counter-clockwise for text reading bottom to top)
    for (int i = 0; i < tw; i++)
    {
        for (int j = 0; j < th; j++)
        {
            Uint32 pixel = ((Uint32 *)surf->pixels)[j * tw + i];
            ((Uint32 *)rotated->pixels)[i * th + (th - 1 - j)] = pixel;
        }
    }

    SDL_UnlockSurface(rotated);
    SDL_UnlockSurface(surf);

    // Create texture from rotated surface
    SDL_Texture *rotated_tex = SDL_CreateTextureFromSurface(r, rotated);
    if (rotated_tex)
    {
        int rw, rh;
        SDL_QueryTexture(rotated_tex, NULL, NULL, &rw, &rh);
        SDL_Rect dst = {x, y - rh / 2, rw, rh};
        SDL_RenderCopy(r, rotated_tex, NULL, &dst);
        SDL_DestroyTexture(rotated_tex);
    }

    SDL_FreeSurface(rotated);
    SDL_DestroyTexture(tex);
    SDL_FreeSurface(surf);
}

// Alternative simpler vertical text using TTF_RenderUTF8_Blended with line breaks
static void draw_vertical_text_simple(SDL_Renderer *r, TTF_Font *font, const char *text, int x, int y, int rgb)
{
    if (!font || !text)
        return;

    Uint8 R = (Uint8)((rgb >> 16) & 255);
    Uint8 G = (Uint8)((rgb >> 8) & 255);
    Uint8 B = (Uint8)(rgb & 255);
    SDL_Color fg = {R, G, B, 255};

    // Draw each character vertically
    int len = strlen(text);
    int char_h = TTF_FontHeight(font);
    int total_h = len * char_h;
    int start_y = y - total_h / 2;

    for (int i = 0; i < len; i++)
    {
        char ch[2] = {text[i], '\0'};
        SDL_Surface *surf = TTF_RenderUTF8_Blended(font, ch, fg);
        if (surf)
        {
            SDL_Texture *tex = SDL_CreateTextureFromSurface(r, surf);
            if (tex)
            {
                int tw, th;
                SDL_QueryTexture(tex, NULL, NULL, &tw, &th);
                SDL_Rect dst = {x - tw / 2, start_y + i * char_h, tw, th};
                SDL_RenderCopy(r, tex, NULL, &dst);
                SDL_DestroyTexture(tex);
            }
            SDL_FreeSurface(surf);
        }
    }
}

// Bold text drawing function
static void draw_text_bold(SDL_Renderer *r, TTF_Font *font, const char *text, int x, int y, int rgb)
{
    if (!font || !text)
        return;

    // Set font style to bold
    int old_style = TTF_GetFontStyle(font);
    TTF_SetFontStyle(font, TTF_STYLE_BOLD);

    Uint8 R = (Uint8)((rgb >> 16) & 255);
    Uint8 G = (Uint8)((rgb >> 8) & 255);
    Uint8 B = (Uint8)(rgb & 255);
    SDL_Color fg = {R, G, B, 255};

    SDL_Surface *surf = TTF_RenderUTF8_Blended(font, text, fg);
    if (surf)
    {
        SDL_Texture *tex = SDL_CreateTextureFromSurface(r, surf);
        if (tex)
        {
            int tw, th;
            SDL_QueryTexture(tex, NULL, NULL, &tw, &th);
            SDL_Rect dst = {x, y, tw, th};
            SDL_RenderCopy(r, tex, NULL, &dst);
            SDL_DestroyTexture(tex);
        }
        SDL_FreeSurface(surf);
    }

    // Restore original font style
    TTF_SetFontStyle(font, old_style);
}

// Bold centered text
static void draw_centered_text_bold(SDL_Renderer *r, TTF_Font *font, const char *text, int center_x, int y, int rgb)
{
    if (!font || !text)
        return;

    int old_style = TTF_GetFontStyle(font);
    TTF_SetFontStyle(font, TTF_STYLE_BOLD);

    Uint8 R = (Uint8)((rgb >> 16) & 255);
    Uint8 G = (Uint8)((rgb >> 8) & 255);
    Uint8 B = (Uint8)(rgb & 255);
    SDL_Color fg = {R, G, B, 255};

    SDL_Surface *surf = TTF_RenderUTF8_Blended(font, text, fg);
    if (surf)
    {
        SDL_Texture *tex = SDL_CreateTextureFromSurface(r, surf);
        if (tex)
        {
            int tw, th;
            SDL_QueryTexture(tex, NULL, NULL, &tw, &th);
            SDL_Rect dst = {center_x - tw / 2, y, tw, th};
            SDL_RenderCopy(r, tex, NULL, &dst);
            SDL_DestroyTexture(tex);
        }
        SDL_FreeSurface(surf);
    }

    TTF_SetFontStyle(font, old_style);
}

// Vertical bold text for y-label
// High-quality vertical text using surface rotation
// Simple and effective vertical text rotation
static void draw_verticalTextBold(SDL_Renderer *r, TTF_Font *font, const char *text, int x, int y, int rgb)
{
    if (!font || !text)
        return;

    int old_style = TTF_GetFontStyle(font);
    TTF_SetFontStyle(font, TTF_STYLE_BOLD);

    Uint8 R = (Uint8)((rgb >> 16) & 255);
    Uint8 G = (Uint8)((rgb >> 8) & 255);
    Uint8 B = (Uint8)(rgb & 255);
    SDL_Color fg = {R, G, B, 255};

    // Render text to surface
    SDL_Surface *surf = TTF_RenderUTF8_Blended(font, text, fg);
    if (!surf)
    {
        TTF_SetFontStyle(font, old_style);
        return;
    }

    // Create a surface with swapped dimensions for rotation
    SDL_Surface *rotated = SDL_CreateRGBSurface(0, surf->h, surf->w, 32,
                                                surf->format->Rmask, surf->format->Gmask,
                                                surf->format->Bmask, surf->format->Amask);
    if (!rotated)
    {
        SDL_FreeSurface(surf);
        TTF_SetFontStyle(font, old_style);
        return;
    }

    // Simple pixel rotation (90 degrees counter-clockwise)
    if (SDL_MUSTLOCK(surf))
        SDL_LockSurface(surf);
    if (SDL_MUSTLOCK(rotated))
        SDL_LockSurface(rotated);

    Uint32 *pixels_src = (Uint32 *)surf->pixels;
    Uint32 *pixels_dst = (Uint32 *)rotated->pixels;
    int pitch_src = surf->pitch / 4;
    int pitch_dst = rotated->pitch / 4;

    for (int i = 0; i < surf->w; i++)
    {
        for (int j = 0; j < surf->h; j++)
        {
            // Rotate 90 degrees counter-clockwise
            pixels_dst[i * pitch_dst + (surf->h - 1 - j)] = pixels_src[j * pitch_src + i];
        }
    }

    if (SDL_MUSTLOCK(rotated))
        SDL_UnlockSurface(rotated);
    if (SDL_MUSTLOCK(surf))
        SDL_UnlockSurface(surf);

    // Create texture from rotated surface
    SDL_Texture *texture = SDL_CreateTextureFromSurface(r, rotated);
    if (texture)
    {
        int w, h;
        SDL_QueryTexture(texture, NULL, NULL, &w, &h);
        SDL_Rect dst = {x - w / 2, y - h / 2, w, h};
        SDL_RenderCopy(r, texture, NULL, &dst);
        SDL_DestroyTexture(texture);
    }

    SDL_FreeSurface(rotated);
    SDL_FreeSurface(surf);
    TTF_SetFontStyle(font, old_style);
}

static void format_axis_label(char *buffer, size_t size, double value)
{
    if (fabs(value) < 0.0001 || fabs(value) > 10000)
        snprintf(buffer, size, "%.2e", value);
    else if (fabs(value - round(value)) < 0.0001)
        snprintf(buffer, size, "%.0f", value);
    else
        snprintf(buffer, size, "%.2f", value);
}

static void draw_axis_ticks(SDL_Renderer *r, PiChart *chart, TTF_Font *font, int W, int H, int margin)
{
    if (!font)
        return;

    set_draw_color(r, 0x666666, 255);

    // X-axis ticks
    int num_ticks = 6;
    for (int i = 0; i <= num_ticks; i++)
    {
        double t = (double)i / num_ticks;
        double x_val = chart->xmin + t * (chart->xmax - chart->xmin);
        int px, py0, py1;
        map_xy(chart, x_val, chart->ymin, W, H, margin, &px, &py0);
        map_xy(chart, x_val, chart->ymin - (chart->ymax - chart->ymin) * 0.02, W, H, margin, &px, &py1);
        SDL_RenderDrawLine(r, px, py0, px, py0 + 6);

        char label[32];
        format_axis_label(label, sizeof(label), x_val);
        int tw, th;
        // Estimate text dimensions
        th = 12;
        tw = strlen(label) * 6;
        draw_text(r, font, label, px - tw / 2, py0 + 8, 0x555555);
    }

    // Y-axis ticks
    for (int i = 0; i <= num_ticks; i++)
    {
        double t = (double)i / num_ticks;
        double y_val = chart->ymin + t * (chart->ymax - chart->ymin);
        int px0, py, px1;
        map_xy(chart, chart->xmin, y_val, W, H, margin, &px0, &py);
        map_xy(chart, chart->xmin - (chart->xmax - chart->xmin) * 0.02, y_val, W, H, margin, &px1, &py);
        SDL_RenderDrawLine(r, px0 - 6, py, px0, py);

        char label[32];
        format_axis_label(label, sizeof(label), y_val);
        int tw = strlen(label) * 6;
        draw_text(r, font, label, px0 - tw - 8, py - 6, 0x555555);
    }
}

// Add this implementation before the BuiltinFunc array
Value pt_chart(vm_t *vm, int argc, Value *argv)
{
    // Check if we have a context argument
    if (argc < 1 || !IS_CONTEXT(argv[0]))
        return NIL_VAL;

    // Get the context from the argument
    PiContext *ctx = AS_CONTEXT(argv[0]);

    // Add the chart as an object to the VM and return it
    return NEW_OBJ(add_obj(vm, new_chart(ctx)));
}

// Helper function to draw a filled circle with border
static void draw_filledBorderedCircle(SDL_Renderer *r, int cx, int cy, int radius, int fill_color, int border_color)
{
    // Draw filled circle
    set_draw_color(r, fill_color, 220);
    for (int dy = -radius; dy <= radius; dy++)
    {
        int dx = (int)sqrt(radius * radius - dy * dy);
        SDL_RenderDrawLine(r, cx - dx, cy + dy, cx + dx, cy + dy);
    }

    // Draw black border
    set_draw_color(r, border_color, 255);
    int x = 0, y = radius;
    int d = 3 - 2 * radius;

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
            d = d + 4 * x + 6;
        else
        {
            d = d + 4 * (x - y) + 10;
            y--;
        }
        x++;
    }
}

Value pt_scatter(vm_t *vm, int argc, Value *argv)
{
    if (argc < 3 || !IS_LIST(argv[1]) || !IS_LIST(argv[2]))
        return NIL_VAL;
    PiChart *chart = chart_from(vm, argv[0]);
    if (!chart)
        return NIL_VAL;
    list_t *tail = list_create(VALUE_SIZE);
    list_add(tail, &argv[1]);
    list_add(tail, &argv[2]);
    return make_kindSeries(vm, chart, "scatter", tail);
}

Value pt_line(vm_t *vm, int argc, Value *argv)
{
    if (argc < 3 || !IS_LIST(argv[1]) || !IS_LIST(argv[2]))
        return NIL_VAL;
    PiChart *chart = chart_from(vm, argv[0]);
    if (!chart)
        return NIL_VAL;
    list_t *tail = list_create(VALUE_SIZE);
    list_add(tail, &argv[1]);
    list_add(tail, &argv[2]);
    return make_kindSeries(vm, chart, "line", tail);
}

Value pt_bar(vm_t *vm, int argc, Value *argv)
{
    if (argc < 3 || !IS_LIST(argv[1]) || !IS_LIST(argv[2]))
        return NIL_VAL;
    PiChart *chart = chart_from(vm, argv[0]);
    if (!chart)
        return NIL_VAL;
    list_t *tail = list_create(VALUE_SIZE);
    list_add(tail, &argv[1]);
    list_add(tail, &argv[2]);
    return make_kindSeries(vm, chart, "bar", tail);
}

Value pt_hist(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_LIST(argv[1]))
        return NIL_VAL;
    PiChart *chart = chart_from(vm, argv[0]);
    if (!chart)
        return NIL_VAL;
    list_t *tail = list_create(VALUE_SIZE);
    list_add(tail, &argv[1]);
    if (argc > 2 && IS_NUM(argv[2]))
    {
        Value vb = argv[2];
        list_add(tail, &vb);
    }
    return make_kindSeries(vm, chart, "hist", tail);
}

Value pt_step(vm_t *vm, int argc, Value *argv)
{
    if (argc < 3 || !IS_LIST(argv[1]) || !IS_LIST(argv[2]))
        return NIL_VAL;
    PiChart *chart = chart_from(vm, argv[0]);
    if (!chart)
        return NIL_VAL;
    list_t *tail = list_create(VALUE_SIZE);
    list_add(tail, &argv[1]);
    list_add(tail, &argv[2]);
    return make_kindSeries(vm, chart, "step", tail);
}

// Matplotlib-like colormap functions
static void heatmap_rgb_magma(double t, Uint8 *R, Uint8 *G, Uint8 *B)
{
    // Magma colormap - similar to matplotlib's magma
    t = fmax(0.0, fmin(1.0, t));

    // Color stops for magma
    double r_stops[] = {0.001462, 0.258228, 0.554815, 0.852315, 0.987154};
    double g_stops[] = {0.000466, 0.118400, 0.330580, 0.651633, 0.907940};
    double b_stops[] = {0.013866, 0.290110, 0.480826, 0.634506, 0.876042};

    // Find the interval
    double scaled_t = t * 4.0;
    int idx = (int)scaled_t;
    if (idx >= 4)
        idx = 3;
    double frac = scaled_t - idx;

    // Interpolate
    *R = (Uint8)((r_stops[idx] * (1.0 - frac) + r_stops[idx + 1] * frac) * 255);
    *G = (Uint8)((g_stops[idx] * (1.0 - frac) + g_stops[idx + 1] * frac) * 255);
    *B = (Uint8)((b_stops[idx] * (1.0 - frac) + b_stops[idx + 1] * frac) * 255);
}

static void heatmap_rgb_plasma(double t, Uint8 *R, Uint8 *G, Uint8 *B)
{
    // Plasma colormap - similar to matplotlib's plasma
    t = fmax(0.0, fmin(1.0, t));

    // Color stops for plasma
    double r_stops[] = {0.050383, 0.385441, 0.782950, 0.959703, 0.940015};
    double g_stops[] = {0.029803, 0.176449, 0.413126, 0.663855, 0.889322};
    double b_stops[] = {0.527975, 0.708415, 0.873010, 0.946857, 0.954525};

    // Find the interval
    double scaled_t = t * 4.0;
    int idx = (int)scaled_t;
    if (idx >= 4)
        idx = 3;
    double frac = scaled_t - idx;

    // Interpolate
    *R = (Uint8)((r_stops[idx] * (1.0 - frac) + r_stops[idx + 1] * frac) * 255);
    *G = (Uint8)((g_stops[idx] * (1.0 - frac) + g_stops[idx + 1] * frac) * 255);
    *B = (Uint8)((b_stops[idx] * (1.0 - frac) + b_stops[idx + 1] * frac) * 255);
}

static void heatmap_rgb_inferno(double t, Uint8 *R, Uint8 *G, Uint8 *B)
{
    // Inferno colormap - similar to matplotlib's inferno
    t = fmax(0.0, fmin(1.0, t));

    // Color stops for inferno
    double r_stops[] = {0.001462, 0.285341, 0.680024, 0.942433, 0.988362};
    double g_stops[] = {0.000466, 0.067732, 0.253384, 0.571154, 0.878120};
    double b_stops[] = {0.013866, 0.157302, 0.255235, 0.405365, 0.754281};

    // Find the interval
    double scaled_t = t * 4.0;
    int idx = (int)scaled_t;
    if (idx >= 4)
        idx = 3;
    double frac = scaled_t - idx;

    // Interpolate
    *R = (Uint8)((r_stops[idx] * (1.0 - frac) + r_stops[idx + 1] * frac) * 255);
    *G = (Uint8)((g_stops[idx] * (1.0 - frac) + g_stops[idx + 1] * frac) * 255);
    *B = (Uint8)((b_stops[idx] * (1.0 - frac) + b_stops[idx + 1] * frac) * 255);
}

static void heatmap_rgb_viridis(double t, Uint8 *R, Uint8 *G, Uint8 *B)
{
    // Viridis colormap - matplotlib's default
    t = fmax(0.0, fmin(1.0, t));

    // Color stops for viridis
    double r_stops[] = {0.267004, 0.229739, 0.312916, 0.575593, 0.993248};
    double g_stops[] = {0.004874, 0.322361, 0.632786, 0.815276, 0.906157};
    double b_stops[] = {0.329415, 0.547495, 0.658336, 0.619543, 0.143936};

    // Find the interval
    double scaled_t = t * 4.0;
    int idx = (int)scaled_t;
    if (idx >= 4)
        idx = 3;
    double frac = scaled_t - idx;

    // Interpolate
    *R = (Uint8)((r_stops[idx] * (1.0 - frac) + r_stops[idx + 1] * frac) * 255);
    *G = (Uint8)((g_stops[idx] * (1.0 - frac) + g_stops[idx + 1] * frac) * 255);
    *B = (Uint8)((b_stops[idx] * (1.0 - frac) + b_stops[idx + 1] * frac) * 255);
}

// Choose colormap (default to viridis)
static int current_colormap = 0; // 0=viridis, 1=plasma, 2=inferno, 3=magma
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

// Draw a vertical color bar for heatmap reference
static void draw_colorBar(SDL_Renderer *r, PiChart *chart, double zmin, double zmax,
                          int W, int H, int margin, int x_pos, int y_top, int y_bottom, int width)
{
    if (zmax <= zmin)
        zmax = zmin + 1e-9;

    int height = y_bottom - y_top;
    if (height <= 0)
        return;

    // Draw the color gradient
    for (int y = 0; y < height; y++)
    {
        double t = 1.0 - (double)y / height; // Reverse so high values are at top
        if (t < 0)
            t = 0;
        if (t > 1)
            t = 1;

        Uint8 R, G, B;
        heatmap_rgb(t, &R, &G, &B);
        SDL_SetRenderDrawColor(r, R, G, B, 255);
        SDL_RenderDrawLine(r, x_pos, y_top + y, x_pos + width, y_top + y);
    }

    // Draw border around color bar
    set_draw_color(r, 0x333333, 255);
    SDL_Rect border = {x_pos, y_top, width, height};
    SDL_RenderDrawRect(r, &border);

    // Draw tick marks and labels on the color bar
    TTF_Font *font = try_open_font(10);
    if (font)
    {
        int num_ticks = 5;
        for (int i = 0; i <= num_ticks; i++)
        {
            double t = (double)i / num_ticks;
            double value = zmin + t * (zmax - zmin);
            int y_pos = y_top + (int)((1.0 - t) * height);

            // Draw tick mark
            SDL_RenderDrawLine(r, x_pos + width, y_pos, x_pos + width + 5, y_pos);

            // Format the value label
            char label[32];
            if (fabs(value) < 0.001 || fabs(value) > 1000)
                snprintf(label, sizeof(label), "%.2e", value);
            else if (fabs(value - round(value)) < 0.0001)
                snprintf(label, sizeof(label), "%.0f", value);
            else
                snprintf(label, sizeof(label), "%.2f", value);

            // Draw the label
            int tw = strlen(label) * 5;
            draw_text(r, font, label, x_pos + width + 8, y_pos - 5, 0x444444);
        }

        // Add min/max labels
        char min_label[32], max_label[32];
        if (fabs(zmin) < 0.001 || fabs(zmin) > 1000)
            snprintf(min_label, sizeof(min_label), "%.2e", zmin);
        else if (fabs(zmin - round(zmin)) < 0.0001)
            snprintf(min_label, sizeof(min_label), "%.0f", zmin);
        else
            snprintf(min_label, sizeof(min_label), "%.2f", zmin);

        if (fabs(zmax) < 0.001 || fabs(zmax) > 1000)
            snprintf(max_label, sizeof(max_label), "%.2e", zmax);
        else if (fabs(zmax - round(zmax)) < 0.0001)
            snprintf(max_label, sizeof(max_label), "%.0f", zmax);
        else
            snprintf(max_label, sizeof(max_label), "%.2f", zmax);

        // Draw color bar title
        draw_text(r, font, "Value", x_pos + width / 2 - 15, y_top - 20, 0x444444);
    }
}

// Alternative horizontal color bar (if preferred)
static void draw_colorBarHorizontal(SDL_Renderer *r, PiChart *chart, double zmin, double zmax,
                                    int W, int H, int margin, int x_left, int x_right, int y_pos, int height)
{
    if (zmax <= zmin)
        zmax = zmin + 1e-9;

    int width = x_right - x_left;
    if (width <= 0)
        return;

    // Draw the color gradient
    for (int x = 0; x < width; x++)
    {
        double t = (double)x / width;
        if (t < 0)
            t = 0;
        if (t > 1)
            t = 1;

        Uint8 R, G, B;
        heatmap_rgb(t, &R, &G, &B);
        SDL_SetRenderDrawColor(r, R, G, B, 255);
        SDL_RenderDrawLine(r, x_left + x, y_pos, x_left + x, y_pos + height);
    }

    // Draw border
    set_draw_color(r, 0x333333, 255);
    SDL_Rect border = {x_left, y_pos, width, height};
    SDL_RenderDrawRect(r, &border);

    // Draw ticks and labels
    TTF_Font *font = try_open_font(10);
    if (font)
    {
        int num_ticks = 5;
        for (int i = 0; i <= num_ticks; i++)
        {
            double t = (double)i / num_ticks;
            double value = zmin + t * (zmax - zmin);
            int x_pos_tick = x_left + (int)(t * width);

            // Draw tick mark
            SDL_RenderDrawLine(r, x_pos_tick, y_pos + height, x_pos_tick, y_pos + height + 5);

            // Format label
            char label[32];
            if (fabs(value) < 0.001 || fabs(value) > 1000)
                snprintf(label, sizeof(label), "%.2e", value);
            else if (fabs(value - round(value)) < 0.0001)
                snprintf(label, sizeof(label), "%.0f", value);
            else
                snprintf(label, sizeof(label), "%.2f", value);

            // Draw label
            int tw = strlen(label) * 5;
            draw_text(r, font, label, x_pos_tick - tw / 2, y_pos + height + 8, 0x444444);
        }
    }
}

Value pt_heatmap(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_MATRIX(argv[1]))
        vm_error(vm, "heatmap requires a matrix");
    PiChart *chart = chart_from(vm, argv[0]);
    if (!chart)
        vm_error(vm, "contour requires a chart");
    list_t *tail = list_create(VALUE_SIZE);
    list_add(tail, &argv[1]);
    return make_kindSeries(vm, chart, "heatmap", tail);
}

Value pt_contour(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_MATRIX(argv[1]))
        vm_error(vm, "contour requires a matrix");
    PiChart *chart = chart_from(vm, argv[0]);
    if (!chart)
        vm_error(vm, "contour requires a chart");
    list_t *tail = list_create(VALUE_SIZE);
    list_add(tail, &argv[1]);
    if (argc > 2 && IS_NUM(argv[2]))
        list_add(tail, &argv[2]);
    return make_kindSeries(vm, chart, "contour", tail);
}

Value pt_quiver(vm_t *vm, int argc, Value *argv)
{
    if (argc < 3 || !IS_MATRIX(argv[1]) || !IS_MATRIX(argv[2]))
        return NIL_VAL;
    PiMatrix *U = AS_MATRIX(argv[1]);
    PiMatrix *V = AS_MATRIX(argv[2]);
    if (U->rows != V->rows || U->cols != V->cols)
        return NIL_VAL;
    PiChart *chart = chart_from(vm, argv[0]);
    if (!chart)
        return NIL_VAL;
    list_t *tail = list_create(VALUE_SIZE);
    list_add(tail, &argv[1]);
    list_add(tail, &argv[2]);
    return make_kindSeries(vm, chart, "quiver", tail);
}

Value pt_streamplot(vm_t *vm, int argc, Value *argv)
{
    if (argc < 3 || !IS_MATRIX(argv[1]) || !IS_MATRIX(argv[2]))
        return NIL_VAL;
    PiMatrix *U = AS_MATRIX(argv[1]);
    PiMatrix *V = AS_MATRIX(argv[2]);
    if (U->rows != V->rows || U->cols != V->cols)
        return NIL_VAL;
    PiChart *chart = chart_from(vm, argv[0]);
    if (!chart)
        return NIL_VAL;
    list_t *tail = list_create(VALUE_SIZE);
    list_add(tail, &argv[1]);
    list_add(tail, &argv[2]);
    return make_kindSeries(vm, chart, "streamplot", tail);
}

Value pt_surface(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_MATRIX(argv[1]))
        return NIL_VAL;
    PiChart *chart = chart_from(vm, argv[0]);
    if (!chart)
        return NIL_VAL;
    list_t *tail = list_create(VALUE_SIZE);
    list_add(tail, &argv[1]);
    return make_kindSeries(vm, chart, "surface", tail);
}

Value pt_mesh(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_MATRIX(argv[1]))
        return NIL_VAL;
    PiChart *chart = chart_from(vm, argv[0]);
    if (!chart)
        return NIL_VAL;
    list_t *tail = list_create(VALUE_SIZE);
    list_add(tail, &argv[1]);
    return make_kindSeries(vm, chart, "mesh", tail);
}

Value pt_wireframe(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_MATRIX(argv[1]))
        return NIL_VAL;
    PiChart *chart = chart_from(vm, argv[0]);
    if (!chart)
        return NIL_VAL;
    list_t *tail = list_create(VALUE_SIZE);
    list_add(tail, &argv[1]);
    return make_kindSeries(vm, chart, "wireframe", tail);
}

Value pt_title(vm_t *vm, int argc, Value *argv)
{
    (void)vm;
    if (argc < 2 || !IS_CHART(argv[0]) || !IS_STRING(argv[1]))
        return NIL_VAL;
    PiChart *chart = AS_CHART(argv[0]);
    if (chart->title)
        free(chart->title);
    chart->title = strdup(AS_CSTRING(argv[1]));
    return NEW_OBJ((Object *)chart);
}

Value pt_xlabel(vm_t *vm, int argc, Value *argv)
{
    (void)vm;
    if (argc < 2 || !IS_CHART(argv[0]) || !IS_STRING(argv[1]))
        return NIL_VAL;
    PiChart *chart = AS_CHART(argv[0]);
    if (chart->xlabel)
        free(chart->xlabel);
    chart->xlabel = strdup(AS_CSTRING(argv[1]));
    return NEW_OBJ((Object *)chart);
}

Value pt_ylabel(vm_t *vm, int argc, Value *argv)
{
    (void)vm;
    if (argc < 2 || !IS_CHART(argv[0]) || !IS_STRING(argv[1]))
        return NIL_VAL;
    PiChart *chart = AS_CHART(argv[0]);
    if (chart->ylabel)
        free(chart->ylabel);
    chart->ylabel = strdup(AS_CSTRING(argv[1]));
    return NEW_OBJ((Object *)chart);
}

Value pt_grid(vm_t *vm, int argc, Value *argv)
{
    (void)vm;
    if (argc < 2 || !IS_CHART(argv[0]))
        return NIL_VAL;
    PiChart *chart = AS_CHART(argv[0]);
    chart->show_grid = IS_BOOL(argv[1]) ? AS_BOOL(argv[1]) : (IS_NUM(argv[1]) && AS_NUM(argv[1]) != 0.0);
    return NEW_OBJ((Object *)chart);
}

Value pt_axes(vm_t *vm, int argc, Value *argv)
{
    (void)vm;
    if (argc < 2 || !IS_CHART(argv[0]))
        return NIL_VAL;
    PiChart *chart = AS_CHART(argv[0]);
    chart->show_axes = IS_BOOL(argv[1]) ? AS_BOOL(argv[1]) : (IS_NUM(argv[1]) && AS_NUM(argv[1]) != 0.0);
    return NEW_OBJ((Object *)chart);
}

Value pt_legend(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_CHART(argv[0]) || !IS_LIST(argv[1]))
        return NIL_VAL;

    PiChart *chart = AS_CHART(argv[0]);
    list_t *tail = list_create(VALUE_SIZE);

    // Add the legend labels list
    list_add(tail, &argv[1]);

    // Optional: legend position x and y
    double legend_x = -1.0; // -1 means auto-position (top-right)
    double legend_y = -1.0;

    if (argc >= 3 && IS_NUM(argv[2]))
        legend_x = AS_NUM(argv[2]);

    if (argc >= 4 && IS_NUM(argv[3]))
        legend_y = AS_NUM(argv[3]);

    // Create a list to hold legend position
    list_t *pos_list = list_create(VALUE_SIZE);
    Value vx = NEW_NUM(legend_x);
    Value vy = NEW_NUM(legend_y);
    list_add(pos_list, &vx);
    list_add(pos_list, &vy);

    Object *pos_obj = new_list(pos_list);
    add_obj(vm, pos_obj);
    Value pos_val = NEW_OBJ(pos_obj);

    // Add position as an extra parameter
    list_add(tail, &pos_val);

    return make_kindSeries(vm, chart, "legend", tail);
}

Value pt_show(vm_t *vm, int argc, Value *argv)
{
    (void)vm;
    if (argc < 1 || !IS_CHART(argv[0]))
        return NIL_VAL;
    PiChart *chart = AS_CHART(argv[0]);
    PiContext *ctx = chart->ctx;
    SDL_Renderer *r = renderer_of(ctx);
    if (!ctx || !r || ctx->width <= 0 || ctx->height <= 0)
        return NIL_VAL;

    int W = ctx->width;
    int H = ctx->height;
    const int margin = 80; // Increased margin for better label spacing

    chart_compute_bounds(chart);

    // Clear with white background
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    SDL_RenderClear(r);

    // Use different fonts for different purposes
    TTF_Font *title_font = try_open_font(18);  // Larger for title
    TTF_Font *label_font = try_open_font(14);  // Bold for axis labels
    TTF_Font *tick_font = try_open_font(11);   // Smaller for ticks
    TTF_Font *legend_font = try_open_font(12); // For legend text

    // Set label font to bold
    if (label_font)
    {
        TTF_SetFontStyle(label_font, TTF_STYLE_BOLD);
    }

    // Draw title (centered, bold)
    if (chart->title && title_font)
        draw_centered_text_bold(r, title_font, chart->title, W / 2, 15, 0x222222);

    // Draw x-label (centered below plot with proper gap)
    if (chart->xlabel && label_font)
    {
        int x_label_y = H - margin + 35; // Gap below the plot
        draw_centered_text_bold(r, label_font, chart->xlabel, W / 2, x_label_y, 0x333333);
    }

    // Draw y-label (vertical, with gap from y-axis)
    if (chart->ylabel && label_font)
    {
        // Position y-label with gap from the left edge
        int y_label_x = 25;    // Gap from left edge
        int y_label_y = H / 2; // Center vertically
        draw_verticalTextBold(r, label_font, chart->ylabel, y_label_x, y_label_y, 0x333333);
    }

    /* FIRST, check if there's any heatmap or contour in the series */
    int has_heatmap = 0;
    PiMatrix *heatmap_matrix = NULL;
    int heatmap_rows = 0, heatmap_cols = 0;

    for (int si = 0; si < list_size(chart->series); si++)
    {
        Value *series_val = (Value *)list_getAt(chart->series, si);
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
        if (!strcmp(kind, "heatmap") || !strcmp(kind, "contour"))
        {
            has_heatmap = 1;
            if (LIST_SIZE(items) >= 2)
            {
                Value *mv = (Value *)list_getAt(items, 1);
                if (IS_MATRIX(*mv))
                {
                    heatmap_matrix = AS_MATRIX(*mv);
                    heatmap_rows = heatmap_matrix->rows;
                    heatmap_cols = heatmap_matrix->cols;
                }
            }
            break;
        }
    }

    /* Calculate border positions - adjusted for heatmap if needed */
    int border_left, border_top, border_right, border_bottom;
    int heatmap_start_x = 0, heatmap_start_y = 0;
    int heatmap_width = 0, heatmap_height = 0;
    double cell_size = 0;

    if (has_heatmap && heatmap_matrix)
    {
        // For heatmap, calculate borders based on the heatmap dimensions
        int plot_width = W - 2 * margin;
        int plot_height = H - 2 * margin;

        // Calculate cell size to maintain square aspect ratio
        double cell_width = (double)plot_width / heatmap_cols;
        double cell_height = (double)plot_height / heatmap_rows;
        cell_size = fmin(cell_width, cell_height);

        // Calculate total heatmap dimensions
        heatmap_width = (int)(cell_size * heatmap_cols);
        heatmap_height = (int)(cell_size * heatmap_rows);

        // Calculate starting position to center the heatmap
        heatmap_start_x = margin + (plot_width - heatmap_width) / 2;
        heatmap_start_y = margin + (plot_height - heatmap_height) / 2;

        // Set borders to exactly match the heatmap edges
        border_left = heatmap_start_x;
        border_top = heatmap_start_y;
        border_right = heatmap_start_x + heatmap_width;
        border_bottom = heatmap_start_y + heatmap_height;
    }
    else
    {
        // For regular plots, use the standard bounds
        map_xy(chart, chart->xmin, chart->ymax, W, H, margin, &border_left, &border_top);
        map_xy(chart, chart->xmax, chart->ymin, W, H, margin, &border_right, &border_bottom);
    }

    /* Draw full border box that fits the content */
    set_draw_color(r, 0x333333, 255);
    SDL_Rect border_rect = {border_left, border_top, border_right - border_left, border_bottom - border_top};
    SDL_RenderDrawRect(r, &border_rect);

    /* Draw axes that fit the border */
    if (chart->show_axes)
    {
        set_draw_color(r, 0x333333, 255);
        // X-axis at the bottom of the border
        SDL_RenderDrawLine(r, border_left, border_bottom, border_right, border_bottom);
        // Y-axis at the left of the border
        SDL_RenderDrawLine(r, border_left, border_top, border_left, border_bottom);
    }

    /* Draw axis ticks and labels - adjusted for heatmap */
    if (tick_font)
    {
        if (has_heatmap && heatmap_matrix)
        {
            // Draw custom ticks for heatmap based on matrix dimensions
            // X-axis ticks at column centers
            for (int col = 0; col < heatmap_cols; col++)
            {
                int x = heatmap_start_x + (int)((col + 0.5) * cell_size);
                int y = border_bottom;

                // Draw tick mark
                SDL_RenderDrawLine(r, x, y, x, y + 5);

                // Draw column label (every few columns to avoid clutter)
                if (heatmap_cols <= 20 || col % (heatmap_cols / 10 + 1) == 0)
                {
                    char label[32];
                    snprintf(label, sizeof(label), "%d", col);
                    int tw = strlen(label) * 5;
                    draw_text(r, tick_font, label, x - tw / 2, y + 8, 0x555555);
                }
            }

            // Y-axis ticks at row centers
            for (int row = 0; row < heatmap_rows; row++)
            {
                int x = border_left;
                int y = heatmap_start_y + (int)((row + 0.5) * cell_size);

                // Draw tick mark
                SDL_RenderDrawLine(r, x - 5, y, x, y);

                // Draw row label (every few rows to avoid clutter)
                if (heatmap_rows <= 20 || row % (heatmap_rows / 10 + 1) == 0)
                {
                    char label[32];
                    snprintf(label, sizeof(label), "%d", row);
                    int tw = strlen(label) * 5;
                    draw_text(r, tick_font, label, x - tw - 8, y - 6, 0x555555);
                }
            }
        }
        else
        {
            // Use standard axis ticks for regular plots
            draw_axis_ticks(r, chart, tick_font, W, H, margin);
        }
    }

    /* Draw default grid ONLY if there's no heatmap/contour */
    if (chart->show_grid && !has_heatmap)
    {
        set_draw_color(r, 0xe0e0e0, 200);
        for (int g = 1; g < 10; g++)
        {
            double tx = chart->xmin + (chart->xmax - chart->xmin) * g / 10.0;
            double ty = chart->ymin + (chart->ymax - chart->ymin) * g / 10.0;
            int px0, py0, px1, py1;
            map_xy(chart, tx, chart->ymin, W, H, margin, &px0, &py0);
            map_xy(chart, tx, chart->ymax, W, H, margin, &px1, &py1);
            SDL_RenderDrawLine(r, px0, py0, px1, py1);
            map_xy(chart, chart->xmin, ty, W, H, margin, &px0, &py0);
            map_xy(chart, chart->xmax, ty, W, H, margin, &px1, &py1);
            SDL_RenderDrawLine(r, px0, py0, px1, py1);
        }
    }

    int series_index = 0;
    int ns = list_size(chart->series);

    for (int si = 0; si < ns; si++)
    {
        Value *series_val = (Value *)list_getAt(chart->series, si);
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
        int col = palette_color(series_index);

        if (!strcmp(kind, "legend"))
        {
            if (LIST_SIZE(items) < 2)
                continue;

            PiList *leg = AS_LIST(*(Value *)list_getAt(items, 1));
            int ln = LIST_SIZE(leg->items);

            // Get legend position (optional)
            double legend_x = -1.0;
            double legend_y = -1.0;

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

            // Calculate legend dimensions
            int legend_width = 120;
            int legend_height = ln * 22 + 10;
            int legend_x_pos, legend_y_pos;

            // Auto-position or use specified coordinates
            if (legend_x >= 0 && legend_y >= 0)
            {
                // Convert data coordinates to screen coordinates
                int px_tmp, py_tmp;
                if (has_heatmap && heatmap_matrix)
                {
                    // For heatmap, map from matrix indices
                    legend_x_pos = border_left + (int)(legend_x * cell_size);
                    legend_y_pos = border_top + (int)(legend_y * cell_size);
                }
                else
                {
                    map_xy(chart, legend_x, legend_y, W, H, margin, &px_tmp, &py_tmp);
                    legend_x_pos = px_tmp;
                    legend_y_pos = py_tmp - legend_height / 2;
                }
            }
            else if (legend_x >= 0 && legend_y < 0)
            {
                // Only X specified, auto Y
                if (has_heatmap && heatmap_matrix)
                {
                    legend_x_pos = border_left + (int)(legend_x * cell_size);
                    legend_y_pos = border_top + 10;
                }
                else
                {
                    int px_tmp, py_tmp;
                    map_xy(chart, legend_x, chart->ymax, W, H, margin, &px_tmp, &py_tmp);
                    legend_x_pos = px_tmp;
                    legend_y_pos = py_tmp + 10;
                }
            }
            else if (legend_x < 0 && legend_y >= 0)
            {
                // Only Y specified, auto X
                if (has_heatmap && heatmap_matrix)
                {
                    legend_x_pos = border_right - legend_width - 10;
                    legend_y_pos = border_top + (int)(legend_y * cell_size);
                }
                else
                {
                    int px_tmp, py_tmp;
                    map_xy(chart, chart->xmax, legend_y, W, H, margin, &px_tmp, &py_tmp);
                    legend_x_pos = px_tmp - legend_width - 10;
                    legend_y_pos = py_tmp - legend_height / 2;
                }
            }
            else
            {
                // Default: top-right corner inside the plot area
                legend_x_pos = border_right - legend_width - 10;
                legend_y_pos = border_top + 10;
            }

            // Ensure legend stays within bounds
            if (legend_x_pos < border_left + 10)
                legend_x_pos = border_left + 10;
            if (legend_x_pos + legend_width > border_right - 10)
                legend_x_pos = border_right - legend_width - 10;
            if (legend_y_pos < border_top + 10)
                legend_y_pos = border_top + 10;
            if (legend_y_pos + legend_height > border_bottom - 10)
                legend_y_pos = border_bottom - legend_height - 10;

            // Draw legend background with semi-transparency
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            set_draw_color(r, 0xffffff, 220);
            SDL_Rect legend_bg = {legend_x_pos - 5, legend_y_pos - 5, legend_width + 10, legend_height + 10};
            SDL_RenderFillRect(r, &legend_bg);

            // Draw legend border
            set_draw_color(r, 0xcccccc, 255);
            SDL_RenderDrawRect(r, &legend_bg);
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

            // Draw legend items
            for (int li = 0; li < ln; li++)
            {
                Value *lv = (Value *)list_getAt(leg->items, li);
                if (!IS_STRING(*lv))
                    continue;

                int c = palette_color(li);

                // Draw colored square
                set_draw_color(r, c, 255);
                SDL_Rect sq = {legend_x_pos, legend_y_pos + li * 22, 12, 12};
                SDL_RenderFillRect(r, &sq);
                set_draw_color(r, 0x888888, 255);
                SDL_RenderDrawRect(r, &sq);

                // Draw text
                if (legend_font)
                    draw_text(r, legend_font, AS_CSTRING(*lv), legend_x_pos + 18, legend_y_pos + li * 22 - 2, 0x333333);
            }
            continue;
        }

        else if (!strcmp(kind, "scatter"))
        {
            if (LIST_SIZE(items) < 3)
                continue;
            PiList *xl = AS_LIST(*(Value *)list_getAt(items, 1));
            PiList *yl = AS_LIST(*(Value *)list_getAt(items, 2));
            int n = LIST_SIZE(xl->items);

            for (int i = 0; i < n; i++)
            {
                Value *vx = (Value *)list_getAt(xl->items, i);
                Value *vy = (Value *)list_getAt(yl->items, i);
                if (!IS_NUM(*vx) || !IS_NUM(*vy))
                    continue;
                int px, py;
                map_xy(chart, AS_NUM(*vx), AS_NUM(*vy), W, H, margin, &px, &py);

                // Draw filled circle with black border
                draw_filledBorderedCircle(r, px, py, 5, col, 0x000000);
            }
            series_index++;
        }

        else if (!strcmp(kind, "line"))
        {
            if (LIST_SIZE(items) < 3)
                continue;
            PiList *xl = AS_LIST(*(Value *)list_getAt(items, 1));
            PiList *yl = AS_LIST(*(Value *)list_getAt(items, 2));
            int n = LIST_SIZE(xl->items);
            set_draw_color(r, col, 255);
            for (int i = 0; i < n - 1; i++)
            {
                Value *vx0 = (Value *)list_getAt(xl->items, i);
                Value *vy0 = (Value *)list_getAt(yl->items, i);
                Value *vx1 = (Value *)list_getAt(xl->items, i + 1);
                Value *vy1 = (Value *)list_getAt(yl->items, i + 1);
                if (!IS_NUM(*vx0) || !IS_NUM(*vy0) || !IS_NUM(*vx1) || !IS_NUM(*vy1))
                    continue;
                int p0x, p0y, p1x, p1y;
                map_xy(chart, AS_NUM(*vx0), AS_NUM(*vy0), W, H, margin, &p0x, &p0y);
                map_xy(chart, AS_NUM(*vx1), AS_NUM(*vy1), W, H, margin, &p1x, &p1y);
                SDL_RenderDrawLine(r, p0x, p0y, p1x, p1y);
            }
            series_index++;
        }
        else if (!strcmp(kind, "step"))
        {
            if (LIST_SIZE(items) < 3)
                continue;
            PiList *xl = AS_LIST(*(Value *)list_getAt(items, 1));
            PiList *yl = AS_LIST(*(Value *)list_getAt(items, 2));
            int n = LIST_SIZE(xl->items);
            set_draw_color(r, col, 255);
            for (int i = 0; i < n - 1; i++)
            {
                Value *vx0 = (Value *)list_getAt(xl->items, i);
                Value *vy0 = (Value *)list_getAt(yl->items, i);
                Value *vx1 = (Value *)list_getAt(xl->items, i + 1);
                Value *vy1 = (Value *)list_getAt(yl->items, i + 1);
                if (!IS_NUM(*vx0) || !IS_NUM(*vy0) || !IS_NUM(*vx1) || !IS_NUM(*vy1))
                    continue;
                int p0x, p0y, p1x, p1y, p2x, p2y;
                map_xy(chart, AS_NUM(*vx0), AS_NUM(*vy0), W, H, margin, &p0x, &p0y);
                map_xy(chart, AS_NUM(*vx1), AS_NUM(*vy0), W, H, margin, &p1x, &p1y);
                map_xy(chart, AS_NUM(*vx1), AS_NUM(*vy1), W, H, margin, &p2x, &p2y);
                SDL_RenderDrawLine(r, p0x, p0y, p1x, p1y);
                SDL_RenderDrawLine(r, p1x, p1y, p2x, p2y);
            }
            series_index++;
        }
        else if (!strcmp(kind, "bar"))
        {
            if (LIST_SIZE(items) < 3)
                continue;
            PiList *xl = AS_LIST(*(Value *)list_getAt(items, 1));
            PiList *yl = AS_LIST(*(Value *)list_getAt(items, 2));
            int n = LIST_SIZE(yl->items);

            // Draw bars
            for (int i = 0; i < n; i++)
            {
                Value *vy = (Value *)list_getAt(yl->items, i);
                if (!IS_NUM(*vy))
                    continue;
                double yi = AS_NUM(*vy);
                double xleft = (double)i - 0.4;
                double xright = (double)i + 0.4;
                int ax0, ay0, ax1, ay1, bx0, ay1b;
                map_xy(chart, xleft, 0, W, H, margin, &ax0, &ay0);
                map_xy(chart, xright, 0, W, H, margin, &ax1, &ay1);
                map_xy(chart, xleft, yi, W, H, margin, &bx0, &ay1b);
                (void)xl;
                SDL_Rect rect = {ax0, ay1b, ax1 - ax0, ay0 - ay1b};
                if (rect.w < 1)
                    rect.w = 1;
                if (rect.h < 1)
                    rect.h = 1;

                // Fill with series color
                set_draw_color(r, col, 220);
                SDL_RenderFillRect(r, &rect);

                // Draw black border
                set_draw_color(r, 0x000000, 255); // Black color
                SDL_RenderDrawRect(r, &rect);
            }
            series_index++;
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

            // Draw histogram bars with black borders
            for (int b = 0; b < bins; b++)
            {
                double x0 = lo + (hi - lo) * b / bins;
                double x1 = lo + (hi - lo) * (b + 1) / bins;
                double h = counts[b];
                int ax0, ay0, ax1, ay1, bx0, by1;
                map_xy(chart, x0, 0, W, H, margin, &ax0, &ay0);
                map_xy(chart, x1, 0, W, H, margin, &ax1, &ay0);
                map_xy(chart, x0, h, W, H, margin, &bx0, &by1);
                SDL_Rect rect = {ax0, by1, ax1 - ax0, ay0 - by1};
                if (rect.w < 1)
                    rect.w = 1;
                if (rect.h < 1)
                    rect.h = 1;

                // Fill with series color
                set_draw_color(r, col, 220);
                SDL_RenderFillRect(r, &rect);

                // Draw black border
                set_draw_color(r, 0x000000, 255); // Black color
                SDL_RenderDrawRect(r, &rect);
            }
            free(counts);
            series_index++;
        }

        else if (!strcmp(kind, "heatmap") || !strcmp(kind, "contour"))
        {
            if (LIST_SIZE(items) < 2)
                continue;
            Value *mv = (Value *)list_getAt(items, 1);
            if (!IS_MATRIX(*mv))
                continue;
            PiMatrix *M = AS_MATRIX(*mv);
            double zmin = INFINITY, zmax = -INFINITY;
            for (int row = 0; row < M->rows; row++)
                for (int col = 0; col < M->cols; col++)
                {
                    double z = matrix_get(M, row, col);
                    if (z < zmin)
                        zmin = z;
                    if (z > zmax)
                        zmax = z;
                }
            if (zmax <= zmin)
                zmax = zmin + 1e-9;

            // Use the pre-calculated heatmap dimensions from earlier
            int start_x = heatmap_start_x;
            int start_y = heatmap_start_y;
            int total_width = heatmap_width;
            int total_height = heatmap_height;

            // Draw the heatmap with square cells (no borders)
            for (int row = 0; row < M->rows; row++)
            {
                for (int col = 0; col < M->cols; col++)
                {
                    double z = matrix_get(M, row, col);
                    double t = (z - zmin) / (zmax - zmin);
                    if (t < 0)
                        t = 0;
                    if (t > 1)
                        t = 1;

                    Uint8 Rcol, Gcol, Bcol;
                    heatmap_rgb(t, &Rcol, &Gcol, &Bcol);
                    SDL_SetRenderDrawColor(r, Rcol, Gcol, Bcol, 255);

                    // Draw square cell without border
                    SDL_Rect rect = {
                        start_x + (int)(col * cell_size),
                        start_y + (int)(row * cell_size),
                        (int)ceil(cell_size), // Use ceil to avoid gaps
                        (int)ceil(cell_size)};

                    SDL_RenderFillRect(r, &rect);
                }
            }

            // Draw grid lines that fit exactly to the heatmap
            if (chart->show_grid && cell_size > 10) // Only show grid if cells are reasonably sized
            {
                set_draw_color(r, 0xffffff, 180); // White grid lines with transparency

                // Draw vertical grid lines at column boundaries
                for (int col = 1; col < M->cols; col++)
                {
                    int x = start_x + (int)(col * cell_size);
                    SDL_RenderDrawLine(r, x, start_y, x, start_y + total_height);
                }

                // Draw horizontal grid lines at row boundaries
                for (int row = 1; row < M->rows; row++)
                {
                    int y = start_y + (int)(row * cell_size);
                    SDL_RenderDrawLine(r, start_x, y, start_x + total_width, y);
                }
            }

            // Draw color bar
            int color_bar_x = border_right + 15;
            int color_bar_width = 25;
            int color_bar_top = border_top;
            int color_bar_bottom = border_bottom;

            draw_colorBar(r, chart, zmin, zmax, W, H, margin,
                          color_bar_x, color_bar_top, color_bar_bottom, color_bar_width);

            // Add color bar label
            TTF_Font *small_font = try_open_font(10);
            if (small_font)
            {
                draw_verticalTextBold(r, small_font, "Value",
                                      color_bar_x + color_bar_width / 2,
                                      (color_bar_top + color_bar_bottom) / 2, 0x444444);
                TTF_CloseFont(small_font);
            }

            // Draw contour lines if needed (now properly aligned with squared cells)
            if (!strcmp(kind, "contour"))
            {
                int levels = 5;
                if (LIST_SIZE(items) > 2)
                {
                    Value *vl = (Value *)list_getAt(items, 2);
                    if (IS_NUM(*vl))
                        levels = (int)AS_NUM(*vl);
                }

                for (int L = 1; L < levels; L++)
                {
                    double level = zmin + (zmax - zmin) * L / levels;
                    set_draw_color(r, 0xffffff, 200);

                    // Draw contour lines using bilinear interpolation within each cell
                    for (int row = 0; row < M->rows - 1; row++)
                    {
                        for (int col = 0; col < M->cols - 1; col++)
                        {
                            double z00 = matrix_get(M, row, col);
                            double z10 = matrix_get(M, row, col + 1);
                            double z01 = matrix_get(M, row + 1, col);
                            double z11 = matrix_get(M, row + 1, col + 1);

                            // Calculate pixel positions for cell corners
                            double x0 = start_x + col * cell_size;
                            double y0 = start_y + row * cell_size;
                            double x1 = start_x + (col + 1) * cell_size;
                            double y1 = start_y + (row + 1) * cell_size;

                            // Check each edge for contour crossing
                            // Top edge (z00 to z10)
                            if ((z00 <= level && z10 > level) || (z00 > level && z10 <= level))
                            {
                                double t = (level - z00) / (z10 - z00);
                                int x = (int)(x0 + t * (x1 - x0));
                                int y = (int)y0;
                                int x2, y2;

                                // Find which other edge to connect to
                                // Right edge (z10 to z11)
                                if ((z10 <= level && z11 > level) || (z10 > level && z11 <= level))
                                {
                                    double t2 = (level - z10) / (z11 - z10);
                                    x2 = (int)x1;
                                    y2 = (int)(y0 + t2 * (y1 - y0));
                                    SDL_RenderDrawLine(r, x, y, x2, y2);
                                }
                                // Bottom edge (z01 to z11)
                                else if ((z01 <= level && z11 > level) || (z01 > level && z11 <= level))
                                {
                                    double t2 = (level - z01) / (z11 - z01);
                                    x2 = (int)(x0 + t2 * (x1 - x0));
                                    y2 = (int)y1;
                                    SDL_RenderDrawLine(r, x, y, x2, y2);
                                }
                                // Left edge (z00 to z01)
                                else if ((z00 <= level && z01 > level) || (z00 > level && z01 <= level))
                                {
                                    double t2 = (level - z00) / (z01 - z00);
                                    x2 = (int)x0;
                                    y2 = (int)(y0 + t2 * (y1 - y0));
                                    SDL_RenderDrawLine(r, x, y, x2, y2);
                                }
                            }

                            // Right edge (z10 to z11)
                            if ((z10 <= level && z11 > level) || (z10 > level && z11 <= level))
                            {
                                double t = (level - z10) / (z11 - z10);
                                int x = (int)x1;
                                int y = (int)(y0 + t * (y1 - y0));
                                int x2, y2;

                                // Bottom edge (z01 to z11)
                                if ((z01 <= level && z11 > level) || (z01 > level && z11 <= level))
                                {
                                    double t2 = (level - z01) / (z11 - z01);
                                    x2 = (int)(x0 + t2 * (x1 - x0));
                                    y2 = (int)y1;
                                    SDL_RenderDrawLine(r, x, y, x2, y2);
                                }
                                // Left edge (z00 to z01)
                                else if ((z00 <= level && z01 > level) || (z00 > level && z01 <= level))
                                {
                                    double t2 = (level - z00) / (z01 - z00);
                                    x2 = (int)x0;
                                    y2 = (int)(y0 + t2 * (y1 - y0));
                                    SDL_RenderDrawLine(r, x, y, x2, y2);
                                }
                            }

                            // Bottom edge (z01 to z11)
                            if ((z01 <= level && z11 > level) || (z01 > level && z11 <= level))
                            {
                                double t = (level - z01) / (z11 - z01);
                                int x = (int)(x0 + t * (x1 - x0));
                                int y = (int)y1;
                                int x2, y2;

                                // Left edge (z00 to z01)
                                if ((z00 <= level && z01 > level) || (z00 > level && z01 <= level))
                                {
                                    double t2 = (level - z00) / (z01 - z00);
                                    x2 = (int)x0;
                                    y2 = (int)(y0 + t2 * (y1 - y0));
                                    SDL_RenderDrawLine(r, x, y, x2, y2);
                                }
                            }
                        }
                    }
                }
            }
            series_index++;
        }
        else if (!strcmp(kind, "quiver"))
        {
            if (LIST_SIZE(items) < 3)
                continue;
            PiMatrix *U = AS_MATRIX(*(Value *)list_getAt(items, 1));
            PiMatrix *V = AS_MATRIX(*(Value *)list_getAt(items, 2));
            double scale = 0.4;
            set_draw_color(r, col, 255);
            for (int row = 0; row < U->rows; row++)
            {
                for (int col = 0; col < U->cols; col++)
                {
                    double u = matrix_get(U, row, col);
                    double vv = matrix_get(V, row, col);
                    double cx = col + 0.5, cy = row + 0.5;
                    double ex = cx + u * scale, ey = cy + vv * scale;
                    int ax, ay, bx, by;
                    map_xy(chart, cx, cy, W, H, margin, &ax, &ay);
                    map_xy(chart, ex, ey, W, H, margin, &bx, &by);
                    SDL_RenderDrawLine(r, ax, ay, bx, by);

                    // Draw arrowhead
                    double angle = atan2(ey - ay, bx - ax);
                    int arrow_x = bx - 5 * cos(angle);
                    int arrow_y = by - 5 * sin(angle);
                    SDL_RenderDrawLine(r, bx, by, arrow_x - 3 * sin(angle), arrow_y + 3 * cos(angle));
                    SDL_RenderDrawLine(r, bx, by, arrow_x + 3 * sin(angle), arrow_y - 3 * cos(angle));
                }
            }
            series_index++;
        }
        else if (!strcmp(kind, "streamplot"))
        {
            if (LIST_SIZE(items) < 3)
                continue;
            PiMatrix *U = AS_MATRIX(*(Value *)list_getAt(items, 1));
            PiMatrix *V = AS_MATRIX(*(Value *)list_getAt(items, 2));
            set_draw_color(r, col, 200);
            double h = 0.25;
            for (int row = 0; row < U->rows; row += 2)
            {
                for (int col = 0; col < U->cols; col += 2)
                {
                    double x = col + 0.5, y = row + 0.5;
                    for (int s = 0; s < 24; s++)
                    {
                        int r0 = (int)floor(y);
                        int c0 = (int)floor(x);
                        if (r0 < 0)
                            r0 = 0;
                        if (r0 >= U->rows)
                            r0 = U->rows - 1;
                        if (c0 < 0)
                            c0 = 0;
                        if (c0 >= U->cols)
                            c0 = U->cols - 1;
                        double u = matrix_get(U, r0, c0);
                        double vv = matrix_get(V, r0, c0);
                        double nx = x + u * h;
                        double ny = y + vv * h;
                        if (nx < 0 || ny < 0 || nx > (double)(U->cols - 1) || ny > (double)(U->rows - 1))
                            break;
                        int ax, ay, bx, by;
                        map_xy(chart, x, y, W, H, margin, &ax, &ay);
                        map_xy(chart, nx, ny, W, H, margin, &bx, &by);
                        SDL_RenderDrawLine(r, ax, ay, bx, by);
                        x = nx;
                        y = ny;
                    }
                }
            }
            series_index++;
        }
        else if (!strcmp(kind, "surface") || !strcmp(kind, "mesh"))
        {
            if (LIST_SIZE(items) < 2)
                continue;
            PiMatrix *M = AS_MATRIX(*(Value *)list_getAt(items, 1));
            double zmin = INFINITY, zmax = -INFINITY;
            for (int row = 0; row < M->rows; row++)
                for (int col = 0; col < M->cols; col++)
                {
                    double z = matrix_get(M, row, col);
                    if (z < zmin)
                        zmin = z;
                    if (z > zmax)
                        zmax = z;
                }
            if (zmax <= zmin)
                zmax = zmin + 1e-9;
            for (int row = 0; row < M->rows - 1; row++)
            {
                for (int col = 0; col < M->cols - 1; col++)
                {
                    double z00 = matrix_get(M, row, col);
                    double z10 = matrix_get(M, row, col + 1);
                    double z01 = matrix_get(M, row + 1, col);
                    double z11 = matrix_get(M, row + 1, col + 1);
                    double t = ((z00 + z10 + z01 + z11) * 0.25 - zmin) / (zmax - zmin);
                    Uint8 Rcol, Gcol, Bcol;
                    heatmap_rgb(t, &Rcol, &Gcol, &Bcol);
                    SDL_SetRenderDrawColor(r, Rcol, Gcol, Bcol, 255);
                    int ax0, ay0, ax1, ay1, ax2, ay2;
                    map_xy(chart, col, row, W, H, margin, &ax0, &ay0);
                    map_xy(chart, col + 1, row, W, H, margin, &ax1, &ay1);
                    map_xy(chart, col, row + 1, W, H, margin, &ax2, &ay2);
                    SDL_Vertex tri[3];
                    SDL_Color cc = {Rcol, Gcol, Bcol, 255};
                    for (int k = 0; k < 3; k++)
                    {
                        tri[k].color = cc;
                        tri[k].tex_coord.x = 0;
                        tri[k].tex_coord.y = 0;
                    }
                    tri[0].position.x = (float)ax0;
                    tri[0].position.y = (float)ay0;
                    tri[1].position.x = (float)ax1;
                    tri[1].position.y = (float)ay1;
                    tri[2].position.x = (float)ax2;
                    tri[2].position.y = (float)ay2;
                    SDL_RenderGeometry(r, NULL, tri, 3, NULL, 0);
                    map_xy(chart, col + 1, row, W, H, margin, &ax0, &ay0);
                    map_xy(chart, col + 1, row + 1, W, H, margin, &ax1, &ay1);
                    map_xy(chart, col, row + 1, W, H, margin, &ax2, &ay2);
                    tri[0].position.x = (float)ax0;
                    tri[0].position.y = (float)ay0;
                    tri[1].position.x = (float)ax1;
                    tri[1].position.y = (float)ay1;
                    tri[2].position.x = (float)ax2;
                    tri[2].position.y = (float)ay2;
                    SDL_RenderGeometry(r, NULL, tri, 3, NULL, 0);
                }
            }
            series_index++;
        }
        else if (!strcmp(kind, "wireframe"))
        {
            if (LIST_SIZE(items) < 2)
                continue;
            PiMatrix *M = AS_MATRIX(*(Value *)list_getAt(items, 1));
            set_draw_color(r, col, 255);
            for (int row = 0; row < M->rows; row++)
            {
                for (int col = 0; col < M->cols - 1; col++)
                {
                    int ax, ay, bx, by;
                    map_xy(chart, col, row, W, H, margin, &ax, &ay);
                    map_xy(chart, col + 1, row, W, H, margin, &bx, &by);
                    SDL_RenderDrawLine(r, ax, ay, bx, by);
                }
            }
            for (int col = 0; col < M->cols; col++)
            {
                for (int row = 0; row < M->rows - 1; row++)
                {
                    int ax, ay, bx, by;
                    map_xy(chart, col, row, W, H, margin, &ax, &ay);
                    map_xy(chart, col, row + 1, W, H, margin, &bx, &by);
                    SDL_RenderDrawLine(r, ax, ay, bx, by);
                }
            }
            series_index++;
        }
    }

    // Cleanup fonts
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
    {"scatter", pt_scatter},
    {"bar", pt_bar},
    {"line", pt_line},
    {"hist", pt_hist},
    {"step", pt_step},
    {"heatmap", pt_heatmap},
    // {"contour", pt_contour},
    // {"quiver", pt_quiver},
    // {"streamplot", pt_streamplot},
    // {"surface", pt_surface},
    // {"mesh", pt_mesh},
    // {"wireframe", pt_wireframe},
    {"show", pt_show},
    {"title", pt_title},
    {"xlabel", pt_xlabel},
    {"ylabel", pt_ylabel},
    {"grid", pt_grid},
    {"axes", pt_axes},
    {"legend", pt_legend},
};

static BuiltinConst plot_consts[] = {};
DEFINE_BUILTIN_MODULE(module_plot, "plot", plot_funcs, plot_consts);