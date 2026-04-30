#ifndef PI_OBJECT_H
#define PI_OBJECT_H

#include <stdint.h>
#ifndef __EMSCRIPTEN__
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#else
typedef struct SDL_Rect
{
    int x;
    int y;
    int w;
    int h;
} SDL_Rect;
#endif

#include "pi_value.h"
#include "list.h"
#include "table.h"
#include "common.h"

#define OBJ_TYPE(o) (AS_OBJ(o)->type)
#define IS_OBJ_TYPE(o, _type) (IS_OBJ(o) && AS_OBJ(o)->type == _type)

#define IS_STRING(o) IS_OBJ_TYPE(o, OBJ_STRING)
#define IS_LIST(o) IS_OBJ_TYPE(o, OBJ_LIST)
#define IS_MATRIX(o) IS_OBJ_TYPE(o, OBJ_MATRIX)
#define IS_NUM_LIST(o) (IS_LIST(o) && AS_LIST(o)->is_numeric)
#define IS_MAP(o) IS_OBJ_TYPE(o, OBJ_MAP)
#define IS_OBJECT(o) (IS_MAP(o) && AS_MAP(o)->proto != NULL)
#define IS_MODULE(o) IS_OBJ_TYPE(o, OBJ_MODULE)
#define IS_FUN(o) IS_OBJ_TYPE(o, OBJ_FUN)
#define IS_RANGE(o) IS_OBJ_TYPE(o, OBJ_RANGE)
#define IS_SET(o) IS_OBJ_TYPE(o, OBJ_SET)
#define IS_TUPLE(o) IS_OBJ_TYPE(o, OBJ_TUPLE)

#define IS_CONTEXT(o) IS_OBJ_TYPE(o, OBJ_CONTEXT)
#define IS_CHART(o) IS_OBJ_TYPE(o, OBJ_CHART)
#define IS_EVENT(o) IS_OBJ_TYPE(o, OBJ_EVENT)

#define IS_COLLECTION(o) (IS_LIST(o) || IS_MATRIX(o) || IS_MAP(o) || IS_SET(o) || IS_STRING(o))

#define IS_SEQUENCE(o) (IS_LIST(o) || IS_STRING(o))

#define AS_STRING(o) ((PiString *)AS_OBJ(o))
#define AS_LIST(o) ((PiList *)AS_OBJ(o))
#define AS_MATRIX(o) ((PiMatrix *)AS_OBJ(o))
#define AS_MAP(o) ((PiMap *)AS_OBJ(o))
#define AS_MODULE(o) ((ObjModule *)AS_OBJ(o))
#define AS_RANGE(o) ((PiRange *)AS_OBJ(o))
#define AS_SET(o) ((PiSet *)AS_OBJ(o))
#define AS_TUPLE(o) ((PiTuple *)AS_OBJ(o))
#define AS_FUN(o) ((Function *)AS_OBJ(o))
#define AS_CODE(o) ((ObjCode *)AS_OBJ(o))
#define AS_FILE(o) ((ObjFile *)AS_OBJ(o))

#define AS_CONTEXT(o) ((PiContext *)AS_OBJ(o))
#define AS_CHART(o) ((PiChart *)AS_OBJ(o))
#define AS_EVENT(o) ((PiEvent *)AS_OBJ(o))

#define AS_CSTRING(o) AS_STRING(o)->chars

#define AS_CLIST(o) AS_LIST(o)->items
#define AS_CMAP(o) AS_MAP(o)->table

#define PISTR_SIZE(o) AS_STRING(o)->length
#define PIMAP_SIZE(o) AS_MAP(o)->table->size
#define PILIST_SIZE(o) AS_LIST(o)->items->size

#define COL_LENGTH(o) (IS_LIST(o) ? PILIST_SIZE(o) : PISTR_SIZE(o))

#define PILIST_GETAT(o, i, t) (*(t *)list_getAt(AS_CLIST(o), i))

typedef enum
{
    OBJ_STRING,
    OBJ_LIST,
    OBJ_MATRIX,
    OBJ_MAP,
    OBJ_SET,
    OBJ_TUPLE,
    OBJ_MODULE,
    OBJ_RANGE,
    OBJ_FUN,
    OBJ_CODE,
    OBJ_FILE,
    OBJ_IMAGE,
    OBJ_SPRITE,
    OBJ_MODEL3D,
    OBJ_SOUND,

    OBJ_CONTEXT, // drawing context
    OBJ_CHART,   // chart context
    OBJ_EVENT,

} o_type;

typedef struct ObjModule ObjModule;

typedef enum
{
    GC_WHITE, // Unmarked, potentially unreachable
    GC_GRAY,  // Marked but children not yet processed
    GC_BLACK  // Marked and all children processed
} GCColor;

struct Object
{
    o_type type;
    bool is_marked; // Flag to indicate if the object is marked for garbage collection
    bool in_gcList; // Flag to indicate if the object is in the GC list

    GCColor gc_color;

    struct Object *next;
};

typedef struct
{
    char name[32];            // event name, e.g. "click"
    Value *fns[MAX_HANDLERS]; // registered pilang callables
    int count;                // number of registered handlers
} HandlerList;

typedef struct
{
    Object object;
    char *type;           // Event type string
    EventType event_type; // Enum for fast switching

    // Common fields
    int x, y;          // Position for mouse events
    int dx, dy;        // Delta for motion/scroll
    char *key;         // Key name for keyboard events
    int button;        // Button number for mouse events
    bool pressed;      // Pressed state for key/mouse
    int width, height; // New size for resize events
} PiEvent;

typedef struct
{
    PiEvent **events;
    int head;
    int tail;
    int count;
    int capacity;
} EventQueue;

typedef struct
{
    Object object;
    char *chars;
    size_t length;
    uint32_t hash;

    int current;
} PiString;

typedef struct
{
    Object object;
    double start;
    double end;
    double step;

    double current; // Iterator state: current value in the range
} PiRange;

typedef struct
{
    Object object;
    list_t *items;

    int current;     // Iterator state
    bool is_numeric; // Flag to indicate if the list contains only double values
    bool is_matrix;  // Flag to indicate if the list is a 2D matrix

    // Matrix dimensions
    int rows;
    int cols;

} PiList;

typedef struct
{
    Object object;
    double *data;
    int rows;
    int cols;
    int current;
} PiMatrix;

typedef enum
{
    TN_FLOAT32,
    TN_FLOAT64,
    TN_INT32,
    TN_INT64,
} TN_TYPE;

typedef struct
{
    Object object; // Object header
    // double *data;  // contiguous block of memory
    union
    {
        double *f32;
        double *f64;
        int32_t *i32;
        int64_t *i64;
    } data;       // union for different data types
    TN_TYPE type; // data type (e.g., float32, int64)
    int *shape;   // array of dimensions
    int *strides; // steps in memory for each dimension
    int ndim;     // number of dimensions
    int size;     // total number of elements
} PiTensor;

typedef struct PiMap
{
    Object object;
    table_t *table;
    char *intrinsic_name;
    bool is_instance;
    Object *super_instance;

    struct PiMap *proto; // Prototype map for inheritance and method lookup

    // int current; // Iterator state
    ht_iter it;

} PiMap;

typedef struct
{
    Object object;
    table_t *table; // Use table for unique elements, keys are values, values are dummy
} PiSet;

typedef struct
{
    Object object;
    list_t *items; // List of values for tuple
} PiTuple;

typedef struct
{
    Object object;
    list_t *data;
    list_t *param_names; // Parameter names for functions using this code

    uint32_t hash;
} ObjCode;

typedef struct
{
    Object object;
    FILE *fp;
    bool closed;
    char *mode;
    char *filename;
} ObjFile;

// Update PiContext structure
typedef struct PiContext
{

    Object object;

    // Existing fields
    void *window;
    void *renderer;
    int width, height;
    bool running;
    float tx, ty, sx, sy, angle, alpha;
    void *font;            // default font
    void *transform_stack; // stack of TransformState

    void *userdata; /* PiDraw extra state (transform stack, font, fps); owned by builtin/pi_draw.c */

    Value frame_callback;

    SDL_Rect clip_rect; // clipping rectangle
    bool clip_enabled;

    // Event handling
    HandlerList handlers[EVENT_COUNT];

    // Event queue (using simple array or list)
    EventQueue *eventQueue;

    // FPS tracking
    uint32_t last_fps_time;
    int frame_count;
    double current_fps;

    // Mouse state
    int mouse_x, mouse_y;
    uint32_t mouse_buttons;
} PiContext;

typedef struct
{
    Object object;

    PiContext *ctx; // where to draw

    list_t *series; // list of plotted data
    list_t *colors; // optional styling

    double xmin, xmax;
    double ymin, ymax;

    bool has_bounds;

    bool show_grid;
    bool show_axes;
    bool show_ticks;

    char *title;
    char *xlabel;
    char *ylabel;
} PiChart;

uint32_t string_hash(char *chars, size_t length);
Object *new_pistring(char *str);
PiString *copy_pistring(char *chars, int length);

Object *new_list(list_t *items);
Object *new_matrix(int rows, int cols);
double matrix_get(PiMatrix *matrix, int row, int col);
void matrix_set(PiMatrix *matrix, int row, int col, double value);
Object *matrix_rowAsList(PiMatrix *matrix, int row);

Object *new_map(table_t *table, bool is_instance);

Object *new_set(table_t *table);

Object *new_tuple(list_t *items);

Object *new_file(FILE *file, char *filename, char *mode);

Value map_get(PiMap *map, Value key);
void map_set(PiMap *map, Value key, Value value);
bool map_has(PiMap *map, Value key);
bool map_delete(PiMap *map, Value key);
PiMap *map_owner(PiMap *map, Value key);

int map_size(PiMap *map);

Object *new_range(double start, double end, double step);

uint32_t code_hash(uint8_t *code);
Object *new_code(list_t *code);

Object *new_context();
Object *new_chart(PiContext *ctx);
Object *new_event(const char *type, EventType event_type);

void iter_reset(Object *col);
bool iter_hasNext(Object *col);
Value iter_next(Object *col);
bool is_iterable(Object *obj);
int get_index(int index, int length);
Value get_slice(Object *sequence, double start, double end, double step);

#endif
