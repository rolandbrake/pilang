#ifndef PI_COMPILER_H
#define PI_COMPILER_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "pi_stack.h"
#include "pi_opcode.h"
#include "pi_value.h"
#include "common.h"
#include "pi_list.h"
#include "pi_table.h"
#include "pi_object.h"

// Represents a local variable in the current scope
typedef struct
{
    char *name;       // Variable name
    int depth;        // Scope depth where the variable is declared
    bool is_captured; // Indicates if the variable is captured as an upvalue
    bool is_const;    // Indicates if the binding cannot be rebound
} Local;

// Represents the compilation context for functions and blocks
typedef struct
{
    bool is_function; // Indicates if this context is for a function
    char *fun_name;   // Name of the function (if applicable)
    List *code;     // PiList of bytecode instructions
    List *instrs;   // PiList of instruction metadata (e.g., debug info)
    List *upvalues; // PiList of upvalues used in the function
    List *param_names; // PiList of parameter names for this function
    PiStack *locals;  // Stack of local variables
    int depth;        // Current scope depth
} Context;

// Represents a loop structure to track break/continue handling
typedef struct
{
    int _continue;   // Address to jump to when 'continue' is encountered
    int depth;       // Scope depth of the loop
    PiStack *breaks; // Stack of break statement addresses
    bool is_for;     // Flag indicating if the loop is a for-loop
} Loop;

// Compiler structure that maintains the current state of compilation
typedef struct
{
    List *code;      // PiList of bytecode instructions
    List *constants; // PiList of constant values
    HT_Table *string_constants; // String contents -> constant pool index

    List *names;         // PiList of variable names
    GlobalCache global_cache; // resolved globals for the top-level code unit
    List *builtin_names; // PiList of built-in names
    HT_Table *declared_globals; // Tracks globally declared names (let/fun)

    PiStack *locals;    // Stack of local variables
    PiStack *contexts;  // Stack of active compilation contexts
    Context *current; // Pointer to the current active context
    PiStack *loops;     // Stack of active loops
    HT_Table *instrs;    // HT_Table to store instruction metadata
    PiStack *objects;   // Stack of objects being allocated

    bool is_lookUp;  // Flag for lookup operations
    bool is_upvalue; // Flag indicating if a variable is an upvalue
    bool is_repl; // Flag indicating if the compiler is in REPL mode

    int current_line; // Current line number in the source code
    int current_col;  // Current column number in the source code

    char *source_name; // Source file/module name for parser diagnostics
    char *name; // Name of the current variable being processed
} Compiler;

// Represents an upvalue (captured variable from an outer scope)
typedef struct
{
    bool is_local; // Indicates if the upvalue comes from a local variable
    int index;     // Index of the captured variable
} Upvalue;

// Represents metadata for an instruction (used for debugging)
typedef struct
{
    char *descr; // Description of the instruction
    int line;    // Line number in the source code
    int column;  // Column number in the source code
    int offset;  // Bytecode offset of the instruction

    char *fun_name; // Name of the function this instruction belongs to (NULL for global)
    OpCode opcode;  // Actual opcode value

    int num_operands;  // How many operands this instruction has
    uint8_t *operands; // Dynamically allocated array of operands

} Instruction;

// Function to initialize a new compiler instance
Compiler *init_compiler();

// Stores a constant value in the compiler's constant pool
int store_const(Compiler *comp, Value value);
// Borrows text; allocates a string object only for a new constant.
int store_stringConst(Compiler *comp, const char *text);
int store_tokenConst(Compiler *comp, Token token);

// Returns the index of a variable name in the compiler's name table
int name_index(Compiler *comp, char *name);
int store_name(Compiler *comp, char *name);

// Adds a new local variable to the current scope
void add_local(Compiler *comp, char *name);
void add_localConst(Compiler *comp, char *name, bool is_const);
bool is_localScope(Compiler *comp);

// Functions for managing object allocation and scope
void push_object(Compiler *comp);
void pop_object(Compiler *comp);
bool is_object(Compiler *comp);

// Functions related to variable lookup and resolution
bool is_lookUp(Compiler *comp);
bool look_up(Compiler *comp, bool value);

// Bytecode emission functions
int emit(Compiler *comp, OpCode opcode);
int emit_8u(Compiler *comp, OpCode opcode, char *descr, int operand);
int emit_16u(Compiler *comp, OpCode opcode, char *descr, int operand);
int emit_16uX2(Compiler *comp, OpCode opcode, char *descr, int first, int second);
bool last_constantCondition(Compiler *comp, bool *value);
void discard_lastConstant(Compiler *comp);

// Emits a pop instruction to remove values from the stack
int emit_pop(Compiler *comp, int depth);

// Emits a jump instruction and later patches its address
int emit_jump(Compiler *comp, int address);
void patch_jump(Compiler *comp, int address);
void patch_switchCompare(Compiler *comp, int address);
void patch_jumpWithFlags(Compiler *comp, int address, uint16_t flags);

// Functions for managing local variables
void remove_locals(Compiler *comp, int size);
int get_local(Compiler *comp, char *name);
int get_localSize(Compiler *comp, int depth);
int resolve_local(Compiler *comp, int depth, char *name);

// Functions for handling upvalues (captured variables from outer scopes)
int resolve_upvalue(Compiler *comp, int depth, char *name);
int add_upvalue(Compiler *comp, int depth, int index, bool is_local);

// Functions for managing variable declarations and assignments
void add_variable(Compiler *comp, char *name);
void add_variableConst(Compiler *comp, char *name, bool is_const);
void store_variable(Compiler *comp, char *name);
void store_variableInit(Compiler *comp, char *name);
void load_variable(Compiler *comp, char *name);

// Functions for managing scope tracking
void push_scope(Compiler *comp);
void pop_scope(Compiler *comp);

// Functions for handling loops (break/continue support)
void push_loop(Compiler *comp, int address, bool is_for);
void pop_loop(Compiler *comp, int address);
bool is_forLoop(Compiler *comp);
bool in_loop(Compiler *comp);
int loop_depth(Compiler *comp);

// Functions for handling function definitions
void push_function(Compiler *comp, char *name);
void pop_function(Compiler *comp, int params);

// Functions for handling break/continue statements in loops
void push_break(Compiler *comp, int address);
int get_continue(Compiler *comp);

// Functions for managing and retrieving bytecode
void add_code(Compiler *comp, Byte _byte);
int code_size(Compiler *comp);




// Error handling functions
void set_errorSource(const char *source_name);
void p_error(const char *message, int line, int column);
void p_errorf(int line, int column, const char *format, ...);

// Debugging and memory management functions
void dis(Compiler *comp);
void dis_setOutput(FILE *file);
void free_compiler(Compiler *comp);

// Prints the list of currently active local variables (for debugging)
void print_locals(Compiler *comp);

// Resets the compiler to its initial state for reuse
void reset_compiler(Compiler *comp);

#endif
