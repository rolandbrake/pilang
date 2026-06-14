#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <math.h>

#include "pi_image.h"
#include "../pi_builtin.h"
#include "../../common.h"
#include "../../pi_value.h"
#include "../../pi_object.h"

ObjImage *new_image(SDL_Surface *s)
{
    ObjImage *img = (ObjImage *)malloc(sizeof(ObjImage));
    if (!img)
        return NULL;

    img->object.type = OBJ_IMAGE;
    img->object.is_marked = false;
    img->object.in_gcList = false;
    img->object.gc_color = GC_WHITE;
    img->object.next = NULL;

    img->surface = s;
    return img;
}

// ensure SDL_image initialized
static void _ensure_img()
{
    static int inited = 0;
    if (!inited)
    {
        IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);
        inited = 1;
    }
}

Value im_load(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_STRING(argv[0]))
        vm_error(vm, "[image.load] expects a file path string.");

    _ensure_img();

    const char *path = AS_CSTRING(argv[0]);
    SDL_Surface *surf = IMG_Load(path);
    if (!surf)
        vm_errorf(vm, "[image.load] failed to load: %s", path);

    ObjImage *img = new_image(surf);
    if (!img)
        vm_error(vm, "[image.load] out of memory.");

    return NEW_OBJ(add_obj(vm, (Object *)img));
}

Value im_save(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_OBJ_TYPE(argv[0], OBJ_IMAGE) || !IS_STRING(argv[1]))
        vm_error(vm, "[image.save] expects (image, path).");

    ObjImage *img = AS_IMAGE(argv[0]);
    const char *path = AS_CSTRING(argv[1]);

    // Use SDL_SaveBMP for portability
    if (SDL_SaveBMP(img->surface, path) != 0)
        vm_errorf(vm, "[image.save] failed to save: %s", path);

    return NEW_BOOL(true);
}

Value im_width(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_OBJ_TYPE(argv[0], OBJ_IMAGE))
        vm_error(vm, "[image.width] expects an image.");
    ObjImage *img = AS_IMAGE(argv[0]);
    return NEW_NUM((double)img->surface->w);
}

Value im_height(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_OBJ_TYPE(argv[0], OBJ_IMAGE))
        vm_error(vm, "[image.height] expects an image.");
    ObjImage *img = AS_IMAGE(argv[0]);
    return NEW_NUM((double)img->surface->h);
}

Value im_channels(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_OBJ_TYPE(argv[0], OBJ_IMAGE))
        vm_error(vm, "[image.channels] expects an image.");
    ObjImage *img = AS_IMAGE(argv[0]);
    int c = img->surface->format->BytesPerPixel;
    return NEW_NUM((double)c);
}

// helper to create a same-format surface
static SDL_Surface *create_surface_same_format(SDL_Surface *src, int w, int h)
{
    Uint32 fmt = src->format->format;
    int bpp = src->format->BitsPerPixel;
    SDL_Surface *dst = SDL_CreateRGBSurfaceWithFormat(0, w, h, bpp, fmt);
    return dst;
}

Value im_resize(vm_t *vm, int argc, Value *argv)
{
    if (argc < 3 || !IS_OBJ_TYPE(argv[0], OBJ_IMAGE) || !IS_NUM(argv[1]) || !IS_NUM(argv[2]))
        vm_error(vm, "[image.resize] expects (image, w, h).");

    ObjImage *img = AS_IMAGE(argv[0]);
    int w = (int)AS_NUM(argv[1]);
    int h = (int)AS_NUM(argv[2]);

    SDL_Surface *dst = create_surface_same_format(img->surface, w, h);
    if (!dst)
        vm_error(vm, "[image.resize] failed to create surface.");

    SDL_Rect srcrect = {0, 0, img->surface->w, img->surface->h};
    SDL_Rect dstrect = {0, 0, w, h};
    if (SDL_BlitScaled(img->surface, &srcrect, dst, &dstrect) != 0)
    {
        SDL_FreeSurface(dst);
        vm_error(vm, "[image.resize] blit failed.");
    }

    ObjImage *n = new_image(dst);
    return NEW_OBJ(add_obj(vm, (Object *)n));
}

Value im_crop(vm_t *vm, int argc, Value *argv)
{
    if (argc < 5 || !IS_OBJ_TYPE(argv[0], OBJ_IMAGE) || !IS_NUM(argv[1]) || !IS_NUM(argv[2]) || !IS_NUM(argv[3]) || !IS_NUM(argv[4]))
        vm_error(vm, "[image.crop] expects (image, x, y, w, h).");

    ObjImage *img = AS_IMAGE(argv[0]);
    int x = (int)AS_NUM(argv[1]);
    int y = (int)AS_NUM(argv[2]);
    int w = (int)AS_NUM(argv[3]);
    int h = (int)AS_NUM(argv[4]);

    if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > img->surface->w || y + h > img->surface->h)
        vm_error(vm, "[image.crop] invalid rectangle.");

    SDL_Surface *dst = create_surface_same_format(img->surface, w, h);
    if (!dst)
        vm_error(vm, "[image.crop] failed to create surface.");

    SDL_Rect srcrect = {x, y, w, h};
    SDL_Rect dstrect = {0, 0, w, h};
    if (SDL_BlitSurface(img->surface, &srcrect, dst, &dstrect) != 0)
    {
        SDL_FreeSurface(dst);
        vm_error(vm, "[image.crop] blit failed.");
    }

    ObjImage *n = new_image(dst);
    return NEW_OBJ(add_obj(vm, (Object *)n));
}

Value im_flip(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_OBJ_TYPE(argv[0], OBJ_IMAGE) || !IS_STRING(argv[1]))
        vm_error(vm, "[image.flip] expects (image, \"x\"|\"y\").");

    ObjImage *img = AS_IMAGE(argv[0]);
    const char *dir = AS_CSTRING(argv[1]);

    int w = img->surface->w;
    int h = img->surface->h;
    int bpp = img->surface->format->BytesPerPixel;
    int pitch = img->surface->pitch;

    SDL_Surface *dst = create_surface_same_format(img->surface, w, h);
    if (!dst)
        vm_error(vm, "[image.flip] failed to create surface.");

    SDL_LockSurface(img->surface);
    SDL_LockSurface(dst);

    Uint8 *srcp = (Uint8 *)img->surface->pixels;
    Uint8 *dstp = (Uint8 *)dst->pixels;

    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            int sx = x, sy = y;
            if (strcmp(dir, "x") == 0)
                sx = w - 1 - x;
            else if (strcmp(dir, "y") == 0)
                sy = h - 1 - y;

            Uint8 *sp = srcp + sy * img->surface->pitch + sx * bpp;
            Uint8 *dp = dstp + y * dst->pitch + x * bpp;
            memcpy(dp, sp, bpp);
        }
    }

    SDL_UnlockSurface(img->surface);
    SDL_UnlockSurface(dst);

    ObjImage *n = new_image(dst);
    return NEW_OBJ(add_obj(vm, (Object *)n));
}

static bool _try_setWindowIcon(SDL_Window *window, const char *path)
{
    SDL_Surface *icon = IMG_Load(path);
    if (!icon)
        return false;

    SDL_SetWindowIcon(window, icon);
    SDL_FreeSurface(icon);
    return true;
}

Value im_show(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_IMAGE(argv[0]))
        vm_error(vm, "[image.show] expects an image.");

    ObjImage *img = AS_IMAGE(argv[0]);
    int w = img->surface->w;
    int h = img->surface->h;

    const char *title = "display image";

    if (argc >= 2)
    {
        if (!IS_STRING(argv[1]))
            vm_error(vm, "[image.show] title must be a string.");

        title = AS_CSTRING(argv[1]);
    }

    if (SDL_WasInit(SDL_INIT_VIDEO) == 0)
    {
        if (SDL_Init(SDL_INIT_VIDEO) != 0)
            vm_errorf(vm, "[image.show] SDL_Init failed: %s", SDL_GetError());
    }

    SDL_Window *win = SDL_CreateWindow(
        title,
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        w,
        h,
        SDL_WINDOW_SHOWN);

    if (!win)
        vm_errorf(vm, "[image.show] CreateWindow failed: %s", SDL_GetError());

    const char *candidates[] = {
        "pi.ico",
        "../pi.ico",
        "release/pi.ico",
    };

    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++)
        _try_setWindowIcon(win, candidates[i]);

    SDL_Renderer *ren = SDL_CreateRenderer(
        win,
        -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    if (!ren)
    {
        SDL_DestroyWindow(win);
        vm_errorf(vm, "[image.show] CreateRenderer failed: %s", SDL_GetError());
    }

    SDL_Texture *tex = SDL_CreateTextureFromSurface(ren, img->surface);

    if (!tex)
    {
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        vm_errorf(vm, "[image.show] CreateTexture failed: %s", SDL_GetError());
    }

    SDL_Event e;
    bool running = true;

    while (running)
    {
        while (SDL_PollEvent(&e))
        {
            if (e.type == SDL_QUIT)
                running = false;
            else if (e.type == SDL_KEYDOWN)
                running = false;
        }

        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);

        SDL_Delay(16);
    }

    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);

    return NEW_BOOL(true);
}

Value im_img2tensor(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_OBJ_TYPE(argv[0], OBJ_IMAGE))
        vm_error(vm, "[image.img2tensor] expects an image and optional normalize flag.");

    ObjImage *img = AS_IMAGE(argv[0]);
    int w = img->surface->w;
    int h = img->surface->h;
    int bpp = img->surface->format->BytesPerPixel;
    int channels = bpp;

    bool normalize = false;
    if (argc >= 2)
    {
        if (IS_BOOL(argv[1]))
            normalize = AS_BOOL(argv[1]);
        else if (IS_NUM(argv[1]))
            normalize = AS_NUM(argv[1]) != 0.0;
    }

    int shape[3] = {h, w, channels};
    PiTensor *tensor = (PiTensor *)add_obj(vm, new_tensor(3, shape, TN_FLOAT64));

    SDL_LockSurface(img->surface);

    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            Uint8 r = 0, g = 0, b = 0, a = 255;
            Uint8 *p = (Uint8 *)img->surface->pixels + y * img->surface->pitch + x * bpp;
            Uint32 pixel = 0;
            if (bpp == 1)
                pixel = p[0];
            else if (bpp == 2)
                pixel = *(Uint16 *)p;
            else if (bpp == 3)
            {
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
                pixel = p[0] << 16 | p[1] << 8 | p[2];
#else
                pixel = p[0] | p[1] << 8 | p[2] << 16;
#endif
            }
            else
                pixel = *(Uint32 *)p;

            SDL_GetRGBA(pixel, img->surface->format, &r, &g, &b, &a);

            int base = ((y * w) + x) * channels;
            if (channels >= 1)
                tensor->data.f64[base + 0] = normalize ? (double)r / 255.0 : (double)r;
            if (channels >= 2)
                tensor->data.f64[base + 1] = normalize ? (double)g / 255.0 : (double)g;
            if (channels >= 3)
                tensor->data.f64[base + 2] = normalize ? (double)b / 255.0 : (double)b;
            if (channels >= 4)
                tensor->data.f64[base + 3] = normalize ? (double)a / 255.0 : (double)a;
        }
    }

    SDL_UnlockSurface(img->surface);

    return NEW_OBJ(tensor);
}

Value im_tensor2img(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_TENSOR(argv[0]))
        vm_error(vm, "[image.tensor2img] expects a tensor [h,w,c] and optional normalize flag.");

    PiTensor *t = AS_TENSOR(argv[0]);
    if (t->ndim != 3)
        vm_error(vm, "[image.tensor2img] tensor must have 3 dimensions [h,w,channels].");

    int h = t->shape[0];
    int w = t->shape[1];
    int channels = t->shape[2];
    if (channels < 1 || channels > 4)
        vm_error(vm, "[image.tensor2img] channels must be 1..4.");

    bool normalize = false;
    if (argc >= 2)
    {
        if (IS_BOOL(argv[1]))
            normalize = AS_BOOL(argv[1]);
        else if (IS_NUM(argv[1]))
            normalize = AS_NUM(argv[1]) != 0.0;
    }

    // detect scale if not explicitly requested
    double scale = 1.0;
    if (!normalize)
    {
        double maxv = 0.0;
        for (int i = 0; i < t->size; i++)
        {
            double v = fabs(t->data.f64[i]);
            if (v > maxv)
                maxv = v;
        }
        if (maxv <= 1.0)
            scale = 255.0;
    }
    else
        scale = 255.0;

    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surface)
        vm_errorf(vm, "[image.tensor2img] failed to create surface: %s", SDL_GetError());

    SDL_LockSurface(surface);

    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            int base = ((y * w) + x) * channels;
            double vr = 0, vg = 0, vb = 0, va = 255.0;
            if (channels >= 1)
                vr = t->data.f64[base + 0] * scale;
            if (channels >= 2)
                vg = t->data.f64[base + 1] * scale;
            if (channels >= 3)
                vb = t->data.f64[base + 2] * scale;
            if (channels >= 4)
                va = t->data.f64[base + 3] * scale;

            int r = (int)fmin(fmax(vr, 0.0), 255.0);
            int g = (int)fmin(fmax(vg, 0.0), 255.0);
            int b = (int)fmin(fmax(vb, 0.0), 255.0);
            int a = (int)fmin(fmax(va, 0.0), 255.0);

            Uint32 pixel = SDL_MapRGBA(surface->format, (Uint8)r, (Uint8)g, (Uint8)b, (Uint8)a);
            Uint32 *ptr = (Uint32 *)((Uint8 *)surface->pixels + y * surface->pitch + x * 4);
            *ptr = pixel;
        }
    }

    SDL_UnlockSurface(surface);

    ObjImage *img = new_image(surface);
    return NEW_OBJ(add_obj(vm, (Object *)img));
}

// Module export
static BuiltinFunc image_funcs[] = {
    {"load", im_load},
    {"save", im_save},
    {"width", im_width},
    {"height", im_height},
    {"channels", im_channels},
    {"resize", im_resize},
    {"crop", im_crop},
    {"flip", im_flip},
    {"show", im_show},
    {"img2tensor", im_img2tensor},
    {"tensor2img", im_tensor2img},
};

static BuiltinConst image_consts[] = {};

DEFINE_BUILTIN_MODULE(module_image, "image", image_funcs, image_consts);
