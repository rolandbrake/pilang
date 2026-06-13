#ifndef PI_VM_H
#define PI_VM_H

#include <pthread.h>
#include <stdarg.h>

#include "pi_compiler.h"
#include "pi_table.h"
#include "pi_stack.h"
#include "pi_list.h"
#include "pi_object.h"
#include "pi_frame.h"

#define STACK_MAX 4096 // max stack size
#define ITER_MAX 256   // max iterator stack size

#define RUN_STEPS 1024 // max number of instructions to run

// Initial GC threshold (number of newly allocated VM objects).
#define NEXT_GC (1024 * 1024 * 8)


// Macros for handling opcodes [for future use]
#define VM_LABEL(name) L_##name
#define VM_TARGET(name) &&L_##name
#define VM_DISPATCH(op) goto *dispatch[op]
#define VM_CASE(name) VM_LABEL(name)

#define VM_DISPATCH_SAFE()                  \
    do                                      \
    {                                       \
        uint8_t _op = code[pc++];           \
        if (!dispatch[_op])                 \
            vm_error(vm, "Invalid opcode"); \
        goto *dispatch[_op];                \
    } while (0)

#define BEGIN_VM_LOOP() VM_DISPATCH()
#define END_INSTR() VM_DISPATCH()


#define TO_PRIM(vm, v, is_str) (IS_MAP(v) ? to_primitive(vm, v, is_str) : (v))

// For non-MAP values this is zero cost — just returns the value itself
#define TO_PRIM_NUM(v)    (IS_MAP(v) ? to_primitive(vm, v, false) : (v))
#define TO_PRIM_STR(v)    (IS_MAP(v) ? to_primitive(vm, v, true)  : (v))

typedef struct vm_t
{
    int pc; // Program Counter: Points to the current instruction being executed.
    int sp; // Stack Pointer: Tracks the top of the stack.
    int bp; // Base Pointer: Used for managing function call frames.
    int ip; // Instruction Pointer: Points to the current instruction being executed.

    Value stack[STACK_MAX]; // Operand stack for storing temporary values and function calls.

    // stack_t *frames; // Call stack frames, storing function call contexts.

    Frame frames[STACK_MAX]; // Call stack frames, storing function call contexts.
    int frame_sp;

    list_t *code;      // PiList of bytecode instructions.
    list_t *constants; // PiList of constant values used in the program.
    list_t *names;     // PiList of variable/function names for identifier lookup.

    table_t *globals; // Hash table storing global variables.

    Object *objects; // Linked list of dynamically allocated objects (for garbage collection).

    Object *iters[STACK_MAX]; // Iterator stack to support loops and iteration constructs.
    int iter_sp;              // Iterator Stack Pointer: Tracks the top of the iterator stack.

    int comp_bases[STACK_MAX]; // Runtime base stack for active list comprehensions.
    int comp_local_bases[STACK_MAX];
    int comp_bps[STACK_MAX];
    int comp_sp;

    // UpValue *openUpvalues[STACK_MAX]; // Stack of open upvalues used in nested functions.
    // int upvalue_sp;

    UpValue *openUpvalues; // linked list of open upvalues used in nested functions.

    bool running;         // Flag indicating whether the VM is currently executing code.
    pthread_mutex_t lock; // Mutex for thread synchronization.

    double fps; // Frames per second (used for performance measurement in graphical applications).

    Object *function;
    Value _kw_args; // Keyword arguments visible to the currently running native function.

    int counter;

    table_t *instrs; // PiList of instruction metadata

    int next_gc; // Next garbage collection threshold
    list_t *gc_stack;

    int obj_count;


    table_t *modules;   // Hash table to store loaded modules by name
    char *current_path; // Current working directory for resolving relative imports
    PiMap *object_proto; // Shared default prototype for object-style maps

} vm_t;

vm_t *init_vm(compiler_t *comp, const char *entry_name, bool is_main);
void vm_reset(vm_t *vm, compiler_t *comp);

Object *add_obj(vm_t *vm, Object *obj);
void run(vm_t *vm);

void push_frame(vm_t *vm, Frame *frame);
Frame *pop_frame(vm_t *vm);

void vm_error(vm_t *vm, const char *message);
void vm_errorf(vm_t *vm, const char *fmt, ...);
Value vm_callMethodNoArgs(vm_t *vm, Value receiver, const char *name);


// keyword arguments for native builtin functions
Value vm_kwargs(vm_t *vm);
bool vm_hasKwarg(vm_t *vm, const char *name);
bool vm_getKwarg(vm_t *vm, const char *name, Value *out);
Value vm_getKwargOr(vm_t *vm, const char *name, Value fallback);


void free_vm(vm_t *vm);

#endif // PI_VM_H
