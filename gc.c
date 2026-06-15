#include "gc.h"
#include "pi_list.h"
#include "pi_func.h"
#include "pi_module.h"

static Object **mark_stack = NULL; // Mark stack for tracking objects to mark during GC

static int stack_count = 0; // Current stack count of objects to mark
static int stack_capacity = 0; // Initial capacity of the mark stack

static bool stack_tracing = false; // Flag to indicate if we are currently tracing the mark stack

static void mark_references(Object *obj);

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

/**
 * @brief Marks all values in a list as reachable.
 *
 * This function iterates over the list and marks each value
 * as reachable. This is necessary for the garbage collector
 * to know not to free the memory associated with any of the
 * values in the list.
 *
 * @param list The list whose values are to be marked.
 */
void mark_list(list_t *list)
{
    if (!list)
        return; // If the list is NULL, exit the function

    int size = list_size(list);
    for (int i = 0; i < size; i++)
    {
        Value *val = (Value *)list_getAt(list, i); // Get the value at index i
        if (val)
            mark_value(*val); // Mark the value if it's not NULL
    }
}

/**
 * @brief Reallocates a memory block to a new size.
 *
 * This function changes the size of the memory block pointed to by `ptr`.
 * If the new size (`n_size`) is zero, the memory is freed.
 * If the reallocation fails, the program exits with an error code.
 *
 * @param ptr Pointer to the currently allocated memory block.
 * @param o_size The original size of the memory block (unused in this function).
 * @param n_size The new size for the memory block.
 * @return A pointer to the newly allocated memory block, or NULL if `n_size` is zero.
 */
void *reallocate(void *ptr, size_t o_size, size_t n_size)
{
    if (n_size == 0)
    {
        free(ptr); // Free the memory if the new size is zero
        return NULL;
    }
    void *result = realloc(ptr, n_size); // Attempt to reallocate memory
    if (!result)
        exit(1); // Exit if reallocation fails
    return result;
}

/**
 * @brief Marks all values in the global hash table as reachable.
 *
 * This function iterates over the global hash table and marks each value
 * as reachable. This is necessary so that the garbage collector knows not
 * to free the memory associated with any of the values in the global hash
 * table.
 *
 * @param vm The virtual machine.
 */
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

/**
 * @brief Marks all iterators in the iterator stack as reachable.
 *
 * This function iterates over the iterator stack and marks each iterator
 * as reachable. This is necessary so that the garbage collector knows not
 * to free the memory associated with any of the iterators in the iterator
 * stack.
 *
 * @param vm The virtual machine.
 */
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

/**
 * @brief Marks a value as reachable.
 *
 * If the value is an object, marks the object as reachable.
 *
 * @param val The value to mark.
 */
void mark_value(Value val)
{
    if (IS_OBJ(val))
        mark_object(AS_OBJ(val));
}

/**
 * @brief Marks an object as reachable.
 *
 * If the object is not marked, marks it as reachable and recursively marks any
 * referenced objects based on the object type.
 *
 * @param obj The object to mark.
 */
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

    // Recursively mark any referenced objects based on the object type.
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
                mark_value(*item); // Ensure items inside lists are also marked
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
                mark_value(*item); // Ensure items inside tuples are also marked
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
        mark_list(code->data); // if necessary
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

/**
 * @brief Sweeps through the VM's object list, freeing unmarked objects.
 *
 * This function iterates over the linked list of objects in the virtual machine.
 * It checks each object to see if it is marked. Unmarked objects are considered
 * unreachable and are freed. The function also updates the linked list to remove
 * the freed objects. Marked objects are prepared for the next garbage collection
 * cycle by resetting their mark status.
 *
 * @param vm The virtual machine instance containing the objects to be swept.
 */

/**
 * @brief Sweeps through the VM's object list, freeing unmarked objects.
 *
 * This function iterates over the linked list of objects in the virtual machine.
 * It checks each object to see if it is marked. Unmarked objects are considered
 * unreachable and are freed. The function also updates the linked list to remove
 * the freed objects. Marked objects are prepared for the next garbage collection
 * cycle by resetting their mark status.
 *
 * @param vm The virtual machine instance containing the objects to be swept.
 */
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
            // If the object is unmarked, it is unreachable and should be freed
            obj->in_gcList = false; // Reset the GC tracking flag

            free_object(obj); // Free the memory of the unmarked object

            // Remove the object from the linked list
            if (prev != NULL)
                prev->next = next;
            else
                vm->objects = next;
        }
        else
        {
            obj->is_marked = false; // Reset the mark for the next GC cycle
            obj->in_gcList = true;  // Ensure it remains in the GC list
            prev = obj;             // Move prev to current object
            live_count++;
        }

        obj = next; // Move to the next object in the list
    }

    vm->obj_count = live_count;
}

/**
 * Frees the allocated memory for an object based on its type.
 *
 * This function will deallocate the memory used by the object and any
 * associated data structures it contains, such as strings or lists.
 *
 * @param obj The object to be freed.
 */
void free_object(Object *obj)
{

    obj->in_gcList = false; // Prevent stale GC tracking

    switch (obj->type)
    {
    case OBJ_STRING:
    {
        // Free the memory allocated for the string characters
        PiString *string = (PiString *)obj;
        free(string->chars);
        break;
    }
    case OBJ_LIST:
    {
        // Free the memory allocated for the list items
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
        // Free the memory allocated for the map's key-value pairs
        PiMap *map = (PiMap *)obj;
        // Values stored in the table are plain Value cells. Any nested
        // objects are owned by the VM object list and must not be freed here.
        if (map->intrinsic_name)
            free(map->intrinsic_name);
        ht_free(map->table);
        break;
    }

    case OBJ_SET:
    {
        // Free the memory allocated for the set's elements
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
        // Free the memory allocated for the code list
        ObjCode *code = (ObjCode *)obj;
        list_free(code->data);
        if (code->param_names)
            list_free(code->param_names);
        break;
    }

    case OBJ_FUN:
    {
        // Free the memory allocated for the function's code
        Function *function = (Function *)obj;
        free_func(function);
        break;
    }

    case OBJ_CONTEXT:
        /* PiContext: SDL resources should be released before GC frees the object. */
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
        // Handle other object types if needed
        break;
    }
    // Free the memory allocated for the object itself
    free(obj);
}

/**
 * @brief Frees the allocated memory for a Value.
 *
 * This function checks the type of the Value and frees any associated
 * objects if necessary. It then frees the Value struct itself.
 *
 * @param val The Value to be freed.
 */
void free_value(Value *val)
{
    if (val == NULL)
        return;

    // If the value is an object, free the associated object.
    if (val->type == VAL_OBJ)
        free_object(AS_OBJ(*val));

    // Free the allocated Value struct.
    free(val);
}

/**
 * Marks all roots: typically the VM's stack, global variables, etc.
 *
 * This function traverses all reachable objects from the roots and marks them
 * as live. This is the first step of the garbage collection process.
 */
void mark_roots(vm_t *vm)
{
    // Stack values
    for (int i = 0; i < vm->sp; i++)
        mark_value(vm->stack[i]);

    // If your VM has global variables or other roots (e.g. open upvalues, etc.),
    // mark them here as well.

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

    // Current function
    if (vm->function)
        mark_object(vm->function);

    mark_value(vm->_kw_args);

    mark_list(vm->names);

    // Open upvalues (linked list)
    UpValue *up = vm->openUpvalues;
    while (up != NULL)
    {
        mark_value(up->value);
        up = up->next;
    }
}

/**
 * Run a full garbage collection cycle.
 *
 * This will mark all reachable objects (by traversing the roots), and then
 * sweep the heap to free any unreachable objects.
 *
 * @param vm The virtual machine instance.
 */
void run_gc(vm_t *vm)
{

    mark_globals(vm);
    mark_modules(vm);
    mark_iters(vm);
    mark_constants(vm);

    // Mark all roots: typically the VM's stack, global variables, etc.
    mark_roots(vm);

    // Sweep the heap to free any unreachable objects.
    sweep(vm);
}

/**
 * Prints out the object chain, including the memory address of each object, its
 * type, and the address of the next object in the chain.
 *
 * @param vm The virtual machine instance.
 */
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
