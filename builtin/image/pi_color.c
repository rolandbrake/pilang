#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "pi_color.h"
#include "pi_image.h"
#include "../../pi_object.h"
#include "../../pi_value.h"

#include <SDL2/SDL.h>

// helper to create a same-format surface (from pi_image.c)
static SDL_Surface *create_surface_same_format(SDL_Surface *src, int w, int h)
{
    Uint32 fmt = src->format->format;
    int bpp = src->format->BitsPerPixel;
    SDL_Surface *dst = SDL_CreateRGBSurfaceWithFormat(0, w, h, bpp, fmt);
    return dst;
}

// Color conversion: RGB to grayscale
Value im_rgb2gray(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_OBJ_TYPE(argv[0], OBJ_IMAGE))
        vm_error(vm, "[image.color.rgb2gray] expects an image.");

    ObjImage *img = AS_IMAGE(argv[0]);
    int w = img->surface->w;
    int h = img->surface->h;
    int bpp = img->surface->format->BytesPerPixel;

    // create RGB24 destination
    SDL_Surface *dst = SDL_CreateRGBSurfaceWithFormat(0, w, h, 24, SDL_PIXELFORMAT_RGB24);
    if (!dst)
        vm_error(vm, "[image.color.rgb2gray] failed to create surface.");

    SDL_LockSurface(img->surface);
    SDL_LockSurface(dst);

    Uint8 *sp = (Uint8 *)img->surface->pixels;
    Uint8 *dp = (Uint8 *)dst->pixels;

    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            Uint8 r, g, b;
            Uint8 *pix = sp + y * img->surface->pitch + x * bpp;
            if (bpp >= 3)
            {
                r = pix[0];
                g = pix[1];
                b = pix[2];
            }
            else
            {
                r = g = b = pix[0];
            }

            Uint8 gray = (Uint8)(0.299f * r + 0.587f * g + 0.114f * b);
            Uint8 *out = dp + y * dst->pitch + x * 3;
            out[0] = out[1] = out[2] = gray;
        }
    }

    SDL_UnlockSurface(img->surface);
    SDL_UnlockSurface(dst);

    ObjImage *n = new_image(dst);
    return NEW_OBJ(add_obj(vm, (Object *)n));
}

// Color conversion: Grayscale to RGB
Value im_gray2rgb(vm_t *vm, int argc, Value *argv)
{
    // For our representation grayscale images are RGB with equal channels,
    // so just return a copy.
    if (argc < 1 || !IS_OBJ_TYPE(argv[0], OBJ_IMAGE))
        vm_error(vm, "[image.color.gray2rgb] expects an image.");

    ObjImage *img = AS_IMAGE(argv[0]);
    SDL_Surface *dst = create_surface_same_format(img->surface, img->surface->w, img->surface->h);
    if (!dst)
        vm_error(vm, "[image.color.gray2rgb] failed to create surface.");

    if (SDL_BlitSurface(img->surface, NULL, dst, NULL) != 0)
    {
        SDL_FreeSurface(dst);
        vm_error(vm, "[image.color.gray2rgb] blit failed.");
    }

    ObjImage *n = new_image(dst);
    return NEW_OBJ(add_obj(vm, (Object *)n));
}

// Color conversion: Grayscale to RGBA
Value im_gray2rgba(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_OBJ_TYPE(argv[0], OBJ_IMAGE))
        vm_error(vm, "[image.color.gray2rgba] expects an image.");

    ObjImage *img = AS_IMAGE(argv[0]);
    int w = img->surface->w;
    int h = img->surface->h;

    SDL_Surface *dst = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
    if (!dst)
        vm_error(vm, "[image.color.gray2rgba] failed to create surface.");

    SDL_LockSurface(img->surface);
    SDL_LockSurface(dst);

    int bpp = img->surface->format->BytesPerPixel;
    Uint8 *sp = (Uint8 *)img->surface->pixels;
    Uint8 *dp = (Uint8 *)dst->pixels;

    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            Uint8 gray = sp[y * img->surface->pitch + x * bpp];
            Uint8 *out = dp + y * dst->pitch + x * 4;
            out[0] = gray;
            out[1] = gray;
            out[2] = gray;
            out[3] = 255;
        }
    }

    SDL_UnlockSurface(img->surface);
    SDL_UnlockSurface(dst);

    ObjImage *n = new_image(dst);
    return NEW_OBJ(add_obj(vm, (Object *)n));
}

// Color conversion: RGB to HSV
Value im_rgb2hsv(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_OBJ_TYPE(argv[0], OBJ_IMAGE))
        vm_error(vm, "[image.color.rgb2hsv] expects an image.");

    ObjImage *img = AS_IMAGE(argv[0]);
    int w = img->surface->w;
    int h = img->surface->h;
    int bpp = img->surface->format->BytesPerPixel;

    SDL_Surface *dst = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
    if (!dst)
        vm_error(vm, "[image.color.rgb2hsv] failed to create surface.");

    SDL_LockSurface(img->surface);
    SDL_LockSurface(dst);

    Uint8 *sp = (Uint8 *)img->surface->pixels;
    Uint8 *dp = (Uint8 *)dst->pixels;

    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            Uint8 *ps = sp + y * img->surface->pitch + x * bpp;
            Uint8 r = ps[0], g = ps[1], b = ps[2];

            // Normalize to [0, 1]
            float rf = r / 255.0f, gf = g / 255.0f, bf = b / 255.0f;
            float maxc = fmax(rf, fmax(gf, bf));
            float minc = fmin(rf, fmin(gf, bf));
            float delta = maxc - minc;

            // Hue
            float h = 0.0f;
            if (delta > 0.0f)
            {
                if (maxc == rf)
                    h = 60.0f * fmod((gf - bf) / delta, 6.0f);
                else if (maxc == gf)
                    h = 60.0f * ((bf - rf) / delta + 2.0f);
                else
                    h = 60.0f * ((rf - gf) / delta + 4.0f);
                if (h < 0.0f)
                    h += 360.0f;
            }

            // Saturation
            float s = (maxc > 0.0f) ? (delta / maxc) : 0.0f;

            // Value
            float v = maxc;

            // Store as RGB: H in [0,360] -> [0,255], S in [0,1] -> [0,255], V in [0,1] -> [0,255]
            Uint8 *pd = dp + y * dst->pitch + x * 4;
            pd[0] = (Uint8)((h / 360.0f) * 255.0f);
            pd[1] = (Uint8)(s * 255.0f);
            pd[2] = (Uint8)(v * 255.0f);
            pd[3] = 255;
        }
    }

    SDL_UnlockSurface(dst);
    SDL_UnlockSurface(img->surface);

    ObjImage *n = new_image(dst);
    return NEW_OBJ(add_obj(vm, (Object *)n));
}

// Color conversion: HSV to RGB
Value im_hsv2rgb(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_OBJ_TYPE(argv[0], OBJ_IMAGE))
        vm_error(vm, "[image.color.hsv2rgb] expects an image.");

    ObjImage *img = AS_IMAGE(argv[0]);
    int w = img->surface->w;
    int h = img->surface->h;
    int bpp = img->surface->format->BytesPerPixel;

    SDL_Surface *dst = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
    if (!dst)
        vm_error(vm, "[image.color.hsv2rgb] failed to create surface.");

    SDL_LockSurface(img->surface);
    SDL_LockSurface(dst);

    Uint8 *sp = (Uint8 *)img->surface->pixels;
    Uint8 *dp = (Uint8 *)dst->pixels;

    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            Uint8 *ps = sp + y * img->surface->pitch + x * bpp;
            // Stored as H in [0,255], S in [0,255], V in [0,255]
            float h = (ps[0] / 255.0f) * 360.0f;
            float s = ps[1] / 255.0f;
            float v = ps[2] / 255.0f;

            float c = v * s;
            float hp = h / 60.0f;
            float x_val = c * (1.0f - fabs(fmod(hp, 2.0f) - 1.0f));

            float rf = 0.0f, gf = 0.0f, bf = 0.0f;

            if (hp >= 0.0f && hp < 1.0f)
            {
                rf = c;
                gf = x_val;
            }
            else if (hp >= 1.0f && hp < 2.0f)
            {
                rf = x_val;
                gf = c;
            }
            else if (hp >= 2.0f && hp < 3.0f)
            {
                gf = c;
                bf = x_val;
            }
            else if (hp >= 3.0f && hp < 4.0f)
            {
                gf = x_val;
                bf = c;
            }
            else if (hp >= 4.0f && hp < 5.0f)
            {
                rf = x_val;
                bf = c;
            }
            else
            {
                rf = c;
                bf = x_val;
            }

            float m = v - c;
            Uint8 *pd = dp + y * dst->pitch + x * 4;
            pd[0] = (Uint8)((rf + m) * 255.0f);
            pd[1] = (Uint8)((gf + m) * 255.0f);
            pd[2] = (Uint8)((bf + m) * 255.0f);
            pd[3] = 255;
        }
    }

    SDL_UnlockSurface(dst);
    SDL_UnlockSurface(img->surface);

    ObjImage *n = new_image(dst);
    return NEW_OBJ(add_obj(vm, (Object *)n));
}

// Module export
static BuiltinFunc color_funcs[] = {
    {"rgb2gray", im_rgb2gray},
    {"gray2rgb", im_gray2rgb},
    {"gray2rgba", im_gray2rgba},
    {"rgb2hsv", im_rgb2hsv},
    {"hsv2rgb", im_hsv2rgb},
};

static BuiltinConst color_consts[] = {};

DEFINE_BUILTIN_MODULE(module_imageColor, "image.color", color_funcs, color_consts);
