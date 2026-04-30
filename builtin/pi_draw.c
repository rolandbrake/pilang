#include "pi_draw.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

#include "../common.h"
#include "pi_builtin.h"
#include "../pi_func.h"
#include "../table.h"

typedef struct TransformState
{
    float tx, ty;
    float sx, sy;
    float angle;
    float alpha;
    struct TransformState *next;
} TransformState;

static char *draw_strdup(const char *text)
{
    size_t length = strlen(text) + 1;
    char *copy = (char *)malloc(length);
    if (copy == NULL)
        return NULL;

    memcpy(copy, text, length);
    return copy;
}

static PiContext *get_ctx(Value v)
{
    if (!IS_CONTEXT(v))
        return NULL;
    return AS_CONTEXT(v);
}

static void set_color(SDL_Renderer *r, int color, float alpha)
{
    Uint8 rC = (color >> 16) & 255;
    Uint8 gC = (color >> 8) & 255;
    Uint8 bC = color & 255;
    Uint8 aC = (Uint8)(alpha * 255);

    SDL_SetRenderDrawColor(r, rC, gC, bC, aC);
}

static void transform(PiContext *ctx, float *x, float *y)
{
    *x = (*x * ctx->sx) + ctx->tx;
    *y = (*y * ctx->sy) + ctx->ty;
}

static Value _map_get(PiMap *map, char *key)
{
    void *item = map ? ht_get(map->table, key) : NULL;

    // Check if the item was found; if not, return nil
    if (item == NULL)
        return NEW_NIL();

    // Return the found value
    return *(Value *)item;
}

// Helper function to parse options from a table
static void parse_drawOptions(Value opts, int *color, char **font_path, int *font_size,
                              char **align, bool *bold, bool *italic, float *img_alpha,
                              int *img_w, int *img_h)
{
    if (!IS_MAP(opts))
        return;

    PiMap *table = AS_MAP(opts);

    // Parse color
    Value _color = _map_get(table, "color");
    if (!IS_NIL(_color) && IS_NUM(_color))
        *color = (int)AS_NUM(_color);

    // Parse font
    Value _font = _map_get(table, "font");
    if (!IS_NIL(_font) && IS_STRING(_font))
        *font_path = AS_CSTRING(_font);

    // Parse font size
    Value _size = _map_get(table, "size");
    if (!IS_NIL(_size) && IS_NUM(_size))
        *font_size = (int)AS_NUM(_size);

    // Parse alignment
    Value _align = _map_get(table, "align");
    if (!IS_NIL(_align) && IS_STRING(_align))
        *align = AS_CSTRING(_align);

    // Parse bold
    Value _bold = _map_get(table, "bold");
    if (!IS_NIL(_bold) && IS_BOOL(_bold))
        *bold = AS_BOOL(_bold);

    // Parse italic
    Value _italic = _map_get(table, "italic");
    if (!IS_NIL(_italic) && IS_BOOL(_italic))
        *italic = AS_BOOL(_italic);

    // Parse image alpha
    Value _alpha = _map_get(table, "alpha");
    if (!IS_NIL(_alpha) && IS_NUM(_alpha))
        *img_alpha = (float)AS_NUM(_alpha);

    // Parse image width/height
    Value _w = _map_get(table, "w");
    if (!IS_NIL(_w) && IS_NUM(_w))
        *img_w = (int)AS_NUM(_w);

    Value _h = _map_get(table, "h");
    if (!IS_NIL(_h) && IS_NUM(_h))
        *img_h = (int)AS_NUM(_h);
}

static EventType get_eventType(const char *name)
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

static const char *get_eventName(EventType type)
{
    static const char *names[] = {
        "keydown", "keyup", "mousedown", "mouseup",
        "mousemove", "click", "scroll", "resize", "close"};
    if (type >= 0 && type < EVENT_COUNT)
        return names[type];
    return "unknown";
}

static void init_handlerList(HandlerList *list, const char *name)
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

static void init_eventQueue(PiContext *ctx)
{
    ctx->eventQueue = (EventQueue *)malloc(sizeof(EventQueue));
    ctx->eventQueue->capacity = INIT_CAP;
    ctx->eventQueue->events = (PiEvent **)calloc(ctx->eventQueue->capacity, sizeof(PiEvent *));
    ctx->eventQueue->head = 0;
    ctx->eventQueue->tail = 0;
    ctx->eventQueue->count = 0;
}

static void init_eventSystem(PiContext *ctx)
{
    for (int i = 0; i < EVENT_COUNT; i++)
        init_handlerList(&ctx->handlers[i], get_eventName((EventType)i));

    init_eventQueue(ctx);
}

static PiEvent *clone_event(const PiEvent *src)
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
        copy->key = draw_strdup(src->key);
    }

    return copy;
}

static void free_event(PiEvent *e)
{
    if (e == NULL)
        return;

    free(e->type);
    free(e->key);
    free(e);
}

// Add event to queue
static void push_event(PiContext *ctx, PiEvent *e)
{
    if (ctx->eventQueue == NULL || ctx->eventQueue->events == NULL)
    {
        init_eventQueue(ctx);
    }

    // Resize if needed
    if (ctx->eventQueue->count >= ctx->eventQueue->capacity)
    {
        int new_capacity = ctx->eventQueue->capacity * 2;
        PiEvent **new_events = (PiEvent **)realloc(ctx->eventQueue->events, new_capacity * sizeof(PiEvent *));
        if (!new_events)
            return;

        // Reorganize if wrapped around
        if (ctx->eventQueue->head > ctx->eventQueue->tail)
        {
            // Move wrapped elements to the end
            int old_capacity = ctx->eventQueue->capacity;
            for (int i = ctx->eventQueue->tail - 1; i >= 0; i--)
                new_events[i + old_capacity] = new_events[i];
            ctx->eventQueue->tail += old_capacity;
        }

        ctx->eventQueue->events = new_events;
        ctx->eventQueue->capacity = new_capacity;
    }

    // Copy event
    PiEvent *copy = clone_event(e);

    ctx->eventQueue->events[ctx->eventQueue->tail] = copy;
    ctx->eventQueue->tail = (ctx->eventQueue->tail + 1) % ctx->eventQueue->capacity;
    ctx->eventQueue->count++;
}

// Pop event from queue
static PiEvent *pop_event(PiContext *ctx)
{
    if (ctx->eventQueue == NULL || !ctx->eventQueue->events || ctx->eventQueue->count == 0)
        return NULL;

    PiEvent *e = ctx->eventQueue->events[ctx->eventQueue->head];
    ctx->eventQueue->events[ctx->eventQueue->head] = NULL;
    ctx->eventQueue->head = (ctx->eventQueue->head + 1) % ctx->eventQueue->capacity;
    ctx->eventQueue->count--;

    return e;
}

// Clear event queue
static void clear_eventQueue(PiContext *ctx)
{
    if (ctx->eventQueue == NULL || !ctx->eventQueue->events)
        return;

    while (ctx->eventQueue->count > 0)
    {
        PiEvent *e = pop_event(ctx);
        free_event(e);
    }
}

// Cleanup event system
static void cleanup_events(PiContext *ctx)
{
    // Clear all handlers
    for (int i = 0; i < EVENT_COUNT; i++)
    {
        ctx->handlers[i].count = 0;
        for (int j = 0; j < MAX_HANDLERS; j++)
        {
            ctx->handlers[i].fns[j] = NULL;
        }
    }

    // Clear event queue
    clear_eventQueue(ctx);
    if (ctx->eventQueue != NULL && ctx->eventQueue->events)
    {
        free(ctx->eventQueue->events);
        ctx->eventQueue->events = NULL;
    }
    if (ctx->eventQueue != NULL)
    {
        free(ctx->eventQueue);
        ctx->eventQueue = NULL;
    }
}

static Value event_toValue(vm_t *vm, PiEvent *event)
{
    return NEW_OBJ(add_obj(vm, (Object *)event));
}

static void dispatch_event(vm_t *vm, PiContext *ctx, PiEvent *event)
{
    HandlerList *handlers = &ctx->handlers[event->event_type];
    if (handlers->count == 0)
        return;

    PiEvent *event_copy = clone_event(event);
    Value event_obj = event_toValue(vm, event_copy);

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

static PiEvent *translate_sdlEvent(const SDL_Event *event)
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
        pi_event->key = draw_strdup(SDL_GetKeyName(event->key.keysym.sym));
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

static void pump_events(vm_t *vm, PiContext *ctx)
{
    SDL_Event sdl_event;

    while (SDL_PollEvent(&sdl_event))
    {
        if (sdl_event.type == SDL_QUIT ||
            (sdl_event.type == SDL_WINDOWEVENT && sdl_event.window.event == SDL_WINDOWEVENT_CLOSE) ||
            (sdl_event.type == SDL_KEYDOWN && sdl_event.key.keysym.sym == SDLK_ESCAPE))
            ctx->running = false;

        if (sdl_event.type == SDL_WINDOWEVENT &&
            (sdl_event.window.event == SDL_WINDOWEVENT_RESIZED ||
             sdl_event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED))
        {
            ctx->width = sdl_event.window.data1;
            ctx->height = sdl_event.window.data2;
        }

        PiEvent *event = translate_sdlEvent(&sdl_event);
        if (event == NULL)
            continue;

        push_event(ctx, event);
        dispatch_event(vm, ctx, event);
        free_event(event);
    }
}

Value dw_canvas(vm_t *vm, int argc, Value *argv)
{
    if (!IS_NUM(argv[0]) || !IS_NUM(argv[1]))
        vm_error(vm, "canvas() takes two numbers as arguments");

    int w = as_number(argv[0]);
    int h = as_number(argv[1]);

    const char *title = (argc > 2 && IS_STRING(argv[2]))
                            ? AS_CSTRING(argv[2])
                            : "PiLang";

    PiContext *ctx = (PiContext *)new_context();

    ctx->window = SDL_CreateWindow(title,
                                   SDL_WINDOWPOS_UNDEFINED,
                                   SDL_WINDOWPOS_UNDEFINED,
                                   w, h,
                                   SDL_WINDOW_SHOWN);

    if (!ctx->window)
    {
        free(ctx);
        vm_errorf(vm, "Failed to create window: %s", SDL_GetError());
    }

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
    ctx->running = true;
    ctx->alpha = 1.0f; // Initialize alpha
    ctx->tx = 0.0f;    // Initialize transform
    ctx->ty = 0.0f;
    ctx->sx = 1.0f;
    ctx->sy = 1.0f;
    init_eventSystem(ctx);

    return NEW_OBJ(ctx);
}

// Update dw_run to call the callback each frame
Value dw_run(vm_t *vm, int argc, Value *argv)
{
    PiContext *ctx = get_ctx(argv[0]);
    if (ctx == NULL)
        vm_error(vm, "run() takes a canvas as first argument");

    SDL_Event e;
    while (ctx->running)
    {
        pump_events(vm, ctx);

        // Call the frame callback if it exists
        if (!IS_NIL(ctx->frame_callback))
        {
            Value args[1] = {argv[0]};
            Function *func = AS_FUN(ctx->frame_callback);
            call_func(vm, func, 1, args, NEW_NIL());
        }

        // Present the rendered frame
        SDL_RenderPresent(ctx->renderer);

        // Cap frame rate to 60 FPS
        SDL_Delay(16);
    }

    SDL_DestroyRenderer(ctx->renderer);
    SDL_DestroyWindow(ctx->window);
    cleanup_events(ctx);
    free(ctx);

    return NEW_NIL();
}

Value dw_clear(vm_t *vm, int argc, Value *argv)
{
    PiContext *ctx = get_ctx(argv[0]);
    if (!ctx)
        vm_error(vm, "clear() takes a canvas as first argument");

    int color = (argc > 1) ? as_number(argv[1]) : 0x000000;
    set_color(ctx->renderer, color, ctx->alpha);
    SDL_RenderClear((SDL_Renderer *)ctx->renderer);

    return NEW_NIL();
}

// Add this helper function for circle drawing
static void draw_circle(SDL_Renderer *r, int cx, int cy, int radius)
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

static void draw_filledCircle(SDL_Renderer *r, int cx, int cy, int radius)
{
    for (int y = -radius; y <= radius; y++)
    {
        int x = (int)sqrt(radius * radius - y * y);
        SDL_RenderDrawLine(r, cx - x, cy + y, cx + x, cy + y);
    }
}

Value dw_pixel(vm_t *vm, int argc, Value *argv)
{
    // Usage: pixel(canvas, x, y, color)
    PiContext *ctx = get_ctx(argv[0]);
    if (!ctx)
        vm_error(vm, "pixel() takes a canvas as first argument");

    if (argc < 3)
        vm_error(vm, "pixel() needs at least x and y coordinates");

    float x = as_number(argv[1]);
    float y = as_number(argv[2]);
    int color = (argc > 3) ? as_number(argv[3]) : 0xFFFFFF;

    transform(ctx, &x, &y);
    set_color(ctx->renderer, color, ctx->alpha);
    SDL_RenderDrawPoint((SDL_Renderer *)ctx->renderer, (int)x, (int)y);

    return NEW_NIL();
}

Value dw_line(vm_t *vm, int argc, Value *argv)
{
    // Usage: line(canvas, x1, y1, x2, y2, color)
    PiContext *ctx = get_ctx(argv[0]);
    if (!ctx)
        vm_error(vm, "line() takes a canvas as first argument");

    if (argc < 5)
        vm_error(vm, "line() needs x1, y1, x2, y2 coordinates");

    float x1 = as_number(argv[1]);
    float y1 = as_number(argv[2]);
    float x2 = as_number(argv[3]);
    float y2 = as_number(argv[4]);
    int color = (argc > 5) ? as_number(argv[5]) : 0xFFFFFF;

    transform(ctx, &x1, &y1);
    transform(ctx, &x2, &y2);
    set_color(ctx->renderer, color, ctx->alpha);
    SDL_RenderDrawLine((SDL_Renderer *)ctx->renderer, (int)x1, (int)y1, (int)x2, (int)y2);

    return NEW_NIL();
}

Value dw_triangle(vm_t *vm, int argc, Value *argv)
{
    // Usage: triangle(canvas, x1, y1, x2, y2, x3, y3, color, filled)
    PiContext *ctx = get_ctx(argv[0]);
    if (!ctx)
        vm_error(vm, "triangle() takes a canvas as first argument");

    if (argc < 7)
        vm_error(vm, "triangle() needs x1,y1,x2,y2,x3,y3 coordinates");

    float x1 = as_number(argv[1]);
    float y1 = as_number(argv[2]);
    float x2 = as_number(argv[3]);
    float y2 = as_number(argv[4]);
    float x3 = as_number(argv[5]);
    float y3 = as_number(argv[6]);
    int color = (argc > 7) ? as_number(argv[7]) : 0xFFFFFF;
    bool filled = (argc > 8) ? as_bool(argv[8]) : false;

    transform(ctx, &x1, &y1);
    transform(ctx, &x2, &y2);
    transform(ctx, &x3, &y3);
    set_color(ctx->renderer, color, ctx->alpha);

    if (filled)
    {
        // Draw filled triangle using geometry rendering
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

    return NEW_NIL();
}

Value dw_rect(vm_t *vm, int argc, Value *argv)
{
    // Usage: rect(canvas, x, y, width, height, color, filled)
    PiContext *ctx = get_ctx(argv[0]);
    if (!ctx)
        vm_error(vm, "rect() takes a canvas as first argument");

    if (argc < 5)
        vm_error(vm, "rect() needs x, y, width, height");

    float x = as_number(argv[1]);
    float y = as_number(argv[2]);
    float w = as_number(argv[3]);
    float h = as_number(argv[4]);
    int color = (argc > 5) ? as_number(argv[5]) : 0xFFFFFF;
    bool filled = (argc > 6) ? as_bool(argv[6]) : false;

    // Transform the rectangle corners
    float x1 = x;
    float y1 = y;
    float x2 = x + w;
    float y2 = y + h;

    transform(ctx, &x1, &y1);
    transform(ctx, &x2, &y2);
    set_color(ctx->renderer, color, ctx->alpha);

    // Calculate transformed rectangle
    float min_x = fminf(x1, x2);
    float min_y = fminf(y1, y2);
    float max_x = fmaxf(x1, x2);
    float max_y = fmaxf(y1, y2);

    SDL_FRect rect = {min_x, min_y, max_x - min_x, max_y - min_y};

    if (filled)
    {
        SDL_RenderFillRectF((SDL_Renderer *)ctx->renderer, &rect);
    }
    else
    {
        SDL_RenderDrawRectF((SDL_Renderer *)ctx->renderer, &rect);
    }

    return NEW_NIL();
}

Value dw_polygon(vm_t *vm, int argc, Value *argv)
{
    // Usage: polygon(canvas, points, color, filled)
    // points is a list of [x1, y1, x2, y2, ...] or list of [x,y] pairs
    PiContext *ctx = get_ctx(argv[0]);
    if (!ctx)
        vm_error(vm, "polygon() takes a canvas as first argument");

    if (argc < 2)
        vm_error(vm, "polygon() needs a points list");

    // Parse points list
    Value points_val = argv[1];
    if (!IS_LIST(points_val))
        vm_error(vm, "polygon() second argument must be a list");

    list_t *list = AS_CLIST(points_val);
    int point_count = PILIST_SIZE(points_val);

    if (point_count < 3)
        vm_error(vm, "polygon() needs at least 3 points");

    // Extract points
    SDL_FPoint *points = (SDL_FPoint *)malloc(point_count * sizeof(SDL_FPoint));
    if (!points)
        vm_error(vm, "Out of memory for polygon points");

    for (int i = 0; i < point_count; i++)
    {
        Value pair = *(Value *)list_getAt(list, i);
        if (!IS_LIST(pair) || as_list(pair)->size < 2)
        {
            free(points);
            vm_error(vm, "Each point must be a list of [x, y]");
        }

        list_t *point = as_list(pair);
        float px = as_number(*(Value *)list_getAt(point, 0));
        float py = as_number(*(Value *)list_getAt(point, 1));
        transform(ctx, &px, &py);
        points[i].x = px;
        points[i].y = py;
    }

    int color = (argc > 2) ? as_number(argv[2]) : 0xFFFFFF;
    bool filled = (argc > 3) ? as_bool(argv[3]) : false;

    set_color(ctx->renderer, color, ctx->alpha);

    if (filled)
    {
        // For filled polygon, we need to triangulate or use a different approach
        // Using SDL_RenderGeometry requires triangulation
        // Simple approach: draw as a triangle fan from first point
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
        // Close the polygon
        SDL_RenderDrawLineF((SDL_Renderer *)ctx->renderer,
                            points[point_count - 1].x, points[point_count - 1].y,
                            points[0].x, points[0].y);
    }

    free(points);
    return NEW_NIL();
}

Value dw_circle(vm_t *vm, int argc, Value *argv)
{
    // Usage: circle(canvas, x, y, radius, color, filled)
    PiContext *ctx = get_ctx(argv[0]);
    if (!ctx)
        vm_error(vm, "circle() takes a canvas as first argument");

    if (argc < 4)
        vm_error(vm, "circle() needs x, y, radius");

    float x = as_number(argv[1]);
    float y = as_number(argv[2]);
    float radius = as_number(argv[3]);
    int color = (argc > 4) ? as_number(argv[4]) : 0xFFFFFF;
    bool filled = (argc > 5) ? as_bool(argv[5]) : false;

    transform(ctx, &x, &y);

    // Apply scale to radius
    float scale = (fabsf(ctx->sx) + fabsf(ctx->sy)) * 0.5f;
    int final_radius = (int)(radius * scale);
    if (final_radius < 1)
        final_radius = 1;

    set_color(ctx->renderer, color, ctx->alpha);

    if (filled)
        draw_filledCircle((SDL_Renderer *)ctx->renderer, (int)x, (int)y, final_radius);
    else
        draw_circle((SDL_Renderer *)ctx->renderer, (int)x, (int)y, final_radius);

    return NEW_NIL();
}

Value dw_translate(vm_t *vm, int argc, Value *argv)
{
    PiContext *ctx = get_ctx(argv[0]);
    if (!ctx)
        return NEW_NIL();

    ctx->tx += as_number(argv[1]);
    ctx->ty += as_number(argv[2]);
    return NEW_NIL();
}

Value dw_scale(vm_t *vm, int argc, Value *argv)
{
    PiContext *ctx = get_ctx(argv[0]);
    if (!ctx)
        return NEW_NIL();

    ctx->sx *= as_number(argv[1]);
    ctx->sy *= as_number(argv[2]);
    return NEW_NIL();
}

Value dw_rotate(vm_t *vm, int argc, Value *argv)
{
    PiContext *ctx = get_ctx(argv[0]);
    if (!ctx)
        return NEW_NIL();

    ctx->angle += as_number(argv[1]);
    return NEW_NIL();
}

Value dw_alpha(vm_t *vm, int argc, Value *argv)
{
    PiContext *ctx = get_ctx(argv[0]);
    if (!ctx)
        return NEW_NIL();

    ctx->alpha = as_number(argv[1]);
    return NEW_NIL();
}

// Helper to apply font styles
static TTF_Font *load_styledFont(const char *font_path, int size, bool bold, bool italic)
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

// Helper to get text dimensions
static void get_textDimensions(TTF_Font *font, const char *text, int *w, int *h)
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

// Helper to align text position
static void align_textPosition(int text_w, int text_h, float *x, float *y, const char *align)
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

// Draw text with full options support
Value dw_text(vm_t *vm, int argc, Value *argv)
{
    // Usage: text(canvas, x, y, text, opts)
    // opts: {color, font, size, align, bold, italic}

    PiContext *ctx = get_ctx(argv[0]);
    if (!ctx)
        vm_error(vm, "text() takes a canvas as first argument");

    if (argc < 4)
        vm_error(vm, "text() needs x, y, and text arguments");

    float x = as_number(argv[1]);
    float y = as_number(argv[2]);
    const char *text = AS_CSTRING(argv[3]);

    // Default options
    int color = 0xFFFFFF;
    char *font_path = NULL;
    int font_size = 16;
    char *align = NULL;
    bool bold = false;
    bool italic = false;

    // Parse options if provided
    if (argc > 4)
        parse_drawOptions(argv[4], &color, &font_path, &font_size, &align, &bold, &italic, NULL, NULL, NULL);

    // Load font
    TTF_Font *font = NULL;
    if (font_path)
    {
        font = load_styledFont(font_path, font_size, bold, italic);
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
        // Try to load default font
        const char *default_fonts[] = {
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "C:/Windows/Fonts/arial.ttf",
            "/System/Library/Fonts/Helvetica.ttc"};
        for (int i = 0; i < 3; i++)
        {
            font = TTF_OpenFont(default_fonts[i], font_size);
            if (font)
                break;
        }
        if (!font)
            vm_error(vm, "Failed to load any font. Please specify a font path.");
    }

    // Get text dimensions
    int text_w, text_h;
    get_textDimensions(font, text, &text_w, &text_h);

    // Apply alignment
    align_textPosition(text_w, text_h, &x, &y, align);

    // Apply transform
    transform(ctx, &x, &y);

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

    if (!texture)
    {
        if (font_path && font)
            TTF_CloseFont(font);
        vm_errorf(vm, "Failed to create texture: %s", SDL_GetError());
    }

    // Draw texture
    SDL_Rect dst = {(int)x, (int)y, text_w, text_h};
    SDL_RenderCopy(ctx->renderer, texture, NULL, &dst);
    SDL_DestroyTexture(texture);

    // Clean up font if we loaded it
    if (font_path && font)
        TTF_CloseFont(font);

    return NEW_NIL();
}

// Draw image with full options support
Value dw_image(vm_t *vm, int argc, Value *argv)
{
    // Usage: image(canvas, path, x, y, opts)
    // opts: {w, h, alpha, color}

    PiContext *ctx = get_ctx(argv[0]);
    if (!ctx)
        vm_error(vm, "image() takes a canvas as first argument");

    if (argc < 4)
        vm_error(vm, "image() needs path, x, and y arguments");

    const char *path = AS_CSTRING(argv[1]);
    float x = as_number(argv[2]);
    float y = as_number(argv[3]);

    // Default options
    int color = 0xFFFFFF;
    float img_alpha = -1.0f; // -1 means use context alpha
    int img_w = 0;
    int img_h = 0;

    // Parse options if provided
    if (argc > 4)
        parse_drawOptions(argv[4], &color, NULL, NULL, NULL, NULL, NULL, &img_alpha, &img_w, &img_h);

    // Apply transform
    transform(ctx, &x, &y);

    // Load image
    SDL_Surface *surface = IMG_Load(path);
    if (!surface)
        vm_errorf(vm, "Failed to load image '%s': %s", path, IMG_GetError());

    SDL_Texture *texture = SDL_CreateTextureFromSurface(ctx->renderer, surface);
    SDL_FreeSurface(surface);

    if (!texture)
        vm_errorf(vm, "Failed to create texture: %s", SDL_GetError());

    // Get original dimensions
    int orig_w, orig_h;
    SDL_QueryTexture(texture, NULL, NULL, &orig_w, &orig_h);

    // Apply resize if specified
    int dst_w = img_w > 0 ? img_w : orig_w;
    int dst_h = img_h > 0 ? img_h : orig_h;

    // Apply alpha
    Uint8 alpha = (Uint8)((img_alpha >= 0 ? img_alpha : ctx->alpha) * 255);
    SDL_SetTextureAlphaMod(texture, alpha);

    // Apply color modulation
    SDL_SetTextureColorMod(texture,
                           (Uint8)((color >> 16) & 255),
                           (Uint8)((color >> 8) & 255),
                           (Uint8)(color & 255));

    // Draw
    SDL_Rect dst = {(int)x, (int)y, dst_w, dst_h};
    SDL_RenderCopy(ctx->renderer, texture, NULL, &dst);
    SDL_DestroyTexture(texture);

    return NEW_NIL();
}

// Push current transform state onto stack
Value dw_push(vm_t *vm, int argc, Value *argv)
{
    // Usage: push(canvas)

    PiContext *ctx = get_ctx(argv[0]);
    if (!ctx)
        vm_error(vm, "push() takes a canvas as first argument");

    // Create new transform state
    TransformState *state = (TransformState *)malloc(sizeof(TransformState));
    if (!state)
        vm_error(vm, "Out of memory for transform state");

    // Save current state
    state->tx = ctx->tx;
    state->ty = ctx->ty;
    state->sx = ctx->sx;
    state->sy = ctx->sy;
    state->angle = ctx->angle;
    state->alpha = ctx->alpha;

    // Push onto stack
    state->next = (TransformState *)ctx->transform_stack;
    ctx->transform_stack = state;

    return NEW_NIL();
}

// Pop transform state from stack
Value dw_pop(vm_t *vm, int argc, Value *argv)
{
    // Usage: pop(canvas)

    PiContext *ctx = get_ctx(argv[0]);
    if (!ctx)
        vm_error(vm, "pop() takes a canvas as first argument");

    // Check if stack has items
    if (!ctx->transform_stack)
        vm_error(vm, "pop() called with no matching push()");

    // Pop from stack
    TransformState *state = (TransformState *)ctx->transform_stack;
    ctx->transform_stack = state->next;

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

Value dw_mouse(vm_t *vm, int argc, Value *argv)
{
    int x, y;
    Uint32 buttons = SDL_GetMouseState(&x, &y);

    table_t *table = ht_create(sizeof(Value));

    ht_put(table, "x", &NEW_NUM((double)x));
    ht_put(table, "y", &NEW_NUM((double)y));
    ht_put(table, "left", &NEW_BOOL(buttons & SDL_BUTTON(SDL_BUTTON_LEFT)));

    PiMap *map = (PiMap *)new_map(table, false);

    return NEW_OBJ(map);
}

Value dw_key(vm_t *vm, int argc, Value *argv)
{
    const Uint8 *state = SDL_GetKeyboardState(NULL);

    int key = as_number(argv[1]);
    return NEW_BOOL(state[key]);
}

Value dw_size(vm_t *vm, int argc, Value *argv)
{
    PiContext *ctx = get_ctx(argv[0]);
    if (!ctx)
        return NEW_NIL();

    list_t *list = list_create(sizeof(Value));

    list_add(list, &NEW_NUM((double)ctx->width));
    list_add(list, &NEW_NUM((double)ctx->height));

    PiList *_list = (PiList *)new_list(list);

    return NEW_OBJ(list);
}

// Check if window is still running
Value dw_isRunning(vm_t *vm, int argc, Value *argv)
{
    PiContext *ctx = get_ctx(argv[0]);
    if (!ctx)
        return NEW_BOOL(false);

    return ctx->running ? NEW_BOOL(true) : NEW_BOOL(false);
}

// Manually present the frame
Value dw_present(vm_t *vm, int argc, Value *argv)
{
    PiContext *ctx = get_ctx(argv[0]);
    if (!ctx)
        vm_error(vm, "present() takes a canvas as first argument");

    SDL_RenderPresent(ctx->renderer);
    return NEW_NIL();
}

// Add this function to pi_draw.c
Value dw_onFrame(vm_t *vm, int argc, Value *argv)
{
    PiContext *ctx = get_ctx(argv[0]);
    if (!ctx)
        vm_error(vm, "on_frame() takes a canvas as first argument");

    if (argc < 2)
        vm_error(vm, "on_frame() needs a callback function");

    if (!IS_FUN(argv[1]))
        vm_error(vm, "on_frame() second argument must be a function");

    // Store the callback in the context
    ctx->frame_callback = argv[1];

    return NEW_NIL();
}

// Close the window
Value dw_close(vm_t *vm, int argc, Value *argv)
{
    PiContext *ctx = get_ctx(argv[0]);
    if (!ctx)
        return NEW_NIL();

    ctx->running = false;
    return NEW_NIL();
}

// Register event handler
Value dw_on(vm_t *vm, int argc, Value *argv)
{
    // Usage: on(canvas, event_type, callback)
    PiContext *ctx = get_ctx(argv[0]);
    if (!ctx)
        vm_error(vm, "on() takes a canvas as first argument");

    if (argc < 3)
        vm_error(vm, "on() needs event type and callback");

    if (!IS_STRING(argv[1]))
        vm_error(vm, "event type must be a string");
    const char *event_name = AS_CSTRING(argv[1]);

    EventType type = get_eventType(event_name);
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
Value dw_off(vm_t *vm, int argc, Value *argv)
{
    // Usage: off(canvas, event_type, [callback])
    PiContext *ctx = get_ctx(argv[0]);
    if (!ctx)
        vm_error(vm, "off() takes a canvas as first argument");

    if (argc < 2)
        vm_error(vm, "off() needs event type");

    if (!IS_STRING(argv[1]))
        vm_error(vm, "event type must be a string");
    const char *event_name = AS_CSTRING(argv[1]);

    EventType type = get_eventType(event_name);
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
Value dw_poll(vm_t *vm, int argc, Value *argv)
{
    // Usage: poll(canvas) -> event or nil
    PiContext *ctx = get_ctx(argv[0]);
    if (!ctx)
        return NEW_NIL();

    pump_events(vm, ctx);

    PiEvent *e = pop_event(ctx);
    if (!e)
        return NEW_NIL();

    return event_toValue(vm, e);
}

// Wait for event (blocking)
Value dw_wait(vm_t *vm, int argc, Value *argv)
{
    // Usage: wait(canvas) -> event
    PiContext *ctx = get_ctx(argv[0]);
    if (!ctx)
        vm_error(vm, "wait() takes a canvas as first argument");

    while (ctx->running)
    {
        pump_events(vm, ctx);

        PiEvent *e = pop_event(ctx);
        if (e)
            return event_toValue(vm, e);

        SDL_Delay(10);
    }

    return NEW_NIL();
}

// Module Registration
static BuiltinConst draw_const[] = {
    {"COLOR_BLACK", NEW_NUM(0x000000)},
    {"COLOR_WHITE", NEW_NUM(0xFFFFFF)},
    {"COLOR_RED", NEW_NUM(0xFF0000)},
    {"COLOR_GREEN", NEW_NUM(0x00FF00)},
    {"COLOR_BLUE", NEW_NUM(0x0000FF)},
    {"COLOR_YELLOW", NEW_NUM(0xFFFF00)},
    {"COLOR_MAGENTA", NEW_NUM(0xFF00FF)},
    {"COLOR_CYAN", NEW_NUM(0x00FFFF)},
    {"COLOR_ORANGE", NEW_NUM(0xFFA500)},
    {"COLOR_PURPLE", NEW_NUM(0x800080)},
    {"COLOR_BROWN", NEW_NUM(0xA52A2A)},
    {"COLOR_GRAY", NEW_NUM(0x808080)},
    {"COLOR_LIGHT_GRAY", NEW_NUM(0xD3D3D3)},
    {"COLOR_DARK_GRAY", NEW_NUM(0xA9A9A9)},
    {"COLOR_TRANSPARENT", NEW_NUM(0x00000000)},

    // font styles
    // {"FONT_NORMAL", NEW_NUM(0)},
    // {"FONT_BOLD", NEW_NUM(1)},
    // {"FONT_ITALIC", NEW_NUM(2)},
    // {"FONT_UNDERLINE", NEW_NUM(4)},
    // {"FONT_ALIGN_LEFT", NEW_NUM(0)},
    // {"FONT_ALIGN_CENTER", NEW_NUM(1)},
    // {"FONT_ALIGN_RIGHT", NEW_NUM(2)},
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
    {"on_frame", dw_onFrame},
    {"on", dw_on},
    {"off", dw_off},
    {"poll", dw_poll},
    {"wait", dw_wait},
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
