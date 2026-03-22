#include <math.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

#include "pi_vm.h"

#include "pi_opcode.h"
#include "pi_value.h"
#include "pi_module.h"

#include "string.h"
#include "common.h"
#include "pi_func.h"
#include "gc.h"

#include "builtin/pi_builtin.h"

#define GC_MIN_THRESHOLD 4096
#define GC_MAX_THRESHOLD (1024 * 1024 * 8)

static PiMap *create_objectProto(vm_t *vm);

/**
 * Initializes the virtual machine by allocating memory and
 * setting initial values for the program counter, stack pointer,
 * base pointer, and other components.
 */
vm_t *init_vm(compiler_t *comp, const char *entry_name, bool is_main)
{

    // Allocate memory for the virtual machine instance
    vm_t *vm = (vm_t *)malloc(sizeof(vm_t));

    // Initialize program counter, stack pointer, and base pointer to 0
    vm->pc = 0;
    vm->sp = 0;
    vm->bp = 0;
    vm->ip = 0;

    // Set the code, constants, and names from the compiler to the VM
    vm->code = comp->code;
    vm->constants = comp->constants;
    vm->names = comp->names;
    vm->instrs = comp->instrs;

    // Create a hash table to store global variables
    vm->globals = ht_create(sizeof(Value));

    vm->objects = NULL;

    for (int i = 0; i < BUILTIN_CONST_COUNT; i++)
        ht_put(vm->globals, builtin_constants[i].name, &builtin_constants[i].value);

    for (int i = 0; i < BUILTIN_FUNC_COUNT; i++)
        ht_put(vm->globals, builtin_functions[i].name,
               new_native(builtin_functions[i].name, builtin_functions[i].func));

    vm->iter_sp = -1;
    vm->frame_sp = 0;

    vm->running = true;

    vm->fps = TARGET_FPS;

    pthread_mutex_init(&vm->lock, NULL);

    mark_constants(vm);

    vm->counter = 0;

    vm->openUpvalues = NULL;

    vm->function = NULL;

    // Initialize the garbage collector
    vm->next_gc = NEXT_GC;
    vm->obj_count = 0;

    // Initialize the GC stack to NULL (it will be allocated when needed)
    vm->gc_stack = NULL;

    // Initialize the modules table to store loaded modules by name
    vm->modules = ht_create(sizeof(Value));
    // Set current path to the working directory at VM initialization
    vm->current_path = getcwd(NULL, 0);
    vm->object_proto = NULL;

    // Expose current module context in every VM as `module`.
    const char *module_name = entry_name ? entry_name : "";
    Object *main_moduleObj = new_module(
        vm,
        module_name,
        vm->current_path ? vm->current_path : "",
        false,
        is_main);

    // Mark main module as loaded to prevent issues with circular imports in the main file.
    ObjModule *main_module = (ObjModule *)main_moduleObj;
    main_module->state = MODULE_LOADED;

    // Add main module to the global modules table so it can be referenced by name.
    Value main_moduleVal = NEW_OBJ(main_moduleObj);
    ht_put(vm->globals, "module", &main_moduleVal);

    vm->object_proto = create_objectProto(vm);
    Value object_proto_val = NEW_OBJ((Object *)vm->object_proto);
    ht_put(vm->globals, "Object", &object_proto_val);

    return vm;
}

/**
 * Resets an existing virtual machine to run new code.
 *
 * This function reinitializes the VM's execution state (PC, stack, etc.)
 * and loads new bytecode from a compiler. It intentionally preserves the
 * global variables table, allowing state to persist between script runs.
 *
 * @param vm The virtual machine instance to reset.
 * @param comp The compiler containing the new code to load.
 */
void vm_reset(vm_t *vm, compiler_t *comp)
{
    // Reset program counter, stack pointer, and base pointer to 0
    vm->pc = 0;
    vm->sp = 0;
    vm->bp = 0;
    vm->ip = 0;

    // Set the code, constants, and names from the compiler to the VM
    vm->code = comp->code;
    vm->constants = comp->constants;
    vm->names = comp->names;
    vm->instrs = comp->instrs;

    // Note: vm->globals is NOT reset. This is intentional to allow
    // persistence of global state between script executions in the shell.

    vm->iter_sp = -1;
    vm->frame_sp = 0;

    vm->running = true;

    // Reset GC stats to trigger collection sooner if needed
    vm->counter = 0;
    vm->next_gc = NEXT_GC;

    vm->openUpvalues = NULL;
    vm->function = NULL;

    // Mark new constants from the new compiler for GC
    mark_constants(vm);
}

/**
 * Adds an object to the VM's object list.
 *
 * This function takes in a newly allocated object and adds it to the
 * front of the list of objects in the virtual machine. It returns the
 * newly added object.
 *
 * @param vm The virtual machine instance.
 * @param obj The object to add to the object list.
 * @return The newly added object.
 */
inline Object *add_obj(vm_t *vm, Object *obj)
{
    if (obj->in_gcList)
        return obj; // Already in the list, skip

    // Mark as added
    obj->in_gcList = true;

    obj->gc_color = GC_WHITE; // New objects start as white

    // Add to the front of the list
    obj->next = vm->objects;
    vm->objects = obj;
    vm->counter++; // Track new allocations (GC trigger is allocation-driven).

    return obj;
}

/**
 * Counts the number of objects in the virtual machine's object list.
 *
 * This function iterates over the linked list of objects and returns the
 * total count of objects in the list. It is used for debugging purposes
 * to track the number of objects in use.
 *
 * @param vm The virtual machine instance.
 * @return The number of objects in the object list.
 */
static inline int count_objs(vm_t *vm)
{
    int count = 0;
    Object *obj = vm->objects;
    while (obj)
    {
#ifdef DEBUG
        // Print debugging information about the object
        printf("[DEBUG] Counting object at %p\n", (void *)obj);
#endif
        count++;
        obj = obj->next;
    }
    return count;
}

/**
 * Reports a virtual machine error with a specified message.
 *
 * This function outputs an error message to the standard error stream,
 * indicating a critical error in the virtual machine operation. It attempts
 * to provide context by displaying the line number and function name where
 * the error occurred, if available. The program will terminate immediately
 * after displaying the error message.
 *
 * @param vm The virtual machine instance containing execution information.
 * @param message The error message to be displayed.
 */

void vm_error(vm_t *vm, const char *message)
{
    instr_t *instr = NULL;
    char *name = "<global>";

    if (vm->frame_sp > 0)
    {
        Frame *top = &vm->frames[vm->frame_sp - 1];
        name = top->function->name;
    }

    list_t *instrs = ht_get(vm->instrs, name);
    int size = instrs ? list_size(instrs) : 0;

    for (int i = 0; i < size; i++)
    {
        instr_t *cur = (instr_t *)list_getAt(instrs, i);

        if (cur->offset > vm->pc)
            break;
        instr = cur;
    }

    if (global_errorHandler)
    {
        char buffer[1024];
        if (instr && instr->fun_name)
            snprintf(buffer, sizeof(buffer), "%s (in function '%s')", message, instr->fun_name);
        else
            snprintf(buffer, sizeof(buffer), "%s", message);

        global_errorHandler(buffer, instr ? instr->line : -1, 0);
        return;
    }

    if (instr)
    {
        fprintf(stderr, "\n\033[1;31m[RUNTIME ERROR] at line %d", instr->line);
        if (instr->fun_name)
            fprintf(stderr, " in function '%s'", instr->fun_name);
        fprintf(stderr, ":\033[0m %s\n\n", message);
    }
    else
        fprintf(stderr, "\n\033[1;31m[RUNTIME ERROR] at unknown location:\033[0m %s\n\n", message);

    exit(EXIT_FAILURE);
}

/**
 * Reports a virtual machine error with a formatted message.
 *
 * This function constructs a formatted error message using a variable
 * argument list and passes it to the vm_error function for reporting.
 *
 * @param vm The virtual machine instance containing execution information.
 * @param fmt The format string for the error message.
 * @param ... The variable arguments to be formatted into the message.
 */
void vm_errorf(vm_t *vm, const char *fmt, ...)
{
    char buffer[1024]; // Buffer to hold the formatted error message
    va_list args;

    // Initialize the variable argument list
    va_start(args, fmt);

    // Format the error message into the buffer
    vsnprintf(buffer, sizeof(buffer), fmt, args);

    // Clean up the variable argument list
    va_end(args);

    // Report the formatted error message
    vm_error(vm, buffer);
}

/**
 * Pops a value from the stack and returns it.
 *
 * This function retrieves the top element from the stack and
 * decrements the stack pointer. If the stack is empty, it will
 * raise an error.
 *
 * @return A Value object representing the popped value.
 */
static inline Value pop_stack(vm_t *vm)
{
    if (vm->sp <= 0)
        vm_error(vm, "Stack underflow: Attempted to pop from an empty stack");

    return vm->stack[--vm->sp];
}

/**
 * Pushes a value onto the stack.
 *
 * This function adds a new value to the top of the stack and increments the
 * stack pointer. If the stack is full, it will not push the value and instead
 * raise an error.
 *
 * @param value The value to be pushed onto the stack.
 */
static inline void push_stack(vm_t *vm, Value value)
{
    if (vm->sp >= STACK_MAX)
        vm_error(vm, "[stack] Stack overflow: Attempted to push onto a full stack");

    vm->stack[vm->sp++] = value;
}

/**
 * Peeks at the top element on the stack without modifying the stack pointer.
 *
 * This function returns the top element from the stack without modifying the
 * stack pointer. If the stack is empty, it will raise an error.
 *
 * @return A Value object representing the top value on the stack.
 */
static inline Value peek_stack(vm_t *vm)
{
    if (vm->sp <= 0)
        vm_error(vm, "Stack underflow: Attempted to peek at an empty stack");

    return vm->stack[vm->sp - 1];
}

/**
 * Checks if the stack is empty relative to the current base pointer.
 *
 * This function compares the stack pointer to the base pointer. If the stack
 * pointer is equal to the base pointer, it means that the stack is empty.
 *
 * @return true if the stack is empty, false otherwise
 */
static bool stack_isEmpty(vm_t *vm)
{
    return vm->sp == vm->bp;
}

/**
 * Pushes a frame onto the stack.
 *
 * This function increments the frame stack pointer and assigns the
 * given frame to the frame stack at the new index. If the frame
 * stack is full, it will raise an error.
 *
 * @param vm The virtual machine instance.
 * @param frame The frame to push onto the stack.
 */
void push_frame(vm_t *vm, Frame *frame)
{
    if (vm->frame_sp >= STACK_MAX)
        vm_error(vm, "[frame] Stack overflow: Attempted to push onto a full stack");

    vm->frames[vm->frame_sp++] = *frame;
}

/**
 * Pops a frame from the stack.
 *
 * This function retrieves the top element from the stack and decrements the
 * frame stack pointer. If the stack is empty, it will raise an error.
 *
 * @return A Frame object representing the popped frame.
 */
Frame *pop_frame(vm_t *vm)
{
    if (vm->frame_sp <= 0)
        vm_error(vm, "Stack underflow: Attempted to pop from an empty stack");

    return &vm->frames[--vm->frame_sp];
}

/**
 * Reads a name from the list of names stored in the virtual machine.
 *
 * @param index The index of the name to read from the list of names.
 * @return A C string containing the name at the specified index.
 */
static inline char *read_name(vm_t *vm, int index)
{
    return string_get(vm->names, index);
}

/**
 * Checks if the given value is considered false.
 *
 * This function is used to compare a value to a boolean false value.
 * It checks if the value is NULL or if the type of the value is NIL.
 *
 * @param value The value to check.
 * @return true if the value is false, false otherwise.
 */
static inline bool is_false(vm_t *vm, Value value)
{
    return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static inline int read_short(vm_t *vm)
{
    uint8_t *code = (uint8_t *)vm->code->data; // Access the bytecode from the VM's code
    int high = code[vm->pc++] & 0xFF;          // Get the high byte and mask it
    int low = code[vm->pc++] & 0xFF;           // Get the low byte and mask it

    return (high << 8) | low; // Combine high and low bytes into a 16-bit short
}

static inline int _read_short(uint8_t *code, int pc)
{
    int high = code[pc] & 0xFF;    // Get the high byte and mask it
    int low = code[pc + 1] & 0xFF; // Get the low byte and mask it
    return (high << 8) | low;      // Combine high and low bytes into a 16-bit short
}

/**
 * Checks if the given function uses an argument slot.
 *
 * This function checks the bytecode of the given function to see if it uses the
 * given argument slot. It checks for the OP_LOAD_LOCAL and OP_STORE_LOCAL
 * instructions and sees if the argument slot matches the given slot.
 *
 * @param body The function's bytecode
 * @param args_slot The argument slot to check
 * @return true if the function uses the argument slot, false otherwise
 */
static bool fun_scanSlot(ObjCode *body, uint8_t args_slot)
{
    uint8_t *bytecode = (uint8_t *)body->data->data;
    int length = body->data->size;

    // Iterate through the bytecode of the function
    for (int i = 0; i < length;)
    {
        uint8_t op = bytecode[i++]; // Get the opcode at the current index

        // Check if the opcode is either OP_LOAD_LOCAL or OP_STORE_LOCAL
        if ((op == OP_LOAD_LOCAL || op == OP_STORE_LOCAL) &&
            // Check if the argument slot matches the given slot
            i < length && bytecode[i] == args_slot)
            return true;

        // Advance the index by the number of operands the instruction has
        i += operand_count(op);
    }

    return false;
}

static UpValue *capture_upvalue(vm_t *vm, int index)
{
    UpValue *prev = NULL;
    UpValue *upvalue = vm->openUpvalues;

    while (upvalue != NULL && upvalue->index != index)
    {
        prev = upvalue;
        upvalue = upvalue->next;
    }

    if (upvalue != NULL && upvalue->index == index)
        return upvalue;

    UpValue *_upvalue = (UpValue *)malloc(sizeof(UpValue));
    _upvalue->value = vm->stack[index]; // Reference stack value
    _upvalue->index = index;

    _upvalue->next = upvalue;
    if (prev == NULL)
        vm->openUpvalues = _upvalue;
    else
        prev->next = _upvalue;
    return _upvalue;
}

/**
 * Removes an upvalue from the linked list of open upvalues in the VM.
 * This function iterates through the linked list of open upvalues and finds
 * the upvalue with the given index. It then removes the upvalue from the
 * list and updates the previous upvalue's next pointer if necessary.
 *
 * @param vm The virtual machine instance.
 * @param index The index of the upvalue to remove.
 */
static void remove_upvalue(vm_t *vm, int index)
{
    UpValue *prev = NULL;                // Previous upvalue in the list
    UpValue *upvalue = vm->openUpvalues; // Current upvalue in the list

    // Iterate through the list of open upvalues until the upvalue with the
    // given index is found.
    while (upvalue != NULL && upvalue->index != index)
    {
        prev = upvalue;
        upvalue = upvalue->next;
    }

    // If the upvalue with the given index is found, remove it from the list
    if (upvalue != NULL && upvalue->index == index)
    {
        // Mark the upvalue as removed by setting its index to -1
        upvalue->index = -1;

        // Set the upvalue's value to the value at the given index in the stack
        upvalue->value = vm->stack[index];

        // If the upvalue is at the beginning of the list, update the VM's openUpvalues
        // pointer to point to the next upvalue in the list.
        if (prev == NULL)
            vm->openUpvalues = upvalue->next;
        else
            // Otherwise, update the previous upvalue's next pointer to point to the next
            // upvalue in the list, effectively removing the upvalue from the list.
            prev->next = upvalue->next;
    }
}

/**
 * Bind a function to an instance. The function is returned as a new function
 * with the first argument set to the instance.
 *
 * @param function The function to bind.
 * @param instance The instance to bind to.
 * @return A new function bound to the given instance.
 */
static Value bind(vm_t *vm, Function *function, Object *instance)
{
    if (function->is_native)
    {
        Value *native = new_native(function->name, function->native);
        Function *bound = AS_FUN(*native);
        bound->instance = instance;
        bound->owner = function->owner;
        bound->is_method = true;
        return *native;
    }

    // Copy the function object to keep the original intact
    Object *fn = new_func(function->name, function->body,
                          function->params, NULL, instance);
    ((Function *)fn)->constants = function->constants;
    ((Function *)fn)->names = function->names;
    ((Function *)fn)->instrs = function->instrs;
    ((Function *)fn)->need_args = function->need_args;
    ((Function *)fn)->need_kwargs = function->need_kwargs;
    ((Function *)fn)->owner = function->owner;

    // Set the is_method flag to true
    ((Function *)fn)->is_method = true;
    add_obj(vm, fn); // Critical - adds to GC tracking

    // Return the new function
    return NEW_OBJ(fn);
}

/**
 * Creates the object prototype map.
 *
 * @param vm The virtual machine instance.
 * @return The object prototype map.
 */
static PiMap *create_objectProto(vm_t *vm)
{
    Object *obj = add_obj(vm, new_map(ht_create(sizeof(Value)), false));
    PiMap *proto = (PiMap *)obj;

    // Create built-in functions
    Value format = *new_native("format", pi_toString);
    Value value = *new_native("value", pi_valueOf);

    Value hash = *new_native("hash", pi_hashCode);

    Value clone = *new_native("clone", pi_clone);
    Value extends_fn = *new_native("extends", pi_extends);

    Value keys = *new_native("keys", pi_keys);
    Value values = *new_native("values", pi_values);

    // Add built-in functions to the object prototype map
    ht_put(proto->table, "format", &format);
    ht_put(proto->table, "value", &value);
    ht_put(proto->table, "hash", &hash);
    ht_put(proto->table, "clone", &clone);
    ht_put(proto->table, "extends", &extends_fn);
    ht_put(proto->table, "keys", &keys);
    ht_put(proto->table, "values", &values);

    return proto;
}

static Value call_methodNoArgs(vm_t *vm, Value receiver, const char *name)
{
    if (!IS_MAP(receiver))
        return receiver;

    Value key = NEW_OBJ(new_pistring(strdup(name)));
    PiMap *owner = map_owner(AS_MAP(receiver), key);
    if (owner == NULL)
        return receiver;

    Value method = map_get(owner, key);
    if (!IS_FUN(method))
        return receiver;

    if (AS_MAP(receiver)->is_instance)
    {
        Object *target = AS_MAP(receiver)->super_instance ? AS_MAP(receiver)->super_instance : AS_OBJ(receiver);
        method = bind(vm, AS_FUN(method), target);
    }

    return call_func(vm, AS_FUN(method), 0, NULL, NEW_NIL());
}

/**
 * Attempts to coerce a given object into a primitive value.
 *
 * This function first attempts to call the object's "format" or "value" method,
 * depending on the value of the is_string parameter. If the object does not
 * contain a method with the given name, or if the method does not return a primitive
 * value, it then attempts to call the object's other method. If the object does not
 * contain either method, or if neither method returns a primitive value, this
 * function returns the original object.
 *
 * @param vm The virtual machine instance.
 * @param value The object to coerce into a primitive value.
 * @param is_string Whether to prefer the "format" or "value" method when
 *                     attempting to coerce the object.
 * @return The coerced primitive value, or the original object if it cannot be coerced.
 */
static Value to_primitive(vm_t *vm, Value value, bool is_string)
{
    if (!IS_MAP(value))
        return value;

    const char *first = is_string ? "format" : "value";
    const char *second = is_string ? "value" : "format";

    // Try the preferred coercion method first, then its alternate spelling.
    Value result = call_methodNoArgs(vm, value, first);
    if (result.type != VAL_OBJ || IS_STRING(result))
        return result;

    // Fall back to the other coercion method and its alternate spelling.
    result = call_methodNoArgs(vm, value, second);
    if (result.type != VAL_OBJ || IS_STRING(result))
        return result;

    return value;
}

/**
 * Constructs a new object instance from a given prototype map.
 *
 * This function creates a new map instance, setting the original map as its
 * prototype and copying over its key-value pairs. If a key holds a function,
 * it is bound to the new instance. The constructor function is called if it exists.
 *
 * @param vm The virtual machine instance.
 * @param map The prototype map from which to construct the object.
 * @param argc The number of arguments provided for the constructor.
 * @param argv The arguments to pass to the constructor.
 * @return A new object instance.
 */
static Object *construct(vm_t *vm, PiMap *map, size_t argc, Value *argv)
{

    // ensure that the object prototype is set if the object has no parent
    if (map != NULL && map->proto == NULL && vm->object_proto != NULL && map != vm->object_proto)
        map->proto = vm->object_proto;

    // Create a new table for the instance
    table_t *table = ht_create(sizeof(Value));

    // Create a new map instance and set its prototype.
    // Members stay on the prototype map by default; only `this.*`
    // assignments create instance-local state.
    Object *instance = new_map(table, true);

    ((PiMap *)instance)->proto = map;

    // Push the new instance onto the VM stack
    // vm->stack[vm->sp] = NEW_OBJ(instance);

    // Invoke the constructor if it exists
    Value constructor = map_get(map, NEW_OBJ(new_pistring(strdup("constructor"))));

    if (IS_FUN(constructor))
    {
        Value bound = bind(vm, AS_FUN(constructor), instance);
        call_func(vm, AS_FUN(bound), argc, argv, NEW_NIL());
    }

    return instance;
}

void run(vm_t *vm)
{
    int length = vm->code->size;
    int pc = vm->pc;

    uint8_t op;
    uint16_t index;
    int address;

    uint8_t *code = (uint8_t *)vm->code->data;

    Value value;

    Value nilValue;

    Object *iter = NULL;

    UpValue *upValue;

    Function *function = (Function *)vm->function;

    while (pc < length && vm->running)
    {

        op = code[pc++];

        vm->ip++; // Advance instruction index

        // printf("OP: %d, PC: %d, IP: %d\n", op, pc, vm->ip);

        // Cast the opcode to the OpCode enum
        switch ((OpCode)op)
        {
        case OP_LOAD_CONST:
        {
            // Read a two-byte short value from the bytecode to get the constant index
            index = (code[pc++] << 8);
            index |= code[pc++];
            // Get the constant from the constants list using the index
            Value constant = *(Value *)list_getAt(vm->constants, index);

            // Push the constant onto the stack
            push_stack(vm, constant);

            break;
        }

        case OP_STORE_GLOBAL:
        {
            index = code[pc++];
            char *name = read_name(vm, index);

            Value _newValue = pop_stack(vm);
            ht_put(vm->globals, name, &_newValue); // Store directly, no malloc!

            break;
        }

        case OP_LOAD_GLOBAL:
        {
            index = code[pc++];
            char *name = string_get(vm->names, index);
            Value *_value = ht_get(vm->globals, name);
            if (_value == NULL)
            {
                nilValue = NEW_NIL();
                _value = &nilValue;
            }
            push_stack(vm, *_value);
            break;
        }

        case OP_LOAD_LOCAL:
        {
            op = code[pc++];
            Value value = vm->stack[vm->bp + op];
            push_stack(vm, value);
            break;
        }

        case OP_LOAD_SUPER:
        {
            if (!function->is_method || function->instance == NULL)
                vm_error(vm, "super is only available inside object methods.");

            Object *super_obj = add_obj(vm, new_map(ht_create(sizeof(Value)), true));
            PiMap *super = (PiMap *)super_obj;
            super->proto = function->owner ? ((PiMap *)function->owner)->proto : NULL;
            super->super_instance = function->instance;
            push_stack(vm, NEW_OBJ(super_obj));
            break;
        }

        case OP_STORE_LOCAL:
        {
            op = code[pc++];
            int slot = vm->bp + op;
            vm->stack[slot] = pop_stack(vm);
            // Ensure the stack pointer reserves space for locals.
            if (vm->sp <= slot)
                vm->sp = slot + 1;
            break;
        }

        case OP_POP:
        {
            remove_upvalue(vm, vm->sp - 1);
            Value value = pop_stack(vm);
            break;
        }
        case OP_POP_N:
        {
            op = code[pc++];
            for (int i = 0; i < op; i++)
            {
                remove_upvalue(vm, vm->sp - 1);
                pop_stack(vm);
            }
        }
        break;

        case OP_DUP_TOP:
            push_stack(vm, peek_stack(vm));
            break;

        case OP_JUMP_IF_FALSE:
        {
            int offset = (int16_t)((code[pc] << 8) | code[pc + 1]); // Signed 16-bit offset

            Value value = pop_stack(vm);
            if (!as_bool(value))
                pc += offset - 1; // relative jump
            else
                pc += 2;
            break;
        }

        case OP_JUMP:
        {
            int offset = (int16_t)((code[pc] << 8) | code[pc + 1]); // Signed 16-bit offset
            pc += offset - 1;
            break;
        }

        case OP_JUMP_IF_TRUE:
        {
            int offset = (int16_t)((code[pc] << 8) | code[pc + 1]); // Signed 16-bit offset

            Value value = pop_stack(vm);
            if (as_bool(value))
                pc += offset - 1; // relative jump
            else
                pc += 2;
            break;
        }

        case OP_COMPARE:
        {
            uint8_t op = code[pc++];

            Value right = pop_stack(vm);
            Value left = pop_stack(vm);

            left = to_primitive(vm, left, false);
            right = to_primitive(vm, right, false);

            bool result = false;
            int cmp = compare(left, right);

            switch (op)
            {
            case 0: // "=="
                result = (cmp == 0);
                break;
            case 1: // "!="
                result = (cmp != 0);
                break;
            case 2: // ">"
                result = (cmp > 0);
                break;
            case 3: // "<"
                result = (cmp < 0);
                break;
            case 4: // ">="
                result = (cmp >= 0);
                break;
            case 5: // "<="
                result = (cmp <= 0);
                break;
            default:
                vm_errorf(vm, "Unknown opcode: [%d]", op);
            }
            push_stack(vm, NEW_BOOL(result));

            break;
        }
        case OP_BINARY:
        {
            uint8_t op = code[pc++];

            Value right = pop_stack(vm);
            Value left = pop_stack(vm);
            Value left_prim = left;
            Value right_prim = right;

            switch (op)
            {
            case 0: // "+"
            {
                bool prefer_string = IS_STRING(left) || IS_STRING(right);
                left_prim = to_primitive(vm, left, prefer_string);
                right_prim = to_primitive(vm, right, prefer_string);

                if (is_numeric(left_prim) && is_numeric(right_prim))
                {
                    push_stack(vm, NEW_NUM(as_number(left_prim) + as_number(right_prim)));
                    break;
                }

                if (IS_STRING(left_prim) || IS_STRING(right_prim))
                {
                    // Coerce both to strings
                    char *l_str = as_string(left_prim);
                    char *r_str = as_string(right_prim);

                    size_t len = strlen(l_str) + strlen(r_str) + 1;
                    char *res = (char *)malloc(len);
                    if (!res)
                        vm_error(vm, "Memory allocation failed.");

                    strcpy(res, l_str);
                    strcat(res, r_str);

                    push_stack(vm, NEW_OBJ(add_obj(vm, new_pistring(res))));

                    free(l_str);
                    free(r_str);
                    break;
                }

                if (IS_LIST(left))
                {
                    PiList *list = AS_LIST(left);
                    list_add(list->items, &right);

                    // --- Matrix integrity check ---
                    if (list->rows == 1 && list->cols >= 0)
                    {
                        // Originally a 1xN matrix, now N+1
                        if (!IS_NUM(right))
                        {
                            list->rows = -1;
                            list->cols = -1;
                            list->is_numeric = false;
                        }
                        else
                            list->cols++; // still a row vector
                    }
                    else if (list->rows > 1 && list->cols > 0)
                    {
                        // Originally NxM matrix
                        if (!IS_LIST(right))
                        {
                            list->rows = -1;
                            list->cols = -1;
                            list->is_numeric = false;
                        }
                        else
                        {
                            PiList *_list = (PiList *)AS_OBJ(right);
                            if (!_list->is_numeric || _list->items->size != list->cols)
                            {
                                list->rows = -1;
                                list->cols = -1;
                                list->is_numeric = false;
                            }
                            else
                                list->rows++; // still an NxM matrix
                        }
                    }
                    else
                    {
                        // Not originally a matrix, check if it can now become one
                        if (list->items->size == 1 && IS_NUM(right) && IS_NUM(((Value *)list->items->data)[0]))
                        {
                            list->is_numeric = true;
                            list->rows = 1;
                            list->cols = 2;
                        }
                    }

                    push_stack(vm, left);
                    break;
                }
                if (IS_NAN(left) || IS_NAN(right))
                {
                    push_stack(vm, NEW_NUM(NAN));
                    break;
                }
                vm_error(vm, "Unsupported operand types for binary operator [+].");
            }
            case 1: // "-"
            {
                left_prim = to_primitive(vm, left, false);
                right_prim = to_primitive(vm, right, false);

                if (is_numeric(left_prim) && is_numeric(right_prim))
                {
                    push_stack(vm, NEW_NUM(as_number(left_prim) - as_number(right_prim)));
                    break;
                }

                if (IS_OBJ(left))
                {
                    if (IS_LIST(left))
                    {
                        PiList *list = AS_LIST(left);
                        for (int i = 0; i < list_size(list->items); i++)
                        {
                            Value item = *(Value *)list_getAt(list->items, i);
                            if (equals(item, right))
                            {
                                list_remove(list->items, i);
                                break;
                            }
                        }
                        push_stack(vm, left);
                        break;
                    }

                    if (IS_STRING(left))
                    {
                        char *l_str = as_string(left);
                        char *r_str = as_string(right_prim);

                        size_t l_len = strlen(l_str);
                        size_t r_len = strlen(r_str);

                        char *res = (char *)malloc(l_len + 1); // Worst case

                        char *w_ptr = res;
                        char *r_ptr = l_str;
                        char *match;

                        while ((match = strstr(r_ptr, r_str)) != NULL)
                        {
                            size_t chunk_len = match - r_ptr;
                            memcpy(w_ptr, r_ptr, chunk_len);
                            w_ptr += chunk_len;
                            r_ptr = match + r_len;
                        }

                        strcpy(w_ptr, r_ptr); // copy the tail

                        push_stack(vm, NEW_OBJ(add_obj(vm, new_pistring(res))));

                        free(l_str);
                        free(r_str);
                        break;
                    }

                    vm_error(vm, "Unsupported operand types for binary operator [-].");
                }

                vm_error(vm, "Unsupported operand types for binary operator [-].");
            }
            break;
            case 2: // "*"
            {
                left_prim = to_primitive(vm, left, false);
                right_prim = to_primitive(vm, right, false);

                if (is_numeric(left_prim) && is_numeric(right_prim))
                    // Multiply two numbers
                    push_stack(vm, NEW_NUM(as_number(left_prim) * as_number(right_prim)));
                else if (left.type == VAL_OBJ)
                {
                    if (IS_LIST(left) && IS_LIST(right))
                    {
                        PiList *A = AS_LIST(left);
                        PiList *B = AS_LIST(right);

                        if (!A->is_numeric || !B->is_numeric)
                            vm_error(vm, "Matrix multiplication requires numeric lists.");

                        if (A->cols == -1 || B->cols == -1)
                            vm_error(vm, "Matrix dimensions are not set properly.");

                        if (A->cols != B->rows)
                            vm_error(vm, "Matrix multiplication dimension mismatch.");

                        int m = A->rows;
                        int n = A->cols;
                        int p = B->cols;

                        list_t *result = list_create(sizeof(Value));

                        for (int i = 0; i < m; i++)
                        {
                            Value *rowA_val = (Value *)list_getAt(A->items, i);
                            list_t *rowA = as_list(*rowA_val);
                            list_t *temp = list_create(sizeof(Value));

                            for (int j = 0; j < p; j++)
                            {
                                double sum = 0.0;

                                for (int k = 0; k < n; k++)
                                {
                                    // Get A[i][k]
                                    Value *a_val = (Value *)list_getAt(rowA, k);
                                    double a = as_number(*a_val);

                                    // Get B[k][j]
                                    Value *rowB_val = (Value *)list_getAt(B->items, k);
                                    list_t *rowB = as_list(*rowB_val);
                                    Value *b_val = (Value *)list_getAt(rowB, j);
                                    double b = as_number(*b_val);

                                    sum += a * b;
                                }

                                list_add(temp, &NEW_NUM(sum));
                            }

                            list_add(result, &NEW_OBJ(new_list(temp)));
                        }

                        Object *res_obj = add_obj(vm, new_list(result));
                        ((PiList *)res_obj)->is_numeric = true;
                        ((PiList *)res_obj)->rows = m;
                        ((PiList *)res_obj)->cols = p;
                        push_stack(vm, NEW_OBJ(res_obj));
                        break;
                    }
                    else if (IS_LIST(left))
                    {
                        int count = (int)as_number(right_prim); // Assuming `right` is a number
                        list_t *list = as_list(left);           // Assuming `as_list` returns a `list_t *`

                        list_t *result = list_create(list->i_size);
                        for (int i = 0; i < count; i++)
                            list_addAll(result, list);

                        Object *_result = new_list(result);
                        if (AS_LIST(left)->is_numeric)
                            ((PiList *)_result)->is_numeric = true;

                        push_stack(vm, NEW_OBJ(add_obj(vm, _result))); // Assuming `new_list` creates a `Value` with type `OBJ_LIST`
                    }
                    else if (IS_STRING(left))
                    {
                        int count = (int)as_number(right_prim); // Assuming `right` is a number
                        // the original strings
                        char *str = as_string(left);
                        // original string length
                        size_t o_len = strlen(str);
                        // result string length
                        size_t r_len = o_len * count;

                        // allocate memory for the result string
                        char *result = (char *)malloc(r_len + 1); // Allocate space for the repeated string
                        result[0] = '\0';

                        for (int i = 0; i < count; i++)
                            strcat(result, str);

                        push_stack(vm, NEW_OBJ(add_obj(vm, new_pistring(result))));
                        free(str);
                    }
                    else
                        vm_error(vm, "Unsupported operand types for binary operator [*].");
                }
                else
                    vm_error(vm, "Unsupported operand types for binary operator [*].");

                break;
            }
            case 3: // "/"
            {
                left_prim = to_primitive(vm, left, false);
                right_prim = to_primitive(vm, right, false);
                double denominator = as_number(right_prim);

                if (denominator == 0.0)
                {
                    push_stack(vm, NEW_NUM(INFINITY)); // Push infinity to indicate undefined result
                    break;
                }

                double numerator = as_number(left_prim);
                push_stack(vm, NEW_NUM(numerator / denominator));
                break;
            }
            case 4: // "%"
            {
                left_prim = to_primitive(vm, left, false);
                right_prim = to_primitive(vm, right, false);
                double denominator = as_number(right_prim);

                if ((int)denominator == 0) // If denominator is zero, return NaN
                    push_stack(vm, NEW_NAN());
                else
                    push_stack(vm, NEW_NUM((int)as_number(left_prim) % (int)denominator));
                break;
            }
            case 5: // "&&"
                push_stack(vm, NEW_BOOL(as_bool(left) && as_bool(right)));
                break;
            case 6: // "||"
                push_stack(vm, NEW_BOOL(as_bool(left) || as_bool(right)));
                break;
            case 7: // "**"
                left_prim = to_primitive(vm, left, false);
                right_prim = to_primitive(vm, right, false);
                push_stack(vm, NEW_NUM(pow(as_number(left_prim), as_number(right_prim))));
                break;
            case 8: // "&"
            {
                left_prim = to_primitive(vm, left, false);
                right_prim = to_primitive(vm, right, false);
                if (is_numeric(left_prim) && is_numeric(right_prim))
                    push_stack(vm, NEW_NUM((int)as_number(left_prim) & (int)as_number(right_prim)));
                else if (left.type == VAL_OBJ && OBJ_TYPE(left) == OBJ_LIST)
                {
                    list_t *list = as_list(left);
                    list_t *result = list_create(sizeof(Value));

                    int _right = (int)as_number(right_prim);

                    for (int i = 0; i < list_size(list); i++)
                    {
                        Value item = *(Value *)list_getAt(list, i);
                        list_add(result, &NEW_NUM((int)as_number(item) & _right));
                    }
                    push_stack(vm, NEW_OBJ(add_obj(vm, new_list(result))));
                }
                else
                    vm_error(vm, "Unsupported operand types for binary operator [&].");

                break;
            }

            case 9: // "|"
            {
                left_prim = to_primitive(vm, left, false);
                right_prim = to_primitive(vm, right, false);
                if (is_numeric(left_prim) && is_numeric(right_prim))
                    push_stack(vm, NEW_NUM((int)as_number(left_prim) | (int)as_number(right_prim)));
                else if (left.type == VAL_OBJ && OBJ_TYPE(left) == OBJ_LIST)
                {
                    list_t *list = as_list(left);
                    list_t *result = list_create(sizeof(Value));

                    int _right = (int)as_number(right_prim);

                    for (int i = 0; i < list_size(list); i++)
                    {
                        Value item = *(Value *)list_getAt(list, i);
                        list_add(result, &NEW_NUM((int)as_number(item) | _right));
                    }
                    push_stack(vm, NEW_OBJ(add_obj(vm, new_list(result))));
                }
                else
                    vm_error(vm, "Unsupported operand types for binary operator [|].");

                break;
            }

            case 10: // "^"
            {
                left_prim = to_primitive(vm, left, false);
                right_prim = to_primitive(vm, right, false);

                if (IS_LIST(left) && IS_LIST(right))
                {
                    PiList *l_list = AS_LIST(left);
                    PiList *r_list = AS_LIST(right);

                    if (!l_list->is_numeric || !r_list->is_numeric)
                        vm_error(vm, "Cross product requires numeric lists.");

                    if (list_size(l_list->items) != 3 || list_size(r_list->items) != 3)
                        vm_error(vm, "Cross product is defined for 3-dimensional vectors only.");

                    Value *a = l_list->items->data;
                    Value *b = r_list->items->data;

                    double x = as_number(a[1]) * as_number(b[2]) - as_number(a[2]) * as_number(b[1]);
                    double y = as_number(a[2]) * as_number(b[0]) - as_number(a[0]) * as_number(b[2]);
                    double z = as_number(a[0]) * as_number(b[1]) - as_number(a[1]) * as_number(b[0]);

                    list_t *res = list_create(sizeof(Value));
                    list_add(res, &NEW_NUM(x));
                    list_add(res, &NEW_NUM(y));
                    list_add(res, &NEW_NUM(z));

                    push_stack(vm, NEW_OBJ(add_obj(vm, new_list(res))));
                    break;
                }
                else if (is_numeric(left_prim) && is_numeric(right_prim))
                    push_stack(vm, NEW_NUM((int)as_number(left_prim) ^ (int)as_number(right_prim)));

                else if (left.type == VAL_OBJ && OBJ_TYPE(left) == OBJ_LIST)
                {
                    list_t *list = as_list(left);
                    list_t *result = list_create(sizeof(Value));

                    int _right = (int)as_number(right_prim);

                    for (int i = 0; i < list_size(list); i++)
                    {
                        Value item = *(Value *)list_getAt(list, i);
                        list_add(result, &NEW_NUM((int)as_number(item) ^ _right));
                    }
                    push_stack(vm, NEW_OBJ(add_obj(vm, new_list(result))));
                }
                else
                    vm_error(vm, "Unsupported operand types for binary operator [^].");

                break;
            }

            case 11: // "<<"
            {
                left_prim = to_primitive(vm, left, false);
                right_prim = to_primitive(vm, right, false);
                if (is_numeric(left_prim) && is_numeric(right_prim))
                    push_stack(vm, NEW_NUM((int)as_number(left_prim) << (int)as_number(right_prim)));

                else if (left.type == VAL_OBJ && OBJ_TYPE(left) == OBJ_LIST)
                {
                    list_t *list = as_list(left);
                    list_t *result = list_create(sizeof(Value));

                    int _right = (int)as_number(right_prim);

                    for (int i = 0; i < list_size(list); i++)
                    {
                        Value item = *(Value *)list_getAt(list, i);
                        list_add(result, &NEW_NUM((int)as_number(item) << _right));
                    }
                    push_stack(vm, NEW_OBJ(add_obj(vm, new_list(result))));
                }
                else
                    vm_error(vm, "Unsupported operand types for binary operator [<<].");

                break;
            }

            case 12: // ">>"
            {
                left_prim = to_primitive(vm, left, false);
                right_prim = to_primitive(vm, right, false);
                if (is_numeric(left_prim) && is_numeric(right_prim))
                    push_stack(vm, NEW_NUM((int)as_number(left_prim) >> (int)as_number(right_prim)));

                else if (left.type == VAL_OBJ && OBJ_TYPE(left) == OBJ_LIST)
                {
                    list_t *list = as_list(left);
                    list_t *result = list_create(sizeof(Value));

                    int _right = (int)as_number(right_prim);

                    for (int i = 0; i < list_size(list); i++)
                    {
                        Value item = *(Value *)list_getAt(list, i);
                        list_add(result, &NEW_NUM((int)as_number(item) >> _right));
                    }
                    push_stack(vm, NEW_OBJ(add_obj(vm, new_list(result))));
                }
                else
                    vm_error(vm, "Unsupported operand types for binary operator [>>].");

                break;
            }

            case 13: // ">>>"
            {
                left_prim = to_primitive(vm, left, false);
                right_prim = to_primitive(vm, right, false);
                if (is_numeric(left_prim) && is_numeric(right_prim))
                    push_stack(vm, NEW_NUM((uint32_t)as_number(left_prim) >> (uint32_t)as_number(right_prim)));

                else if (left.type == VAL_OBJ && OBJ_TYPE(left) == OBJ_LIST)
                {
                    list_t *list = as_list(left);
                    list_t *result = list_create(sizeof(Value));

                    uint32_t _right = (uint32_t)as_number(right_prim);

                    for (int i = 0; i < list_size(list); i++)
                    {
                        Value item = *(Value *)list_getAt(list, i);
                        list_add(result, &NEW_NUM((uint32_t)as_number(item) >> _right));
                    }
                    push_stack(vm, NEW_OBJ(add_obj(vm, new_list(result))));
                }
                else
                    vm_error(vm, "Unsupported operand types for binary operator [>>>].");

                break;
            }

            case 14: // "." (dot product)
            {
                if (IS_LIST(left) && IS_LIST(right))
                {
                    PiList *l_list = AS_LIST(left);
                    PiList *r_list = AS_LIST(right);

                    if (!l_list->is_numeric || !r_list->is_numeric)
                        vm_error(vm, "Dot product requires numeric lists.");

                    int l_size = list_size(l_list->items);
                    int r_size = list_size(r_list->items);

                    if (l_size != r_size)
                        vm_error(vm, "Dot product requires lists of the same length.");

                    double result = 0;
                    for (int i = 0; i < l_size; i++)
                    {
                        Value a = *(Value *)list_getAt(l_list->items, i);
                        Value b = *(Value *)list_getAt(r_list->items, i);
                        result += as_number(a) * as_number(b);
                    }
                    push_stack(vm, NEW_NUM(result));
                    break;
                }
                vm_error(vm, "Unsupported operand types for binary operator [.]");
            }

            case 15: // instance of operator [is]
            {

                if (!IS_MAP(left) || !IS_MAP(right))
                {
                    push_stack(vm, NEW_BOOL(false));
                    break;
                }

                Object *inst_obj = AS_OBJ(left);
                Object *proto_obj = AS_OBJ(right);

                if (inst_obj->type != OBJ_MAP || proto_obj->type != OBJ_MAP)
                {
                    push_stack(vm, NEW_BOOL(false));
                    break;
                }

                PiMap *map = (PiMap *)inst_obj;
                PiMap *proto = (PiMap *)proto_obj;

                // Traverse the prototype chain
                while (map != NULL)
                {
                    if (map == proto)
                    {
                        push_stack(vm, NEW_BOOL(true));
                        break;
                    }
                    map = map->proto;
                }

                if (!map)
                    push_stack(vm, NEW_BOOL(false));

                break;
            }

            break;
            }
            break;
        }
        case OP_UNARY:
        {

            uint8_t op = code[pc++];       // Get the unary operation code
            Value operand = pop_stack(vm); // Get the operand from the stack
            Value operand_prim = to_primitive(vm, operand, false);

            switch (op)
            {
            case 0: // Unary plus
                push_stack(vm, NEW_NUM(as_number(operand_prim)));
                break;

            case 1: // Unary minus
                push_stack(vm, NEW_NUM(-as_number(operand_prim)));
                break;

            case 2: // Logical NOT
                push_stack(vm, NEW_BOOL(!as_bool(operand)));
                break;

            case 3: // Bitwise NOT
                push_stack(vm, NEW_NUM(~(int)as_number(operand_prim)));
                break;

            case 4: // Collection size
            {
                if (IS_COLLECTION(operand))
                {
                    switch (OBJ_TYPE(operand))
                    {
                    case OBJ_LIST:
                        push_stack(vm, NEW_NUM(list_size(AS_LIST(operand)->items)));
                        break;
                    case OBJ_STRING:
                        push_stack(vm, NEW_NUM(AS_STRING(operand)->length));
                        break;
                    case OBJ_MAP:
                        push_stack(vm, NEW_NUM(map_size(AS_MAP(operand))));
                        break;
                    }
                }
                else
                    vm_error(vm, "Unsupported operand type for '#' operator.");

                break;
            }
            case 5: // "++"
                push_stack(vm, NEW_NUM(as_number(operand_prim) + 1));
                break;

            case 6: // "--"
                push_stack(vm, NEW_NUM(as_number(operand_prim) - 1));
                break;

            default:
                vm_error(vm, "Unknown unary operator.");
            }

            break;
        }
        case OP_CALL_FUNCTION:
        {

            // Read the number of arguments from the bytecode
            uint8_t raw_args = code[pc++];
            bool has_named = (raw_args & 0x80) != 0;
            uint8_t num_args = raw_args & 0x7F;

            // Allocate memory for the arguments
            Value args[num_args];
            Value kw_args = NEW_NIL();

            if (has_named)
            {
                kw_args = pop_stack(vm);
                if (!IS_OBJ(kw_args) || OBJ_TYPE(kw_args) != OBJ_MAP)
                    vm_error(vm, "Named arguments must be a map.");
            }

            // Pop the arguments off the VM's stack in reverse order.
            for (int i = num_args - 1; i >= 0; i--)
                args[i] = pop_stack(vm);

            // Pop the function (callee) from the stack.
            Value callee = pop_stack(vm);

            if (IS_FUN(callee))
            {

                vm->function = AS_OBJ(callee);

                vm->pc = pc;
                // Call native function if it's a built-in
                Value result = call_func(vm, AS_FUN(callee), num_args, args, kw_args);
                if (IS_OBJ(result))
                    add_obj(vm, AS_OBJ(result));
                push_stack(vm, result);
            }
            else if (IS_MAP(callee))
            {
                PiMap *map = AS_MAP(callee);
                if (map->is_instance)
                    vm_error(vm, "Attempt to call an Object instance.");
                else
                {
                    if (has_named)
                        vm_error(vm, "Named arguments are not supported for map constructors.");
                    push_stack(vm, NEW_OBJ(add_obj(vm, construct(vm, map, num_args, args))));
                }
            }
            else
                vm_error(vm, "Attempt to call a non-function object.");

            break;
        }

        case OP_PUSH_ITER:
        {
            // Pop the iterable object from the stack
            Value iterable = pop_stack(vm);

            // Ensure the object is iterable
            if (!IS_OBJ(iterable) || !is_iterable(AS_OBJ(iterable)))
                vm_error(vm, "Error: Object is not iterable.");

            iter = AS_OBJ(iterable);

            // Reset iterator state (common for all iterators)
            iter_reset(iter);

            // Push the iterator onto the iterator stack
            vm->iters[++vm->iter_sp] = iter; // Push a pointer to the iterator
            break;
        }

        case OP_LOOP:
        {
            // Read the jump address from the bytecode
            uint16_t address = (code[pc] << 8);
            address |= code[pc + 1];

            // Get the current iterator from the top of the stack
            if (vm->iter_sp == -1)
                vm_error(vm, "Error: No active iterator.");

            iter = vm->iters[vm->iter_sp];

            // Check if the iterator has more elements
            if (iter_hasNext(iter))
            {
                if (iter->type == OBJ_MAP)
                {
                    PiMap *map = (PiMap *)iter;
                    ht_next(&map->it);
                    char *key = map->it.key;
                    push_stack(vm, NEW_OBJ(add_obj(vm, new_pistring(key))));
                }
                else
                {
                    // Get the next value from the iterator
                    Value value = iter_next(iter);
                    // TODO: check me in the future
                    // if (IS_OBJ(value))
                    //     add_obj(vm, AS_OBJ(value));
                    push_stack(vm, value);
                }
                pc += 2;
            }
            else
            {
                // Iterator exhausted; pop it from the stack
                vm->iter_sp--;

                // Jump to the specified address
                pc += address - 1;
            }
            break;
        }

        case OP_POP_ITER:
        {
            if (vm->iter_sp != -1)
                iter = vm->iters[vm->iter_sp--];
            // Perform cleanup if needed
            break;
        }
        case OP_PUSH_RANGE:
        {
            // Pop the range values from the stack
            Value step = pop_stack(vm);
            Value end = pop_stack(vm);
            Value start = pop_stack(vm);

            if (!IS_NUM(start) || !IS_NUM(end))
                vm_error(vm, "PiRange `start` and `end` must be numbers.");

            // Create a new range object
            if (!IS_NIL(step) && !IS_NUM(step))
                vm_error(vm, "PiRange `step` must be nil or a number.");
            else
            {

                // Extract numerical values
                double _start = as_number(start);
                double _end = as_number(end);
                double _step;
                if (IS_NIL(step))
                    _step = (_start < _end) ? 1.0 : -1.0;
                else
                    _step = as_number(step);
                Object *range = add_obj(vm, new_range(_start, _end, _step));
                push_stack(vm, NEW_OBJ(range)); // Push the range onto the stack
            }

            break;
        }

        case OP_PUSH_LIST:
        {
            int numElements = (code[pc++] << 8) | code[pc++];
            list_t *list = list_create(sizeof(Value));

            if (numElements == 0)
            {
                Object *l_obj = add_obj(vm, new_list(list));
                PiList *plist = (PiList *)l_obj;
                plist->is_numeric = true;
                plist->is_matrix = false;
                plist->rows = 0;
                plist->cols = 0;
                push_stack(vm, NEW_OBJ(l_obj));
                break;
            }

            vm->sp -= numElements;

            bool is_numeric = true;
            bool is_matrix = true;
            int rows = -1, cols = -1;

            // First: collect all values and add to list
            for (int i = 0; i < numElements; i++)
            {
                Value v = vm->stack[vm->sp + i];
                if (is_numeric && !IS_NUM(v))
                    is_numeric = false;
                list_add(list, &v);
            }

            if (is_numeric)
            {
                is_matrix = false;
                rows = 1;
                cols = numElements;
            }
            else
            {
                // check for matrix: list of equal-sized numeric lists
                Value first = vm->stack[vm->sp];
                if (IS_LIST(first))
                {
                    PiList *pl0 = (PiList *)AS_OBJ(first);
                    if (pl0->is_numeric)
                    {
                        cols = pl0->items->size;
                        rows = numElements;
                        for (int i = 0; i < numElements; i++)
                        {
                            Value v = vm->stack[vm->sp + i];
                            if (!IS_LIST(v))
                            {
                                is_matrix = false;
                                break;
                            }
                            PiList *pl = (PiList *)AS_OBJ(v);
                            if (!pl->is_numeric || pl->items->size != cols)
                            {
                                is_matrix = false;
                                break;
                            }
                        }
                    }
                    else
                        is_matrix = false;
                }
                else
                    is_matrix = false;
            }

            Object *l_obj = add_obj(vm, new_list(list));
            PiList *plist = (PiList *)l_obj;
            plist->is_numeric = is_numeric;
            plist->is_matrix = is_matrix;
            plist->rows = is_matrix ? rows : -1;
            plist->cols = is_matrix ? cols : -1;

            push_stack(vm, NEW_OBJ(l_obj));
            break;
        }

        case OP_PUSH_MAP:
        {

            // Read the number of elements in the map
            int numElements = code[pc++] << 8;
            numElements |= code[pc++];
            // create a new hashtable
            table_t *table = ht_create(sizeof(Value));
            PiMap *proto = NULL;
            bool has_methods = false;

            // Adjust the stack pointer to the first element of the map
            int _sp = vm->sp - (numElements * 2);

            // Populate the map directly from the stack
            for (int i = _sp; i < vm->sp; i += 2)
            {
                Value value = vm->stack[i];

                char *key = AS_CSTRING(vm->stack[i + 1]);
                if (IS_FUN(value))
                {
                    AS_FUN(value)->is_method = true;
                    has_methods = true;
                }

                ht_put(table, key, &value);
            }

            vm->sp = _sp;

            // Push the new map onto the stack
            Object *map = add_obj(vm, new_map(table, false));
            if (proto == NULL && has_methods)
                proto = vm->object_proto;
            ((PiMap *)map)->proto = proto;
            char **keys = ht_keys(table);
            int size = ht_length(table);
            for (int i = 0; i < size; i++)
            {
                Value *item = ht_get(table, keys[i]);
                if (item && IS_FUN(*item))
                    AS_FUN(*item)->owner = map;
            }
            push_stack(vm, NEW_OBJ(map));

            break;
        }

        case OP_PUSH_FUNCTION:
        {
            // Read the number of parameters
            int numParams = code[pc++];

            ObjCode *body = AS_CODE(pop_stack(vm));
            char *name = AS_CSTRING(pop_stack(vm));

            list_t *defaults = list_create(sizeof(Value));

            // Adjust the stack pointer to the first parameter
            vm->sp -= numParams;

            // Populate the parameter list directly from the stack
            for (int i = 0; i < numParams; i++)
            {
                Value param = vm->stack[vm->sp + i];
                list_add(defaults, &param);
            }

            // Create a new function object
            Object *function = new_func(name, body, defaults, NULL, NULL);
            ((Function *)function)->need_args = fun_scanSlot(body, (uint8_t)numParams);
            ((Function *)function)->need_kwargs = fun_scanSlot(body, (uint8_t)(numParams + 1));
            ((Function *)function)->constants = vm->constants;
            ((Function *)function)->names = vm->names;

            // Push the new function onto the stack
            push_stack(vm, NEW_OBJ(add_obj(vm, function)));

            break;
        }

        case OP_PUSH_CLOSURE:
        {
            int numParams = code[pc++];
            // Read the number of upvalues
            int numUpvalues = code[pc++];

            UpValue **upvalues = ALLOCATE(UpValue *, numUpvalues + 1);

            // Populate the upvalue list directly from the stack
            for (int i = 0; i < numUpvalues; i++)
            {
                bool is_local = as_bool(pop_stack(vm));
                int index = as_number(pop_stack(vm));
                UpValue *upvalue;

                if (is_local)
                    upvalue = capture_upvalue(vm, vm->bp + index);
                else
                    upvalue = function->upvalues[index];

                upvalues[numUpvalues - i - 1] = upvalue;
            }
            upvalues[numUpvalues] = NULL;

            ObjCode *body = AS_CODE(pop_stack(vm));
            char *name = AS_CSTRING(pop_stack(vm));

            list_t *defaults = list_create(sizeof(Value));

            // Adjust the stack pointer to the first parameter
            vm->sp -= numParams;

            // Populate the parameter list directly from the stack
            for (int i = 0; i < numParams; i++)
            {
                Value param = vm->stack[vm->sp + i];
                list_add(defaults, &param);
            }

            Object *fun_obj = new_func(name, body, defaults, upvalues, NULL);
            ((Function *)fun_obj)->need_args = fun_scanSlot(body, (uint8_t)numParams);
            ((Function *)fun_obj)->need_kwargs = fun_scanSlot(body, (uint8_t)(numParams + 1));
            ((Function *)fun_obj)->constants = vm->constants;
            ((Function *)fun_obj)->names = vm->names;

            // Push the new closure onto the stack
            push_stack(vm, NEW_OBJ(add_obj(vm, fun_obj)));

            break;
        }

        case OP_LOAD_UPVALUE:
        {
            int index = code[pc++];
            UpValue *upValue = function->upvalues[index];
            if (upValue->index != -1)
                push_stack(vm, vm->stack[upValue->index]);
            else
                push_stack(vm, upValue->value);
            break;
        }

        case OP_STORE_UPVALUE:
        {
            int index = code[pc++];
            UpValue *upValue = function->upvalues[index];
            if (upValue->index != -1)
                vm->stack[upValue->index] = pop_stack(vm);
            else
                function->upvalues[index]->value = pop_stack(vm);
            break;
        }

        case OP_PUSH_SLICE:
        {
            // Pop the slice values from the stack
            Value step = pop_stack(vm);
            Value end = pop_stack(vm);
            Value start = pop_stack(vm);

            if (!IS_NUM(start) || !IS_NUM(end))
                vm_error(vm, "Slice [start] and [end] must be numbers.");

            // Create a new slice object
            if (!IS_NIL(step) && !IS_NUM(step))
                vm_error(vm, "Slice [step] must be nil or a number.");
            else
            {
                Value sequence = pop_stack(vm);
                if (IS_SEQUENCE(sequence))
                {
                    double end_num = as_number(end);
                    Value slice = get_slice(AS_OBJ(sequence), as_number(start), as_number(end),
                                            IS_NIL(step) ? 1.0 : as_number(step));
                    push_stack(vm, slice); // Push the slice onto the stack
                }
                else
                    vm_error(vm, "Slice operand must be a list or string.");
            }

            break;
        }

        case OP_GET_ITEM:
        {
            Value index = pop_stack(vm);     // Get the index from the stack
            Value container = pop_stack(vm); // Get the container from the stack

            if (!IS_OBJ(container))
                vm_error(vm, "Unsupported operand type for get item operator.\n");

            switch (OBJ_TYPE(container))
            {
            case OBJ_LIST:
            {
                list_t *list = as_list(container);
                if (list->size == 0)
                    push_stack(vm, NEW_NIL());
                else
                {
                    int _index = as_number(index);
                    Value item = *(Value *)list_getAt(list, _index);
                    push_stack(vm, item); // Avoid unsafe memory access
                }
                break;
            }
            case OBJ_MAP:
            {
                PiMap *map = AS_MAP(container);
                PiMap *owner = map_owner(map, index);
                Value item = owner ? map_get(owner, index) : NEW_NIL();

                bool bind_extends = owner != NULL &&
                                    IS_FUN(item) &&
                                    IS_STRING(index) &&
                                    strcmp(AS_CSTRING(index), "extends") == 0;

                if ((map->is_instance && owner != NULL && IS_FUN(item)) || bind_extends)
                {
                    Object *target = map->super_instance ? map->super_instance : AS_OBJ(container);
                    item = bind(vm, AS_FUN(item), target);
                }

                push_stack(vm, item); // Push NIL if key not found
                break;
            }

            case OBJ_MODULE:
            {
                ObjModule *module = AS_MODULE(container);
                char *property = as_string(index);
                Value item = NEW_NIL();

                if (strcmp(property, "name") == 0)
                {
                    char *name = module->name ? module->name : "";
                    item = NEW_OBJ(add_obj(vm, new_pistring(strdup(name))));
                }
                else if (strcmp(property, "is_main") == 0)
                {
                    item = NEW_BOOL(module->is_main);
                }
                else if (strcmp(property, "path") == 0)
                {
                    char *path = module->path ? module->path : "";
                    item = NEW_OBJ(add_obj(vm, new_pistring(strdup(path))));
                }
                // else if (strcmp(property, "builtin") == 0)
                //     item = NEW_BOOL(module->builtin);

                // else if (strcmp(property, "state") == 0)
                //     item = NEW_NUM(module->state);

                else if (strcmp(property, "exports") == 0)
                    item = NEW_OBJ((Object *)module->exports);

                else if (module->exports)
                    item = map_get(module->exports, index);

                free(property);
                push_stack(vm, item);
                break;
            }

            case OBJ_STRING:
            {

                char *str = as_string(container);                      // Convert Value to char*
                int _index = get_index(as_number(index), strlen(str)); // Convert index to int

                // Convert the character to a string (newly allocated)
                char *_char = malloc(2); // 1 char + null terminator
                _char[0] = str[_index];
                _char[1] = '\0';
                push_stack(vm, NEW_OBJ(add_obj(vm, new_pistring(_char))));
                free(str);
                break;
            }

            default:
                vm_error(vm, "Unsupported operand type for get item operator.\n");
            }
            break;
        }

        case OP_SET_ITEM:
        {
            Value index = pop_stack(vm);     // The index/key
            Value container = pop_stack(vm); // The container (list/map)
            Value value = pop_stack(vm);     // The value to set

            if (!IS_OBJ(container))
                vm_error(vm, "Unsupported operand type for set item operator.\n");

            switch (OBJ_TYPE(container))
            {
            case OBJ_LIST:
            {
                list_t *list = as_list(container);
                int _index = get_index(as_number(index), list_size(list));

                list_set(list, _index, &value);
                break;
            }

            case OBJ_MAP:
            {
                table_t *table = AS_MAP(container)->table;

                map_set(AS_MAP(container), index, value);
                break;
            }

            case OBJ_MODULE:
                vm_error(vm, "Cannot modify module object directly.");
                break;

            case OBJ_STRING:
                vm_error(vm, "Cannot modify immutable string.\n");
                break;

            default:
                vm_error(vm, "Unsupported operand type for set item operator.\n");
            }
            break;
        }

        case OP_IMPORT:
        {
            Value name = pop_stack(vm);
            if (!IS_STRING(name))
                vm_error(vm, "Module name must be a string.");

            Value module = load_module(vm, AS_STRING(name)->chars);
            push_stack(vm, module);
            break;
        }

        case OP_GET_EXPORT:
        {
            Value name = pop_stack(vm);
            Value module = pop_stack(vm);

            if (!IS_OBJ(module) || (OBJ_TYPE(module) != OBJ_MAP && OBJ_TYPE(module) != OBJ_MODULE))
                vm_error(vm, "Attempt to access export from non-module object.");

            if (!IS_STRING(name))
                vm_error(vm, "Export name must be a string.");

            PiMap *_module = (OBJ_TYPE(module) == OBJ_MODULE) ? AS_MODULE(module)->exports : AS_MAP(module);
            Value value = map_get(_module, name);
            push_stack(vm, value); // Push NIL if export not found
            break;
        }

        case OP_IMPORT_ALL:
        {
            Value module = pop_stack(vm);

            if (!IS_OBJ(module) || (OBJ_TYPE(module) != OBJ_MAP && OBJ_TYPE(module) != OBJ_MODULE))
                vm_error(vm, "Attempt to import from non-module object.");

            PiMap *_module = (OBJ_TYPE(module) == OBJ_MODULE) ? AS_MODULE(module)->exports : AS_MAP(module);
            table_t *table = _module->table;

            int size = ht_length(table);
            char **keys = ht_keys(table);
            for (int i = 0; i < size; i++)
            {
                char *key = keys[i];
                Value *value = (Value *)ht_get(table, key);
                if (!value)
                    continue;

                if (!ht_set(vm->globals, key, value))
                    ht_put(vm->globals, key, value);
            }

            break;
        }

        case OP_IMPORT_DEFAULT:
        {
            Value name = pop_stack(vm);
            Value module = pop_stack(vm);

            if (!IS_OBJ(module) || (OBJ_TYPE(module) != OBJ_MAP && OBJ_TYPE(module) != OBJ_MODULE))
                vm_error(vm, "Attempt to import from non-module object.");

            if (!IS_STRING(name))
                vm_error(vm, "Export name must be a string.");

            PiMap *_module = (OBJ_TYPE(module) == OBJ_MODULE) ? AS_MODULE(module)->exports : AS_MAP(module);
            Value value = map_get(_module, name);

            if (IS_FUN(value))
                push_stack(vm, value);
            else
                push_stack(vm, module);
            break;
        }

        case OP_RETURN:
        {
            // Handle return operation
            Value retval = pop_stack(vm);

            for (int i = vm->sp - 1; i >= vm->bp; i--)
                remove_upvalue(vm, i);

            Frame *frame = pop_frame(vm);

            while (vm->iter_sp > frame->iters_top)
                vm->iter_sp--;

            if (vm->iter_sp != -1)
                iter = vm->iters[vm->iter_sp];

            vm->pc = frame->pc;
            vm->bp = frame->bp;
            vm->sp = frame->sp;
            vm->ip = frame->ip;

            vm->code = frame->code;
            vm->constants = frame->constants;
            vm->names = frame->names;
            vm->instrs = frame->instrs;
            vm->function = (Object *)frame->function;

            push_stack(vm, retval);

            return;
        }

        case OP_HALT:
        {
            vm->running = false;
            // Halt the VM
            return;
        }

        case OP_NO:
            break;

        case OP_PUSH_NIL:
            push_stack(vm, NEW_NIL());
            break;

        case OP_DEBUG:
            // Handle debug operation
            printf("[DEBUG] Current PC: %d\n", pc);
            break;

        // Add more cases for other opcodes as needed
        default:
            vm_errorf(vm, "Unknown opcode: [%d]\n", op);

            vm->pc = pc;
        }

#ifdef __EMSCRIPTEN__
        // Allocation-driven threshold to avoid collecting on instruction-heavy loops.
        if (vm->counter >= vm->next_gc)
        {
            run_gc(vm);
            vm->counter = 0;
        }
#else
        if (vm->counter >= vm->next_gc)
        {
            int before = count_objs(vm);
            run_gc(vm);
            int after = count_objs(vm);
            int collected = before - after;

            vm->counter = 0;

            // Adapt threshold to avoid over-collecting in long-running loops.
            if (collected <= 0)
                vm->next_gc += vm->next_gc / 2; // GC reclaimed nothing: back off.
            else
                vm->next_gc = after + (after / 2); // Target ~1.5x live set allocations.
            vm->obj_count = after;

            // Clamp bounds (prevent very frequent or very rare GC).
            if (vm->next_gc < GC_MIN_THRESHOLD)
                vm->next_gc = GC_MIN_THRESHOLD;
            else if (vm->next_gc > GC_MAX_THRESHOLD)
                vm->next_gc = GC_MAX_THRESHOLD;

#ifdef DEBUG
            printf("[DEBUG] SP: %d\n", vm->sp);
            printf("[GC] Running garbage collection...\n");
            printf("[GC] Before: %d objects in memory\n", before);
            printf("[GC] After: %d objects in memory\n", after);
            printf("[GC] Collected: %d, Next threshold: %d\n", collected, vm->next_gc);
#endif
        }
#endif
        vm->pc = pc;
    }
}

/**
 * Frees the memory allocated for a virtual machine instance.
 *
 * This function is used to clean up the memory allocated to the virtual
 * machine structure. It first frees the memory allocated to the global
 * hash table and then frees the virtual machine structure itself.
 *
 * @param vm The virtual machine instance to be deallocated.
 */
void free_vm(vm_t *vm)
{

    // Free the memory allocated for hash tables
    if (vm->globals)
        ht_free(vm->globals);
    if (vm->modules)
        ht_free(vm->modules);

    if (vm->current_path)
        free(vm->current_path);

    // Free the memory allocated for the mutex
    pthread_mutex_destroy(&vm->lock);

    // Free the virtual machine structure itself
    free(vm);
}
