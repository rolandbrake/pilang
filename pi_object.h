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
#include "pi_list.h"
#include "pi_table.h"
#include "common.h"

#define OBJ_TYPE(o) (AS_OBJ(o)->type)
#define IS_OBJ_TYPE(o, _type) (IS_OBJ(o) && AS_OBJ(o)->type == _type)

#define IS_STRING(o) IS_OBJ_TYPE(o, OBJ_STRING)
#define IS_LIST(o) IS_OBJ_TYPE(o, OBJ_LIST)
#define IS_TENSOR(o) IS_OBJ_TYPE(o, OBJ_TENSOR)
#define IS_NUM_LIST(o) (IS_LIST(o) && AS_LIST(o)->is_numeric)
#define IS_MAP(o) IS_OBJ_TYPE(o, OBJ_MAP)
#define IS_CLASS(o) IS_OBJ_TYPE(o, OBJ_CLASS)
#define IS_INSTANCE(o) IS_OBJ_TYPE(o, OBJ_INSTANCE)
#define IS_OBJECT(o) (IS_CLASS(o) || IS_INSTANCE(o))
#define IS_MODULE(o) IS_OBJ_TYPE(o, OBJ_MODULE)
#define IS_FUN(o) IS_OBJ_TYPE(o, OBJ_FUN)
#define IS_RANGE(o) IS_OBJ_TYPE(o, OBJ_RANGE)
#define IS_SLICE(o) IS_OBJ_TYPE(o, OBJ_SLICE)
#define IS_SET(o) IS_OBJ_TYPE(o, OBJ_SET)
#define IS_TUPLE(o) IS_OBJ_TYPE(o, OBJ_TUPLE)

#define IS_CONTEXT(o) IS_OBJ_TYPE(o, OBJ_CONTEXT)
#define IS_CHART(o) IS_OBJ_TYPE(o, OBJ_CHART)
#define IS_CHART3D(o) IS_OBJ_TYPE(o, OBJ_CHART3D)
#define IS_EVENT(o) IS_OBJ_TYPE(o, OBJ_EVENT)

#define IS_IMAGE(o) IS_OBJ_TYPE(o, OBJ_IMAGE)
#define AS_IMAGE(o) ((ObjImage *)AS_OBJ(o))

#define IS_COLLECTION(o) (IS_LIST(o) || IS_TENSOR(o) || IS_MAP(o) || IS_SET(o) || IS_TUPLE(o) || IS_STRING(o))

#define IS_SEQUENCE(o) (IS_LIST(o) || IS_STRING(o) || IS_TUPLE(o))

#define AS_STRING(o) ((PiString *)AS_OBJ(o))
#define AS_LIST(o) ((PiList *)AS_OBJ(o))
#define AS_TENSOR(o) ((PiTensor *)AS_OBJ(o))
#define AS_MAP(o) ((PiMap *)AS_OBJ(o))
#define AS_CLASS(o) ((PiClass *)AS_OBJ(o))
#define AS_INSTANCE(o) ((PiInstance *)AS_OBJ(o))
#define AS_MODULE(o) ((ObjModule *)AS_OBJ(o))
#define AS_RANGE(o) ((PiRange *)AS_OBJ(o))
#define AS_SLICE(o) ((PiSlice *)AS_OBJ(o))
#define AS_SET(o) ((PiSet *)AS_OBJ(o))
#define AS_TUPLE(o) ((PiTuple *)AS_OBJ(o))
#define AS_FUN(o) ((Function *)AS_OBJ(o))
#define AS_CODE(o) ((ObjCode *)AS_OBJ(o))
#define AS_FILE(o) ((ObjFile *)AS_OBJ(o))

#define AS_CONTEXT(o) ((PiContext *)AS_OBJ(o))
#define AS_CHART(o) ((PiChart *)AS_OBJ(o))
#define AS_CHART3D(o) ((PiChart3D *)AS_OBJ(o))
#define AS_EVENT(o) ((PiEvent *)AS_OBJ(o))

#define AS_CSTRING(o) AS_STRING(o)->chars

#define AS_CLIST(o) AS_LIST(o)->items
#define AS_CMAP(o) AS_MAP(o)->table

#define PISTR_SIZE(o) AS_STRING(o)->length
#define PIMAP_SIZE(o) AS_MAP(o)->table->size
#define PILIST_SIZE(o) AS_LIST(o)->items->size

#define COL_LENGTH(o) (IS_LIST(o) ? PILIST_SIZE(o) : PISTR_SIZE(o))

#define PILIST_GETAT(o, i, t) (*(t *)list_getAt(AS_CLIST(o), i))

#define OBJECT_HEAD Object object;

typedef enum
{
    OBJ_STRING,
    OBJ_LIST,
    OBJ_TENSOR,
    OBJ_MAP,
    OBJ_CLASS,
    OBJ_INSTANCE,
    OBJ_SET,
    OBJ_TUPLE,
    OBJ_MODULE,
    OBJ_RANGE,
    OBJ_SLICE,
    OBJ_FUN,
    OBJ_CODE,
    OBJ_FILE,
    OBJ_IMAGE,
    OBJ_SPRITE,
    OBJ_MODEL3D,
    OBJ_SOUND,

    OBJ_CONTEXT, // drawing context
    OBJ_CHART,   // chart context
    OBJ_CHART3D, // 3D chart context
    OBJ_EVENT,

} o_type;

typedef struct ObjModule ObjModule;
typedef struct PiMap PiMap;
typedef struct PiClass PiClass;
typedef struct PiInstance PiInstance;

#define BOUND_CACHE_SIZE 8

typedef struct
{
    uint64_t key_hash;
    Object *key;

    table_t *owner_table;
    uint64_t owner_version;

    uint64_t class_epoch;

    uint64_t fields_version;

    Value bound_fn;
    bool valid;
} BoundCache;

typedef enum
{
    GC_WHITE, // Unmarked, potentially unreachable
    GC_GRAY,  // Marked but children not yet processed
    GC_BLACK  // Marked and all children processed
} GCColor;

struct Object
{
    o_type type;
    uint64_t id;
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
    uint64_t hash;

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
    double start;
    double stop;
    double step;

} PiSlice;

typedef struct
{
    Object object;
    list_t *items;

    int current;     // Iterator state
    bool is_numeric; // Cached flag: true if every item is numeric

} PiList;

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
    union
    {
        float *f32;   /* TN_FLOAT32 - 32-bit IEEE float                  */
        double *f64;  /* TN_FLOAT64 - 64-bit IEEE double                 */
        int32_t *i32; /* TN_INT32   - 32-bit signed integer              */
        int64_t *i64; /* TN_INT64   - 64-bit signed integer              */
        void *raw;    /* untyped pointer used for float casts in getFlat */

    } data; // union for different data types

    TN_TYPE type; // data type (e.g., float32, int64)
    int *shape;   // array of dimensions
    int *strides; // steps in memory for each dimension
    int ndim;     // number of dimensions
    int size;     // total number of elements
    int rows;     // cached shape[0] for rank-2 matrix compatibility helpers
    int cols;     // cached shape[1] for rank-2 matrix compatibility helpers
    int current;  // Iterator state along the first dimension
} PiTensor;

typedef struct PiClass
{
    Object object;

    char *name;
    struct PiClass *super;
    table_t *members;

    table_t field_names; // map field names to their indices
    uint16_t slot_count;

    uint64_t version;
    BoundCache bound_cache[BOUND_CACHE_SIZE];
    uint8_t bound_cache_next;
    ht_iter it;

} PiClass;

typedef struct PiInstance
{
    Object object;

    PiClass *_class;
    Value *slots;

    table_t *fields;
    BoundCache bound_cache[BOUND_CACHE_SIZE];
    uint8_t bound_cache_next;
    ht_iter it;

} PiInstance;

typedef struct PiMap
{
    Object object;
    table_t *table;
    ht_iter it;

} PiMap;

typedef struct
{
    Object object;
    void *table; // private hash table backing this PiSet
    int current; // iterator state for traversing the set
} PiSet;

typedef struct
{
    Object object;
    list_t *items; // List of values for tuple
    int current;   // Iterator state
} PiTuple;

/* Resolved global slots belong to compiled code, not to a VM instance. */
typedef struct GlobalCache
{
    Value *slots[UINT8_MAX + 1];
    table_t *globals;
    list_t *names;
} GlobalCache;

typedef struct
{
    Object object;
    list_t *data;
    list_t *param_names; // Parameter names for functions using this code

    bool need_args;
    bool need_kwargs;

    bool method_need_args;
    bool method_need_kwargs;

    GlobalCache global_cache;

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

#ifndef __EMSCRIPTEN__
typedef struct
{
    Object object;
    SDL_Surface *surface;
} ObjImage;

ObjImage *new_image(SDL_Surface *surface);
#endif

struct PiChart3D;

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
    void *font;             // default font
    void *_transform_stack; // stack of _transformState

    void *userdata; /* PiDraw extra state (_transform stack, font, fps); owned by builtin/pi_draw.c */

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

    bool plot_subplots_cleared;

    // Mouse state
    int mouse_x, mouse_y;
    uint32_t mouse_buttons;

    struct PiChart3D *active_plot3d;
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

    int subplot_rows;
    int subplot_cols;
    int subplot_index;

    char *title;
    char *xlabel;
    char *ylabel;
} PiChart;

typedef struct PiChart3D
{
    Object object;

    PiContext *ctx;
    list_t *series;

    bool show_grid;
    bool show_axes;

    int subplot_rows;
    int subplot_cols;
    int subplot_index;

    double azimuth;
    double elevation;
    double distance;

    char *title;
    char *xlabel;
    char *ylabel;
    char *zlabel;
} PiChart3D;

uint64_t string_hash(char *chars, size_t length);

Object *alloc_object(size_t size, o_type type);

Object *new_pistring(char *str);
PiString *copy_pistring(char *chars, int length);

Object *new_list(list_t *items);

Object *new_tensor(int ndim, int *shape, TN_TYPE type);
Object *new_tensorUninit(int ndim, int *shape, TN_TYPE type);

double tensor_get(PiTensor *tensor, int *indices);
void tensor_set(PiTensor *tensor, int *indices, double value);
double tensor_getFlat(PiTensor *tensor, int index);
void tensor_setFlat(PiTensor *tensor, int index, double value);
Object *tensor_rowAsList(PiTensor *tensor, int row);

Object *new_map(table_t *table);

Object *new_class(const char *name, PiClass *super, table_t *members);
Object *new_instance(PiClass *_class);

bool class_getMember(PiClass *_class, const char *name, Value *out);
bool class_getMemberHash(PiClass *_class, const char *name, uint64_t hash, Value *out);

void class_setMember(PiClass *_class, const char *name, Value value);
bool class_deleteMember(PiClass *_class, const char *name);
uint64_t class_mutationVersion(void);

bool instance_getMember(PiInstance *instance, const char *name, Value *out);
bool instance_getMemberHash(PiInstance *instance, const char *name, uint64_t hash, Value *out);

void instance_setMember(PiInstance *instance, const char *name, Value value);

Object *new_set(void); // Create empty set

bool set_add(PiSet *set, Value value); // Add element
bool set_has(PiSet *set, Value value); // Check membership

bool set_remove(PiSet *set, Value value); // Remove element

int set_size(PiSet *set); // Get size

Value set_get(PiSet *set, int index); // Get value by iteration order

void set_clear(PiSet *set); // Remove all elements
void set_free(PiSet *set);  // Free memory

Object *new_tuple(list_t *items);

Object *new_file(FILE *file, char *filename, char *mode);

Value map_get(PiMap *map, Value key);
Value map_getValueByKey(PiMap *map, const char *key);
void map_setValueByKey(PiMap *map, const char *key, Value value);

void map_set(PiMap *map, Value key, Value value);
bool map_has(PiMap *map, Value key);
bool map_delete(PiMap *map, Value key);

int map_size(PiMap *map);

Object *new_range(double start, double end, double step);
Object *new_slice(double start, double end, double step);

uint32_t code_hash(uint8_t *code);
Object *new_code(list_t *code);

Object *new_context();
Object *new_chart(PiContext *ctx);
Object *new_chart3d(PiContext *ctx);
Object *new_event(const char *type, EventType event_type);

void iter_reset(Object *col);
bool iter_hasNext(Object *col);
Value iter_next(Object *col);
bool is_iterable(Object *obj);

int get_index(int index, int length);
int slice_index(int index, int length, int step);
Value get_slice(Object *sequence, double start, double end, double step);

#endif
