#ifndef PI_VM_H
#define PI_VM_H

#include <pthread.h>
#include <stdarg.h>
#ifndef __EMSCRIPTEN__
#include <signal.h>
typedef sig_atomic_t interrupt_flag_t;
#else
typedef int interrupt_flag_t;
#endif

#include "pi_compiler.h"
#include "pi_table.h"
#include "pi_stack.h"
#include "pi_list.h"
#include "pi_object.h"
#include "pi_frame.h"

#define STACK_MAX 4096     // max stack size
#define ITER_MAX 256       // max iterator stack size
#define COMP_MAX STACK_MAX // max nested list-comprehension contexts

#define RUN_STEPS 1024 // max number of instructions to run

// Initial GC threshold (number of newly allocated VM objects).
#define NEXT_GC (1024 * 1024)

// Macros for computed-goto opcode dispatch.
// Requires GCC/Clang labels-as-values support.
#define VM_LABEL(name) L_##name
#define VM_TARGET(name) &&name
#define VM_DISPATCH(op) goto *dispatch[(op)]
#define VM_CASE(name) VM_LABEL(name)

#define VM_SYNC_PC()             \
    do                           \
    {                            \
        vm->pc = pc;             \
        vm->error_pc = instr_pc; \
    } while (0)

#define VM_DISPATCH_FAST()        \
    do                            \
    {                             \
        instr_pc = pc;            \
        uint8_t _op = code[pc++]; \
        current_op = _op;         \
        vm->ip++;                 \
        goto *dispatch[_op];      \
    } while (0)

#define VM_DISPATCH_SLOW()                  \
    do                                      \
    {                                       \
        if (pc >= length || !vm->running)   \
            goto L_VM_DONE;                 \
        instr_pc = pc;                      \
        VM_SYNC_PC();                       \
        if (vm->gc_requested)               \
            gc_collect(vm);                 \
        if (interrupt_requested)            \
        {                                   \
            vm->running = false;            \
            goto L_VM_DONE;                 \
        }                                   \
        if (!vm->running)                   \
            goto L_VM_DONE;                 \
        uint8_t _op = code[pc++];           \
        current_op = _op;                   \
        if (!dispatch[_op])                 \
            vm_error(vm, "Invalid opcode"); \
        vm->ip++;                           \
        goto *dispatch[_op];                \
    } while (0)

#define VM_DISPATCH_SAFE()                           \
    do                                               \
    {                                                \
        if (++safepoint_steps >= VM_SAFEPOINT_STEPS) \
        {                                            \
            safepoint_steps = 0;                     \
            VM_DISPATCH_SLOW();                      \
        }                                            \
        VM_DISPATCH_FAST();                          \
    } while (0)

#define BEGIN_VM_LOOP() VM_DISPATCH_SLOW()
#define END_INSTR() goto L_VM_AFTER_INSTR

#define GC_MIN_THRESHOLD (1024 * 64)
#define GC_MAX_THRESHOLD (1024 * 1024)
#define GC_RECLAIM_THRESHOLD (4096 * 4)

#define BROWSER_YIELD_STEPS 50000
#define INTERRUPT_CHECK_STEPS 4096
#define VM_SAFEPOINT_STEPS 8192

#define TO_PRIM(vm, v, is_str) (IS_MAP(v) ? to_primitive(vm, v, is_str) : (v))

// For non-MAP values this is zero cost — just returns the value itself
#define TO_PRIM_NUM(v) (IS_MAP(v) ? to_primitive(vm, v, false) : (v))
#define TO_PRIM_STR(v) (IS_MAP(v) ? to_primitive(vm, v, true) : (v))

// for frequent stack operations:
#define PUSH(v) (vm->stack[vm->sp++] = (v))
#define POP() (vm->stack[--vm->sp])

typedef struct
{
    int base;       // Stack slot holding the comprehension result list.
    int local_base; // Compiler local-slot base redirected to that list.
    int bp;         // Function base pointer active when the frame was opened.
} CompFrame;

typedef struct vm_t
{
    int pc;       // Program Counter: Points to the current instruction being executed.
    int sp;       // Stack Pointer: Tracks the top of the stack.
    int bp;       // Base Pointer: Used for managing function call frames.
    int ip;       // Instruction Pointer: Points to the current instruction being executed.
    int error_pc; // Bytecode offset used for runtime error source mapping.

    Value stack[STACK_MAX]; // Operand stack for storing temporary values and function calls.

    // pistack_t *frames; // Call stack frames, storing function call contexts.

    Frame frames[STACK_MAX]; // Call stack frames, storing function call contexts.
    int frame_sp;

    list_t *code;      // PiList of bytecode instructions.
    list_t *constants; // PiList of constant values used in the program.
    list_t *names;     // PiList of variable/function names for identifier lookup.

    table_t *globals;          // Hash table storing global variables.
    GlobalCache *global_cache; // resolved globals for the active code unit

    Object *objects; // Linked list of dynamically allocated objects (for garbage collection).

    Object *iters[STACK_MAX]; // Iterator stack to support loops and iteration constructs.
    int iter_sp;              // Iterator Stack Pointer: Tracks the top of the iterator stack.

    CompFrame comp_frames[COMP_MAX]; // Active list-comprehension contexts.
    int comp_sp;

    // UpValue *openUpvalues[STACK_MAX]; // Stack of open upvalues used in nested functions.
    // int upvalue_sp;

    UpValue *openUpvalues; // linked list of open upvalues used in nested functions.

    bool running;         // Flag indicating whether the VM is currently executing code.
    pthread_mutex_t lock; // Mutex for thread synchronization.

    double fps; // Frames per second (used for performance measurement in graphical applications).

    Object *function;
    Value _kw_args; // Keyword arguments visible to the currently running native function.

    int counter;       // Allocation debt since the previous collection.
    int gc_count;      // the Reclaim debt is the number of Object references overwritten since the previous collection.
    bool gc_requested; // A safe-point collection is pending.

    table_t *instrs;        // PiList of instruction metadata
    instr_t *current_instr; // Source metadata for the opcode currently executing.

    int next_gc; // Next garbage collection threshold
    list_t *gc_stack;

    int obj_count;

    table_t *modules;    // Hash table to store loaded modules by name
    char *current_path;  // Current working directory for resolving relative imports
    PiMap *object_proto; // Shared default prototype for object-style maps

} vm_t;

extern volatile interrupt_flag_t interrupt_requested;

vm_t *init_vm(compiler_t *comp, const char *entry_name, bool is_main);
void vm_reset(vm_t *vm, compiler_t *comp);

Object *add_obj(vm_t *vm, Object *obj);
void vm_run(vm_t *vm);

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
