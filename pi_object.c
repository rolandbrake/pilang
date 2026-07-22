#include <math.h>
#include <string.h>
#include <limits.h>
#include "pi_object.h"
#include "common.h"

#define CREATE_OBJ(obj, type) (obj *)alloc_object(sizeof(obj), type)

typedef struct
{
    bool occupied;
    uint64_t hash;
    Value value;
} set_item;

typedef struct set_table
{
    int size;
    int capacity;
    set_item *items;
    list_t *values; /* insertion-order list for indexed access / iteration */
} set_table;

/* Monotonically increasing runtime object identifier. */
static uint64_t _ID = 1;

Object *alloc_object(size_t size, o_type type)
{
    Object *obj = (Object *)malloc(size);
    if (!obj)
        error("[create_obj] Memory allocation failed.");
    obj->type = type;
    obj->id = _ID++;
    obj->is_marked = false;
    obj->in_gcList = false;
    obj->gc_color = GC_WHITE;
    obj->next = NULL;
    return obj;
}

/* FNV-1a hash used for string interning, maps, and sets. */
uint32_t string_hash(char *chars, size_t length)
{
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < length; i++)
    {
        hash ^= (uint8_t)chars[i];
        hash *= 16777619u;
    }
    return hash;
}

Object *new_pistring(char *str)
{
    PiString *string = CREATE_OBJ(PiString, OBJ_STRING);
    string->chars = str;
    string->length = strlen(str);
    string->hash = string_hash(str, string->length);
    string->current = 0;
    return (Object *)string;
}

PiString *copy_pistring(char *chars, int length)
{
    PiString *string = CREATE_OBJ(PiString, OBJ_STRING);
    string->length = length;
    string->chars = malloc(length + 1);
    if (!string->chars)
        error("[copy_pistring] Memory allocation failed.");
    memcpy(string->chars, chars, length);
    string->chars[length] = '\0';
    string->hash = string_hash(chars, length);
    string->current = 0;
    return string;
}

Object *new_list(list_t *items)
{
    PiList *list = CREATE_OBJ(PiList, OBJ_LIST);
    list->items = items;
    list->current = 0;
    list->is_numeric = false;
    list->cols = -1;
    list->rows = -1;
    return (Object *)list;
}

static int tensor_elemSize(TN_TYPE type)
{
    switch (type)
    {
    case TN_FLOAT32:
        return sizeof(float);
    case TN_FLOAT64:
        return sizeof(double);
    case TN_INT32:
        return sizeof(int32_t);
    case TN_INT64:
        return sizeof(int64_t);
    }
    return sizeof(double);
}

/* Shared constructor used by both zero-init and uninit paths. */
static Object *new_tensorWithInit(int ndim, int *shape, TN_TYPE type, bool zero_initialize)
{
    if (ndim < 0 || ndim > MAX_TENSOR_DIMS)
        error("[new_tensor] Invalid tensor rank.");

    PiTensor *tensor = CREATE_OBJ(PiTensor, OBJ_TENSOR);
    tensor->type = type;
    tensor->ndim = ndim;
    tensor->size = 1;
    tensor->current = 0;
    tensor->rows = ndim > 0 ? shape[0] : 0;
    tensor->cols = ndim > 1 ? shape[1] : 1;

    tensor->shape = malloc(sizeof(int) * (size_t)ndim);
    tensor->strides = malloc(sizeof(int) * (size_t)ndim);
    if (!tensor->shape || !tensor->strides)
        error("[new_tensor] Memory allocation failed.");

    /* Compute total element count, checking for overflow. */
    size_t elem_count = 1;
    for (int i = 0; i < ndim; i++)
    {
        if (shape[i] < 0)
            error("[new_tensor] Tensor shape dimensions must be non-negative.");
        if (shape[i] != 0 && elem_count > (size_t)INT_MAX / (size_t)shape[i])
            error("[new_tensor] Tensor is too large: element count exceeds runtime limit.");
        tensor->shape[i] = shape[i];
        elem_count *= (size_t)shape[i];
    }
    tensor->size = (int)elem_count;

    /* Row-major (C-order) strides. */
    size_t stride = 1;
    for (int i = ndim - 1; i >= 0; i--)
    {
        if (stride > (size_t)INT_MAX)
            error("[new_tensor] Tensor strides exceed runtime limit.");
        tensor->strides[i] = (int)stride;
        stride *= (size_t)shape[i];
    }

    size_t elem_size = (size_t)tensor_elemSize(type);
    if (elem_count != 0 && elem_size > SIZE_MAX / elem_count)
        error("[new_tensor] Tensor data allocation size overflow.");

    if (elem_count == 0)
        tensor->data.f64 = NULL;
    else if (zero_initialize)
        tensor->data.f64 = calloc(elem_count, elem_size);
    else
        tensor->data.f64 = malloc(elem_count * elem_size);

    if (elem_count != 0 && !tensor->data.f64)
        error("[new_tensor] Data allocation failed.");

    return (Object *)tensor;
}

Object *new_tensor(int ndim, int *shape, TN_TYPE type)
{
    return new_tensorWithInit(ndim, shape, type, true);
}

Object *new_tensorUninit(int ndim, int *shape, TN_TYPE type)
{
    return new_tensorWithInit(ndim, shape, type, false);
}

static int tensor_offset(PiTensor *tensor, int *indices)
{
    int offset = 0;
    for (int i = 0; i < tensor->ndim; i++)
        offset += get_index(indices[i], tensor->shape[i]) * tensor->strides[i];
    return offset;
}

double tensor_getFlat(PiTensor *tensor, int index)
{
    switch (tensor->type)
    {
    case TN_FLOAT32:
        return ((float *)tensor->data.raw)[index];
    case TN_FLOAT64:
        return tensor->data.f64[index];
    case TN_INT32:
        return tensor->data.i32[index];
    case TN_INT64:
        return (double)tensor->data.i64[index];
    }
    return 0.0;
}

void tensor_setFlat(PiTensor *tensor, int index, double value)
{
    switch (tensor->type)
    {
    case TN_FLOAT32:
        ((float *)tensor->data.raw)[index] = (float)value;
        break;
    case TN_FLOAT64:
        tensor->data.f64[index] = value;
        break;
    case TN_INT32:
        tensor->data.i32[index] = (int32_t)value;
        break;
    case TN_INT64:
        tensor->data.i64[index] = (int64_t)value;
        break;
    }
}

double tensor_get(PiTensor *tensor, int *indices)
{
    return tensor_getFlat(tensor, tensor_offset(tensor, indices));
}

void tensor_set(PiTensor *tensor, int *indices, double value)
{
    tensor_setFlat(tensor, tensor_offset(tensor, indices), value);
}

Object *tensor_rowAsList(PiTensor *tensor, int row)
{
    list_t *items = list_create(sizeof(Value));
    if (tensor->ndim == 0)
        return new_list(items);

    int row_size = tensor->size / tensor->shape[0];
    int start = get_index(row, tensor->shape[0]) * row_size;

    for (int i = 0; i < row_size; i++)
    {
        Value value = NEW_NUM(tensor_getFlat(tensor, start + i));
        list_add(items, &value);
    }

    PiList *list = (PiList *)new_list(items);
    list->is_numeric = true;
    list->is_matrix = false;
    list->rows = 1;
    list->cols = row_size;
    return (Object *)list;
}

Object *new_map(table_t *table, bool is_instance)
{
    PiMap *map = CREATE_OBJ(PiMap, OBJ_MAP);
    map->table = table;
    map->bound_cache = NULL; // allocated lazily; plain dicts never touch this
    map->_key = NULL;
    map->_value = NEW_NIL();
    map->version = 0;
    map->owner_version = 0;
    map->owner = NULL;
    map->intrinsic_name = NULL;
    map->flags = MAP_BRACKET;
    map->it = ht_iterator(table);
    MAP_SET_FLAG(map, MAP_IS_INSTANCE, is_instance);
    map->super_instance = NULL;
    map->proto = NULL;
    return (Object *)map;
}

Object *new_set(void)
{
    PiSet *set = CREATE_OBJ(PiSet, OBJ_SET);

    set_table *table = malloc(sizeof(set_table));
    if (!table)
        error("[new_set] Memory allocation for set table failed.");

    table->size = 0;
    table->capacity = INIT_CAP;
    table->items = calloc((size_t)table->capacity, sizeof(set_item));
    table->values = list_create(sizeof(Value));
    if (!table->items || !table->values)
        error("[new_set] Memory allocation for set table failed.");

    set->table = table;
    set->current = 0;
    return (Object *)set;
}

/* Returns the slot index for value, or -1 if not present.
 * Centralises the probe loop so set_has and set_remove share one copy. */
static int set_find(set_table *table, Value value, uint64_t hash)
{
    int mask = table->capacity - 1;
    int index = (int)(hash & (uint64_t)mask);

    while (table->items[index].occupied)
    {
        if (table->items[index].hash == hash &&
            value_keyEquals(table->items[index].value, value))
            return index;
        index = (index + 1) & mask;
    }
    return -1;
}

/* Insert value into table without a grow check — caller is responsible.
 * append=false skips the values list, used during rehash. */
static void set_insertRaw(set_table *table, Value value, uint64_t hash, bool append)
{
    int mask = table->capacity - 1;
    int index = (int)(hash & (uint64_t)mask);

    while (table->items[index].occupied)
    {
        if (table->items[index].hash == hash &&
            value_keyEquals(table->items[index].value, value))
        {
            /* Update in-place; do not add a duplicate to values. */
            table->items[index].value = value;
            return;
        }
        index = (index + 1) & mask;
    }

    table->items[index].occupied = true;
    table->items[index].hash = hash;
    table->items[index].value = value;
    table->size++;
    if (append)
        list_add(table->values, &value);
}

static void set_grow(PiSet *set)
{
    set_table *table = set->table;
    int old_cap = table->capacity;
    set_item *old_items = table->items;

    table->capacity *= 2;
    table->items = calloc((size_t)table->capacity, sizeof(set_item));
    if (!table->items)
        error("[set_grow] Memory allocation failed.");

    /* Rehash all occupied slots.  The values list already has the right
     * order, so skip appending (append=false). */
    table->size = 0;
    for (int i = 0; i < old_cap; i++)
    {
        if (old_items[i].occupied)
            set_insertRaw(table, old_items[i].value, old_items[i].hash, false);
    }
    free(old_items);
}

bool set_add(PiSet *set, Value value)
{
    set_table *table = set->table;

    /* Grow when load factor exceeds 75%. */
    if ((table->size + 1) * 4 > table->capacity * 3)
        set_grow(set);

    set_insertRaw(table, value, value_hash(value), true);
    return true;
}

bool set_has(PiSet *set, Value value)
{
    set_table *table = set ? set->table : NULL;
    if (!table)
        return false;
    return set_find(table, value, value_hash(value)) >= 0;
}

bool set_remove(PiSet *set, Value value)
{
    if (!set || !set->table)
        return false;

    set_table *table = set->table;
    if (set_find(table, value, value_hash(value)) < 0)
        return false;

    /* Rebuild the table from the ordered values list, skipping the removed
     * element.  This preserves insertion order in the values list. */
    int old_cap = table->capacity;
    set_item *old_items = table->items;
    list_t *old_values = table->values;

    table->size = 0;
    table->items = calloc((size_t)old_cap, sizeof(set_item));
    table->values = list_create(sizeof(Value));
    if (!table->items || !table->values)
        error("[set_remove] Memory allocation failed.");

    for (int i = 0; i < old_values->size; i++)
    {
        Value item = *(Value *)list_getAt(old_values, i);
        if (!value_keyEquals(item, value))
            set_insertRaw(table, item, value_hash(item), true);
    }

    free(old_items);
    list_free(old_values);
    set->current = 0;
    return true;
}

int set_size(PiSet *set)
{
    set_table *table = set ? set->table : NULL;
    return table ? table->size : 0;
}

Value set_get(PiSet *set, int index)
{
    set_table *table = set ? set->table : NULL;
    if (!table || !table->values || index < 0 || index >= table->values->size)
        return NEW_NIL();
    return *(Value *)list_getAt(table->values, index);
}

void set_clear(PiSet *set)
{
    if (!set || !set->table)
        return;

    set_table *table = set->table;
    free(table->items);
    list_free(table->values);

    table->size = 0;
    table->capacity = INIT_CAP;
    table->items = calloc((size_t)table->capacity, sizeof(set_item));
    table->values = list_create(sizeof(Value));
    if (!table->items || !table->values)
        error("[set_clear] Memory allocation failed.");

    set->current = 0;
}

void set_free(PiSet *set)
{
    if (!set)
        return;
    set_table *table = set->table;
    if (table)
    {
        free(table->items);
        list_free(table->values);
        free(table);
        set->table = NULL;
    }
    free(set);
}

Object *new_tuple(list_t *items)
{
    PiTuple *tuple = CREATE_OBJ(PiTuple, OBJ_TUPLE);
    tuple->items = items;
    tuple->current = 0;
    return (Object *)tuple;
}

Object *new_file(FILE *file, char *filename, char *mode)
{
    ObjFile *f = CREATE_OBJ(ObjFile, OBJ_FILE);
    f->fp = file;
    f->filename = filename;
    f->mode = mode;
    f->closed = false;
    return (Object *)f;
}

Object *new_range(double start, double end, double step)
{
    PiRange *range = CREATE_OBJ(PiRange, OBJ_RANGE);
    range->start = start;
    range->end = end;
    range->step = step;
    range->current = start;
    return (Object *)range;
}

Object *new_slice(double start, double stop, double step)
{
    PiSlice *slice = CREATE_OBJ(PiSlice, OBJ_SLICE);
    slice->start = start;
    slice->stop = stop;
    slice->step = step;
    return (Object *)slice;
}

/* Walks the prototype chain and returns the map that owns the key. */
static PiMap *map_findOwner(PiMap *map, const char *key_str)
{
    while (map != NULL)
    {
        if (ht_get(map->table, key_str) != NULL)
            return map;
        map = map->proto;
    }
    return NULL;
}

/* Member names are compiled PiStrings.  Borrow their stable character buffer
 * instead of allocating a duplicate through as_string(); other key types keep
 * the existing conversion path and report ownership through owned_out. */
static inline const char *map_keyChars(Value key, char **owned_out)
{
    if (IS_STRING(key))
    {
        *owned_out = NULL;
        return AS_CSTRING(key);
    }
    *owned_out = as_string(key);
    return *owned_out;
}

static inline bool map_cacheHit(PiMap *map, Value key)
{
    return IS_STRING(key) &&
           map->_key == AS_OBJ(key) &&
           map->owner != NULL &&
           map->owner->version == map->owner_version;
}

static inline void map_cacheStore(PiMap *map, Value key, PiMap *owner, Value value)
{
    if (!IS_STRING(key) || owner == NULL)
        return;

    map->_key = AS_OBJ(key);
    map->_value = value;
    map->owner = owner;
    map->owner_version = owner->version;
}

void map_dirty(PiMap *map)
{
    if (!map)
        return;

    map->version++;
    map->_key = NULL;
    map->_value = NEW_NIL();
    map->owner = NULL;
    map->owner_version = 0;
}

Value map_get(PiMap *map, Value key)
{
    if (map_cacheHit(map, key))
        return map->_value;

    char *owned;
    const char *key_str = map_keyChars(key, &owned);
    PiMap *owner = map_findOwner(map, key_str);
    void *item = owner ? ht_get(owner->table, key_str) : NULL;
    Value value = item ? *(Value *)item : NEW_NIL();
    if (item)
        map_cacheStore(map, key, owner, value);
    free(owned);
    return value;
}

Value map_getValueByKey(PiMap *map, const char *key)
{
    PiMap *owner = map_findOwner(map, key);
    void *item = owner ? ht_get(owner->table, key) : NULL;
    return item ? *(Value *)item : NEW_NIL();
}

bool map_has(PiMap *map, Value key)
{
    char *owned;
    const char *key_str = map_keyChars(key, &owned);
    bool found = map_findOwner(map, key_str) != NULL;
    free(owned);
    return found;
}

bool map_delete(PiMap *map, Value key)
{
    char *owned;
    const char *key_str = map_keyChars(key, &owned);
    PiMap *owner = map_findOwner(map, key_str);
    bool removed = false;
    if (owner != NULL && !MAP_HAS_FLAG(owner, MAP_LOCKED))
    {
        removed = ht_delete(owner->table, key_str);
        if (removed)
            map_dirty(owner);
    }
    free(owned);
    return removed;
}

/* Updates an existing prototype owner when possible; inserts into current map otherwise. */
void map_set(PiMap *map, Value key, Value value)
{
    char *owned;
    const char *key_str = map_keyChars(key, &owned);
    PiMap *owner = map_findOwner(map, key_str);
    if (owner == NULL)
        owner = map;

    bool changed = ht_set(owner->table, key_str, &value);
    if (!changed && !MAP_HAS_FLAG(owner, MAP_LOCKED))
        changed = ht_put(owner->table, key_str, &value);
    if (changed)
        map_dirty(owner);

    if (IS_FUN(value))
    {
        if (strcmp(key_str, "compute") == 0)
            MAP_SET_FLAG(owner, MAP_HAS_COMPUTE, true);
        if (strcmp(key_str, "rcompute") == 0)
            MAP_SET_FLAG(owner, MAP_HAS_RCOMPUTE, true);
    }

    free(owned);
}

PiMap *map_owner(PiMap *map, Value key)
{
    if (map_cacheHit(map, key))
        return map->owner;

    char *owned;
    const char *key_str = map_keyChars(key, &owned);
    PiMap *owner = map_findOwner(map, key_str);
    if (owner)
    {
        Value *item = ht_get(owner->table, key_str);
        if (item)
            map_cacheStore(map, key, owner, *item);
    }
    free(owned);
    return owner;
}

int map_size(PiMap *map)
{
    return map->table->size;
}

uint32_t code_hash(uint8_t *code)
{
    uint32_t hash = 2166136261u;
    for (int i = 0; i < 16; i++)
    {
        hash ^= code[i];
        hash *= 16777619u;
    }
    return hash;
}

Object *new_code(list_t *code)
{
    ObjCode *c = CREATE_OBJ(ObjCode, OBJ_CODE);
    c->hash = code_hash((uint8_t *)code->data);
    c->data = code;
    c->param_names = NULL;
    c->need_args = false;
    c->need_kwargs = false;
    c->method_need_args = false;
    c->method_need_kwargs = false;
    memset(&c->global_cache, 0, sizeof(c->global_cache));
    return (Object *)c;
}

Object *new_context(void)
{
    PiContext *ctx = CREATE_OBJ(PiContext, OBJ_CONTEXT);

    ctx->window = NULL;
    ctx->renderer = NULL;
    ctx->width = 0;
    ctx->height = 0;
    ctx->tx = 0;
    ctx->ty = 0;
    ctx->sx = 1;
    ctx->sy = 1;
    ctx->angle = 0;
    ctx->alpha = 1.0f;
    ctx->running = true;
    ctx->userdata = NULL;
    ctx->frame_callback = NEW_NIL();
    ctx->_transform_stack = NULL;
    ctx->font = NULL;
    ctx->clip_rect = (SDL_Rect){0, 0, 0, 0};
    ctx->clip_enabled = false;
    ctx->plot_subplots_cleared = false;
    ctx->active_plot3d = NULL;

    return (Object *)ctx;
}

Object *new_chart(PiContext *ctx)
{
    PiChart *chart = CREATE_OBJ(PiChart, OBJ_CHART);
    chart->ctx = ctx;
    chart->series = list_create(VALUE_SIZE);
    chart->colors = list_create(VALUE_SIZE);
    chart->xmin = 0.0;
    chart->xmax = 1.0;
    chart->ymin = 0.0;
    chart->ymax = 1.0;
    chart->has_bounds = false;
    chart->show_grid = true;
    chart->show_axes = true;
    chart->show_ticks = true;
    chart->subplot_rows = 1;
    chart->subplot_cols = 1;
    chart->subplot_index = 1;
    chart->title = NULL;
    chart->xlabel = NULL;
    chart->ylabel = NULL;
    return (Object *)chart;
}

Object *new_chart3d(PiContext *ctx)
{
    PiChart3D *chart = CREATE_OBJ(PiChart3D, OBJ_CHART3D);
    chart->ctx = ctx;
    chart->series = list_create(VALUE_SIZE);
    chart->show_grid = true;
    chart->show_axes = true;
    chart->subplot_rows = 1;
    chart->subplot_cols = 1;
    chart->subplot_index = 1;
    chart->azimuth = -45.0;
    chart->elevation = 28.0;
    chart->distance = 1.0;
    chart->title = NULL;
    chart->xlabel = NULL;
    chart->ylabel = NULL;
    chart->zlabel = NULL;
    return (Object *)chart;
}

Object *new_event(const char *type, EventType event_type)
{
    PiEvent *e = CREATE_OBJ(PiEvent, OBJ_EVENT);
    e->type = strdup(type);
    e->event_type = event_type;
    e->x = 0;
    e->y = 0;
    e->dx = 0;
    e->dy = 0;
    e->key = NULL;
    e->button = 0;
    e->pressed = false;
    e->width = 0;
    e->height = 0;
    return (Object *)e;
}

void iter_reset(Object *col)
{
    switch (col->type)
    {
    case OBJ_RANGE:
        ((PiRange *)col)->current = ((PiRange *)col)->start;
        break;
    case OBJ_LIST:
        ((PiList *)col)->current = 0;
        break;
    case OBJ_TENSOR:
        ((PiTensor *)col)->current = 0;
        break;
    case OBJ_STRING:
        ((PiString *)col)->current = 0;
        break;
    case OBJ_TUPLE:
        ((PiTuple *)col)->current = 0;
        break;
    case OBJ_MAP:
        ht_reset(&((PiMap *)col)->it);
        break;
    case OBJ_SET:
        ((PiSet *)col)->current = 0;
        break;
    default:
        fprintf(stderr, "Object type is not iterable.\n");
        exit(EXIT_FAILURE);
    }
}

bool iter_hasNext(Object *col)
{
    switch (col->type)
    {
    case OBJ_LIST:
    {
        PiList *list = (PiList *)col;
        return list->current < (int)LIST_SIZE(list->items);
    }
    case OBJ_TENSOR:
    {
        PiTensor *t = (PiTensor *)col;
        return t->ndim > 0 && t->current < t->shape[0];
    }
    case OBJ_STRING:
    {
        PiString *str = (PiString *)col;
        return str->current < (int)str->length;
    }
    case OBJ_RANGE:
    {
        PiRange *r = (PiRange *)col;
        return r->step > 0 ? r->current < r->end : r->current > r->end;
    }
    case OBJ_MAP:
        return ht_hasNext(&((PiMap *)col)->it);
    case OBJ_SET:
    {
        PiSet *set = (PiSet *)col;
        return set->current < set_size(set);
    }
    case OBJ_TUPLE:
    {
        PiTuple *t = (PiTuple *)col;
        return t->current < (int)LIST_SIZE(t->items);
    }
    default:
        return false;
    }
}

Value iter_next(Object *col)
{
    switch (col->type)
    {
    case OBJ_LIST:
    {
        PiList *list = (PiList *)col;
        return *(Value *)list_getAt(list->items, list->current++);
    }
    case OBJ_TENSOR:
    {
        PiTensor *tensor = (PiTensor *)col;
        return NEW_OBJ(tensor_rowAsList(tensor, tensor->current++));
    }
    case OBJ_STRING:
    {
        PiString *str = (PiString *)col;
        char *chars = malloc(2); /* single char + NUL */
        chars[0] = str->chars[str->current++];
        chars[1] = '\0';
        return NEW_OBJ(new_pistring(chars));
    }
    case OBJ_RANGE:
    {
        PiRange *range = (PiRange *)col;
        Value value = NEW_NUM(range->current);
        range->current += range->step;
        return value;
    }
    case OBJ_TUPLE:
    {
        PiTuple *tuple = (PiTuple *)col;
        return *(Value *)list_getAt(tuple->items, tuple->current++);
    }
    case OBJ_MAP:
    {
        PiMap *map = (PiMap *)col;
        ht_next(&map->it);
        return *(Value *)map->it.value;
    }
    case OBJ_SET:
    {
        PiSet *set = (PiSet *)col;
        return set_get(set, set->current++);
    }
    default:
        fprintf(stderr, "Invalid col type for iteration.\n");
        exit(EXIT_FAILURE);
    }
}

bool is_iterable(Object *obj)
{
    if (!obj)
        return false;
    switch (obj->type)
    {
    case OBJ_LIST:
    case OBJ_TENSOR:
    case OBJ_STRING:
    case OBJ_RANGE:
    case OBJ_MAP:
    case OBJ_SET:
    case OBJ_TUPLE:
        return true;
    default:
        return false;
    }
}

int get_index(int index, int length)
{
    if (length <= 0)
        error("Index out of range.");
    if (index < 0)
        index += length;
    if (index < 0 || index >= length)
        error("Index out of range.");
    return index;
}

/* Python-style slice bound normalisation. */
int slice_index(int index, int length, int step)
{
    if (index < 0)
        index += length;

    if (step > 0)
    {
        /* Forward slice: clamp to [0, length]. */
        if (index < 0)
            index = 0;
        if (index > length)
            index = length;
    }
    else
    {
        /* Reverse slice: clamp to [-1, length-1]. */
        if (index < -1)
            index = -1;
        if (index > length - 1)
            index = length - 1;
    }
    return index;
}

static Value slice_sequence(list_t *src, int start, int end, int step, bool is_tuple)
{
    list_t *dst = list_create(sizeof(Value));

    if (step > 0)
    {
        for (int i = start; i < end; i += step)
            list_add(dst, list_getAt(src, i));
    }
    else
    {
        for (int i = start; i > end; i += step)
            list_add(dst, list_getAt(src, i));
    }

    return is_tuple ? NEW_OBJ(new_tuple(dst)) : NEW_OBJ(new_list(dst));
}

Value get_slice(Object *sequence, double start, double end, double step)
{
    if (step == 0)
    {
        fprintf(stderr, "Slice step cannot be zero.\n");
        exit(EXIT_FAILURE);
    }

    int _step = (int)step;

    switch (sequence->type)
    {
    case OBJ_LIST:
    case OBJ_TUPLE:
    {
        list_t *src = sequence->type == OBJ_LIST
                          ? ((PiList *)sequence)->items
                          : ((PiTuple *)sequence)->items;
        int size = LIST_SIZE(src);

        int _start = isinf(start) ? (_step > 0 ? 0 : size - 1)
                                  : slice_index((int)start, size, _step);
        int _end = isinf(end) ? (_step > 0 ? size : -1)
                              : slice_index((int)end, size, _step);

        return slice_sequence(src, _start, _end, _step, sequence->type == OBJ_TUPLE);
    }

    case OBJ_STRING:
    {
        PiString *str = (PiString *)sequence;
        int size = (int)str->length;

        int _start = isinf(start) ? (_step > 0 ? 0 : size - 1)
                                  : slice_index((int)start, size, _step);
        int _end = isinf(end) ? (_step > 0 ? size : -1)
                              : slice_index((int)end, size, _step);

        /* Pre-count exactly how many characters the slice produces so we
         * allocate the right number of bytes (not size+1 for every slice). */
        int count = 0;
        if (_step > 0)
        {
            for (int i = _start; i < _end; i += _step)
                count++;
        }
        else
        {
            for (int i = _start; i > _end; i += _step)
                count++;
        }

        char *buf = malloc((size_t)count + 1);
        if (!buf)
            error("[get_slice] Memory allocation failed.");

        int j = 0;
        if (_step > 0)
        {
            for (int i = _start; i < _end; i += _step)
                buf[j++] = str->chars[i];
        }
        else
        {
            for (int i = _start; i > _end; i += _step)
                buf[j++] = str->chars[i];
        }
        buf[j] = '\0';

        return NEW_OBJ(new_pistring(buf));
    }

    default:
        fprintf(stderr, "Invalid sequence type.\n");
        exit(EXIT_FAILURE);
    }
}
