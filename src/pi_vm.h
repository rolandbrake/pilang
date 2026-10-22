#ifndef PI_VM_H
#define PI_VM_H

#include <pthread.h>
#include <stdarg.h>
#ifndef __EMSCRIPTEN__
#include <signal.h>
typedef sig_atomic_t InterruptFlag;
#else
#include <emscripten.h>
typedef int InterruptFlag;
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


#define GC_INITIAL_BYTES (16 * 1024 * 1024)
#define GC_MIN_ALLOCATION_BYTES (16 * 1024 * 1024)
#define GC_MAX_ALLOCATION_BYTES (64 * 1024 * 1024)

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
            _gc_collect(vm);                \
        if (interrupt_requested)            \
        {                                   \
            vm->running = FALSE;            \
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

#ifdef __EMSCRIPTEN__
#define VM_BROWSER_YIELD()                                      \
    do                                                          \
    {                                                           \
        if (!vm->running)                                       \
        {                                                       \
            VM_SYNC_PC();                                       \
            goto L_VM_DONE;                                     \
        }                                                       \
        if (++browser_steps >= BROWSER_YIELD_STEPS)             \
        {                                                       \
            browser_steps = 0;                                  \
            VM_SYNC_PC();                                       \
            emscripten_sleep(0);                                \
            VM_DISPATCH_SLOW();                                 \
        }                                                       \
    } while (0)
#else
#define VM_BROWSER_YIELD() do { } while (0)
#endif

#define VM_DISPATCH_SAFE()                           \
    do                                               \
    {                                                \
        VM_BROWSER_YIELD();                          \
        if (++safepoint_steps >= VM_SAFEPOINT_STEPS) \
        {                                            \
            safepoint_steps = 0;                     \
            VM_DISPATCH_SLOW();                      \
        }                                            \
        VM_DISPATCH_FAST();                          \
    } while (0)

#define BEGIN_VM_LOOP() VM_DISPATCH_SLOW()
#define END_INSTR() goto L_VM_AFTER_INSTR

#define BROWSER_YIELD_STEPS 4096
#define INTERRUPT_CHECK_STEPS 4096
#define VM_SAFEPOINT_STEPS 8192

#define TO_PRIM(vm, v, is_str) (IS_MAP(v) ? _to_primitive(vm, v, is_str) : (v))

// For non-MAP values this is zero cost — just returns the value itself
#define TO_PRIM_NUM(v) (IS_MAP(v) ? _to_primitive(vm, v, FALSE) : (v))
#define TO_PRIM_STR(v) (IS_MAP(v) ? _to_primitive(vm, v, TRUE) : (v))

// for frequent stack operations:
#define PUSH(v) (vm->stack[vm->sp++] = (v))
#define POP() (vm->stack[--vm->sp])

typedef struct
{
    int base;       // Stack slot holding the comprehension result list.
    int local_base; // Compiler local-slot base redirected to that list.
    int bp;         // Function base pointer active when the frame was opened.
} CompFrame;

typedef struct VM
{
    int pc;       // Program Counter: Points to the current instruction being executed.
    int sp;       // Stack Pointer: Tracks the top of the stack.
    int bp;       // Base Pointer: Used for managing function call frames.
    int ip;       // Instruction Pointer: Points to the current instruction being executed.
    int error_pc; // Bytecode offset used for runtime error source mapping.

    Value stack[STACK_MAX]; // Operand stack for storing temporary values and function calls.

    // PiStack *frames; // Call stack frames, storing function call contexts.

    Frame frames[STACK_MAX]; // Call stack frames, storing function call contexts.
    int frame_sp;

    List *code;      // PiList of bytecode instructions.
    List *constants; // PiList of constant values used in the program.
    List *names;     // PiList of variable/function names for identifier lookup.

    HT_Table *globals;          // Hash table storing global variables.
    GlobalCache *global_cache; // resolved globals for the active code unit

    Object *objects; // Linked list of dynamically allocated objects (for garbage collection).

    Object *iters[STACK_MAX]; // Iterator stack to support loops and iteration constructs.
    int iter_sp;              // Iterator Stack Pointer: Tracks the top of the iterator stack.

    CompFrame comp_frames[COMP_MAX]; // Active list-comprehension contexts.
    int comp_sp;

    // UpValue *openUpvalues[STACK_MAX]; // Stack of open upvalues used in nested functions.
    // int upvalue_sp;

    UpValue *open_upvalues; // linked list of open upvalues used in nested functions.

    bool running;         // Flag indicating whether the VM is currently executing code.
    pthread_mutex_t lock; // Mutex for thread synchronization.

    double fps; // Frames per second (used for performance measurement in graphical applications).

    Object *function;
    Value _kw_args; // Keyword arguments visible to the currently running native function.

    size_t gc_bytes;      // Estimated bytes currently owned by tracked objects.
    size_t next_gc_bytes; // Heap size at which the next collection is requested.
    bool gc_requested; // A safe-point collection is pending.

    HT_Table *instrs;        // PiList of instruction metadata
    Instruction *current_instr; // Source metadata for the opcode currently executing.

    List *gc_stack;

    int obj_count;

    HT_Table *modules;      // Hash table to store loaded modules by name
    struct PX_ProjectBuild *project_build; // Optional validated .px project build
    char *current_path;    // Current working directory for resolving relative imports
    PiClass *object_class; // Root class for the class/instance model

} VM;

extern volatile InterruptFlag interrupt_requested;

VM *init_vm(Compiler *comp, const char *entry_name, bool is_main);
void vm_reset(VM *vm, Compiler *comp);

Object *add_obj(VM *vm, Object *obj);
void vm_run(VM *vm);

void push_frame(VM *vm, Frame *frame);
Frame *pop_frame(VM *vm);

void vm_error(VM *vm, const char *message);
void vm_errorf(VM *vm, const char *fmt, ...);
Value vm_callMethodNoArgs(VM *vm, Value receiver, const char *name);

// keyword arguments for native builtin functions
Value vm_kwargs(VM *vm);
bool vm_hasKwarg(VM *vm, const char *name);
bool vm_getKwarg(VM *vm, const char *name, Value *out);
Value vm_getKwargOr(VM *vm, const char *name, Value fallback);

void free_vm(VM *vm);

#endif // PI_VM_H
