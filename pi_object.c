#include <math.h>
#include <string.h>
#include <limits.h>
#include "pi_object.h"
#include "common.h"

#define CREATE_OBJ(obj, type) (obj *)create_obj(sizeof(obj), type)

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
    list_t *values;
} set_table;

static uint64_t _ID = 1;

static Object *create_obj(size_t size, o_type type)
{
    Object *obj = (Object *)malloc(size);
    if (!obj)
        error("[create_obj] Memory allocation failed.");
    obj->type = type;
    obj->id = _ID++;
    obj->is_marked = false;
    obj->in_gcList = false;
    obj->gc_color = GC_WHITE;

    return obj;
}

/**
 * Calculates the hash of a string.
 *
 * The hash is calculated by the djb2 algorithm, which is a simple string
 * hashing algorithm. The algorithm is as follows:
 *
 * 1. Set the hash to a prime number (here, 2166136261).
 * 2. Iterate through the string and for each character:
 *    a. XOR the hash with the character.
 *    b. Multiply the hash with a prime number (here, 16777619).
 * 3. Return the hash.
 *
 * @param chars The string to hash.
 * @param length The length of the string.
 * @return The hash of the string.
 */
uint32_t string_hash(char *chars, size_t length)
{
    uint32_t hash = 2166136261u;
    for (int i = 0; i < length; i++)
    {
        hash ^= (uint8_t)chars[i];
        hash *= 16777619;
    }
    return hash;
}

/**
 * Creates a new PiString object from a given C-string.
 *
 * This function creates a new PiString object, which is a wrapper around
 * a C-string with additional metadata like length and hash. It calculates
 * the length and hash of the provided string and sets the initial state
 * for iteration.
 *
 * @param str The C-string to wrap in a PiString object.
 * @return A pointer to the newly created PiString object, cast as Object.
 */
Object *new_pistring(char *str)
{
    // Allocate memory for the PiString object and initialize its type
    PiString *string = CREATE_OBJ(PiString, OBJ_STRING);

    // Set the string characters to the input C-string
    string->chars = str;

    // Calculate and store the length of the string
    string->length = strlen(str);

    // Calculate and store the hash of the string
    string->hash = string_hash(str, string->length);

    // Initialize the current position for iteration
    string->current = 0;

    // Return the PiString object cast as a generic Object
    return (Object *)string;
}

/**
 * Copies a given C-string into a new PiString object.
 *
 * This function allocates a new PiString object and copies the
 * given C-string into it. It also calculates the hash of the
 * string and sets the initial state for iteration.
 *
 * @param chars The C-string to copy into the new PiString object.
 * @param length The length of the input C-string.
 * @return A pointer to the newly created PiString object.
 */
PiString *copy_pistring(char *chars, int length)
{
    PiString *string = CREATE_OBJ(PiString, OBJ_STRING);

    string->length = length;
    string->chars = malloc(length + 1);

    if (!string->chars)
        error("[copy_pistring] Memory allocation failed.");

    // Copy the input C-string into the new PiString object
    memcpy(string->chars, chars, length);
    string->chars[length] = '\0';

    // Calculate the hash of the string
    string->hash = string_hash(chars, length);

    // Initialize the current position for iteration
    string->current = 0;

    return string;
}

/**
 * Creates a new PiList object containing the given list of items.
 *
 * @param items The list of items to contain in the new PiList object.
 * @return The newly created PiList object.
 */
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

static Object *new_tensorWithInit(int ndim, int *shape, TN_TYPE type, bool zero_initialize)
{
    PiTensor *tensor = CREATE_OBJ(PiTensor, OBJ_TENSOR);
    tensor->type = type;
    tensor->ndim = ndim;
    tensor->size = 1;
    tensor->current = 0;
    tensor->rows = ndim > 0 ? shape[0] : 0;
    tensor->cols = ndim > 1 ? shape[1] : 1;

    if (ndim < 0 || ndim > MAX_TENSOR_DIMS)
        error("[new_tensor] Invalid tensor rank.");

    tensor->shape = malloc(sizeof(int) * (size_t)ndim);
    tensor->strides = malloc(sizeof(int) * (size_t)ndim);

    if (!tensor->shape || !tensor->strides)
        error("[new_tensor] Memory allocation failed.");

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
        return ((float *)tensor->data.f32)[index];
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
        ((float *)tensor->data.f32)[index] = (float)value;
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

/**
 * Creates a new PiMap object from a given table.
 *
 * @param table The underlying table that this object wraps.
 * @param is_instance Whether this object is an instance of another object.
 * @return The newly created PiMap object.
 */
Object *new_map(table_t *table, bool is_instance)
{
    PiMap *map = CREATE_OBJ(PiMap, OBJ_MAP);

    // Store the given table in the object
    map->table = table;
    map->intrinsic_name = NULL;
    map->locked = false;
    map->bracket_access = true;
    map->has_compute = false;
    map->has_rcompute = false;

    // Initialize the iterator for the object
    map->it = ht_iterator(table);

    // Store whether this object is an instance of another object
    map->is_instance = is_instance;
    map->super_instance = NULL;

    // Set the prototype to NULL
    map->proto = NULL;

    return (Object *)map;
}

Object *new_set(void)
{
    PiSet *set = CREATE_OBJ(PiSet, OBJ_SET);

    // Store the given table in the object and initialize iterator state
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

static bool _set_insert(PiSet *set, Value value, bool append)
{
    set_table *table = set->table;

    if ((table->size + 1) * 4 > table->capacity * 3)
    {
        int old_capacity = table->capacity;
        set_item *old_items = table->items;

        table->capacity *= 2;
        table->items = calloc((size_t)table->capacity, sizeof(set_item));
        if (!table->items)
            error("[set_add] Memory allocation for set table failed.");

        table->size = 0;
        for (int i = 0; i < old_capacity; i++)
        {
            if (old_items[i].occupied)
                _set_insert(set, old_items[i].value, false);
        }
        free(old_items);
    }

    uint64_t hash = value_hash(value);
    int mask = table->capacity - 1;
    int index = (int)(hash & (uint64_t)mask);

    while (table->items[index].occupied)
    {
        if (table->items[index].hash == hash &&
            value_keyEquals(table->items[index].value, value))
        {
            table->items[index].value = value;
            return true;
        }
        index = (index + 1) & mask;
    }

    table->items[index].occupied = true;
    table->items[index].hash = hash;
    table->items[index].value = value;
    table->size++;

    if (append)
        list_add(table->values, &value);

    return true;
}

bool set_add(PiSet *set, Value value)
{
    return _set_insert(set, value, true);
}

bool set_has(PiSet *set, Value value)
{
    set_table *table = set->table;
    if (!table)
        return false;

    uint64_t hash = value_hash(value);
    int mask = table->capacity - 1;
    int index = (int)(hash & (uint64_t)mask);

    while (table->items[index].occupied)
    {
        if (table->items[index].hash == hash &&
            value_keyEquals(table->items[index].value, value))
            return true;
        index = (index + 1) & mask;
    }

    return false;
}

bool set_remove(PiSet *set, Value value)
{
    if (!set || !set->table || !set_has(set, value))
        return false;

    set_table *table = set->table;
    int old_capacity = table->capacity;
    set_item *old_items = table->items;
    list_t *old_values = table->values;

    table->size = 0;
    table->items = calloc((size_t)old_capacity, sizeof(set_item));
    table->values = list_create(sizeof(Value));
    if (!table->items || !table->values)
        error("[set_remove] Memory allocation for set table failed.");

    for (int i = 0; i < old_values->size; i++)
    {
        Value item = *(Value *)list_getAt(old_values, i);
        if (!value_keyEquals(item, value))
            _set_insert(set, item, true);
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
        error("[set_clear] Memory allocation for set table failed.");

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

/**
 * Creates a new ObjFile object that represents a file stream.
 *
 * @param file The underlying FILE * that this object wraps.
 * @param filename The name of the file.
 * @param mode The mode string that was used to open the file.
 * @return The newly created ObjFile object.
 */
Object *new_file(FILE *file, char *filename, char *mode)
{
    ObjFile *f = CREATE_OBJ(ObjFile, OBJ_FILE);

    f->fp = file;
    f->filename = filename;
    f->mode = mode;
    f->closed = false;

    return (Object *)f;
}

/**
 * Finds the owner of a given key in a PiMap.
 *
 * This function searches for the specified key in the map's
 * underlying table, and if found, it returns the map itself.
 * If the key does not exist in the map, it searches up the prototype
 * chain until it finds the key or reaches the root of the prototype
 * chain.
 *
 * @param map The map to search for the given key.
 * @param key_str The key to search for.
 * @return The owner of the key if found, NULL otherwise.
 */
static PiMap *map_findOwner(PiMap *map, const char *key_str)
{
    // Search for the key in the map's underlying table
    while (map != NULL)
    {
        // If the key is found in the map, return the map itself
        if (ht_get(map->table, key_str) != NULL)
            return map;

        // If the key is not found in the map, move up the prototype chain
        map = map->proto;
    }

    // If the key is not found in any map in the prototype chain, return NULL
    return NULL;
}

/**
 * Retrieves a value from a PiMap by a given key.
 *
 * This function searches for the specified key in the map's
 * underlying table. If the key exists, it returns the associated
 * value. Otherwise, it returns nil.
 *
 * @param map The map to search for the given key.
 * @param key The key to search for.
 * @return The associated value if the key exists, nil otherwise.
 */
Value map_get(PiMap *map, Value key)
{
    // Convert the key to a string
    char *key_str = as_string(key);

    // Find the owner map of this key
    PiMap *owner = map_findOwner(map, key_str);

    // Search for the item in the owner's table
    void *item = owner ? ht_get(owner->table, key_str) : NULL;

    // Free the allocated key string
    free(key_str);

    // Check if the item was found; if not, return nil
    if (item == NULL)
    {
        return NEW_NIL();
    }

    // Return the found value
    return *(Value *)item;
}

Value map_getValueByKey(PiMap *map, const char *key)
{
    PiMap *owner = map_findOwner(map, key);
    void *item = owner ? ht_get(owner->table, key) : NULL;
    return item == NULL ? NEW_NIL() : *(Value *)item;
}

/**
 * Checks if a given key exists in a PiMap.
 *
 * This function searches for the specified key in the map's
 * underlying table. If the key exists, it returns true.
 * Otherwise, it returns false.
 *
 * @param map The map to search for the given key.
 * @param key The key to search for.
 * @return true if the key exists in the map, false otherwise.
 */
bool map_has(PiMap *map, Value key)
{
    char *key_str = as_string(key);
    bool found = map_findOwner(map, key_str) != NULL;
    free(key_str);
    return found;
}

bool map_delete(PiMap *map, Value key)
{
    char *key_str = as_string(key);
    PiMap *owner = map_findOwner(map, key_str);
    bool removed = false;

    if (owner != NULL && !owner->locked)
        removed = ht_delete(owner->table, key_str);

    free(key_str);
    return removed;
}

/**
 * Sets the value associated with a given key in a PiMap.
 *
 * This function either creates a new key-value pair in the map's
 * underlying table or updates the value associated with an existing
 * key. If the key does not exist in the table, it is added. If the
 * key already exists, its associated value is updated.
 *
 * @param map The map in which to set the value.
 * @param key The key with which to associate the value.
 * @param value The value to associate with the given key.
 */
void map_set(PiMap *map, Value key, Value value)
{
    char *key_str = as_string(key);
    PiMap *owner = map_findOwner(map, key_str);
    if (owner == NULL)
        owner = map;

    // Attempt to set the item in the hash table using the key
    bool updated = ht_set(owner->table, key_str, &value);

    // If the key does not exist in the table, add it
    if (!updated && !owner->locked)
        ht_put(owner->table, key_str, &value);

    if (IS_FUN(value))
    {
        if (strcmp(key_str, "compute") == 0)
            owner->has_compute = true;
        else if (strcmp(key_str, "rcompute") == 0)
            owner->has_rcompute = true;
    }

    free(key_str);
}

PiMap *map_owner(PiMap *map, Value key)
{
    char *key_str = as_string(key);
    PiMap *owner = map_findOwner(map, key_str);
    free(key_str);
    return owner;
}

/**
 * Returns the size of a PiMap.
 *
 * This function returns the number of key-value pairs in the map's
 * underlying table.
 *
 * @param map The map for which to return the size.
 * @return The number of key-value pairs in the map.
 */
int map_size(PiMap *map)
{
    return map->table->size;
}

/**
 * Computes a hash value for a given block of code using a variant of the FNV-1a hash algorithm.
 *
 * This function processes the first 16 bytes of the input code array and
 * returns a 32-bit hash value. It's designed for quick hashing of small data blocks.
 *
 * @param code A pointer to the code block (array of bytes) to be hashed.
 * @return A 32-bit hash value representing the input code block.
 */
uint32_t code_hash(uint8_t *code)
{
    uint32_t hash = 2166136261u; // FNV offset basis
    for (int i = 0; i < 16; i++)
    {
        hash ^= code[i];  // XOR the next byte into the hash
        hash *= 16777619; // Multiply by FNV prime
    }
    return hash;
}

/**
 * Creates a new ObjCode object containing the given code.
 *
 * This function creates a new ObjCode object containing the given
 * code list and computes a hash value for the code using the
 * code_hash() function. The hash value is stored in the object for
 * quick comparison.
 *
 * @param code A pointer to the code list to be stored in the object.
 * @return The newly created ObjCode object.
 */
Object *new_code(list_t *code)
{
    ObjCode *c = CREATE_OBJ(ObjCode, OBJ_CODE);

    // Compute the hash value of the code
    c->hash = code_hash((uint8_t *)code->data);

    // Store the code list in the object
    c->data = code;
    c->param_names = NULL;

    return (Object *)c;
}

Object *new_context()
{
    PiContext *ctx = (PiContext *)malloc(sizeof(PiContext));

    ctx->object.type = OBJ_CONTEXT;
    ctx->object.id = _ID++;
    ctx->object.is_marked = false;
    ctx->object.in_gcList = false;
    ctx->object.gc_color = GC_WHITE;

    ctx->object.next = NULL;

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

    ctx->transform_stack = NULL;

    ctx->font = NULL;
    ctx->clip_rect = (SDL_Rect){0, 0, 0, 0};
    ctx->clip_enabled = false;

    return (Object *)ctx;
}

Object *new_chart(PiContext *ctx)
{
    PiChart *chart = CREATE_OBJ(PiChart, OBJ_CHART);

    chart->object.next = NULL; /* create_obj does not zero the allocation */
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
    chart->title = NULL;
    chart->xlabel = NULL;
    chart->ylabel = NULL;

    return (Object *)chart;
}

// Create a new PiEvent (allocates memory)
Object *new_event(const char *type, EventType event_type)
{
    PiEvent *e = (PiEvent *)malloc(sizeof(PiEvent));

    e->object.type = OBJ_EVENT;
    e->object.id = _ID++;
    e->object.is_marked = false;
    e->object.in_gcList = false;
    e->object.gc_color = GC_WHITE;

    e->type = strdup(type);
    e->event_type = event_type;

    // Initialize fields to defaults
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

/**
 * Creates a new ObjRange object with the given start, end, and step values.
 *
 * This function allocates a new ObjRange object and initializes its
 * start, end, and step fields with the given values. The current value
 * of the range is set to the start value.
 *
 * @param start The start value of the range (inclusive).
 * @param end The end value of the range (exclusive).
 * @param step The step value of the range.
 * @return The newly created ObjRange object.
 */
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

/**
 * Resets the given iterable object to its initial state.
 *
 * This function resets the current item in the given iterable object
 * to its initial state. For ranges, this means the start value. For
 * lists and strings, this means the first element. For maps, this
 * means the first key-value pair.
 *
 * @param col The iterable object to be reset.
 */
void iter_reset(Object *col)
{
    switch (col->type)
    {
    case OBJ_RANGE:
        // Reset the current value of the range to its start value
        ((PiRange *)col)->current = ((PiRange *)col)->start;
        break;
    case OBJ_LIST:
        // Reset the current index of the list to 0
        ((PiList *)col)->current = 0;
        break;
    case OBJ_TENSOR:
        ((PiTensor *)col)->current = 0;
        break;
    case OBJ_STRING:
        // Reset the current index of the string to 0
        ((PiString *)col)->current = 0;
        break;
    case OBJ_TUPLE:
        ((PiTuple *)col)->current = 0;
        break;
    case OBJ_MAP:
    {
        // Reset the map's iterator to its first key-value pair
        PiMap *map = (PiMap *)col;
        ht_reset(&map->it);
        break;
    }
    case OBJ_SET:
    {
        // Reset the set's iterator to its first element
        PiSet *set = (PiSet *)col;
        set->current = 0;
        break;
    }
    default:
        // Raise an error if the object type is not iterable
        fprintf(stderr, "Object type is not iterable.\n");
        exit(EXIT_FAILURE);
    }
}

/**
 * Checks if the given iterable object has more items to iterate.
 *
 * This function takes an iterable object and checks if there are more
 * items to iterate. If the object is a range, it checks if the current
 * value is less than (or greater than, if the step is negative) the end
 * value. If the object is a list or string, it checks if the current index
 * is less than the length of the list or string. If the object is a map, it
 * checks if there are more key-value pairs to iterate.
 *
 * @param col The iterable object to be checked.
 * @return true if the object has more items to iterate, false otherwise.
 */
bool iter_hasNext(Object *col)
{
    o_type type = col->type;
    if (type == OBJ_LIST)
    {
        PiList *list = (PiList *)col;
        // Check if the current index is less than the length of the list
        return list->current < LIST_SIZE(list->items);
    }
    else if (type == OBJ_TENSOR)
    {
        PiTensor *tensor = (PiTensor *)col;
        return tensor->ndim > 0 && tensor->current < tensor->shape[0];
    }
    else if (type == OBJ_STRING)
    {
        PiString *str = (PiString *)col;
        // Check if the current index is less than the length of the string
        return str->current < str->length;
    }
    else if (type == OBJ_RANGE)
    {
        PiRange *range = (PiRange *)col;
        // If step is positive, check if current < end
        // If step is negative, check if current > end
        return (range->step > 0) ? (range->current < range->end) : (range->current > range->end);
    }
    else if (type == OBJ_MAP)
    {
        PiMap *map = (PiMap *)col;
        // Check if there are more key-value pairs to iterate
        return ht_hasNext(&map->it);
    }
    else if (type == OBJ_SET)
    {
        PiSet *set = (PiSet *)col;
        return set->current < set_size(set);
    }
    else if (type == OBJ_TUPLE)
    {
        PiTuple *tuple = (PiTuple *)col;
        return tuple->current < LIST_SIZE(tuple->items);
    }
    return false;
}

/**
 * Retrieves the next item from the iterable object.
 *
 * This function takes an iterable object and returns the next item in the
 * iteration. If the object is a list or string, it returns the item at the
 * current index. If the object is a range, it returns the current value and
 * increments the current value by the step. If the object is a map, it returns
 * the value associated with the current key.
 *
 * @param col The iterable object to retrieve the next item from.
 * @return The next item in the iteration.
 */
Value iter_next(Object *col)
{
    o_type type = col->type;
    if (type == OBJ_LIST)
    {
        PiList *list = (PiList *)col;
        Value value = *(Value *)list_getAt(list->items, list->current);
        list->current++;
        return value;
    }
    else if (type == OBJ_TENSOR)
    {
        PiTensor *tensor = (PiTensor *)col;
        Value value = NEW_OBJ(tensor_rowAsList(tensor, tensor->current));
        tensor->current++;
        return value;
    }
    else if (type == OBJ_STRING)
    {
        PiString *str = (PiString *)col;
        char *_chars = malloc(2); // 1 char + null terminator
        _chars[0] = str->chars[str->current];
        _chars[1] = '\0';
        Value value = NEW_OBJ(new_pistring(_chars));
        str->current++;
        return value;
    }
    else if (type == OBJ_RANGE)
    {
        PiRange *range = (PiRange *)col;
        Value value = NEW_NUM(range->current);
        range->current += range->step;
        return value;
    }
    else if (type == OBJ_TUPLE)
    {
        PiTuple *tuple = (PiTuple *)col;
        Value value = *(Value *)list_getAt(tuple->items, tuple->current);
        tuple->current++;
        return value;
    }
    else if (type == OBJ_MAP)
    {
        PiMap *map = (PiMap *)col;
        ht_next(&map->it);
        return *(Value *)map->it.value;
    }
    else if (type == OBJ_SET)
    {
        PiSet *set = (PiSet *)col;
        return set_get(set, set->current++);
    }

    fprintf(stderr, "Invalid col type for iteration.\n");
    exit(EXIT_FAILURE);
}

/**
 * @brief Check if an object is iterable.
 *
 * This function determines whether a given object can be iterated over.
 * Supported iterable types include lists, strings, ranges, and maps.
 *
 * @param obj The object to check for iterability.
 * @return true if the object is iterable, false otherwise.
 */
bool is_iterable(Object *obj)
{
    if (!obj)
        return false; // Return false if the object is null

    switch (obj->type)
    {
    case OBJ_LIST:
    case OBJ_TENSOR:
    case OBJ_STRING:
    case OBJ_RANGE:
    case OBJ_MAP:
    case OBJ_SET:
    case OBJ_TUPLE:
        return true; // Return true for iterable types
    default:
        return false; // Return false for non-iterable types
    }
}

/**
 * @brief Converts a given index to a valid index within a sequence.
 *
 * @details This function takes an index and a sequence length as input, and
 * returns a valid index within the sequence. If the index is negative, it is
 * converted to a positive index by adding the sequence length. If the index is
 * greater than the sequence length, it is wrapped around to the beginning of
 * the sequence by taking the modulo of the sequence length.
 *
 * @param index The index to convert.
 * @param length The length of the sequence.
 * @return A valid index within the sequence.
 */
int get_index(int index, int length)
{
    if (length == 0)
        return 0;
    int _index = index % length;
    if (_index < 0) // Handle negative indices
        _index += length;
    return _index;
}

int slice_index(int index, int length, int step)
{

    // Handle negative indexing first (Python rule)
    if (index < 0)
        index += length;

    if (step > 0)
    {
        // Forward slice: clamp to [0, length]
        if (index < 0)
            index = 0;
        if (index > length)
            index = length;
    }
    else
    {
        // Reverse slice: clamp to [-1, length-1]
        if (index < -1)
            index = -1;
        if (index > length - 1)
            index = length - 1;
    }

    return index;
}

/**
 * Retrieves a slice of a sequence (list or string) from the specified start
 * index to the specified end index with the specified step.
 *
 * @param sequence The sequence (list or string) to retrieve the slice from.
 * @param start The index to start the slice from.
 * @param end The index to end the slice at.
 * @param step The increment between each element in the slice.
 * @return A new sequence containing the sliced elements.
 */
Value get_slice(Object *sequence, double start, double end, double step)
{
    int size = 0;
    int _start, _end, _step;

    if (step == 0)
    {
        fprintf(stderr, "Slice step cannot be zero.\n");
        exit(EXIT_FAILURE);
    }

    _step = (int)step;

    if (sequence->type == OBJ_LIST)
    {
        PiList *list = (PiList *)sequence;
        size = LIST_SIZE(list->items);

        // START
        if (isinf(start))
        {
            _start = (_step > 0) ? 0 : size - 1;
        }
        else
        {
            _start = slice_index((int)start, size, _step);
        }

        // END
        if (isinf(end))
        {
            _end = (_step > 0) ? size : -1;
        }
        else
        {
            _end = slice_index((int)end, size, _step);
        }

        list_t *s_list = list_create(sizeof(Value));

        if (_step > 0)
        {
            while (_start < _end)
            {
                Value *item = (Value *)list_getAt(list->items, _start);
                list_add(s_list, item);
                _start += _step;
            }
        }
        else
        {
            while (_start > _end)
            {
                Value *item = (Value *)list_getAt(list->items, _start);
                list_add(s_list, item);
                _start += _step;
            }
        }

        return NEW_OBJ(new_list(s_list));
    }

    else if (sequence->type == OBJ_TUPLE)
    {
        PiTuple *tuple = (PiTuple *)sequence;
        size = LIST_SIZE(tuple->items);

        if (isinf(start))
            _start = (_step > 0) ? 0 : size - 1;
        else
            _start = slice_index((int)start, size, _step);

        if (isinf(end))
            _end = (_step > 0) ? size : -1;
        else
            _end = slice_index((int)end, size, _step);

        list_t *s_list = list_create(sizeof(Value));

        if (_step > 0)
        {
            while (_start < _end)
            {
                Value *item = (Value *)list_getAt(tuple->items, _start);
                list_add(s_list, item);
                _start += _step;
            }
        }
        else
        {
            while (_start > _end)
            {
                Value *item = (Value *)list_getAt(tuple->items, _start);
                list_add(s_list, item);
                _start += _step;
            }
        }

        return NEW_OBJ(new_tuple(s_list));
    }

    else if (sequence->type == OBJ_STRING)
    {
        PiString *str = (PiString *)sequence;
        size = str->length;

        if (isinf(start))
            _start = (_step > 0) ? 0 : size - 1;
        else
            _start = slice_index((int)start, size, _step);

        if (isinf(end))
            _end = (_step > 0) ? size : -1;
        else
            _end = slice_index((int)end, size, _step);

        char *s_str = malloc(size + 1);
        int j = 0;

        if (_step > 0)
        {
            for (int i = _start; i < _end; i += _step)
                s_str[j++] = str->chars[i];
        }
        else
        {
            for (int i = _start; i > _end; i += _step)
                s_str[j++] = str->chars[i];
        }

        s_str[j] = '\0';

        return NEW_OBJ(new_pistring(s_str));
    }

    fprintf(stderr, "Invalid sequence type.\n");
    exit(EXIT_FAILURE);
}
