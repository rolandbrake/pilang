#include "pi_draw.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#ifdef __EMSCRIPTEN__

#include <emscripten.h>

EM_JS(void, _web_canvas_init, (int w, int h), {
    const c = Module.canvas;

    c.width = w;
    c.height = h;

    Module.piCtx = c.getContext('2d');
    Module.piKeys = {};
    Module.piMouse = {
        x : 0,
        y : 0,
        left : false
    };

    addEventListener('keydown', e = > { Module.piKeys[e.keyCode] = true; });

    addEventListener('keyup', e = > { Module.piKeys[e.keyCode] = false; });

    c.onmousemove = e =>
    {
        const r = c.getBoundingClientRect();

        Module.piMouse.x =
            (e.clientX - r.left) * c.width / r.width;

        Module.piMouse.y =
            (e.clientY - r.top) * c.height / r.height;
    };

    c.onmousedown = e =>
    {
        Module.piMouse.left = e.button == = 0;
    };

    c.onmouseup = e =>
    {
        if (e.button == = 0)
        {
            Module.piMouse.left = false;
        }
    };
});

EM_JS(void, _web_canvas_image,
      (const char *p, double x, double y, double w, double h,
       double angle, int flip),
      {
          const c = Module.piCtx;
          const path = UTF8ToString(p);

          Module.piImgs || = {};

          let i = Module.piImgs[path];

          if (!i)
          {
              i = new Image();
              i.src = path;
              Module.piImgs[path] = i;
          }

          if (!i.complete)
          {
              return;
          }

          w = w || i.width;
          h = h || i.height;

          c.save();

          c.translate(x + w / 2, y + h / 2);
          c.rotate(angle * Math.PI / 180);
          c.scale(1, flip ? -1 : 1);

          c.drawImage(i, -w / 2, -h / 2, w, h);

          c.restore();
      });

EM_JS(void, _web_canvas_text,
      (const char *text, double x, double y, int color, int size,
       int bold, int italic, const char *align, double alpha),
      {
          const c = Module.piCtx;
          const a = UTF8ToString(align);

          c.save();

          c.globalAlpha = alpha;

          c.fillStyle = '#' +
                        (color >>> 0).toString(16).padStart(6, '0').slice(-6);

          c.font =
              (bold ? "bold " : "") +
              (italic ? "italic " : "") +
              size + 'px sans-serif';

          c.textBaseline = 'top';

          c.textAlign =
              (a == = 'center' || a == = 'middle')
                  ? 'center'
              : (a == = 'right' || a == = 'end')
                  ? 'right'
                  : 'left';

          if (a == = 'middle')
          {
              y -= size / 2;
          }
          else if (a == = 'bottom')
          {
              y -= size;
          }

          c.fillText(UTF8ToString(text), x, y);

          c.restore();
      });

EM_JS(int, _web_mouse_x, (), {
    return Module.piMouse ? Module.piMouse.x : 0;
});

EM_JS(int, _web_mouse_y, (), {
    return Module.piMouse ? Module.piMouse.y : 0;
});

EM_JS(int, _web_mouse_left, (), {
    return Module.piMouse && Module.piMouse.left ? 1 : 0;
});

EM_JS(int, _web_key_down, (int key), {
    const arrows = {
        1073741903 : 39,
        1073741904 : 37,
        1073741905 : 40,
        1073741906 : 38
    };

    return Module.piKeys && !!Module.piKeys[arrows[key] || key];
});

EM_JS(void, _web_set_cursor, (int visible), {
    if (Module.canvas)
    {
        Module.canvas.style.cursor = visible ? 'default' : 'none';
    }
});

EM_JS(void, _web_start_loop, (), {
    if (Module.piFrameLoop)
    {
        cancelAnimationFrame(Module.piFrameLoop);
    }

    const tick = () =>
    {
        if (Module._dw_web_runFrame())
        {
            Module.piFrameLoop = requestAnimationFrame(tick);
        }
        else
        {
            Module.piFrameLoop = 0;
        }
    };

    Module.piFrameLoop = requestAnimationFrame(tick);
});

EM_JS(void, _web_stop_loop, (), {
    if (Module.piFrameLoop)
    {
        cancelAnimationFrame(Module.piFrameLoop);
        Module.piFrameLoop = 0;
    }
});

#endif

#include "../src/common.h"
#include "pi_builtin.h"
#include "pi_kwargs.h"
#include "pi_plot3d.h"
#include "../src/pi_func.h"
#include "../src/pi_table.h"
#include "../src/pi_package.h"

typedef struct TransformState
{
    float tx, ty;
    float sx, sy;
    float angle;
    float alpha;
    struct TransformState *next;
} TransformState;

#define EVENT_QUEUE_CAPACITY 256
static void _pump_events(VM *vm, PiContext *ctx);

#ifdef __EMSCRIPTEN__
static PiContext *web_loop_context = NULL;

static int _web_runFrame(void)
{
    PiContext *ctx = web_loop_context;
    VM *vm = ctx ? ctx->vm : NULL;
    if (!ctx || !vm || !ctx->running || ctx->cleaned)
    {
        if (web_loop_context == ctx)
            web_loop_context = NULL;
        return FALSE;
    }
    _pump_events(vm, ctx);
    if (ctx->running && !IS_NIL(ctx->frame_callback))
    {
        vm->running = TRUE;
        Value args[1] = {NEW_OBJ((Object *)ctx)};
        call_func(vm, AS_FUN(ctx->frame_callback), 1, args, NEW_NIL());
        if (!vm->running)
        {
            dw_cleanupContext(ctx);
            return FALSE;
        }
        vm->running = FALSE;
    }
    if (ctx->running)
    {
        pt3d_RedrawContext(ctx);
#ifndef __EMSCRIPTEN__
        SDL_RenderPresent(ctx->renderer);
#endif
    }
    if (!ctx->running)
    {
        dw_cleanupContext(ctx);
        return FALSE;
    }
    return TRUE;
}

EMSCRIPTEN_KEEPALIVE
int dw_web_runFrame(void)
{
    return _web_runFrame();
}

void dw_stopWebLoop(void)
{
    if (web_loop_context)
        dw_cleanupContext(web_loop_context);
    _web_stop_loop();
}
#endif

static char *_draw_strdup(const char *text)
{
    size_t length = strlen(text) + 1;
    char *copy = (char *)malloc(length);
    if (copy == NULL)
        return NULL;

    memcpy(copy, text, length);
    return copy;
}

static PiContext *_get_ctx(Value v)
{
    if (!IS_CONTEXT(v))
        return NULL;

    PiContext *ctx = AS_CONTEXT(v);
    return ctx->renderer ? ctx : NULL;
}

static bool _try_setWindowIcon(SDL_Window *window, const char *path)
{
    SDL_Surface *icon = IMG_Load(path);
    if (!icon)
        return FALSE;

    SDL_SetWindowIcon(window, icon);
    SDL_FreeSurface(icon);
    return TRUE;
}

static void _set_windowIcon(SDL_Window *window)
{
    if (!window)
        return;

    const char *candidates[] = {
        "pi.ico",
        "../pi.ico",
        "release/pi.ico",
    };

    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++)
    {
        if (_try_setWindowIcon(window, candidates[i]))
            return;
    }

    char *base_path = SDL_GetBasePath();
    if (!base_path)
        return;

    const char *base_candidates[] = {
        "pi.ico",
        "../pi.ico",
    };

    for (size_t i = 0; i < sizeof(base_candidates) / sizeof(base_candidates[0]); i++)
    {
        size_t length = strlen(base_path) + strlen(base_candidates[i]) + 1;
        char *path = (char *)malloc(length);
        if (!path)
            continue;

        snprintf(path, length, "%s%s", base_path, base_candidates[i]);
        bool loaded = _try_setWindowIcon(window, path);
        free(path);

        if (loaded)
        {
            SDL_free(base_path);
            return;
        }
    }

    SDL_free(base_path);
}

static void _set_color(SDL_Renderer *r, int color, float alpha)
{
    Uint8 r_c = (color >> 16) & 255;
    Uint8 g_c = (color >> 8) & 255;
    Uint8 b_c = color & 255;
    Uint8 a_c = (Uint8)(alpha * 255);

    SDL_SetRenderDrawColor(r, r_c, g_c, b_c, a_c);
}

static void _transform(PiContext *ctx, float *x, float *y)
{
    *x = (*x * ctx->sx) + ctx->tx;
    *y = (*y * ctx->sy) + ctx->ty;
}

static Value _map_get(PiMap *map, char *key)
{
    void *item = map ? _ht_get(map->table, key) : NULL;

    // Check if the item was found; if not, return nil
    if (item == NULL)
        return NEW_NIL();

    // Return the found value
    return *(Value *)item;
}

static void _parse_drawOptions(Value opts, int *color, char **font_path, int *font_size,
                               char **align, bool *bold, bool *italic, float *img_alpha,
                               int *img_w, int *img_h)
{
    if (!IS_MAP(opts))
        return;

    PiMap *table = AS_MAP(opts);

    Value _color = _map_get(table, "color");
    if (!IS_NIL(_color) && IS_NUM(_color))
        *color = (int)AS_NUM(_color);

    Value _font = _map_get(table, "font");
    if (!IS_NIL(_font) && IS_STRING(_font))
        *font_path = AS_CSTRING(_font);

    Value _size = _map_get(table, "size");
    if (!IS_NIL(_size) && IS_NUM(_size))
        *font_size = (int)AS_NUM(_size);

    Value _align = _map_get(table, "align");
    if (!IS_NIL(_align) && IS_STRING(_align))
        *align = AS_CSTRING(_align);

    Value _bold = _map_get(table, "bold");
    if (!IS_NIL(_bold) && IS_BOOL(_bold))
        *bold = AS_BOOL(_bold);

    Value _italic = _map_get(table, "italic");
    if (!IS_NIL(_italic) && IS_BOOL(_italic))
        *italic = AS_BOOL(_italic);

    Value _alpha = _map_get(table, "alpha");
    if (!IS_NIL(_alpha) && IS_NUM(_alpha))
        *img_alpha = (float)AS_NUM(_alpha);

    Value _w = _map_get(table, "w");
    if (!IS_NIL(_w) && IS_NUM(_w))
        *img_w = (int)AS_NUM(_w);

    Value _h = _map_get(table, "h");
    if (!IS_NIL(_h) && IS_NUM(_h))
        *img_h = (int)AS_NUM(_h);
}

static EventType _get_eventType(const char *name)
{
    if (strcmp(name, "keydown") == 0)
        return EVENT_KEYDOWN;
    if (strcmp(name, "keyup") == 0)
        return EVENT_KEYUP;
    if (strcmp(name, "mousedown") == 0)
        return EVENT_MOUSEDOWN;
    if (strcmp(name, "mouseup") == 0)
        return EVENT_MOUSEUP;
    if (strcmp(name, "mousemove") == 0)
        return EVENT_MOUSEMOVE;
    if (strcmp(name, "click") == 0)
        return EVENT_CLICK;
    if (strcmp(name, "scroll") == 0)
        return EVENT_SCROLL;
    if (strcmp(name, "resize") == 0)
        return EVENT_RESIZE;
    if (strcmp(name, "close") == 0)
        return EVENT_CLOSE;
    return -1;
}

static const char *_get_eventName(EventType type)
{
    static const char *names[] = {
        "keydown", "keyup", "mousedown", "mouseup",
        "mousemove", "click", "scroll", "resize", "close"};
    if (type >= 0 && type < EVENT_COUNT)
        return names[type];
    return "unknown";
}

static void _init_handlerList(HandlerList *list, const char *name)
{
    int i = 0;
    while (i < 31 && name[i] != '\0')
    {
        list->name[i] = name[i];
        i++;
    }
    list->name[i] = '\0';
    list->count = 0;
    for (int i = 0; i < MAX_HANDLERS; i++)
    {
        list->fns[i] = NULL;
    }
}

static void _init_eventQueue(PiContext *ctx)
{
    ctx->event_queue = (EventQueue *)malloc(sizeof(EventQueue));
    ctx->event_queue->capacity = EVENT_QUEUE_CAPACITY;
    ctx->event_queue->events = (PiEvent **)calloc(ctx->event_queue->capacity, sizeof(PiEvent *));
    ctx->event_queue->head = 0;
    ctx->event_queue->tail = 0;
    ctx->event_queue->count = 0;
}

static void _init_eventSystem(PiContext *ctx)
{
    for (int i = 0; i < EVENT_COUNT; i++)
        _init_handlerList(&ctx->handlers[i], _get_eventName((EventType)i));

    _init_eventQueue(ctx);
}

static PiEvent *_clone_event(const PiEvent *src)
{
    PiEvent *copy = (PiEvent *)new_event(src->type, src->event_type);

    copy->x = src->x;
    copy->y = src->y;
    copy->dx = src->dx;
    copy->dy = src->dy;
    copy->button = src->button;
    copy->pressed = src->pressed;
    copy->width = src->width;
    copy->height = src->height;

    if (src->key != NULL)
    {
        free(copy->key);
        copy->key = _draw_strdup(src->key);
    }

    return copy;
}

static void _free_event(PiEvent *e)
{
    if (e == NULL)
        return;

    free(e->type);
    free(e->key);
    free(e);
}

static PiEvent *_pop_event(PiContext *ctx);

static void _push_event(PiContext *ctx, PiEvent *e)
{
    if (ctx->event_queue == NULL || ctx->event_queue->events == NULL)
    {
        _init_eventQueue(ctx);
    }

    // Keep queued events bounded. draw.run() already dispatches every event,
    // while draw.poll() is optional, so an unconsumed queue must not grow for
    // the lifetime of a graphical application.
    if (ctx->event_queue->count >= ctx->event_queue->capacity)
    {
        PiEvent *oldest = _pop_event(ctx);
        _free_event(oldest);
    }

    // Copy event
    PiEvent *copy = _clone_event(e);

    ctx->event_queue->events[ctx->event_queue->tail] = copy;
    ctx->event_queue->tail = (ctx->event_queue->tail + 1) % ctx->event_queue->capacity;
    ctx->event_queue->count++;
}

static PiEvent *_pop_event(PiContext *ctx)
{
    if (ctx->event_queue == NULL || !ctx->event_queue->events || ctx->event_queue->count == 0)
        return NULL;

    PiEvent *e = ctx->event_queue->events[ctx->event_queue->head];
    ctx->event_queue->events[ctx->event_queue->head] = NULL;
    ctx->event_queue->head = (ctx->event_queue->head + 1) % ctx->event_queue->capacity;
    ctx->event_queue->count--;

    return e;
}

static void _clear_eventQueue(PiContext *ctx)
{
    if (ctx->event_queue == NULL || !ctx->event_queue->events)
        return;

    while (ctx->event_queue->count > 0)
    {
        PiEvent *e = _pop_event(ctx);
        _free_event(e);
    }
}

static void _cleanup_events(PiContext *ctx)
{
    // Clear all handlers
    for (int i = 0; i < EVENT_COUNT; i++)
    {
        for (int j = 0; j < ctx->handlers[i].count; j++)
        {
            free(ctx->handlers[i].fns[j]);
            ctx->handlers[i].fns[j] = NULL;
        }
        ctx->handlers[i].count = 0;
    }

    _clear_eventQueue(ctx);
    if (ctx->event_queue != NULL && ctx->event_queue->events)
    {
        free(ctx->event_queue->events);
        ctx->event_queue->events = NULL;
    }
    if (ctx->event_queue != NULL)
    {
        free(ctx->event_queue);
        ctx->event_queue = NULL;
    }
}

typedef struct DrawFontCacheEntry
{
    int size;
    int style;
    TTF_Font *font;
    struct DrawFontCacheEntry *next;
} DrawFontCacheEntry;

typedef struct DrawImageCacheEntry
{
    char *path;
    SDL_Texture *texture;
    int width;
    int height;
    struct DrawImageCacheEntry *next;
} DrawImageCacheEntry;

static void _cleanup_fontCache(PiContext *ctx)
{
    DrawFontCacheEntry *entry = (DrawFontCacheEntry *)ctx->userdata;
    while (entry)
    {
        DrawFontCacheEntry *next = entry->next;
        TTF_CloseFont(entry->font);
        free(entry);
        entry = next;
    }
    ctx->userdata = NULL;
}

static void _cleanup_imageCache(PiContext *ctx)
{
    DrawImageCacheEntry *entry = (DrawImageCacheEntry *)ctx->image_cache;
    while (entry)
    {
        DrawImageCacheEntry *next = entry->next;
        SDL_DestroyTexture(entry->texture);
        free(entry->path);
        free(entry);
        entry = next;
    }
    ctx->image_cache = NULL;
}

static DrawImageCacheEntry *_load_cachedImage(VM *vm, PiContext *ctx, const char *path)
{
    for (DrawImageCacheEntry *entry = (DrawImageCacheEntry *)ctx->image_cache;
         entry != NULL; entry = entry->next)
    {
        if (strcmp(entry->path, path) == 0)
            return entry;
    }

    SDL_Surface *surface = IMG_Load(path);
    if (!surface)
        vm_errorf(vm, "Failed to load image '%s': %s", path, IMG_GetError());

    SDL_Texture *texture = SDL_CreateTextureFromSurface(ctx->renderer, surface);
    SDL_FreeSurface(surface);
    if (!texture)
        vm_errorf(vm, "Failed to create texture: %s", SDL_GetError());

    DrawImageCacheEntry *entry = malloc(sizeof(*entry));
    if (!entry)
    {
        SDL_DestroyTexture(texture);
        vm_error(vm, "Out of memory caching image texture.");
    }
    entry->path = _draw_strdup(path);
    if (!entry->path)
    {
        SDL_DestroyTexture(texture);
        free(entry);
        vm_error(vm, "Out of memory caching image path.");
    }
    entry->texture = texture;
    SDL_QueryTexture(texture, NULL, NULL, &entry->width, &entry->height);
    entry->next = (DrawImageCacheEntry *)ctx->image_cache;
    ctx->image_cache = entry;
    return entry;
}

void dw_cleanupContext(PiContext *ctx)
{
    if (ctx == NULL || ctx->cleaned)
        return;

#ifndef __EMSCRIPTEN__
    if (ctx->gl_context != NULL)
    {
        d3_cleanupContext(ctx);
        return;
    }
#endif

    // draw.run() releases these resources when the window closes, then the
    // context is released again later by the VM's garbage collector.  Mark it
    // first so both paths are safe even if shutdown is re-entered.
    ctx->cleaned = TRUE;
    ctx->running = FALSE;
#ifdef __EMSCRIPTEN__
    if (web_loop_context == ctx)
    {
        web_loop_context = NULL;
        _web_stop_loop();
    }
#endif
    ctx->frame_callback = NEW_NIL();
    ctx->active_plot3d = NULL;
    ctx->font = NULL;

    while (ctx->_transform_stack != NULL)
    {
        TransformState *state = (TransformState *)ctx->_transform_stack;
        ctx->_transform_stack = state->next;
        free(state);
    }

    _cleanup_events(ctx);
    _cleanup_fontCache(ctx);
    _cleanup_imageCache(ctx);

    if (ctx->renderer != NULL)
    {
        SDL_DestroyRenderer((SDL_Renderer *)ctx->renderer);
        ctx->renderer = NULL;
    }
    if (ctx->window != NULL)
    {
        SDL_DestroyWindow((SDL_Window *)ctx->window);
        ctx->window = NULL;
    }
}

static Value _event_toValue(VM *vm, PiEvent *event)
{
    return NEW_OBJ(add_obj(vm, (Object *)event));
}

static void _dispatch_event(VM *vm, PiContext *ctx, PiEvent *event)
{
    HandlerList *handlers = &ctx->handlers[event->event_type];
    if (handlers->count == 0)
        return;

    PiEvent *event_copy = _clone_event(event);
    Value event_obj = _event_toValue(vm, event_copy);

    for (int i = 0; i < handlers->count; i++)
    {
        if (handlers->fns[i] == NULL || IS_NIL(*(handlers->fns[i])))
            continue;

        Value handler = *(handlers->fns[i]);
        if (!IS_FUN(handler))
            continue;

        Value args[1] = {event_obj};
        call_func(vm, AS_FUN(handler), 1, args, NEW_NIL());
    }
}

static PiEvent *_translate_sdlEvent(const SDL_Event *event)
{
    PiEvent *pi_event = NULL;

    switch (event->type)
    {
    case SDL_QUIT:
        pi_event = (PiEvent *)new_event("close", EVENT_CLOSE);
        break;

    case SDL_KEYDOWN:
    case SDL_KEYUP:
        pi_event = (PiEvent *)new_event(
            event->type == SDL_KEYDOWN ? "keydown" : "keyup",
            event->type == SDL_KEYDOWN ? EVENT_KEYDOWN : EVENT_KEYUP);
        pi_event->pressed = (event->type == SDL_KEYDOWN);
        pi_event->key = _draw_strdup(SDL_GetKeyName(event->key.keysym.sym));
        break;

    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
        pi_event = (PiEvent *)new_event(
            event->type == SDL_MOUSEBUTTONDOWN ? "mousedown" : "mouseup",
            event->type == SDL_MOUSEBUTTONDOWN ? EVENT_MOUSEDOWN : EVENT_MOUSEUP);
        pi_event->x = event->button.x;
        pi_event->y = event->button.y;
        pi_event->button = event->button.button;
        pi_event->pressed = (event->type == SDL_MOUSEBUTTONDOWN);
        break;

    case SDL_MOUSEMOTION:
        pi_event = (PiEvent *)new_event("mousemove", EVENT_MOUSEMOVE);
        pi_event->x = event->motion.x;
        pi_event->y = event->motion.y;
        pi_event->dx = event->motion.xrel;
        pi_event->dy = event->motion.yrel;
        break;

    case SDL_MOUSEWHEEL:
        pi_event = (PiEvent *)new_event("scroll", EVENT_SCROLL);
        pi_event->dx = event->wheel.x;
        pi_event->dy = event->wheel.y;
        break;

    case SDL_WINDOWEVENT:
        if (event->window.event == SDL_WINDOWEVENT_CLOSE)
        {
            pi_event = (PiEvent *)new_event("close", EVENT_CLOSE);
        }
        else if (event->window.event == SDL_WINDOWEVENT_RESIZED ||
                 event->window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
        {
            pi_event = (PiEvent *)new_event("resize", EVENT_RESIZE);
            pi_event->width = event->window.data1;
            pi_event->height = event->window.data2;
        }
        break;
    }

    return pi_event;
}

static void _pump_events(VM *vm, PiContext *ctx)
{
    SDL_Event sdl_event;

#ifdef __EMSCRIPTEN__
    while (SDL_PeepEvents(&sdl_event, 1, SDL_GETEVENT,
                          SDL_FIRSTEVENT, SDL_LASTEVENT) > 0)
#else
    while (SDL_PollEvent(&sdl_event))
#endif
    {
        bool closing = sdl_event.type == SDL_QUIT ||
                       (sdl_event.type == SDL_WINDOWEVENT &&
                        sdl_event.window.event == SDL_WINDOWEVENT_CLOSE) ||
                       (sdl_event.type == SDL_KEYDOWN &&
                        sdl_event.key.keysym.sym == SDLK_ESCAPE);
        if (closing)
            ctx->running = FALSE;

        if (sdl_event.type == SDL_WINDOWEVENT &&
            (sdl_event.window.event == SDL_WINDOWEVENT_RESIZED ||
             sdl_event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED))
        {
            ctx->width = sdl_event.window.data1;
            ctx->height = sdl_event.window.data2;
        }

        PiEvent *event = _translate_sdlEvent(&sdl_event);
        if (event != NULL)
        {
            _push_event(ctx, event);
            _dispatch_event(vm, ctx, event);
            _free_event(event);
        }

        // Do not dispatch stale window events after the application asks to close.
        if (closing)
            break;
    }
}

Value dw_canvas(VM *vm, int argc, Value *argv)
{

    if (!IS_NUM(argv[0]) || !IS_NUM(argv[1]))
        vm_error(vm, "canvas() takes two numbers as arguments");

    int w = as_number(argv[0]);
    int h = as_number(argv[1]);

    const char *title = (argc > 2 && IS_STRING(argv[2]))
                            ? AS_CSTRING(argv[2])
                            : "PiLang";
    const char *icon_path = NULL;
    if (argc > 3 && !IS_NIL(argv[3]))
    {
        if (!IS_STRING(argv[3]))
            vm_error(vm, "canvas() icon must be an image path");
        icon_path = AS_CSTRING(argv[3]);
    }
    bool show_launch = TRUE;
    if (argc > 4 && !IS_NIL(argv[4]))
    {
        if (!IS_BOOL(argv[4]))
            vm_error(vm, "canvas() launch flag must be a boolean");
        show_launch = AS_BOOL(argv[4]);
    }

    /*
     * canvas() is a configuration-style API, so accept names for its
     * optional trailing arguments.  Positional calls deliberately retain
     * their existing fast path and behavior.
     */
    PiMap *kwargs = _kw_map(vm);
    if (kwargs)
    {
        Value value;
        if (_kw_get(kwargs, "title", &value) && !IS_NIL(value))
        {
            if (!IS_STRING(value))
                vm_error(vm, "canvas() title must be a string");
            title = AS_CSTRING(value);
        }
        if (_kw_get(kwargs, "icon", &value) && !IS_NIL(value))
        {
            if (!IS_STRING(value))
                vm_error(vm, "canvas() icon must be an image path");
            icon_path = AS_CSTRING(value);
        }
        if (_kw_get(kwargs, "show_launch", &value) && !IS_NIL(value))
        {
            if (!IS_BOOL(value))
                vm_error(vm, "canvas() launch flag must be a boolean");
            show_launch = AS_BOOL(value);
        }
    }

#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
    if (show_launch)
        package_showLaunchScreen(w, h, title);
#endif

    PiContext *ctx = (PiContext *)new_context();
#ifdef __EMSCRIPTEN__
    _web_canvas_init(w, h);
    ctx->renderer = ctx;
    ctx->width = w;
    ctx->height = h;
    _init_eventSystem(ctx);
    return NEW_OBJ(add_obj(vm, (Object *)ctx));
#else

    ctx->window = SDL_CreateWindow(title,
                                   SDL_WINDOWPOS_UNDEFINED,
                                   SDL_WINDOWPOS_UNDEFINED,
                                   w, h,
                                   SDL_WINDOW_HIDDEN);

    if (!ctx->window)
    {
        free(ctx);
        vm_errorf(vm, "Failed to create window: %s", SDL_GetError());
    }

    if (icon_path)
    {
        if (!_try_setWindowIcon((SDL_Window *)ctx->window, icon_path))
        {
            SDL_DestroyWindow((SDL_Window *)ctx->window);
            free(ctx);
            vm_errorf(vm, "Failed to load window icon '%s': %s", icon_path, IMG_GetError());
        }
    }
    else
    {
        _set_windowIcon((SDL_Window *)ctx->window);
    }

    SDL_ShowWindow((SDL_Window *)ctx->window);

    ctx->renderer = SDL_CreateRenderer(
        (SDL_Window *)ctx->window,
        -1,
        SDL_RENDERER_ACCELERATED);

    if (!ctx->renderer)
    {
        SDL_DestroyWindow(ctx->window);
        free(ctx);
        vm_errorf(vm, "Failed to create renderer: %s", SDL_GetError());
    }

    ctx->width = w;
    ctx->height = h;
    ctx->running = TRUE;
    ctx->alpha = 1.0f; // Initialize alpha
    ctx->tx = 0.0f;    // Initialize _transform
    ctx->ty = 0.0f;
    ctx->sx = 1.0f;
    ctx->sy = 1.0f;
    _init_eventSystem(ctx);

    return NEW_OBJ(add_obj(vm, (Object *)ctx));
#endif
}

Value dw_run(VM *vm, int argc, Value *argv)
{
    PiContext *ctx = _get_ctx(argv[0]);
    if (ctx == NULL)
        vm_error(vm, "run() takes a canvas as first argument");

#ifdef __EMSCRIPTEN__
    if (web_loop_context && web_loop_context != ctx)
        dw_stopWebLoop();
    ctx->vm = vm;
    web_loop_context = ctx;
    _web_start_loop();
    return NEW_NIL();
#else
    SDL_Event e;
    while (ctx->running)
    {
        _pump_events(vm, ctx);
        if (!ctx->running)
            break;

        if (!IS_NIL(ctx->frame_callback))
        {
            Value args[1] = {argv[0]};
            Function *func = AS_FUN(ctx->frame_callback);
            call_func(vm, func, 1, args, NEW_NIL());
        }
        if (!ctx->running)
            break;

        pt3d_RedrawContext(ctx);

        SDL_RenderPresent(ctx->renderer);

        // Cap frame rate to 60 FPS
        SDL_Delay(16);
    }

    dw_cleanupContext(ctx);

    return NEW_NIL();
#endif
}

Value dw_clear(VM *vm, int argc, Value *argv)
{
    PiContext *ctx = _get_ctx(argv[0]);
    if (!ctx)
        vm_error(vm, "clear() takes a canvas as first argument");

    int color = (argc > 1) ? as_number(argv[1]) : 0x000000;
    _set_color(ctx->renderer, color, ctx->alpha);
    SDL_RenderClear((SDL_Renderer *)ctx->renderer);
    ctx->plot_subplots_cleared = FALSE;

    return argv[0];
}

static void _draw_circle(SDL_Renderer *r, int cx, int cy, int radius)
{
    int x = radius;
    int y = 0;
    int err = 0;

    while (x >= y)
    {
        SDL_RenderDrawPoint(r, cx + x, cy + y);
        SDL_RenderDrawPoint(r, cx + y, cy + x);
        SDL_RenderDrawPoint(r, cx - y, cy + x);
        SDL_RenderDrawPoint(r, cx - x, cy + y);
        SDL_RenderDrawPoint(r, cx - x, cy - y);
        SDL_RenderDrawPoint(r, cx - y, cy - x);
        SDL_RenderDrawPoint(r, cx + y, cy - x);
        SDL_RenderDrawPoint(r, cx + x, cy - y);

        if (err <= 0)
        {
            y++;
            err += 2 * y + 1;
        }
        if (err > 0)
        {
            x--;
            err -= 2 * x + 1;
        }
    }
}

static void _draw_filledCircle(SDL_Renderer *r, int cx, int cy, int radius)
{
    for (int y = -radius; y <= radius; y++)
    {
        int x = (int)sqrt(radius * radius - y * y);
        SDL_RenderDrawLine(r, cx - x, cy + y, cx + x, cy + y);
    }
}

Value dw_pixel(VM *vm, int argc, Value *argv)
{
    // Usage: pixel(canvas, x, y, color)
    PiContext *ctx = _get_ctx(argv[0]);
    if (!ctx)
        vm_error(vm, "pixel() takes a canvas as first argument");

    if (argc < 3)
        vm_error(vm, "pixel() needs at least x and y coordinates");

    float x = as_number(argv[1]);
    float y = as_number(argv[2]);
    int color = (argc > 3) ? as_number(argv[3]) : 0xffffff;

    _transform(ctx, &x, &y);
    _set_color(ctx->renderer, color, ctx->alpha);
    SDL_RenderDrawPoint((SDL_Renderer *)ctx->renderer, (int)x, (int)y);

    return argv[0];
}

Value dw_line(VM *vm, int argc, Value *argv)
{
    // Usage: line(canvas, x1, y1, x2, y2, color)
    PiContext *ctx = _get_ctx(argv[0]);
    if (!ctx)
        vm_error(vm, "line() takes a canvas as first argument");

    if (argc < 5)
        vm_error(vm, "line() needs x1, y1, x2, y2 coordinates");

    float x1 = as_number(argv[1]);
    float y1 = as_number(argv[2]);
    float x2 = as_number(argv[3]);
    float y2 = as_number(argv[4]);
    int color = (argc > 5) ? as_number(argv[5]) : 0xffffff;

    _transform(ctx, &x1, &y1);
    _transform(ctx, &x2, &y2);
    _set_color(ctx->renderer, color, ctx->alpha);
    SDL_RenderDrawLine((SDL_Renderer *)ctx->renderer, (int)x1, (int)y1, (int)x2, (int)y2);

    return argv[0];
}

Value dw_triangle(VM *vm, int argc, Value *argv)
{
    // Usage: Triangle(canvas, x1, y1, x2, y2, x3, y3, color, filled)
    PiContext *ctx = _get_ctx(argv[0]);
    if (!ctx)
        vm_error(vm, "Triangle() takes a canvas as first argument");

    if (argc < 7)
        vm_error(vm, "Triangle() needs x1,y1,x2,y2,x3,y3 coordinates");

    float x1 = as_number(argv[1]);
    float y1 = as_number(argv[2]);
    float x2 = as_number(argv[3]);
    float y2 = as_number(argv[4]);
    float x3 = as_number(argv[5]);
    float y3 = as_number(argv[6]);
    int color = (argc > 7) ? as_number(argv[7]) : 0xffffff;
    bool filled = (argc > 8) ? as_bool(argv[8]) : FALSE;

    _transform(ctx, &x1, &y1);
    _transform(ctx, &x2, &y2);
    _transform(ctx, &x3, &y3);
    _set_color(ctx->renderer, color, ctx->alpha);

    if (filled)
    {
        // Draw filled Triangle using geometry rendering
        SDL_Vertex vertices[3];
        SDL_Color col = {
            (Uint8)((color >> 16) & 255),
            (Uint8)((color >> 8) & 255),
            (Uint8)(color & 255),
            (Uint8)(ctx->alpha * 255)};

        vertices[0].position.x = x1;
        vertices[0].position.y = y1;
        vertices[0].color = col;
        vertices[0].tex_coord.x = 0;
        vertices[0].tex_coord.y = 0;

        vertices[1].position.x = x2;
        vertices[1].position.y = y2;
        vertices[1].color = col;
        vertices[1].tex_coord.x = 0;
        vertices[1].tex_coord.y = 0;

        vertices[2].position.x = x3;
        vertices[2].position.y = y3;
        vertices[2].color = col;
        vertices[2].tex_coord.x = 0;
        vertices[2].tex_coord.y = 0;

        SDL_RenderGeometry((SDL_Renderer *)ctx->renderer, NULL, vertices, 3, NULL, 0);
    }
    else
    {
        // Draw outline
        SDL_RenderDrawLine((SDL_Renderer *)ctx->renderer, (int)x1, (int)y1, (int)x2, (int)y2);
        SDL_RenderDrawLine((SDL_Renderer *)ctx->renderer, (int)x2, (int)y2, (int)x3, (int)y3);
        SDL_RenderDrawLine((SDL_Renderer *)ctx->renderer, (int)x3, (int)y3, (int)x1, (int)y1);
    }

    return argv[0];
}

Value dw_rect(VM *vm, int argc, Value *argv)
{
    // Usage: rect(canvas, x, y, width, height, color, filled)
    PiContext *ctx = _get_ctx(argv[0]);
    if (!ctx)
        vm_error(vm, "rect() takes a canvas as first argument");

    if (argc < 5)
        vm_error(vm, "rect() needs x, y, width, height");

    float x = as_number(argv[1]);
    float y = as_number(argv[2]);
    float w = as_number(argv[3]);
    float h = as_number(argv[4]);
    int color = (argc > 5) ? as_number(argv[5]) : 0xffffff;
    bool filled = (argc > 6) ? as_bool(argv[6]) : FALSE;

    // _transform the rectangle corners
    float x1 = x;
    float y1 = y;
    float x2 = x + w;
    float y2 = y + h;

    _transform(ctx, &x1, &y1);
    _transform(ctx, &x2, &y2);
    _set_color(ctx->renderer, color, ctx->alpha);

    // Calculate _transformed rectangle
    float min_x = fminf(x1, x2);
    float min_y = fminf(y1, y2);
    float max_x = fmaxf(x1, x2);
    float max_y = fmaxf(y1, y2);

    SDL_FRect rect = {min_x, min_y, max_x - min_x, max_y - min_y};

    if (filled)
        SDL_RenderFillRectF((SDL_Renderer *)ctx->renderer, &rect);
    else
        SDL_RenderDrawRectF((SDL_Renderer *)ctx->renderer, &rect);

    return argv[0];
}

Value dw_polygon(VM *vm, int argc, Value *argv)
{
    // Usage: polygon(canvas, points, color, filled)
    // points is a list of [x1, y1, x2, y2, ...] or list of [x,y] pairs
    PiContext *ctx = _get_ctx(argv[0]);
    if (!ctx)
        vm_error(vm, "polygon() takes a canvas as first argument");

    if (argc < 2)
        vm_error(vm, "polygon() needs a points list");

    // Parse points list
    Value points_val = argv[1];
    if (!IS_LIST(points_val))
        vm_error(vm, "polygon() second argument must be a list");

    List *list = AS_CLIST(points_val);
    int point_count = PILIST_SIZE(points_val);

    if (point_count < 3)
        vm_error(vm, "polygon() needs at least 3 points");

    // Extract points
    SDL_FPoint *points = (SDL_FPoint *)malloc(point_count * sizeof(SDL_FPoint));
    if (!points)
        vm_error(vm, "Out of memory for polygon points");

    for (int i = 0; i < point_count; i++)
    {
        Value pair = *(Value *)_list_getAt(list, i);
        if (!IS_LIST(pair) || as_list(pair)->size < 2)
        {
            free(points);
            vm_error(vm, "Each point must be a list of [x, y]");
        }

        List *point = as_list(pair);
        float px = as_number(*(Value *)_list_getAt(point, 0));
        float py = as_number(*(Value *)_list_getAt(point, 1));
        _transform(ctx, &px, &py);
        points[i].x = px;
        points[i].y = py;
    }

    int color = (argc > 2) ? as_number(argv[2]) : 0xffffff;
    bool filled = (argc > 3) ? as_bool(argv[3]) : FALSE;

    _set_color(ctx->renderer, color, ctx->alpha);

    if (filled)
    {
        // For filled polygon, we need to triangulate or use a different approach
        // Using SDL_RenderGeometry requires triangulation
        // Simple approach: draw as a Triangle fan from first point
        for (int i = 1; i < point_count - 1; i++)
        {
            SDL_Vertex vertices[3];
            SDL_Color col = {
                (Uint8)((color >> 16) & 255),
                (Uint8)((color >> 8) & 255),
                (Uint8)(color & 255),
                (Uint8)(ctx->alpha * 255)};

            vertices[0].position = points[0];
            vertices[0].color = col;
            vertices[0].tex_coord.x = 0;
            vertices[0].tex_coord.y = 0;

            vertices[1].position = points[i];
            vertices[1].color = col;
            vertices[1].tex_coord.x = 0;
            vertices[1].tex_coord.y = 0;

            vertices[2].position = points[i + 1];
            vertices[2].color = col;
            vertices[2].tex_coord.x = 0;
            vertices[2].tex_coord.y = 0;

            SDL_RenderGeometry((SDL_Renderer *)ctx->renderer, NULL, vertices, 3, NULL, 0);
        }
    }
    else
    {
        // Draw outline
        for (int i = 0; i < point_count - 1; i++)
        {
            SDL_RenderDrawLineF((SDL_Renderer *)ctx->renderer,
                                points[i].x, points[i].y,
                                points[i + 1].x, points[i + 1].y);
        }

        SDL_RenderDrawLineF((SDL_Renderer *)ctx->renderer,
                            points[point_count - 1].x, points[point_count - 1].y,
                            points[0].x, points[0].y);
    }

    free(points);
    return argv[0];
}

Value dw_circle(VM *vm, int argc, Value *argv)
{
    // Usage: circle(canvas, x, y, radius, color, filled)
    PiContext *ctx = _get_ctx(argv[0]);
    if (!ctx)
        vm_error(vm, "circle() takes a canvas as first argument");

    if (argc < 4)
        vm_error(vm, "circle() needs x, y, radius");

    float x = as_number(argv[1]);
    float y = as_number(argv[2]);
    float radius = as_number(argv[3]);
    int color = (argc > 4) ? as_number(argv[4]) : 0xffffff;
    bool filled = (argc > 5) ? as_bool(argv[5]) : FALSE;

    _transform(ctx, &x, &y);

    // Apply scale to radius
    float scale = (fabsf(ctx->sx) + fabsf(ctx->sy)) * 0.5f;
    int final_radius = (int)(radius * scale);
    if (final_radius < 1)
        final_radius = 1;

    _set_color(ctx->renderer, color, ctx->alpha);

    if (filled)
        _draw_filledCircle((SDL_Renderer *)ctx->renderer, (int)x, (int)y, final_radius);
    else
        _draw_circle((SDL_Renderer *)ctx->renderer, (int)x, (int)y, final_radius);

    return argv[0];
}

Value dw_translate(VM *vm, int argc, Value *argv)
{
    PiContext *ctx = _get_ctx(argv[0]);
    if (!ctx)
        return NEW_NIL();

    ctx->tx += as_number(argv[1]);
    ctx->ty += as_number(argv[2]);
    return argv[0];
}

Value dw_scale(VM *vm, int argc, Value *argv)
{
    PiContext *ctx = _get_ctx(argv[0]);
    if (!ctx)
        return NEW_NIL();

    ctx->sx *= as_number(argv[1]);
    ctx->sy *= as_number(argv[2]);
    return argv[0];
}

Value dw_rotate(VM *vm, int argc, Value *argv)
{
    PiContext *ctx = _get_ctx(argv[0]);
    if (!ctx)
        return NEW_NIL();

    ctx->angle += as_number(argv[1]);
    return argv[0];
}

Value dw_alpha(VM *vm, int argc, Value *argv)
{
    PiContext *ctx = _get_ctx(argv[0]);
    if (!ctx)
        return NEW_NIL();

    ctx->alpha = as_number(argv[1]);
    return argv[0];
}

static TTF_Font *_load_styledFont(const char *font_path, int size, bool bold, bool italic)
{
    if (!font_path)
        return NULL;

    int style = TTF_STYLE_NORMAL;
    if (bold)
        style |= TTF_STYLE_BOLD;
    if (italic)
        style |= TTF_STYLE_ITALIC;

    TTF_Font *font = TTF_OpenFont(font_path, size);
    if (font)
        TTF_SetFontStyle(font, style);

    return font;
}

static TTF_Font *_load_defaultFont(PiContext *ctx, int size, bool bold, bool italic)
{
    int style = TTF_STYLE_NORMAL;
    if (bold)
        style |= TTF_STYLE_BOLD;
    if (italic)
        style |= TTF_STYLE_ITALIC;

    for (DrawFontCacheEntry *entry = (DrawFontCacheEntry *)ctx->userdata;
         entry != NULL; entry = entry->next)
    {
        if (entry->size == size && entry->style == style)
            return entry->font;
    }

    const char *default_fonts[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "/System/Library/Fonts/Helvetica.ttc"};
    TTF_Font *font = NULL;
    for (int i = 0; i < 3; i++)
    {
        font = TTF_OpenFont(default_fonts[i], size);
        if (font)
            break;
    }
    if (!font)
        return NULL;

    TTF_SetFontStyle(font, style);
    DrawFontCacheEntry *entry = malloc(sizeof(DrawFontCacheEntry));
    if (!entry)
    {
        TTF_CloseFont(font);
        return NULL;
    }

    entry->size = size;
    entry->style = style;
    entry->font = font;
    entry->next = (DrawFontCacheEntry *)ctx->userdata;
    ctx->userdata = entry;
    return font;
}

static void _get_textDimensions(TTF_Font *font, const char *text, int *w, int *h)
{
    if (!font || !text)
    {
        if (w)
            *w = 0;
        if (h)
            *h = 0;
        return;
    }

    TTF_SizeUTF8(font, text, w, h);
}

static void _align_textPosition(int text_w, int text_h, float *x, float *y, const char *align)
{
    if (!align)
        return;

    if (strcmp(align, "center") == 0 || strcmp(align, "middle") == 0)
    {
        *x -= text_w / 2.0f;
        *y -= text_h / 2.0f;
    }
    else if (strcmp(align, "right") == 0 || strcmp(align, "end") == 0)
        *x -= text_w;

    else if (strcmp(align, "bottom") == 0)
        *y -= text_h;

    // "left" and "top" are default, no adjustment needed
}

Value dw_text(VM *vm, int argc, Value *argv)
{
    // Usage: text(canvas, x, y, text, opts)
    // opts: {color, font, size, align, bold, italic}

    PiContext *ctx = _get_ctx(argv[0]);
    if (!ctx)
        vm_error(vm, "text() takes a canvas as first argument");

    if (argc < 4)
        vm_error(vm, "text() needs x, y, and text arguments");

    float x = as_number(argv[1]);
    float y = as_number(argv[2]);
    // Convert any PiLang value to display text. AS_CSTRING assumes an object
    // string and can dereference invalid memory for numbers (for example a
    // score counter), which previously terminated the process.
    char *text = as_stringWithFormat(vm, argv[3]);
    if (!text)
        vm_error(vm, "text() could not format its text argument");

    // Default options
    int color = 0xffffff;
    char *font_path = NULL;
    int font_size = 16;
    char *align = NULL;
    bool bold = FALSE;
    bool italic = FALSE;

    if (argc > 4 && IS_MAP(argv[4]))
        _parse_drawOptions(argv[4], &color, &font_path, &font_size, &align, &bold, &italic, NULL, NULL, NULL);

#ifdef __EMSCRIPTEN__
    _web_canvas_text(text, x, y, color, font_size, bold, italic,
                     align ? align : "", ctx->alpha);
    free(text);
    return NEW_NIL();
#endif

    TTF_Font *font = NULL;
    if (font_path)
    {
        font = _load_styledFont(font_path, font_size, bold, italic);
    }
    else if (ctx->font)
    {
        font = ctx->font;
        // Apply style to existing font
        int style = TTF_STYLE_NORMAL;
        if (bold)
            style |= TTF_STYLE_BOLD;
        if (italic)
            style |= TTF_STYLE_ITALIC;
        TTF_SetFontStyle(font, style);
    }
    else
    {
        font = _load_defaultFont(ctx, font_size, bold, italic);
        if (!font)
            vm_error(vm, "Failed to load any font. Please specify a font path.");
    }

    int text_w, text_h;
    _get_textDimensions(font, text, &text_w, &text_h);

    _align_textPosition(text_w, text_h, &x, &y, align);

    _transform(ctx, &x, &y);

    // Create surface and texture
    SDL_Color fg = {
        (Uint8)((color >> 16) & 255),
        (Uint8)((color >> 8) & 255),
        (Uint8)(color & 255),
        (Uint8)(ctx->alpha * 255)};

    SDL_Surface *surface = TTF_RenderUTF8_Blended(font, text, fg);
    if (!surface)
    {
        if (font_path && font)
            TTF_CloseFont(font);
        vm_errorf(vm, "Failed to render text: %s", TTF_GetError());
    }

    SDL_Texture *texture = SDL_CreateTextureFromSurface(ctx->renderer, surface);
    SDL_FreeSurface(surface);
    free(text);

    if (!texture)
    {
        if (font_path && font)
            TTF_CloseFont(font);
        vm_errorf(vm, "Failed to create texture: %s", SDL_GetError());
    }

    SDL_FRect dst = {x, y, (float)text_w, (float)text_h};
    SDL_RenderCopyF(ctx->renderer, texture, NULL, &dst);
    SDL_DestroyTexture(texture);

    if (font_path && font)
        TTF_CloseFont(font);

    return NEW_NIL();
}

Value dw_image(VM *vm, int argc, Value *argv)
{
    // Usage: image(canvas, path, x, y, opts)
    // opts: {w, h, alpha, color, flip_y, angle}

    PiContext *ctx = _get_ctx(argv[0]);
    if (!ctx)
        vm_error(vm, "image() takes a canvas as first argument");

    if (argc < 4)
        vm_error(vm, "image() needs path, x, and y arguments");

    const char *path = AS_CSTRING(argv[1]);
    float x = as_number(argv[2]);
    float y = as_number(argv[3]);

    // Default options
    int color = 0xffffff;
    float img_alpha = -1.0f; // -1 means use context alpha
    int img_w = 0;
    int img_h = 0;
    bool flip_y = FALSE;
    double angle = 0;

    if (argc > 4 && IS_MAP(argv[4]))
    {
        _parse_drawOptions(argv[4], &color, NULL, NULL, NULL, NULL, NULL, &img_alpha, &img_w, &img_h);
        Value flip = _map_get(AS_MAP(argv[4]), "flip_y");
        if (IS_BOOL(flip))
            flip_y = AS_BOOL(flip);
        Value rotation = _map_get(AS_MAP(argv[4]), "angle");
        if (IS_NUM(rotation))
            angle = AS_NUM(rotation);
    }

#ifdef __EMSCRIPTEN__
    _web_canvas_image(path, x, y, img_w > 0 ? img_w : 0, img_h > 0 ? img_h : 0, angle, flip_y);
    return NEW_NIL();
#endif

    _transform(ctx, &x, &y);

    DrawImageCacheEntry *image = _load_cachedImage(vm, ctx, path);

    // Apply resize if specified
    int dst_w = img_w > 0 ? img_w : image->width;
    int dst_h = img_h > 0 ? img_h : image->height;

    // Apply alpha
    Uint8 alpha = (Uint8)((img_alpha >= 0 ? img_alpha : ctx->alpha) * 255);
    SDL_SetTextureAlphaMod(image->texture, alpha);

    // Apply color modulation
    SDL_SetTextureColorMod(image->texture,
                           (Uint8)((color >> 16) & 255),
                           (Uint8)((color >> 8) & 255),
                           (Uint8)(color & 255));

    // Draw
    SDL_FRect dst = {x, y, (float)dst_w, (float)dst_h};
    SDL_RenderCopyExF(ctx->renderer, image->texture, NULL, &dst, angle, NULL,
                      flip_y ? SDL_FLIP_VERTICAL : SDL_FLIP_NONE);

    return NEW_NIL();
}

// Push current _transform state onto stack
Value dw_push(VM *vm, int argc, Value *argv)
{
    // Usage: push(canvas)

    PiContext *ctx = _get_ctx(argv[0]);
    if (!ctx)
        vm_error(vm, "push() takes a canvas as first argument");

    // Create new _transform state
    TransformState *state = (TransformState *)malloc(sizeof(TransformState));
    if (!state)
        vm_error(vm, "Out of memory for _transform state");

    // Save current state
    state->tx = ctx->tx;
    state->ty = ctx->ty;
    state->sx = ctx->sx;
    state->sy = ctx->sy;
    state->angle = ctx->angle;
    state->alpha = ctx->alpha;

    // Push onto stack
    state->next = (TransformState *)ctx->_transform_stack;
    ctx->_transform_stack = state;

    return NEW_NIL();
}

Value dw_pop(VM *vm, int argc, Value *argv)
{
    // Usage: pop(canvas)

    PiContext *ctx = _get_ctx(argv[0]);
    if (!ctx)
        vm_error(vm, "pop() takes a canvas as first argument");

    // Check if stack has items
    if (!ctx->_transform_stack)
        vm_error(vm, "pop() called with no matching push()");

    // Pop from stack
    TransformState *state = (TransformState *)ctx->_transform_stack;
    ctx->_transform_stack = state->next;

    // Restore state
    ctx->tx = state->tx;
    ctx->ty = state->ty;
    ctx->sx = state->sx;
    ctx->sy = state->sy;
    ctx->angle = state->angle;
    ctx->alpha = state->alpha;

    free(state);

    return NEW_NIL();
}

Value dw_mouse(VM *vm, int argc, Value *argv)
{
    PiContext *ctx = _get_ctx(argv[0]);
    if (!ctx)
        vm_error(vm, "mouse() takes a canvas as first argument");

    int x, y;
    Uint32 buttons;
#ifdef __EMSCRIPTEN__
    x = _web_mouse_x();
    y = _web_mouse_y();
    buttons = _web_mouse_left() ? SDL_BUTTON(SDL_BUTTON_LEFT) : 0;
#else
    buttons = SDL_GetMouseState(&x, &y);
#endif

    HT_Table *table = ht_create(sizeof(Value));

    ht_put(table, "x", &NEW_NUM((double)x));
    ht_put(table, "y", &NEW_NUM((double)y));
    ht_put(table, "left", &NEW_BOOL(buttons & SDL_BUTTON(SDL_BUTTON_LEFT)));
    ht_put(table, "middle", &NEW_BOOL(buttons & SDL_BUTTON(SDL_BUTTON_MIDDLE)));
    ht_put(table, "right", &NEW_BOOL(buttons & SDL_BUTTON(SDL_BUTTON_RIGHT)));

    PiMap *map = (PiMap *)new_map(table);

    return NEW_OBJ(map);
}

Value dw_cursor(VM *vm, int argc, Value *argv)
{
    PiContext *ctx = _get_ctx(argv[0]);
    if (!ctx || argc < 2 || !IS_BOOL(argv[1]))
        vm_error(vm, "cursor() takes a canvas and a visibility boolean");

#ifdef __EMSCRIPTEN__
    _web_set_cursor(AS_BOOL(argv[1]));
#else
    SDL_ShowCursor(AS_BOOL(argv[1]) ? SDL_ENABLE : SDL_DISABLE);
#endif
    return NEW_NIL();
}

Value dw_key(VM *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_NUM(argv[1]))
        vm_error(vm, "key() takes a canvas and a key code");

    SDL_Keycode keycode = (SDL_Keycode)as_number(argv[1]);
#ifdef __EMSCRIPTEN__
    return NEW_BOOL(_web_key_down(keycode));
#else
    const Uint8 *state = SDL_GetKeyboardState(NULL);
    SDL_Scancode scancode = SDL_GetScancodeFromKey(keycode);

    // Keep the previous raw-scancode API working for existing programs.
    if (scancode == SDL_SCANCODE_UNKNOWN &&
        keycode >= 0 && keycode < SDL_NUM_SCANCODES)
        scancode = (SDL_Scancode)keycode;

    return NEW_BOOL(scancode != SDL_SCANCODE_UNKNOWN && state[scancode]);
#endif
}

Value dw_size(VM *vm, int argc, Value *argv)
{
    PiContext *ctx = _get_ctx(argv[0]);
    if (!ctx)
        return NEW_NIL();

    List *list = list_create(sizeof(Value));

    _list_add(list, &NEW_NUM((double)ctx->width));
    _list_add(list, &NEW_NUM((double)ctx->height));

    PiList *result = (PiList *)add_obj(vm, new_list(list));

    return NEW_OBJ(result);
}

// Check if window is still running
Value dw_isRunning(VM *vm, int argc, Value *argv)
{
    PiContext *ctx = _get_ctx(argv[0]);
    if (!ctx)
        return NEW_BOOL(FALSE);

    return ctx->running ? NEW_BOOL(TRUE) : NEW_BOOL(FALSE);
}

// Manually present the frame
Value dw_present(VM *vm, int argc, Value *argv)
{
    PiContext *ctx = _get_ctx(argv[0]);
    if (!ctx)
        vm_error(vm, "present() takes a canvas as first argument");

    SDL_RenderPresent(ctx->renderer);
    return argv[0];
}

// Add this function to pi_draw.c
Value dw_animate(VM *vm, int argc, Value *argv)
{
    PiContext *ctx = _get_ctx(argv[0]);
    if (!ctx)
        vm_error(vm, "animate() takes a canvas as first argument");

    if (argc < 2)
        vm_error(vm, "animate() needs a callback function");

    if (!IS_FUN(argv[1]))
        vm_error(vm, "animate() second argument must be a function");

    // Store the callback in the context
    ctx->frame_callback = argv[1];

    return argv[0];
}

// Close the window
Value dw_close(VM *vm, int argc, Value *argv)
{
    PiContext *ctx = _get_ctx(argv[0]);
    if (!ctx)
        return NEW_NIL();

    ctx->running = FALSE;
    return argv[0];
}

// Register event handler
Value dw_on(VM *vm, int argc, Value *argv)
{
    // Usage: on(canvas, event_type, callback)
    PiContext *ctx = _get_ctx(argv[0]);
    if (!ctx)
        vm_error(vm, "on() takes a canvas as first argument");

    if (argc < 3)
        vm_error(vm, "on() needs event type and callback");

    if (!IS_STRING(argv[1]))
        vm_error(vm, "event type must be a string");
    const char *event_name = AS_CSTRING(argv[1]);

    EventType type = _get_eventType(event_name);
    if (type == -1)
        vm_errorf(vm, "Unknown event type: %s", event_name);

    if (!IS_FUN(argv[2]))
        vm_error(vm, "callback must be a function");

    HandlerList *handlers = &ctx->handlers[type];

    // Check if already at max handlers
    if (handlers->count >= MAX_HANDLERS)
    {
        vm_errorf(vm, "Maximum handlers (%d) reached for event '%s'", MAX_HANDLERS, event_name);
        return NEW_NIL();
    }

    // Add handler
    handlers->fns[handlers->count] = (Value *)malloc(sizeof(Value));
    *(handlers->fns[handlers->count]) = argv[2];
    handlers->count++;

    return NEW_NIL();
}

// Remove event handler
Value dw_off(VM *vm, int argc, Value *argv)
{
    // Usage: off(canvas, event_type, [callback])
    PiContext *ctx = _get_ctx(argv[0]);
    if (!ctx)
        vm_error(vm, "off() takes a canvas as first argument");

    if (argc < 2)
        vm_error(vm, "off() needs event type");

    if (!IS_STRING(argv[1]))
        vm_error(vm, "event type must be a string");
    const char *event_name = AS_CSTRING(argv[1]);

    EventType type = _get_eventType(event_name);
    if (type == -1)
        vm_errorf(vm, "Unknown event type: %s", event_name);

    HandlerList *handlers = &ctx->handlers[type];

    if (argc > 2 && !IS_NIL(argv[2]))
    {
        // Remove specific handler
        Value to_remove = argv[2];
        for (int i = 0; i < handlers->count; i++)
        {
            if (equals(*(handlers->fns[i]), to_remove))
            {
                // Free the handler
                free(handlers->fns[i]);
                // Shift remaining handlers
                for (int j = i; j < handlers->count - 1; j++)
                {
                    handlers->fns[j] = handlers->fns[j + 1];
                }
                handlers->count--;
                handlers->fns[handlers->count] = NULL;
                break;
            }
        }
    }
    else
    {
        // Remove all handlers for this event
        for (int i = 0; i < handlers->count; i++)
        {
            free(handlers->fns[i]);
            handlers->fns[i] = NULL;
        }
        handlers->count = 0;
    }

    return NEW_NIL();
}

// Poll for event (non-blocking)
Value dw_poll(VM *vm, int argc, Value *argv)
{
    // Usage: poll(canvas) -> event or nil
    PiContext *ctx = _get_ctx(argv[0]);
    if (!ctx)
        return NEW_NIL();

    _pump_events(vm, ctx);

    PiEvent *e = _pop_event(ctx);
    if (!e)
        return NEW_NIL();

    return _event_toValue(vm, e);
}

// Wait for event (blocking)
Value dw_wait(VM *vm, int argc, Value *argv)
{
    // Usage: wait(canvas) -> event
    PiContext *ctx = _get_ctx(argv[0]);
    if (!ctx)
        vm_error(vm, "wait() takes a canvas as first argument");

    while (ctx->running)
    {
        _pump_events(vm, ctx);

        PiEvent *e = _pop_event(ctx);
        if (e)
            return _event_toValue(vm, e);

        SDL_Delay(10);
    }

    return NEW_NIL();
}

// Module Registration
static BuiltinConst draw_const[] = {
    {"COLOR_BLACK", NEW_NUM(0x000000)},
    {"COLOR_WHITE", NEW_NUM(0xffffff)},

    // Primary vivid colors
    {"COLOR_RED", NEW_NUM(0xff1744)},
    {"COLOR_GREEN", NEW_NUM(0x00e676)},
    {"COLOR_BLUE", NEW_NUM(0x2979ff)},

    // Secondary vivid colors
    {"COLOR_YELLOW", NEW_NUM(0xffea00)},
    {"COLOR_MAGENTA", NEW_NUM(0xff00ff)},
    {"COLOR_CYAN", NEW_NUM(0x00e5ff)},

    // Accent colors
    {"COLOR_ORANGE", NEW_NUM(0xff6d00)},
    {"COLOR_PURPLE", NEW_NUM(0xaa00ff)},
    {"COLOR_PINK", NEW_NUM(0xff4081)},
    {"COLOR_LIME", NEW_NUM(0xc6ff00)},
    {"COLOR_TEAL", NEW_NUM(0x1de9b6)},
    {"COLOR_INDIGO", NEW_NUM(0x536dfe)},

    {"COLOR_BROWN", NEW_NUM(0xbf360c)},

    // Neutral grays
    {"COLOR_GRAY", NEW_NUM(0x757575)},
    {"COLOR_LIGHT_GRAY", NEW_NUM(0xe0e0e0)},
    {"COLOR_DARK_GRAY", NEW_NUM(0x424242)},

    {"COLOR_TRANSPARENT", NEW_NUM(0x00000000)},

    // Font styles
    {"FONT_NORMAL", NEW_NUM(0)},
    {"FONT_BOLD", NEW_NUM(1)},
    {"FONT_ITALIC", NEW_NUM(2)},
    {"FONT_UNDERLINE", NEW_NUM(4)},
    {"FONT_ALIGN_LEFT", NEW_NUM(0)},
    {"FONT_ALIGN_CENTER", NEW_NUM(1)},
    {"FONT_ALIGN_RIGHT", NEW_NUM(2)},

    // Keyboard
    {"KEY_UNKNOWN", NEW_NUM(SDLK_UNKNOWN)},
    {"KEY_RETURN", NEW_NUM(SDLK_RETURN)},
    {"KEY_ESCAPE", NEW_NUM(SDLK_ESCAPE)},
    {"KEY_BACKSPACE", NEW_NUM(SDLK_BACKSPACE)},
    {"KEY_TAB", NEW_NUM(SDLK_TAB)},
    {"KEY_SPACE", NEW_NUM(SDLK_SPACE)},

    // Arrows
    {"KEY_LEFT", NEW_NUM(SDLK_LEFT)},
    {"KEY_RIGHT", NEW_NUM(SDLK_RIGHT)},
    {"KEY_UP", NEW_NUM(SDLK_UP)},
    {"KEY_DOWN", NEW_NUM(SDLK_DOWN)},

    // Navigation
    {"KEY_INSERT", NEW_NUM(SDLK_INSERT)},
    {"KEY_DELETE", NEW_NUM(SDLK_DELETE)},
    {"KEY_HOME", NEW_NUM(SDLK_HOME)},
    {"KEY_END", NEW_NUM(SDLK_END)},
    {"KEY_PAGEUP", NEW_NUM(SDLK_PAGEUP)},
    {"KEY_PAGEDOWN", NEW_NUM(SDLK_PAGEDOWN)},

    // Function keys
    {"KEY_F1", NEW_NUM(SDLK_F1)},
    {"KEY_F2", NEW_NUM(SDLK_F2)},
    {"KEY_F3", NEW_NUM(SDLK_F3)},
    {"KEY_F4", NEW_NUM(SDLK_F4)},
    {"KEY_F5", NEW_NUM(SDLK_F5)},
    {"KEY_F6", NEW_NUM(SDLK_F6)},
    {"KEY_F7", NEW_NUM(SDLK_F7)},
    {"KEY_F8", NEW_NUM(SDLK_F8)},
    {"KEY_F9", NEW_NUM(SDLK_F9)},
    {"KEY_F10", NEW_NUM(SDLK_F10)},
    {"KEY_F11", NEW_NUM(SDLK_F11)},
    {"KEY_F12", NEW_NUM(SDLK_F12)},

    // Modifiers
    {"KEY_LSHIFT", NEW_NUM(SDLK_LSHIFT)},
    {"KEY_RSHIFT", NEW_NUM(SDLK_RSHIFT)},
    {"KEY_LCTRL", NEW_NUM(SDLK_LCTRL)},
    {"KEY_RCTRL", NEW_NUM(SDLK_RCTRL)},
    {"KEY_LALT", NEW_NUM(SDLK_LALT)},
    {"KEY_RALT", NEW_NUM(SDLK_RALT)},

    // Numbers
    {"KEY_0", NEW_NUM(SDLK_0)},
    {"KEY_1", NEW_NUM(SDLK_1)},
    {"KEY_2", NEW_NUM(SDLK_2)},
    {"KEY_3", NEW_NUM(SDLK_3)},
    {"KEY_4", NEW_NUM(SDLK_4)},
    {"KEY_5", NEW_NUM(SDLK_5)},
    {"KEY_6", NEW_NUM(SDLK_6)},
    {"KEY_7", NEW_NUM(SDLK_7)},
    {"KEY_8", NEW_NUM(SDLK_8)},
    {"KEY_9", NEW_NUM(SDLK_9)},

    // Letters
    {"KEY_A", NEW_NUM(SDLK_a)},
    {"KEY_B", NEW_NUM(SDLK_b)},
    {"KEY_C", NEW_NUM(SDLK_c)},
    {"KEY_D", NEW_NUM(SDLK_d)},
    {"KEY_E", NEW_NUM(SDLK_e)},
    {"KEY_F", NEW_NUM(SDLK_f)},
    {"KEY_G", NEW_NUM(SDLK_g)},
    {"KEY_H", NEW_NUM(SDLK_h)},
    {"KEY_I", NEW_NUM(SDLK_i)},
    {"KEY_J", NEW_NUM(SDLK_j)},
    {"KEY_K", NEW_NUM(SDLK_k)},
    {"KEY_L", NEW_NUM(SDLK_l)},
    {"KEY_M", NEW_NUM(SDLK_m)},
    {"KEY_N", NEW_NUM(SDLK_n)},
    {"KEY_O", NEW_NUM(SDLK_o)},
    {"KEY_P", NEW_NUM(SDLK_p)},
    {"KEY_Q", NEW_NUM(SDLK_q)},
    {"KEY_R", NEW_NUM(SDLK_r)},
    {"KEY_S", NEW_NUM(SDLK_s)},
    {"KEY_T", NEW_NUM(SDLK_t)},
    {"KEY_U", NEW_NUM(SDLK_u)},
    {"KEY_V", NEW_NUM(SDLK_v)},
    {"KEY_W", NEW_NUM(SDLK_w)},
    {"KEY_X", NEW_NUM(SDLK_x)},
    {"KEY_Y", NEW_NUM(SDLK_y)},
    {"KEY_Z", NEW_NUM(SDLK_z)},
};

static BuiltinFunc draw_funcs[] = {
    {"canvas", dw_canvas},
    {"run", dw_run},
    {"clear", dw_clear},
    {"pixel", dw_pixel},
    {"line", dw_line},
    {"triangle", dw_triangle},
    {"rect", dw_rect},
    {"polygon", dw_polygon},
    {"circle", dw_circle},
    {"present", dw_present},
    {"animate", dw_animate},
    {"on", dw_on},
    {"off", dw_off},
    {"poll", dw_poll},
    {"wait", dw_wait},
    {"mouse", dw_mouse},
    {"cursor", dw_cursor},
    {"key", dw_key},
    {"text", dw_text},
    {"image", dw_image},
    {"is_running", dw_isRunning},
    {"close", dw_close},
    {"push", dw_push},
    {"pop", dw_pop},
    {"translate", dw_translate},
    {"scale", dw_scale},
    {"rotate", dw_rotate},
    {"alpha", dw_alpha},
};

DEFINE_BUILTIN_MODULE(module_draw, "draw", draw_funcs, draw_const);
