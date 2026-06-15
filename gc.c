#include "gc.h"
#include "pi_list.h"
#include "pi_func.h"
#include "pi_module.h"

// Non-recursive marking stack used during graph traversal.
static Object **mark_stack = NULL;

static int stack_count = 0;
static int stack_capacity = 0;

static bool stack_tracing = false;

static void mark_references(Object *obj);

// Uses an explicit stack to avoid deep recursive marking.
static void push_stack(Object *obj)
{
    if (obj == NULL || obj->is_marked)
        return;

    obj->is_marked = true;

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
static void trace_stack(void)
{
    stack_tracing = true;

    while (stack_count > 0)
    {
        Object *obj = mark_stack[--stack_count];
        mark_references(obj);
    }

    stack_tracing = false;
}

void mark_list(list_t *list)
{
    if (!list)
        return;

    int size = list_size(list);
    for (int i = 0; i < size; i++)
    {
        Value *val = (Value *)list_getAt(list, i);
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
        exit(1);
    return result;
}

void mark_globals(vm_t *vm)
{
    ht_iter it = ht_iterator(vm->globals);
    while (ht_next(&it))
    {
        Value *val = it.value;
        if (val != NULL)
            mark_value(*val);
    }
}

void mark_modules(vm_t *vm)
{
    if (!vm->modules)
        return;

    ht_iter it = ht_iterator(vm->modules);
    while (ht_next(&it))
    {
        Value *val = (Value *)it.value;
        if (val)
            mark_value(*val);
    }
}

void mark_iters(vm_t *vm)
{
    for (int i = 0; i <= vm->iter_sp; i++)
    {
        if (vm->iters[i] != NULL)
            mark_object(vm->iters[i]);
    }
}

void mark_constants(vm_t *vm)
{
    int count = list_size(vm->constants);
    for (int i = 0; i < count; i++)
        mark_value(*(Value *)list_getAt(vm->constants, i));
}

void mark_value(Value val)
{
    if (IS_OBJ(val))
        mark_object(AS_OBJ(val));
}

// Entry point for marking. Starts tracing only from the outermost call.
void mark_object(Object *obj)
{
    push_stack(obj);

    if (!stack_tracing)
        trace_stack();
}

static void mark_references(Object *obj)
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
            Value *item = (Value *)list_getAt(list->items, i);
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

        if (map->proto)
            mark_object((Object *)map->proto);
        if (map->super_instance)
            mark_object(map->super_instance);

        table_t *table = map->table;
        if (!table)
            break;

        for (int i = 0; i < table->capacity; i++)
        {
            ht_item *item = &table->items[i];
            if (!item->key || !item->value)
                continue;

            Value *val = (Value *)item->value;
            if (val && IS_OBJ(*val))
                mark_object(AS_OBJ(*val));
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
            Value *item = (Value *)list_getAt(tuple->items, i);
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
            ht_iter it = ht_iterator(fn->globals);
            while (ht_next(&it))
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

        break;
    }

    case OBJ_CONTEXT:
    {
        PiContext *ctx = (PiContext *)obj;
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

void sweep(vm_t *vm)
{

    Object *obj = vm->objects;
    Object *prev = NULL;
    int live_count = 0;

    while (obj != NULL)
    {
        Object *next = obj->next;

        if (!obj->is_marked)
        {
            obj->in_gcList = false;

            free_object(obj);

            if (prev != NULL)
                prev->next = next;
            else
                vm->objects = next;
        }
        else
        {
            obj->is_marked = false;
            obj->in_gcList = true;
            prev = obj;
            live_count++;
        }

        obj = next;
    }

    vm->obj_count = live_count;
}

void free_object(Object *obj)
{

    obj->in_gcList = false;

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

#ifndef __EMSCRIPTEN__
    case OBJ_IMAGE:
    {
        ObjImage *image = (ObjImage *)obj;
        if (image->surface)
            SDL_FreeSurface(image->surface);
        break;
    }
#endif

    case OBJ_MAP:
    {
        PiMap *map = (PiMap *)obj;
        // Nested objects are owned by the VM object list and are freed separately.
        if (map->intrinsic_name)
            free(map->intrinsic_name);
        ht_free(map->table);
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
        // SDL resources must be released explicitly before the context is collected.
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
        break;
    }
    case OBJ_EVENT:
        break;

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

void mark_roots(vm_t *vm)
{
    for (int i = 0; i < vm->sp; i++)
        mark_value(vm->stack[i]);


    for (int i = 0; i < vm->frame_sp; i++)
    {
        Frame *frame = &vm->frames[i];
        if (frame->function != NULL)
            mark_object((Object *)frame->function);

        mark_list(frame->constants);
        mark_list(frame->names);
        if (frame->globals)
        {
            ht_iter it = ht_iterator(frame->globals);
            while (ht_next(&it))
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

    UpValue *up = vm->openUpvalues;
    while (up != NULL)
    {
        mark_value(up->value);
        up = up->next;
    }
}

void run_gc(vm_t *vm)
{

    mark_globals(vm);
    mark_modules(vm);
    mark_iters(vm);
    mark_constants(vm);

    mark_roots(vm);

    sweep(vm);
}

static void print_objectChain(vm_t *vm)
{
    printf("\n[DEBUG] Object Chain:\n");
    Object *obj = vm->objects;
    while (obj)
    {
        printf("  - Object at %p (Type: %s, Next: %p)\n", (void *)obj, type_name(NEW_OBJ(obj)), (void *)obj->next);
        obj = obj->next;
    }
}
