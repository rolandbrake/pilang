#include "gc.h"
#include "pi_list.h"
#include "pi_func.h"
#include "pi_module.h"

#ifndef __EMSCRIPTEN__
#include "builtin/pi_draw.h"
#endif

#ifdef _WIN32
#include <windows.h>
#endif

// Non-recursive marking stack used during graph traversal.
static Object **mark_stack = NULL;

static int stack_count = 0;
static int stack_capacity = 0;

static bool stack_tracing = FALSE;

static void _mark_references(Object *obj);

void gc_trimHeap(void)
{
#ifdef _WIN32
    HeapCompact(GetProcessHeap(), 0);
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
#endif
}

// Uses an explicit stack to avoid deep recursive marking.
static void _push_stack(Object *obj)
{
    if (obj == NULL || obj->is_marked)
        return;

    obj->is_marked = TRUE;

    if (stack_count >= stack_capacity)
    {
        int old_capacity = stack_capacity;
        stack_capacity = old_capacity < 8 ? 8 : old_capacity * 2;
        mark_stack = reallocate(mark_stack,
                                sizeof(Object *) * (size_t)old_capacity,
                                sizeof(Object *) * (size_t)stack_capacity);
    }

    mark_stack[stack_count++] = obj;
}

// Processes all pending objects and marks their references.
static void _trace_stack(void)
{
    stack_tracing = TRUE;

    while (stack_count > 0)
    {
        Object *obj = mark_stack[--stack_count];
        _mark_references(obj);
    }

    stack_tracing = FALSE;
}

void mark_list(List *list)
{
    if (!list)
        return;

    int size = LIST_SIZE(list);
    for (int i = 0; i < size; i++)
    {
        Value *val = (Value *)_list_getAt(list, i);
        if (val)
            mark_value(*val);
    }
}

void *reallocate(void *ptr, size_t o_size, size_t n_size)
{
    if (n_size == 0)
    {
        free(ptr);
        return NULL;
    }
    void *result = realloc(ptr, n_size);
    if (!result)
        error("[gc] Out of memory resizing allocation from %zu to %zu bytes.", o_size, n_size);
    return result;
}

static size_t _gc_listBytes(const List *list)
{
    return list ? sizeof(*list) + (size_t)list->capacity * (size_t)list->i_size : 0;
}

static size_t _gc_tableBytes(const HT_Table *table)
{
    if (!table)
        return 0;

    return sizeof(*table) +
           (size_t)table->capacity * table->item_stride +
           (size_t)table->keys_cap +
           (size_t)table->order_cap * sizeof(uint32_t);
}

static size_t _gc_tensorElementSize(TensorType type)
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

size_t gc_objectBytes(Object *obj)
{
    if (!obj)
        return 0;

    switch (obj->type)
    {
    case OBJ_STRING:
    {
        PiString *string = (PiString *)obj;
        return sizeof(*string) + string->length + 1;
    }
    case OBJ_LIST:
    {
        PiList *list = (PiList *)obj;
        return sizeof(*list) + _gc_listBytes(list->items);
    }
    case OBJ_TUPLE:
    {
        PiTuple *tuple = (PiTuple *)obj;
        return sizeof(*tuple) + _gc_listBytes(tuple->items);
    }
    case OBJ_TENSOR:
    {
        PiTensor *tensor = (PiTensor *)obj;
        return sizeof(*tensor) +
               (size_t)tensor->ndim * sizeof(int) * 2 +
               (size_t)tensor->size * _gc_tensorElementSize(tensor->type);
    }
    case OBJ_MAP:
    {
        PiMap *map = (PiMap *)obj;
        return sizeof(*map) + _gc_tableBytes(map->table);
    }
    case OBJ_CLASS:
    {
        PiClass *klass = (PiClass *)obj;
        return sizeof(*klass) +
               (klass->name ? strlen(klass->name) + 1 : 0) +
               _gc_tableBytes(klass->members) + _gc_tableBytes(klass->field_names);
    }
    case OBJ_INSTANCE:
    {
        PiInstance *instance = (PiInstance *)obj;
        return sizeof(*instance) + _gc_tableBytes(instance->fields) +
               (instance->_class ? (size_t)instance->_class->slot_count * sizeof(Value) : 0);
    }
    case OBJ_FUN:
    {
        Function *function = (Function *)obj;
        return sizeof(*function) +
               (function->name ? strlen(function->name) + 1 : 0) +
               _gc_listBytes(function->params) +
               (size_t)function->upvalue_count * sizeof(UpValue *);
    }
    case OBJ_CODE:
    {
        ObjCode *code = (ObjCode *)obj;
        return sizeof(*code) + _gc_listBytes(code->data) + _gc_listBytes(code->param_names) +
               (size_t)code->member_cache_count * sizeof(MemberCache);
    }
    case OBJ_MODULE:
    {
        ObjModule *module = (ObjModule *)obj;
        return sizeof(*module) +
               (module->name ? strlen(module->name) + 1 : 0) +
               (module->path ? strlen(module->path) + 1 : 0) +
               _gc_listBytes(module->code) + _gc_listBytes(module->constants) + _gc_listBytes(module->names) +
               _gc_tableBytes(module->instrs) + _gc_tableBytes(module->globals);
    }
    case OBJ_RANGE:
        return sizeof(PiRange);
    case OBJ_SLICE:
        return sizeof(PiSlice);
    case OBJ_SET:
        return sizeof(PiSet);
    case OBJ_FILE:
        return sizeof(ObjFile);
    case OBJ_SOCKET:
        return sizeof(PiSocket);
    case OBJ_CONTEXT:
        return sizeof(PiContext);
    case OBJ_CHART:
        return sizeof(PiChart);
    case OBJ_CHART3D:
        return sizeof(PiChart3D);
    case OBJ_EVENT:
        return sizeof(PiEvent);
    default:
        return sizeof(Object);
    }
}

size_t gc_recountBytes(VM *vm)
{
    size_t total = 0;
    for (Object *obj = vm->objects; obj; obj = obj->next)
    {
        obj->gc_bytes = gc_objectBytes(obj);
        total += obj->gc_bytes;
    }
    return total;
}

void mark_globals(VM *vm)
{
    HT_Iterator it = _ht_iterator(vm->globals);
    while (_ht_next(&it))
    {
        Value *val = it.value;
        if (val != NULL)
            mark_value(*val);
    }
}

void mark_modules(VM *vm)
{
    if (!vm->modules)
        return;

    HT_Iterator it = _ht_iterator(vm->modules);
    while (_ht_next(&it))
    {
        Value *val = (Value *)it.value;
        if (val)
            mark_value(*val);
    }
}

void mark_iters(VM *vm)
{
    for (int i = 0; i <= vm->iter_sp; i++)
    {
        if (vm->iters[i] != NULL)
            mark_object(vm->iters[i]);
    }
}

void mark_constants(VM *vm)
{
    int count = LIST_SIZE(vm->constants);
    for (int i = 0; i < count; i++)
        mark_value(*(Value *)_list_getAt(vm->constants, i));
}

void mark_value(Value val)
{
    if (IS_OBJ(val))
        mark_object(AS_OBJ(val));
}

// Entry point for marking. Starts tracing only from the outermost call.
void mark_object(Object *obj)
{
    _push_stack(obj);

    if (!stack_tracing)
        _trace_stack();
}

static void _mark_references(Object *obj)
{
    if (obj == NULL)
        return;

    switch (obj->type)
    {
    case OBJ_LIST:
    {
        // Mark the elements of a list
        PiList *list = (PiList *)obj;
        int size = LIST_SIZE(list->items);
        for (int i = 0; i < size; i++)
        {
            Value *item = (Value *)_list_getAt(list->items, i);
            if (item)
                mark_value(*item);
        }
        break;
    }

    case OBJ_TENSOR:
        break;

    case OBJ_MAP:
    {
        PiMap *map = (PiMap *)obj;

        HT_Table *table = map->table;
        if (table)
        {
            HT_Iterator it = _ht_iterator(table);
            while (_ht_next(&it))
            {
                Value *val = (Value *)it.value;
                if (val)
                    mark_value(*val);
            }
        }

        break;
    }

    case OBJ_CLASS:
    {
        PiClass *_class = (PiClass *)obj;
        if (_class->super)
            mark_object((Object *)_class->super);
        if (_class->members)
        {
            HT_Iterator it = _ht_iterator(_class->members);
            while (_ht_next(&it))
            {
                Value *value = (Value *)it.value;
                if (value)
                    mark_value(*value);
            }
        }
        for (int i = 0; i < BOUND_CACHE_SIZE; i++)
        {
            if (_class->bound_cache[i].valid)
            {
                if (_class->bound_cache[i].key)
                    mark_object(_class->bound_cache[i].key);
                mark_value(_class->bound_cache[i].bound_fn);
            }
        }
        break;
    }

    case OBJ_INSTANCE:
    {
        PiInstance *instance = (PiInstance *)obj;
        if (instance->_class)
            mark_object((Object *)instance->_class);
        if (instance->fields)
        {
            HT_Iterator it = _ht_iterator(instance->fields);
            while (_ht_next(&it))
            {
                Value *value = (Value *)it.value;
                if (value)
                    mark_value(*value);
            }
        }
        for (uint16_t i = 0; instance->_class && i < instance->_class->slot_count; i++)
            mark_value(instance->slots[i]);
        for (int i = 0; i < BOUND_CACHE_SIZE; i++)
        {
            if (instance->bound_cache[i].valid)
            {
                if (instance->bound_cache[i].key)
                    mark_object(instance->bound_cache[i].key);
                mark_value(instance->bound_cache[i].bound_fn);
            }
        }
        break;
    }

    case OBJ_SET:
    {
        PiSet *set = (PiSet *)obj;

        for (int i = 0; i < set_size(set); i++)
            mark_value(set_get(set, i));
        break;
    }

    case OBJ_TUPLE:
    {
        PiTuple *tuple = (PiTuple *)obj;
        int size = LIST_SIZE(tuple->items);
        for (int i = 0; i < size; i++)
        {
            Value *item = (Value *)_list_getAt(tuple->items, i);
            if (item)
                mark_value(*item);
        }
        break;
    }

    case OBJ_MODULE:
    {
        ObjModule *module = (ObjModule *)obj;
        if (module->exports)
            mark_object((Object *)module->exports);
        break;
    }

    case OBJ_CODE:
    {
        ObjCode *code = (ObjCode *)obj;
        mark_list(code->data);
        mark_list(code->param_names);
        for (int i = 0; i < code->member_cache_count; i++)
            if (code->member_caches[i].valid && code->member_caches[i].cached_class)
                mark_object((Object *)code->member_caches[i].cached_class);
        break;
    }

    case OBJ_FUN:
    {
        Function *fn = (Function *)obj;

        mark_list(fn->params);
        mark_list(fn->constants);
        mark_list(fn->names);

        if (fn->body)
            mark_object((Object *)fn->body);

        if (fn->globals)
        {
            HT_Iterator it = _ht_iterator(fn->globals);
            while (_ht_next(&it))
            {
                Value *val = (Value *)it.value;
                if (val)
                    mark_value(*val);
            }
        }

        if (fn->upvalues)
            for (int i = 0; i < fn->upvalue_count; i++)
                if (fn->upvalues[i])
                    mark_value(fn->upvalues[i]->value);

        if (fn->instance)
            mark_object(fn->instance);
        if (fn->owner)
            mark_object(fn->owner);
        if (fn->bound_source)
            mark_object(fn->bound_source);

        break;
    }

    case OBJ_CONTEXT:
    {
        PiContext *ctx = (PiContext *)obj;
        mark_value(ctx->frame_callback);
        for (int i = 0; i < EVENT_COUNT; i++)
            for (int j = 0; j < ctx->handlers[i].count; j++)
                if (ctx->handlers[i].fns[j])
                    mark_value(*ctx->handlers[i].fns[j]);
        if (ctx->active_plot3d)
            mark_object((Object *)ctx->active_plot3d);
        break;
    }

    case OBJ_CHART:
    {
        PiChart *chart = (PiChart *)obj;
        if (chart->ctx)
            mark_object((Object *)chart->ctx);
        mark_list(chart->series);
        mark_list(chart->colors);
        break;
    }

    case OBJ_CHART3D:
    {
        PiChart3D *chart = (PiChart3D *)obj;
        if (chart->ctx)
            mark_object((Object *)chart->ctx);
        mark_list(chart->series);
        break;
    }

    default:
        break;
    }
}

void sweep(VM *vm)
{

    Object *obj = vm->objects;
    Object *prev = NULL;
    int live_count = 0;
    size_t collected_bytes = 0;

    while (obj != NULL)
    {
        Object *next = obj->next;

        if (!obj->is_marked)
        {
            obj->in_gcList = FALSE;

            collected_bytes += obj->gc_bytes;
            free_object(obj);

            if (prev != NULL)
                prev->next = next;
            else
                vm->objects = next;
        }
        else
        {
            obj->is_marked = FALSE;
            obj->in_gcList = TRUE;
            prev = obj;
            live_count++;
        }

        obj = next;
    }

    vm->obj_count = live_count;

#ifdef _WIN32
    /*
     * Trim only after releasing a meaningful amount of memory. Counting
     * objects missed workloads such as draw.mouse(), where a small number of
     * maps owns most of the reclaimed allocations.
     */
    static ULONGLONG last_trim_ms = 0;
    ULONGLONG now_ms = GetTickCount64();
    if (collected_bytes >= 8 * 1024 * 1024 && now_ms - last_trim_ms >= 1000)
    {
        gc_trimHeap();
        last_trim_ms = now_ms;
    }
#endif
}

void free_object(Object *obj)
{

    obj->in_gcList = FALSE;

    switch (obj->type)
    {
    case OBJ_STRING:
    {
        PiString *string = (PiString *)obj;
        free(string->chars);
        break;
    }
    case OBJ_LIST:
    {
        PiList *list = (PiList *)obj;
        list_free(list->items);
        break;
    }

    case OBJ_TENSOR:
    {
        PiTensor *tensor = (PiTensor *)obj;
        free(tensor->data.f64);
        free(tensor->shape);
        free(tensor->strides);
        break;
    }

    case OBJ_FILE:
    {
        ObjFile *file = (ObjFile *)obj;
        if (file->fp && !file->closed)
            fclose(file->fp);
        if (file->mode)
            free(file->mode);
        if (file->filename)
            free(file->filename);
        break;
    }

    case OBJ_SOCKET:
        close_socket((PiSocket *)obj);
        break;

#ifndef __EMSCRIPTEN__
    case OBJ_IMAGE:
    {
        ObjImage *image = (ObjImage *)obj;
        if (image->surface)
            SDL_FreeSurface(image->surface);
        break;
    }

    case OBJ_AUDIO:
    {
        ObjAudio *audio = (ObjAudio *)obj;
        if (audio->chunk)
        {
            if (audio->channel >= 0 && Mix_GetChunk(audio->channel) == audio->chunk)
                Mix_HaltChannel(audio->channel);
            Mix_FreeChunk(audio->chunk);
        }
        free(audio->samples);
        break;
    }
#endif

    case OBJ_MAP:
    {
        PiMap *map = (PiMap *)obj;
        // Nested objects are owned by the VM object list and are freed separately.
        ht_free(map->table);
        break;
    }

    case OBJ_CLASS:
    {
        PiClass *_class = (PiClass *)obj;
        free(_class->name);
        ht_free(_class->members);
        ht_free(_class->field_names);
        break;
    }

    case OBJ_INSTANCE:
    {
        PiInstance *instance = (PiInstance *)obj;
        if (instance->owns_storage)
        {
            ht_free(instance->fields);
            free(instance->slots);
        }
        break;
    }

    case OBJ_SET:
    {
        PiSet *set = (PiSet *)obj;
        set_free(set);
        return;
    }

    case OBJ_TUPLE:
    {
        PiTuple *tuple = (PiTuple *)obj;
        list_free(tuple->items);
        break;
    }

    case OBJ_MODULE:
    {
        ObjModule *module = (ObjModule *)obj;
        free(module->name);
        free(module->path);
        if (module->code)
            list_free(module->code);
        if (module->constants)
            list_free(module->constants);
        if (module->names)
            list_free(module->names);
        if (module->instrs)
            ht_free(module->instrs);
        break;
    }

    case OBJ_CODE:
    {
        ObjCode *code = (ObjCode *)obj;
        list_free(code->data);
        free(code->member_caches);
        if (code->param_names)
            list_free(code->param_names);
        break;
    }

    case OBJ_FUN:
    {
        Function *function = (Function *)obj;
        free_func(function);
        break;
    }

    case OBJ_CONTEXT:
#ifndef __EMSCRIPTEN__
        dw_cleanupContext((PiContext *)obj);
#endif
        break;

    case OBJ_CHART:
    {
        PiChart *chart = (PiChart *)obj;
        if (chart->series)
            list_free(chart->series);
        if (chart->colors)
            list_free(chart->colors);
        if (chart->title)
            free(chart->title);
        if (chart->xlabel)
            free(chart->xlabel);
        if (chart->ylabel)
            free(chart->ylabel);
        break;
    }
    case OBJ_CHART3D:
    {
        PiChart3D *chart = (PiChart3D *)obj;
        if (chart->series)
            list_free(chart->series);
        if (chart->title)
            free(chart->title);
        if (chart->xlabel)
            free(chart->xlabel);
        if (chart->ylabel)
            free(chart->ylabel);
        if (chart->zlabel)
            free(chart->zlabel);
        free(chart->depth_buffer);
        break;
    }
    case OBJ_EVENT:
    {
        PiEvent *event = (PiEvent *)obj;
        free(event->type);
        free(event->key);
        break;
    }

    default:
        break;
    }
    free(obj);
}

void free_value(Value *val)
{
    if (val == NULL)
        return;

    if (val->type == VAL_OBJ)
        free_object(AS_OBJ(*val));

    free(val);
}

void mark_roots(VM *vm)
{
    for (int i = 0; i < vm->sp; i++)
        mark_value(vm->stack[i]);

    for (int i = 0; i < vm->frame_sp; i++)
    {
        Frame *frame = &vm->frames[i];
        if (frame->is_recursive)
            continue;

        if (frame->function != NULL)
            mark_object((Object *)frame->function);

        mark_list(frame->constants);
        mark_list(frame->names);
        if (frame->globals)
        {
            HT_Iterator it = _ht_iterator(frame->globals);
            while (_ht_next(&it))
            {
                Value *val = (Value *)it.value;
                if (val)
                    mark_value(*val);
            }
        }
    }

    if (vm->function)
        mark_object(vm->function);

    mark_value(vm->_kw_args);

    mark_list(vm->names);

    UpValue *up = vm->open_upvalues;
    while (up != NULL)
    {
        mark_value(up->value);
        up = up->next;
    }
}

void run_gc(VM *vm)
{

    mark_globals(vm);
    mark_modules(vm);
    mark_iters(vm);
    mark_constants(vm);

    mark_roots(vm);

    sweep(vm);
}

static void _print_objectChain(VM *vm)
{
    printf("\n[DEBUG] Object Chain:\n");
    Object *obj = vm->objects;
    while (obj)
    {
        printf("  - Object at %p (Type: %s, Next: %p)\n", (void *)obj, type_name(NEW_OBJ(obj)), (void *)obj->next);
        obj = obj->next;
    }
}
