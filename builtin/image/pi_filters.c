#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "pi_filters.h"
#include "./pi_image.h"
#include "../../pi_object.h"
#include "../../pi_value.h"

#include <SDL2/SDL.h>

// Ensure an image is converted to RGBA32 for easy processing.
static SDL_Surface *to_rgba32(SDL_Surface *src)
{
    if (!src)
        return NULL;
    if (src->format->format == SDL_PIXELFORMAT_RGBA32)
        return src;
    SDL_Surface *conv = SDL_ConvertSurfaceFormat(src, SDL_PIXELFORMAT_RGBA32, 0);
    return conv;
}

// Apply a convolution kernel (ksize x ksize) to src (RGBA32) -> dst (RGBA32).
static void apply_convolution_rgba(SDL_Surface *src, SDL_Surface *dst, const float *kernel, int ksize)
{
    int w = src->w;
    int h = src->h;
    int half = ksize / 2;

    SDL_LockSurface(src);
    SDL_LockSurface(dst);

    Uint8 *sp = (Uint8 *)src->pixels;
    Uint8 *dp = (Uint8 *)dst->pixels;
    int spitch = src->pitch;
    int dpitch = dst->pitch;

    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            float sumr = 0.0f, sumg = 0.0f, sumb = 0.0f;
            for (int ky = -half; ky <= half; ky++)
            {
                int sy = y + ky;
                if (sy < 0)
                    sy = 0;
                else if (sy >= h)
                    sy = h - 1;

                for (int kx = -half; kx <= half; kx++)
                {
                    int sx = x + kx;
                    if (sx < 0)
                        sx = 0;
                    else if (sx >= w)
                        sx = w - 1;

                    int ki = (ky + half) * ksize + (kx + half);
                    const float kval = kernel[ki];

                    Uint8 *pp = sp + sy * spitch + sx * 4;
                    sumr += kval * pp[0];
                    sumg += kval * pp[1];
                    sumb += kval * pp[2];
                }
            }

            Uint8 *out = dp + y * dpitch + x * 4;
            int r = (int)fmin(fmax(sumr, 0.0f), 255.0f);
            int g = (int)fmin(fmax(sumg, 0.0f), 255.0f);
            int b = (int)fmin(fmax(sumb, 0.0f), 255.0f);
            out[0] = (Uint8)r;
            out[1] = (Uint8)g;
            out[2] = (Uint8)b;
            out[3] = 255;
        }
    }

    SDL_UnlockSurface(dst);
    SDL_UnlockSurface(src);
}

static int clamp_u8(double value)
{
    if (value < 0.0)
        return 0;
    if (value > 255.0)
        return 255;
    return (int)lrint(value);
}

typedef struct
{
    const char *name;
    const double *values;
    int rows;
    int cols;
} KernelDef;

static const KernelDef *find_kernelDefById(int id);

static int copy_builtinKernel(vm_t *vm, const KernelDef *def, double **kernel, int *rows, int *cols)
{
    *rows = def->rows;
    *cols = def->cols;
    *kernel = (double *)malloc(sizeof(double) * (size_t)(*rows * *cols));
    if (!*kernel)
    {
        vm_error(vm, "[image.filters.filter] out of memory.");
        return 0;
    }

    memcpy(*kernel, def->values, sizeof(double) * (size_t)(*rows * *cols));
    return 1;
}

static int parse_listKernel(vm_t *vm, Value value, double **kernel, int *rows, int *cols)
{
    if (!IS_LIST(value))
    {
        vm_error(vm, "[image.filters.filter] kernel must be a builtin kernel id or nested numeric list.");
        return 0;
    }

    PiList *outer = AS_LIST(value);
    int rcount = LIST_SIZE(outer->items);
    if (rcount <= 0)
    {
        vm_error(vm, "[image.filters.filter] kernel list must be non-empty.");
        return 0;
    }

    Value *first = (Value *)list_getAt(outer->items, 0);
    if (!first || !IS_LIST(*first))
    {
        vm_error(vm, "[image.filters.filter] kernel list must be nested, e.g. [[0,-1,0],[-1,5,-1],[0,-1,0]].");
        return 0;
    }

    int ccount = LIST_SIZE(AS_LIST(*first)->items);
    if (ccount <= 0)
    {
        vm_error(vm, "[image.filters.filter] kernel rows must be non-empty.");
        return 0;
    }

    *kernel = (double *)malloc(sizeof(double) * (size_t)(rcount * ccount));
    if (!*kernel)
    {
        vm_error(vm, "[image.filters.filter] out of memory.");
        return 0;
    }

    for (int r = 0; r < rcount; r++)
    {
        Value *row_value = (Value *)list_getAt(outer->items, r);
        if (!row_value || !IS_LIST(*row_value))
        {
            free(*kernel);
            *kernel = NULL;
            vm_error(vm, "[image.filters.filter] every kernel row must be a numeric list.");
            return 0;
        }

        PiList *row = AS_LIST(*row_value);
        if (LIST_SIZE(row->items) != ccount)
        {
            free(*kernel);
            *kernel = NULL;
            vm_error(vm, "[image.filters.filter] kernel rows must have the same length.");
            return 0;
        }

        for (int c = 0; c < ccount; c++)
        {
            Value *cell = (Value *)list_getAt(row->items, c);
            if (!cell || !IS_NUM(*cell))
            {
                free(*kernel);
                *kernel = NULL;
                vm_error(vm, "[image.filters.filter] kernel values must be numbers.");
                return 0;
            }
            (*kernel)[r * ccount + c] = AS_NUM(*cell);
        }
    }

    *rows = rcount;
    *cols = ccount;
    return 1;
}

static int parse_kernelValue(vm_t *vm, Value value, double **kernel, int *rows, int *cols)
{
    *kernel = NULL;
    *rows = 0;
    *cols = 0;

    if (IS_NUM(value))
    {
        int id = (int)AS_NUM(value);
        const KernelDef *def = find_kernelDefById(id);
        if (!def)
        {
            vm_error(vm, "[image.filters.filter] unknown builtin kernel id.");
            return 0;
        }
        return copy_builtinKernel(vm, def, kernel, rows, cols);
    }

    return parse_listKernel(vm, value, kernel, rows, cols);
}

static void apply_kernel_rgba(SDL_Surface *src, SDL_Surface *dst, const double *kernel, int krows, int kcols)
{
    int w = src->w;
    int h = src->h;
    int row_anchor = krows / 2;
    int col_anchor = kcols / 2;

    SDL_LockSurface(src);
    SDL_LockSurface(dst);

    Uint8 *sp = (Uint8 *)src->pixels;
    Uint8 *dp = (Uint8 *)dst->pixels;

    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            double sumr = 0.0;
            double sumg = 0.0;
            double sumb = 0.0;

            for (int kr = 0; kr < krows; kr++)
            {
                int sy = y + kr - row_anchor;
                if (sy < 0)
                    sy = 0;
                else if (sy >= h)
                    sy = h - 1;

                for (int kc = 0; kc < kcols; kc++)
                {
                    int sx = x + kc - col_anchor;
                    if (sx < 0)
                        sx = 0;
                    else if (sx >= w)
                        sx = w - 1;

                    double kval = kernel[kr * kcols + kc];
                    Uint8 *pixel = sp + sy * src->pitch + sx * 4;
                    sumr += kval * pixel[0];
                    sumg += kval * pixel[1];
                    sumb += kval * pixel[2];
                }
            }

            Uint8 *src_pixel = sp + y * src->pitch + x * 4;
            Uint8 *out = dp + y * dst->pitch + x * 4;
            out[0] = (Uint8)clamp_u8(sumr);
            out[1] = (Uint8)clamp_u8(sumg);
            out[2] = (Uint8)clamp_u8(sumb);
            out[3] = src_pixel[3];
        }
    }

    SDL_UnlockSurface(dst);
    SDL_UnlockSurface(src);
}

static const double kernel_identity[] = {
    0, 0, 0,
    0, 1, 0,
    0, 0, 0};
static const double kernel_sharpen[] = {
    0, -1, 0,
    -1, 5, -1,
    0, -1, 0};
static const double kernel_edge[] = {
    -1, -1, -1,
    -1, 8, -1,
    -1, -1, -1};
static const double kernel_emboss[] = {
    -2, -1, 0,
    -1, 1, 1,
    0, 1, 2};
static const double kernel_gaussian3[] = {
    1.0 / 16.0, 2.0 / 16.0, 1.0 / 16.0,
    2.0 / 16.0, 4.0 / 16.0, 2.0 / 16.0,
    1.0 / 16.0, 2.0 / 16.0, 1.0 / 16.0};
static const double kernel_sobel_x[] = {
    -1, 0, 1,
    -2, 0, 2,
    -1, 0, 1};
static const double kernel_sobel_y[] = {
    -1, -2, -1,
    0, 0, 0,
    1, 2, 1};

enum
{
    KERNEL_IDENTITY = 0,
    KERNEL_SHARPEN,
    KERNEL_EDGE,
    KERNEL_EMBOSS,
    KERNEL_GAUSSIAN,
    KERNEL_SOBEL_X,
    KERNEL_SOBEL_Y,
    KERNEL_COUNT
};

static const KernelDef builtin_kernels[KERNEL_COUNT] = {
    {"identity", kernel_identity, 3, 3},
    {"sharpen", kernel_sharpen, 3, 3},
    {"edge", kernel_edge, 3, 3},
    {"emboss", kernel_emboss, 3, 3},
    {"gaussian3", kernel_gaussian3, 3, 3},
    {"sobel_x", kernel_sobel_x, 3, 3},
    {"sobel_y", kernel_sobel_y, 3, 3},
};

typedef struct
{
    const char *name;
    int id;
} KernelAlias;

static const KernelAlias kernel_aliases[] = {
    {"identity", KERNEL_IDENTITY},
    {"sharpen", KERNEL_SHARPEN},
    {"edge", KERNEL_EDGE},
    {"edge8", KERNEL_EDGE},
    {"emboss", KERNEL_EMBOSS},
    {"gaussian", KERNEL_GAUSSIAN},
    {"gaussian3", KERNEL_GAUSSIAN},
    {"sobel", KERNEL_SOBEL_X},
    {"sobel_x", KERNEL_SOBEL_X},
    {"sobelx", KERNEL_SOBEL_X},
    {"sobel_y", KERNEL_SOBEL_Y},
    {"sobely", KERNEL_SOBEL_Y},
};

static const KernelDef *find_kernelDefById(int id)
{
    if (id < 0 || id >= KERNEL_COUNT)
        return NULL;
    return &builtin_kernels[id];
}

static int find_kernelId(const char *name)
{
    int count = (int)(sizeof(kernel_aliases) / sizeof(KernelAlias));
    for (int i = 0; i < count; i++)
    {
        if (!strcmp(name, kernel_aliases[i].name))
            return kernel_aliases[i].id;
    }
    return -1;
}

static Value make_namedKernel(vm_t *vm, const char *name)
{
    int id = find_kernelId(name);
    if (id >= 0)
        return NEW_NUM(id);

    vm_error(vm, "[image.filters.kernel] unknown kernel name.");
    return NIL_VAL;
}

static Value make_kernelList(vm_t *vm, const double *values, int rows, int cols)
{
    list_t *outer_items = list_create(sizeof(Value));

    for (int r = 0; r < rows; r++)
    {
        list_t *row_items = list_create(sizeof(Value));
        for (int c = 0; c < cols; c++)
        {
            Value cell = NEW_NUM(values[r * cols + c]);
            list_add(row_items, &cell);
        }

        PiList *row = (PiList *)add_obj(vm, new_list(row_items));
        row->is_numeric = true;
        row->is_matrix = false;
        row->rows = 1;
        row->cols = cols;

        Value row_value = NEW_OBJ(row);
        list_add(outer_items, &row_value);
    }

    PiList *outer = (PiList *)add_obj(vm, new_list(outer_items));
    outer->is_numeric = false;
    outer->is_matrix = true;
    outer->rows = rows;
    outer->cols = cols;
    return NEW_OBJ(outer);
}

Value im_filter(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_IMAGE(argv[0]))
    {
        vm_error(vm, "[image.filters.filter] expects (image, kernel).");
        return NIL_VAL;
    }

    double *kernel = NULL;
    int rows = 0;
    int cols = 0;
    if (!parse_kernelValue(vm, argv[1], &kernel, &rows, &cols))
        return NIL_VAL;

    ObjImage *img = AS_IMAGE(argv[0]);
    SDL_Surface *src = to_rgba32(img->surface);
    bool converted = (src != img->surface);
    SDL_Surface *dst = SDL_CreateRGBSurfaceWithFormat(0, src->w, src->h, 32, SDL_PIXELFORMAT_RGBA32);
    if (!dst)
    {
        free(kernel);
        if (converted)
            SDL_FreeSurface(src);
        vm_error(vm, "[image.filters.filter] failed to create surface.");
        return NIL_VAL;
    }

    apply_kernel_rgba(src, dst, kernel, rows, cols);
    free(kernel);

    ObjImage *n = new_image(dst);
    Value out = NEW_OBJ(add_obj(vm, (Object *)n));
    if (converted)
        SDL_FreeSurface(src);
    return out;
}

Value im_kernel(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_STRING(argv[0]))
    {
        vm_error(vm, "[image.filters.kernel] expects a kernel name string.");
        return NIL_VAL;
    }
    return make_namedKernel(vm, AS_CSTRING(argv[0]));
}

Value im_boxKernel(vm_t *vm, int argc, Value *argv)
{
    int size = 3;
    if (argc >= 1 && IS_NUM(argv[0]))
        size = (int)AS_NUM(argv[0]);
    if (size < 1)
        size = 1;
    if (size > 51)
        size = 51;

    int count = size * size;
    double *values = (double *)malloc(sizeof(double) * (size_t)count);
    if (!values)
    {
        vm_error(vm, "[image.filters.box_kernel] out of memory.");
        return NIL_VAL;
    }

    double v = 1.0 / (double)count;
    for (int i = 0; i < count; i++)
        values[i] = v;

    Value result = make_kernelList(vm, values, size, size);
    free(values);
    return result;
}

// Builtin: invert(image)
Value im_invert(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_OBJ_TYPE(argv[0], OBJ_IMAGE))
        vm_error(vm, "[image.filters.invert] expects an image.");

    ObjImage *img = AS_IMAGE(argv[0]);
    SDL_Surface *src = to_rgba32(img->surface);
    bool converted = (src != img->surface);

    SDL_Surface *dst = SDL_CreateRGBSurfaceWithFormat(0, src->w, src->h, 32, SDL_PIXELFORMAT_RGBA32);
    if (!dst)
    {
        if (converted)
            SDL_FreeSurface(src);
        vm_error(vm, "[image.filters.invert] failed to create surface.");
    }

    SDL_LockSurface(src);
    SDL_LockSurface(dst);

    Uint8 *sp = (Uint8 *)src->pixels;
    Uint8 *dp = (Uint8 *)dst->pixels;
    for (int y = 0; y < src->h; y++)
    {
        for (int x = 0; x < src->w; x++)
        {
            Uint8 *ps = sp + y * src->pitch + x * 4;
            Uint8 *pd = dp + y * dst->pitch + x * 4;
            pd[0] = 255 - ps[0];
            pd[1] = 255 - ps[1];
            pd[2] = 255 - ps[2];
            pd[3] = ps[3];
        }
    }

    SDL_UnlockSurface(dst);
    SDL_UnlockSurface(src);

    ObjImage *n = new_image(dst);
    Value out = NEW_OBJ(add_obj(vm, (Object *)n));

    if (converted)
        SDL_FreeSurface(src);

    return out;
}

// Builtin: brightness(image, delta)
Value im_brightness(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_OBJ_TYPE(argv[0], OBJ_IMAGE) || !IS_NUM(argv[1]))
        vm_error(vm, "[image.filters.brightness] expects (image, delta).");

    ObjImage *img = AS_IMAGE(argv[0]);
    double delta = AS_NUM(argv[1]);

    SDL_Surface *src = to_rgba32(img->surface);
    bool converted = (src != img->surface);
    SDL_Surface *dst = SDL_CreateRGBSurfaceWithFormat(0, src->w, src->h, 32, SDL_PIXELFORMAT_RGBA32);
    if (!dst)
    {
        if (converted)
            SDL_FreeSurface(src);
        vm_error(vm, "[image.filters.brightness] failed to create surface.");
    }

    SDL_LockSurface(src);
    SDL_LockSurface(dst);

    Uint8 *sp = (Uint8 *)src->pixels;
    Uint8 *dp = (Uint8 *)dst->pixels;
    for (int y = 0; y < src->h; y++)
    {
        for (int x = 0; x < src->w; x++)
        {
            Uint8 *ps = sp + y * src->pitch + x * 4;
            Uint8 *pd = dp + y * dst->pitch + x * 4;
            int r = (int)fmin(fmax(ps[0] + delta, 0.0), 255.0);
            int g = (int)fmin(fmax(ps[1] + delta, 0.0), 255.0);
            int b = (int)fmin(fmax(ps[2] + delta, 0.0), 255.0);
            pd[0] = (Uint8)r;
            pd[1] = (Uint8)g;
            pd[2] = (Uint8)b;
            pd[3] = ps[3];
        }
    }

    SDL_UnlockSurface(dst);
    SDL_UnlockSurface(src);

    ObjImage *n = new_image(dst);
    Value out = NEW_OBJ(add_obj(vm, (Object *)n));
    if (converted)
        SDL_FreeSurface(src);
    return out;
}

// Builtin: contrast(image, factor)
Value im_contrast(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_OBJ_TYPE(argv[0], OBJ_IMAGE) || !IS_NUM(argv[1]))
        vm_error(vm, "[image.filters.contrast] expects (image, factor).");

    ObjImage *img = AS_IMAGE(argv[0]);
    double factor = AS_NUM(argv[1]);

    SDL_Surface *src = to_rgba32(img->surface);
    bool converted = (src != img->surface);
    SDL_Surface *dst = SDL_CreateRGBSurfaceWithFormat(0, src->w, src->h, 32, SDL_PIXELFORMAT_RGBA32);
    if (!dst)
    {
        if (converted)
            SDL_FreeSurface(src);
        vm_error(vm, "[image.filters.contrast] failed to create surface.");
    }

    SDL_LockSurface(src);
    SDL_LockSurface(dst);

    Uint8 *sp = (Uint8 *)src->pixels;
    Uint8 *dp = (Uint8 *)dst->pixels;
    for (int y = 0; y < src->h; y++)
    {
        for (int x = 0; x < src->w; x++)
        {
            Uint8 *ps = sp + y * src->pitch + x * 4;
            Uint8 *pd = dp + y * dst->pitch + x * 4;
            int r = (int)fmin(fmax((ps[0] - 128) * factor + 128, 0.0), 255.0);
            int g = (int)fmin(fmax((ps[1] - 128) * factor + 128, 0.0), 255.0);
            int b = (int)fmin(fmax((ps[2] - 128) * factor + 128, 0.0), 255.0);
            pd[0] = (Uint8)r;
            pd[1] = (Uint8)g;
            pd[2] = (Uint8)b;
            pd[3] = ps[3];
        }
    }

    SDL_UnlockSurface(dst);
    SDL_UnlockSurface(src);

    ObjImage *n = new_image(dst);
    Value out = NEW_OBJ(add_obj(vm, (Object *)n));
    if (converted)
        SDL_FreeSurface(src);
    return out;
}

// Builtin: blur(image, radius)
Value im_blur(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_OBJ_TYPE(argv[0], OBJ_IMAGE))
        vm_error(vm, "[image.filters.blur] expects (image, radius=1).");

    int radius = 1;
    if (argc >= 2 && IS_NUM(argv[1]))
        radius = (int)AS_NUM(argv[1]);
    if (radius < 1)
        radius = 1;

    int ksize = radius * 2 + 1;
    int kcount = ksize * ksize;
    float *kernel = (float *)malloc(sizeof(float) * kcount);
    if (!kernel)
        vm_error(vm, "[image.filters.blur] out of memory.");
    for (int i = 0; i < kcount; i++)
        kernel[i] = 1.0f / (float)kcount;

    ObjImage *img = AS_IMAGE(argv[0]);
    SDL_Surface *src = to_rgba32(img->surface);
    bool converted = (src != img->surface);
    SDL_Surface *dst = SDL_CreateRGBSurfaceWithFormat(0, src->w, src->h, 32, SDL_PIXELFORMAT_RGBA32);
    if (!dst)
    {
        free(kernel);
        if (converted)
            SDL_FreeSurface(src);
        vm_error(vm, "[image.filters.blur] failed to create surface.");
    }

    apply_convolution_rgba(src, dst, kernel, ksize);

    free(kernel);
    ObjImage *n = new_image(dst);
    Value out = NEW_OBJ(add_obj(vm, (Object *)n));
    if (converted)
        SDL_FreeSurface(src);
    return out;
}

// Builtin: sharpen(image)
Value im_sharpen(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_OBJ_TYPE(argv[0], OBJ_IMAGE))
        vm_error(vm, "[image.filters.sharpen] expects an image.");

    float kernel[9] = {0, -1, 0, -1, 5, -1, 0, -1, 0};

    ObjImage *img = AS_IMAGE(argv[0]);
    SDL_Surface *src = to_rgba32(img->surface);
    bool converted = (src != img->surface);
    SDL_Surface *dst = SDL_CreateRGBSurfaceWithFormat(0, src->w, src->h, 32, SDL_PIXELFORMAT_RGBA32);
    if (!dst)
    {
        if (converted)
            SDL_FreeSurface(src);
        vm_error(vm, "[image.filters.sharpen] failed to create surface.");
    }

    apply_convolution_rgba(src, dst, kernel, 3);

    ObjImage *n = new_image(dst);
    Value out = NEW_OBJ(add_obj(vm, (Object *)n));
    if (converted)
        SDL_FreeSurface(src);
    return out;
}

// Builtin: sobel(image) -> grayscale edge image
Value im_sobel(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_OBJ_TYPE(argv[0], OBJ_IMAGE))
        vm_error(vm, "[image.filters.sobel] expects an image.");

    // Sobel kernels
    float kx[9] = {-1, 0, 1, -2, 0, 2, -1, 0, 1};
    float ky[9] = {-1, -2, -1, 0, 0, 0, 1, 2, 1};

    ObjImage *img = AS_IMAGE(argv[0]);
    SDL_Surface *src = to_rgba32(img->surface);
    bool converted = (src != img->surface);
    int w = src->w, h = src->h;

    SDL_Surface *dst = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
    if (!dst)
    {
        if (converted)
            SDL_FreeSurface(src);
        vm_error(vm, "[image.filters.sobel] failed to create surface.");
    }

    SDL_LockSurface(src);
    SDL_LockSurface(dst);
    Uint8 *sp = (Uint8 *)src->pixels;
    Uint8 *dp = (Uint8 *)dst->pixels;
    int spitch = src->pitch;
    int dpitch = dst->pitch;

    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            float gx = 0.0f, gy = 0.0f;
            for (int kyoff = -1; kyoff <= 1; kyoff++)
            {
                int sy = y + kyoff;
                if (sy < 0)
                    sy = 0;
                else if (sy >= h)
                    sy = h - 1;
                for (int kxoff = -1; kxoff <= 1; kxoff++)
                {
                    int sx = x + kxoff;
                    if (sx < 0)
                        sx = 0;
                    else if (sx >= w)
                        sx = w - 1;

                    Uint8 *pp = sp + sy * spitch + sx * 4;
                    // luminance
                    float lum = 0.299f * pp[0] + 0.587f * pp[1] + 0.114f * pp[2];
                    int ki = (kyoff + 1) * 3 + (kxoff + 1);
                    gx += lum * kx[ki];
                    gy += lum * ky[ki];
                }
            }
            float mag = sqrtf(gx * gx + gy * gy);
            int v = (int)fmin(fmax(mag, 0.0f), 255.0f);
            Uint8 *out = dp + y * dpitch + x * 4;
            out[0] = out[1] = out[2] = (Uint8)v;
            out[3] = 255;
        }
    }

    SDL_UnlockSurface(dst);
    SDL_UnlockSurface(src);

    ObjImage *n = new_image(dst);
    Value out = NEW_OBJ(add_obj(vm, (Object *)n));
    if (converted)
        SDL_FreeSurface(src);
    return out;
}

// Builtin: threshold(image, thresh=128)
Value im_threshold(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_OBJ_TYPE(argv[0], OBJ_IMAGE))
        vm_error(vm, "[image.filters.threshold] expects (image, thresh=128).");

    int thresh = 128;
    if (argc >= 2 && IS_NUM(argv[1]))
        thresh = (int)AS_NUM(argv[1]);

    ObjImage *img = AS_IMAGE(argv[0]);
    SDL_Surface *src = to_rgba32(img->surface);
    bool converted = (src != img->surface);
    int w = src->w, h = src->h;

    SDL_Surface *dst = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
    if (!dst)
    {
        if (converted)
            SDL_FreeSurface(src);
        vm_error(vm, "[image.filters.threshold] failed to create surface.");
    }

    SDL_LockSurface(src);
    SDL_LockSurface(dst);
    Uint8 *sp = (Uint8 *)src->pixels;
    Uint8 *dp = (Uint8 *)dst->pixels;
    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            Uint8 *ps = sp + y * src->pitch + x * 4;
            float lum = 0.299f * ps[0] + 0.587f * ps[1] + 0.114f * ps[2];
            Uint8 *pd = dp + y * dst->pitch + x * 4;
            if (lum >= thresh)
                pd[0] = pd[1] = pd[2] = 255;
            else
                pd[0] = pd[1] = pd[2] = 0;
            pd[3] = ps[3];
        }
    }
    SDL_UnlockSurface(dst);
    SDL_UnlockSurface(src);

    ObjImage *n = new_image(dst);
    Value out = NEW_OBJ(add_obj(vm, (Object *)n));
    if (converted)
        SDL_FreeSurface(src);
    return out;
}

// Module export
static BuiltinFunc filter_funcs[] = {
    {"filter", im_filter},
    {"kernel", im_kernel},
    {"box_kernel", im_boxKernel},
    {"boxKernel", im_boxKernel},
    {"invert", im_invert},
    {"brightness", im_brightness},
    {"contrast", im_contrast},
    {"blur", im_blur},
    {"sharpen", im_sharpen},
    {"sobel", im_sobel},
    {"threshold", im_threshold},
};

static BuiltinConst filter_consts[] = {
    {"KERNEL_IDENTITY", NEW_NUM(KERNEL_IDENTITY)},
    {"KERNEL_SHARPEN", NEW_NUM(KERNEL_SHARPEN)},
    {"KERNEL_EDGE", NEW_NUM(KERNEL_EDGE)},
    {"KERNEL_EDGE8", NEW_NUM(KERNEL_EDGE)},
    {"KERNEL_EMBOSS", NEW_NUM(KERNEL_EMBOSS)},
    {"KERNEL_GAUSSIAN", NEW_NUM(KERNEL_GAUSSIAN)},
    {"KERNEL_GAUSSIAN3", NEW_NUM(KERNEL_GAUSSIAN)},
    {"KERNEL_SOBEL", NEW_NUM(KERNEL_SOBEL_X)},
    {"KERNEL_SOBEL_X", NEW_NUM(KERNEL_SOBEL_X)},
    {"KERNEL_SOBEL_Y", NEW_NUM(KERNEL_SOBEL_Y)},
    {"KERNEL_COUNT", NEW_NUM(KERNEL_COUNT)},
};

DEFINE_BUILTIN_MODULE(module_imageFilters, "image.filters", filter_funcs, filter_consts);
