#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

#include "pi_compiler.h"
#include "pi_object.h"
#include "pi_opcode.h"
#include "pi_stack.h"
#include "common.h"
#include "pi_list.h"
#include "pi_string.h"

#include "builtin/pi_builtin.h"

static FILE *dis_output = NULL;

#ifdef __EMSCRIPTEN__
static void dis_emit(const char *text)
{
    EM_ASM({ console.log(UTF8ToString($0)); }, text);
}
#else
static void dis_emitPlain(FILE *file, const char *text)
{
    const char *p = text;
    while (*p)
    {
        if (*p == '\033' && p[1] == '[')
        {
            p += 2;
            while (*p && !isalpha((unsigned char)*p))
                p++;
            if (*p)
                p++;
            continue;
        }
        fputc(*p++, file);
    }
}

static void dis_emit(const char *text)
{
    if (dis_output)
        dis_emitPlain(dis_output, text);
    else
        printf("%s", text);
}
#endif

static const char *error_source = NULL;

void dis_setOutput(FILE *file)
{
    dis_output = file;
}

static const char *op_names[] = {
    [0x4] = "RETURN_VALUE",
    [0x5] = "LOAD_CONST",
    [0x6] = "PRINT_VALUE",
    [0x7] = "POP_TOP",
    [0x8] = "PUSH_STACK",
    [0x9] = "PUSH_NIL",
    [0xa] = "STORE_GLOBAL",
    [0xb] = "LOAD_GLOBAL",
    [0xc] = "STORE_LOCAL",
    [0xd] = "LOAD_LOCAL",
    [0xe] = "JUMP",
    [0xf] = "JUMP_IF_FALSE",
    [0x10] = "RETURN_NIL",
    [0x11] = "CALL_FUNCTION",
    [0x12] = "CALL_FUNCTION_KW",
    [0x13] = "POP_N",
    [0x14] = "COMPARE_OP",
    [0x15] = "JUMP_IF_TRUE",
    [0x16] = "HALT_OP",
    [0x17] = "PUSH_ITER",
    [0x18] = "LOOP_OP",
    [0x19] = "PUSH_RANGE",
    [0x1a] = "BINARY_OP",
    [0x1b] = "PUSH_LIST",
    [0x20] = "LIST_APPEND",
    [0x21] = "LIST_EXTEND",
    [0x1c] = "STORE_UPVALUE",
    [0x1d] = "LOAD_UPVALUE",
    [0x1e] = "NO_OP",
    [0x1f] = "CREATE_UPVALUE",
    [0x22] = "PUSH_FUNCTION",
    [0x23] = "PUSH_MAP",
    [0x24] = "PUSH_UPVALUE",
    [0x25] = "PUSH_CLOSURE",
    [0x26] = "DUP_TOP",
    [0x27] = "PUSH_SLICE",
    [0x28] = "GET_ITEM",
    [0x29] = "SET_ITEM",
    [0x2a] = "UNARY_OP",
    [0x2b] = "DEBUG_OP",
    [0x2c] = "POP_ITER",
    [0x3c] = "CLOSE_UPVALUE",
    [0x3d] = "IMPORT_OP",
    [0x3e] = "GET_EXPORT",
    [0x3f] = "IMPORT_ALL",
    [0x40] = "IMPORT_DEFAULT",
    [0x41] = "LOAD_SUPER",
    [0x42] = "TENSOR_GET",
    [0x43] = "TENSOR_SET",
    [0x44] = "LIST_FINALIZE",
    [0x45] = "CALL_SPREAD",
    [0x46] = "MAP_SET",
    [0x47] = "MAP_EXTEND",
    [0x48] = "MAP_FINALIZE",
    [0x49] = "COMP_APPEND",
    [0x4a] = "PUSH_SET",
    [0x4b] = "PUSH_TUPLE",
    [0x4c] = "GET_MEMBER",
    [0x4d] = "SET_MEMBER",
    [0x4e] = "COMP_BEGIN",
    [0x4f] = "COMP_END",
};

static context_t *create_context(bool is_function, list_t *code, char *fun_name)
{
    static int f_count = 0;
    context_t *context = malloc(sizeof(context_t));

    context->upvalues = list_create(sizeof(upvalue_t));
    context->locals = stack_create(sizeof(local_t));
    context->instrs = list_create(sizeof(instr_t));
    context->param_names = NULL;

    context->is_function = is_function;
    context->depth = 0;
    context->code = code;

    if (fun_name == NULL && is_function)
    {
        context->fun_name = malloc(32);
        sprintf(context->fun_name, "<LAMBDA: %d>", f_count);
        f_count++;
    }
    else
        context->fun_name = fun_name;

    return context;
}

static void free_instr(instr_t *instr)
{
    if (!instr)
        return;

    free(instr->descr);
    free(instr->fun_name);
    free(instr->operands);
}

static void free_context(context_t *context)
{
    list_free(context->upvalues);

    // locals stack stores local_t* pointer VALUES
    // stack_pop returns pointer to the slot holding the local_t*
    // dereference to get the actual local_t*, then free that
    while (!stack_isEmpty(context->locals))
    {
        local_t *local = *(local_t **)stack_pop(context->locals);
        free(local->name);
        free(local); // local_t was heap-alloc'd in add_localConst — safe to free
    }
    stack_free(context->locals);

    if (context->instrs)
    {
        while (!list_isEmpty(context->instrs))
        {
            instr_t *instr = (instr_t *)list_pop(context->instrs);
            free_instr(instr);
        }
        list_free(context->instrs);
    }

    if (context->param_names)
        list_free(context->param_names);

    free(context->fun_name);
    free(context);
}

static void free_loop(loop_t *loop)
{
    stack_free(loop->breaks);
    free(loop);
}

static void free_loop(loop_t *loop);

static context_t *current_context(compiler_t *comp)
{
    context_t **context = (context_t **)stack_peek(comp->contexts);
    return context ? *context : NULL;
}

static context_t *context_at(compiler_t *comp, int depth)
{
    context_t **context = (context_t **)stack_getAt(comp->contexts, depth);
    return context ? *context : NULL;
}

static context_t *pop_context(compiler_t *comp)
{
    context_t **context = (context_t **)stack_pop(comp->contexts);
    return context ? *context : NULL;
}

static loop_t *current_loop(compiler_t *comp)
{
    loop_t **loop = (loop_t **)stack_peek(comp->loops);
    return loop ? *loop : NULL;
}

static loop_t *pop_loopContext(compiler_t *comp)
{
    loop_t **loop = (loop_t **)stack_pop(comp->loops);
    return loop ? *loop : NULL;
}

static upvalue_t *create_upvalue(int index, bool is_local)
{
    upvalue_t *upvalue = malloc(sizeof(upvalue_t));

    upvalue->index = index;
    upvalue->is_local = is_local;
    return upvalue;
}

compiler_t *init_compiler()
{

    compiler_t *comp = (compiler_t *)malloc(sizeof(compiler_t));

    comp->code = list_create(sizeof(uint8_t));
    comp->constants = list_create(sizeof(Value));

    list_add(comp->constants, &NEW_NUM(NAN));
    list_add(comp->constants, &NEW_NUM(INFINITY));

    list_add(comp->constants, &NEW_BOOL(true));
    list_add(comp->constants, &NEW_BOOL(false));

    comp->names = list_create(sizeof(String));
    memset(&comp->global_cache, 0, sizeof(comp->global_cache));
    comp->builtin_names = list_create(sizeof(String));
    comp->declared_globals = ht_create(sizeof(bool));

    for (int i = 0; i < BUILTIN_CONST_COUNT; i++)
        list_add(comp->builtin_names, new_string(builtin_constants[i].name));

    for (int i = 0; i < BUILTIN_FUNC_COUNT; i++)
        list_add(comp->builtin_names, new_string(builtin_functions[i].name));

    comp->locals = stack_create(sizeof(local_t));
    comp->contexts = stack_create(sizeof(context_t *));
    comp->loops = stack_create(sizeof(loop_t *));
    comp->objects = stack_create(sizeof(String));
    comp->name = "";

    comp->current = create_context(false, comp->code, NULL);

    comp->instrs = ht_create(sizeof(list_t));

    comp->is_lookUp = false;
    comp->is_upvalue = false;
    comp->is_repl = false;
    comp->source_name = NULL;

    stack_push(comp->contexts, &comp->current);

    return comp;
}

static int read_short(compiler_t *comp, int index)
{
    uint8_t *code = (uint8_t *)comp->code->data;
    int high = code[index] & 0xFF;
    int low = code[index + 1] & 0xFF;

    return (high << 8) | low;
}

void add_code(compiler_t *comp, byte _byte)
{
    if (!comp || !comp->code)
    {
        fprintf(stderr, "Invalid compiler or code list\n");
        exit(EXIT_FAILURE);
    }

    int size = comp->code->size;
    int cap = comp->code->capacity;

    if (size == cap)
    {
        cap = cap == 0 ? 8 : cap * 2;
        comp->code->data = realloc(comp->code->data, cap * sizeof(byte));
        if (comp->code->data == NULL)
        {
            fprintf(stderr, "Memory allocation failed\n");
            exit(EXIT_FAILURE);
        }
        comp->code->capacity = cap;
    }

    ((byte *)comp->code->data)[size++] = _byte;
    comp->code->size = size;
}

bool is_localScope(compiler_t *comp)
{
    return comp->current->depth > 0 || comp->current->is_function;
}

void push_object(compiler_t *comp)
{
    if (!comp->is_lookUp)
        stack_push(comp->objects, new_string(comp->name));
}

void pop_object(compiler_t *comp)
{
    if (!comp->is_lookUp)
        stack_pop(comp->objects);
}

bool is_object(compiler_t *comp)
{
    return !stack_isEmpty(comp->objects);
}

bool is_constructor(compiler_t *comp)
{
    if (is_object(comp) && comp->current->is_function)
        return strcmp(comp->current->fun_name, "constructor") == 0;

    return false;
}
bool is_lookUp(compiler_t *comp)
{
    return comp->is_lookUp;
}

bool look_up(compiler_t *comp, bool value)
{
    bool look_up = comp->is_lookUp;

    comp->is_lookUp = value;

    return look_up;
}

void print_locals(compiler_t *comp)
{
    printf("Locals stack (top to bottom):\n");

    local_t *local = (local_t *)stack_peek(comp->current->locals);
    printf("Local: name = %s, depth = %d, is_captured = %s\n",
           local->name,
           local->depth,
           local->is_captured ? "true" : "false");

    printf("\n");
}

static void print_local(void *_local)
{
    local_t *local = (local_t *)_local;
    printf("Local: name = %s, depth = %d, is_captured = %s\n",
           local->name,
           local->depth,
           local->is_captured ? "true" : "false");
}

void add_localConst(compiler_t *comp, char *name, bool is_const)
{
    pistack_t *locals = comp->current->locals;

    // Check for name conflict ONLY in current block
    for (int i = stack_size(locals) - 1; i >= 0; i--)
    {
        local_t *local = (local_t *)stack_getAt(locals, i);

        if (local->depth < comp->current->depth)
            break;

        if (strcmp(local->name, name) == 0)
        {
            p_errorf(comp->current_line, comp->current_col,
                     "Name already declared in this scope: [%s]", name);
        }
    }

    local_t *local = malloc(sizeof(local_t));
    local->name = strdup(name);
    local->depth = comp->current->depth;
    local->is_captured = false;
    local->is_const = is_const;

    stack_push(locals, local);
}

void add_local(compiler_t *comp, char *name)
{
    add_localConst(comp, name, false);
}

int get_local(compiler_t *comp, char *name)
{
    int index = -1;
    int depth = stack_size(comp->contexts) - 1;
    comp->is_upvalue = false;

    index = resolve_local(comp, depth, name);
    if (index != -1)
        return index;
    else
    {
        index = resolve_upvalue(comp, depth, name);
        comp->is_upvalue = true;
    }
    return index;
}

int get_localSize(compiler_t *comp, int depth)
{
    int size = 0;

    pistack_t *locals = comp->current->locals;
    int l_size = stack_size(locals);

    for (int i = l_size - 1; i >= 0; i--)
    {
        local_t *local = (local_t *)stack_getAt(locals, i);
        if (local->depth >= depth)
            size++;
        else
            break;
    }
    return size;
}

int resolve_local(compiler_t *comp, int depth, char *name)
{
    int index = -1;
    local_t *local;
    context_t *context = context_at(comp, depth);
    for (int i = stack_size(context->locals) - 1; i >= 0; i--)
    {
        local = (local_t *)stack_getAt(context->locals, i);
        if (strcmp(local->name, name) == 0)
        {
            index = i;
            break;
        }
    }
    return index;
}

int resolve_upvalue(compiler_t *comp, int depth, char *name)
{
    if (depth == 0)
        return -1;

    int index = resolve_local(comp, depth - 1, name);
    if (index != -1)
        return add_upvalue(comp, depth, index, true);

    int upvalue = resolve_upvalue(comp, depth - 1, name);
    if (upvalue != -1)
        return add_upvalue(comp, depth, upvalue, false);

    return -1;
}

int add_upvalue(compiler_t *comp, int depth, int index, bool is_local)
{
    context_t *current = context_at(comp, depth);

    int size = list_size(current->upvalues);
    for (int i = 0; i < size; i++)
    {
        upvalue_t *_upvalue = (upvalue_t *)list_getAt(current->upvalues, i);

        if (_upvalue->index == index && _upvalue->is_local == is_local)
            return i;
    }

    upvalue_t *upvalue = malloc(sizeof(upvalue_t));
    upvalue->index = index;
    upvalue->is_local = is_local;
    list_add(current->upvalues, upvalue);

    return size;
}

bool is_builtin(compiler_t *comp, const char *name)
{
    for (int i = 0; i < comp->builtin_names->size; i++)
    {
        char *existing = string_get(comp->builtin_names, i);
        if (strcmp(existing, name) == 0)
            return true;
    }
    return false;
}

static bool global_isConst(compiler_t *comp, const char *name)
{
    bool *is_const = comp->declared_globals ? (bool *)ht_get(comp->declared_globals, name) : NULL;
    return is_const != NULL && *is_const;
}

static bool local_isConstAt(compiler_t *comp, int depth, int index)
{
    context_t *context = context_at(comp, depth);
    if (!context || index < 0 || index >= stack_size(context->locals))
        return false;

    local_t *local = (local_t *)stack_getAt(context->locals, index);
    return local && local->is_const;
}

static bool resolved_upvalueIsConst(compiler_t *comp, const char *name)
{
    int depth = stack_size(comp->contexts) - 2;
    for (int d = depth; d >= 0; d--)
    {
        context_t *context = context_at(comp, d);
        for (int i = stack_size(context->locals) - 1; i >= 0; i--)
        {
            local_t *local = (local_t *)stack_getAt(context->locals, i);
            if (strcmp(local->name, name) == 0)
                return local->is_const;
        }
    }

    return false;
}

static void check_constAssignment(compiler_t *comp, const char *name, int local_index, bool is_upvalue)
{
    if (local_index != -1)
    {
        bool is_const = is_upvalue
                            ? resolved_upvalueIsConst(comp, name)
                            : local_isConstAt(comp, stack_size(comp->contexts) - 1, local_index);
        if (is_const)
            p_errorf(comp->current_line, comp->current_col,
                     "Cannot reassign const binding [%s]", name);
        return;
    }

    if (global_isConst(comp, name))
        p_errorf(comp->current_line, comp->current_col,
                 "Cannot reassign const binding [%s]", name);
}

void add_variableConst(compiler_t *comp, char *name, bool is_const)
{
    int g_index = -1;
    if (is_localScope(comp))
    {
        add_localConst(comp, name, is_const);
    }
    else
    {
        if (ht_get(comp->declared_globals, name) != NULL)
            p_errorf(comp->current_line, comp->current_col, "Name already exists [%s]", name);
        if (is_builtin(comp, name))
            p_errorf(comp->current_line, comp->current_col,
                     "Name already exists [%s]; this name is reserved by a builtin.", name);

        ht_put(comp->declared_globals, name, &is_const);

        g_index = store_name(comp, name);
        emit_8u(comp, OP_STORE_GLOBAL, name, g_index);
    }
}

void add_variable(compiler_t *comp, char *name)
{
    add_variableConst(comp, name, false);
}

static void store_variableWithMode(compiler_t *comp, char *name, bool allow_const_init)
{
    if (is_localScope(comp))
    {
        int index = get_local(comp, name);
        bool is_upvalue = comp->is_upvalue;
        if (index != -1)
        {
            if (!allow_const_init)
                check_constAssignment(comp, name, index, is_upvalue);
            emit_8u(comp, is_upvalue ? OP_STORE_UPVALUE : OP_STORE_LOCAL, name, index);
        }
        else
        {
            if (!allow_const_init)
                check_constAssignment(comp, name, -1, false);
            int g_index = store_name(comp, name);
            emit_8u(comp, OP_STORE_GLOBAL, name, g_index);
        }
    }
    else
    {
        if (!allow_const_init)
            check_constAssignment(comp, name, -1, false);
        int g_index = store_name(comp, name);
        emit_8u(comp, OP_STORE_GLOBAL, name, g_index);
    }
}

void store_variable(compiler_t *comp, char *name)
{
    store_variableWithMode(comp, name, false);
}

void store_variableInit(compiler_t *comp, char *name)
{
    store_variableWithMode(comp, name, true);
}

void load_variable(compiler_t *comp, char *name)
{
    int index = get_local(comp, name);
    if (index != -1)
    {
        if (comp->is_upvalue)
            emit_8u(comp, OP_LOAD_UPVALUE, name, index);
        else
            emit_8u(comp, OP_LOAD_LOCAL, name, index);
    }
    else
    {
        int g_index = name_index(comp, name);
        if (g_index == -1)
            g_index = store_name(comp, name);
        emit_8u(comp, OP_LOAD_GLOBAL, name, g_index);
    }
}

int name_index(compiler_t *comp, char *name)
{
    for (int i = 0; i < comp->names->size; i++)
    {
        char *_name = string_get(comp->names, i);
        if (strcmp(_name, name) == 0)
            return i;
    }
    return -1;
}

int store_name(compiler_t *comp, char *name)
{
    int index = name_index(comp, name);
    if (index != -1)
        return index;

    list_add(comp->names, new_string(name));

    // Return the new index
    return comp->names->size - 1;
}

void remove_locals(compiler_t *comp, int size)
{
    while (size-- > 0)
        stack_pop(comp->current->locals);
}

void push_scope(compiler_t *comp)
{
    comp->current->depth++;
}

void pop_scope(compiler_t *comp)
{
    int size = emit_pop(comp, comp->current->depth);

    remove_locals(comp, size);

    comp->current->depth--;
}

void push_loop(compiler_t *comp, int address, bool is_for)
{
    loop_t *loop = (loop_t *)malloc(sizeof(loop_t));

    loop->_continue = address;
    loop->depth = comp->current->depth;
    loop->breaks = stack_create(sizeof(int));
    loop->is_for = is_for;

    stack_push(comp->loops, &loop);
}

void pop_loop(compiler_t *comp, int address)
{
    loop_t *loop = pop_loopContext(comp);
    pistack_t *breaks = loop->breaks;

    int16_t offset = address - comp->code->size;

    emit_16u(comp, OP_JUMP, "", offset);

    while (stack_isEmpty(breaks) == false)
        patch_jump(comp, POP_INT(breaks));

    free_loop(loop);
}

void push_break(compiler_t *comp, int address)
{
    PUSH_INT(current_loop(comp)->breaks, address);
}

int get_continue(compiler_t *comp)
{
    return current_loop(comp)->_continue;
}
bool is_forLoop(compiler_t *comp)
{
    return current_loop(comp)->is_for;
}

bool in_loop(compiler_t *comp)
{
    return !stack_isEmpty(comp->loops);
}

int loop_depth(compiler_t *comp)
{
    return current_loop(comp)->depth;
}

static bool code_usesLocalSlot(list_t *code, uint8_t slot)
{
    uint8_t *bytecode = (uint8_t *)code->data;
    int length = code->size;

    for (int i = 0; i < length;)
    {
        uint8_t op = bytecode[i++];

        if ((op == OP_LOAD_LOCAL || op == OP_STORE_LOCAL) &&
            i < length && bytecode[i] == slot)
            return true;

        i += operand_count(op);
    }

    return false;
}

void push_function(compiler_t *comp, char *name)
{
    if (!comp->is_lookUp)
    {
        current_context(comp)->depth = comp->current->depth;
        context_t *context = create_context(true, list_create(sizeof(uint8_t)), name);
        stack_push(comp->contexts, &context);

        comp->current = current_context(comp);
        comp->code = comp->current->code;
        comp->locals = comp->current->locals;
    }
}

void pop_function(compiler_t *comp, int params)
{
    if (!comp->is_lookUp)
    {
        char *name = comp->current->fun_name;

        ht_put(comp->instrs, name, comp->current->instrs);

        int uv_size = list_size(comp->current->upvalues);
        list_t *upvalues = comp->current->upvalues;

        ObjCode *code = (ObjCode *)new_code(comp->code);
        
        code->need_args = code_usesLocalSlot(comp->code, (uint8_t)params);
        code->need_kwargs = code_usesLocalSlot(comp->code, (uint8_t)(params + 1));

        code->method_need_args = code_usesLocalSlot(comp->code, (uint8_t)(params + 1));
        code->method_need_kwargs = code_usesLocalSlot(comp->code, (uint8_t)(params + 2));

        int c_index = store_const(comp, NEW_OBJ(code));

        context_t *context = pop_context(comp);

        code->param_names = context->param_names;
        context->param_names = NULL;

        comp->current = current_context(comp);
        comp->code = comp->current->code;
        comp->locals = comp->current->locals;

        free(context);

        int n_index = store_const(comp, NEW_OBJ(new_pistring(name)));

        emit_16u(comp, OP_LOAD_CONST, name, n_index);

        char code_descr[32];
        snprintf(code_descr, sizeof(code_descr), "<code: 0x%04X>", code->hash);

        emit_16u(comp, OP_LOAD_CONST, code_descr, c_index);

        for (int i = 0; i < uv_size; i++)
        {
            upvalue_t *upvalue = (upvalue_t *)list_getAt(upvalues, i);
            int index = store_const(comp, NEW_NUM(upvalue->index));
            emit_16u(comp, OP_LOAD_CONST, itos(upvalue->index), index);
            index = store_const(comp, NEW_BOOL(upvalue->is_local));
            emit_16u(comp, OP_LOAD_CONST, upvalue->is_local ? "true" : "false", index);
        }

        if (uv_size > 0)
            emit_16u(comp, OP_PUSH_CLOSURE, name, (params << 8) | uv_size);
        else
            emit_8u(comp, OP_PUSH_FUNCTION, name, (byte)params);
    }
}
int store_const(compiler_t *comp, Value value)
{
    Value _value;
    for (int i = 0; i < comp->constants->size; i++)
    {
        _value = *(Value *)list_getAt(comp->constants, i);
        if (equals(_value, value))
            return i;
    }
    list_add(comp->constants, &value);
    return comp->constants->size - 1;
}

static int _emit(compiler_t *comp, OpCode opcode, char *descr, int num_operands, int line, int column, ...)
{
    if (comp->code == NULL || comp->instrs == NULL || comp->is_lookUp)
        return -1;

    int size = list_size(comp->code);

    uint8_t _opcode = (uint8_t)opcode;
    list_add(comp->code, &_opcode);

    uint8_t *operands = malloc(sizeof(uint8_t) * num_operands);
    if (!operands)
        return -1;

    va_list args;
    va_start(args, column);
    for (int i = 0; i < num_operands; i++)
    {
        uint8_t operand = (uint8_t)va_arg(args, int);
        list_add(comp->code, &operand);
        operands[i] = operand;
    }
    va_end(args);

    instr_t instr = {0};
    instr.descr = strdup(descr);
    instr.line = line;
    instr.column = column;
    instr.offset = size;
    if (comp->current->fun_name != NULL)
        instr.fun_name = strdup(comp->current->fun_name);
    else
        instr.fun_name = NULL;

    instr.opcode = opcode;
    instr.num_operands = num_operands;
    instr.operands = operands;

    list_add(comp->current->instrs, &instr);

    return comp->code->size - 1;
}

int emit(compiler_t *comp, OpCode opcode)
{
    return _emit(comp, opcode, "", 0, comp->current_line, comp->current_col);
}

int emit_8u(compiler_t *comp, OpCode opcode, char *descr, int operand)
{
    return _emit(comp, opcode, descr, 1, comp->current_line, comp->current_col, operand);
}

int emit_16u(compiler_t *comp, OpCode opcode, char *descr, int operand)
{
    byte op1 = (byte)((operand >> 8) & 0xff);
    byte op2 = (byte)(operand & 0xff);
    return _emit(comp, opcode, descr, 2, comp->current_line, comp->current_col, op1, op2);
}

int emit_pop(compiler_t *comp, int depth)
{
    int size = get_localSize(comp, depth);
    if (size > 1)
        emit_8u(comp, OP_POP_N, "", (uint8_t)size);
    else if (size == 1)
        emit(comp, OP_POP);
    return size;
}

int emit_jump(compiler_t *comp, int address)
{
    emit_16u(comp, OP_JUMP, "", address);
    return comp->code->size - 1;
}

void patch_jumpWithFlags(compiler_t *comp, int address, uint16_t flags)
{
    if (!comp->is_lookUp)
    {

        uint8_t *code = (uint8_t *)comp->code->data;
        int offset = comp->code->size - (address - 2);
        uint16_t encoded = ((uint16_t)offset & OP_LOOP_OFFSET_MASK) | flags;

        code[address - 1] = (encoded >> 8) & 0xff;
        code[address] = encoded & 0xff;

        for (int i = list_size(comp->current->instrs) - 1; i >= 0; i--)
        {
            instr_t *instr = list_getAt(comp->current->instrs, i);
            if (instr->offset == address - 2)
            {
                instr->operands[0] = (encoded >> 8) & 0xff;
                instr->operands[1] = encoded & 0xff;
                break;
            }
        }
    }
}

void patch_jump(compiler_t *comp, int address)
{
    patch_jumpWithFlags(comp, address, 0);
}

int code_size(compiler_t *comp)
{
    return comp->code->size;
}

void dis(compiler_t *comp)
{
    dis_emit("disassembling...\n");

    if (stack_size(comp->contexts) > 0)
    {
        context_t *global_ctx = context_at(comp, 0);
        ht_put(comp->instrs, "<global>", global_ctx->instrs);
    }

    // Use iterator over the instruction table
    ht_iter it = ht_iterator(comp->instrs);
    while (ht_next(&it))
    {
        const char *scope_name = it.key;
        list_t *instrs = (list_t *)it.value; // stored as void*, cast back

        char header_buf[256];
#ifdef __EMSCRIPTEN__
        snprintf(header_buf, sizeof(header_buf), "\n== Disassembly of %s ==\n\n",
                 strcmp(scope_name, "<global>") == 0 ? "global scope" : scope_name);
#else
        snprintf(header_buf, sizeof(header_buf), "\n\033[1;36m== Disassembly of %s ==\033[0m\n\n",
                 strcmp(scope_name, "<global>") == 0 ? "global scope" : scope_name);
#endif
        dis_emit(header_buf);

        if (!instrs)
            continue;

        int line = 0, pc = 0;
        for (int j = 0; j < instrs->size; j++)
        {
            instr_t *instr = (instr_t *)list_getAt(instrs, j);
            OpCode opcode = instr->opcode;
            uint8_t *operands = instr->operands;
            char *descr = instr->descr;

            char line_buf[256] = {0};

#ifdef __EMSCRIPTEN__
            switch (opcode)
            {
            case OP_STORE_GLOBAL:
            case OP_STORE_LOCAL:
            case OP_LOAD_GLOBAL:
            case OP_LOAD_LOCAL:
            case OP_LOAD_UPVALUE:
            case OP_STORE_UPVALUE:
            case OP_BINARY:
            case OP_COMPARE:
            case OP_UNARY:
            case OP_POP_N:
            case OP_CALL_FUNCTION:
            case OP_CALL_FUNCTION_KW:
            case OP_CALL_SPREAD:
            case OP_PUSH_FUNCTION:
            case OP_TENSOR_GET:
            case OP_TENSOR_SET:
            case OP_COMP_BEGIN:
            case OP_COMP_APPEND:
            case OP_MAP_FINALIZE:
                snprintf(line_buf, sizeof(line_buf), "%-4d: %-15s %-5d",
                         line++, op_names[opcode], operands[0]);
                line++;
                pc++;
                break;

            case OP_JUMP_IF_FALSE:
            case OP_JUMP:
            case OP_LOOP:
            {
                int offset = (int16_t)((operands[0] << 8) | operands[1]);
                if (opcode == OP_LOOP)
                    offset &= OP_LOOP_OFFSET_MASK;
                int target = instr->offset + offset;

                snprintf(line_buf, sizeof(line_buf),
                         offset < 0 ? "%-4d: %-14s %-6d [<< %-3d]\n"
                                    : "%-4d: %-14s %-6d [>> %-3d]\n",
                         line++, op_names[opcode], offset, target);
                line += 2;
                pc += 2;

                dis_emit(line_buf);
                continue;
            }

            case OP_LOAD_CONST:
            case OP_PUSH_LIST:
            case OP_PUSH_MAP:
            case OP_GET_MEMBER:
            case OP_SET_MEMBER:
                snprintf(line_buf, sizeof(line_buf), "%-4d: %-15s %-5d",
                         line++, op_names[opcode], (int16_t)((operands[0] << 8) | operands[1]));
                line += 2;
                pc += 2;
                break;

            case OP_LIST_APPEND:
            case OP_LIST_EXTEND:
            case OP_LIST_FINALIZE:
            case OP_COMP_END:
            case OP_MAP_SET:
            case OP_MAP_EXTEND:
                snprintf(line_buf, sizeof(line_buf), "%-4d: %-15s",
                         line++, op_names[opcode]);
                break;

            case OP_PUSH_CLOSURE:
                snprintf(line_buf, sizeof(line_buf), "%-4d: %-15s %d %3d",
                         line++, op_names[opcode], operands[0], operands[1]);
                line += 2;
                pc += 2;
                break;

            default:
                snprintf(line_buf, sizeof(line_buf), "%-4d: %-15s",
                         line++, op_names[opcode]);
                break;
            }
#else
            switch (opcode)
            {
            case OP_STORE_GLOBAL:
            case OP_STORE_LOCAL:
            case OP_LOAD_GLOBAL:
            case OP_LOAD_LOCAL:
            case OP_LOAD_UPVALUE:
            case OP_STORE_UPVALUE:
            case OP_BINARY:
            case OP_COMPARE:
            case OP_UNARY:
            case OP_POP_N:
            case OP_CALL_FUNCTION:
            case OP_CALL_FUNCTION_KW:
            case OP_CALL_SPREAD:
            case OP_PUSH_FUNCTION:
            case OP_TENSOR_GET:
            case OP_TENSOR_SET:
            case OP_COMP_BEGIN:
            case OP_COMP_APPEND:
            case OP_MAP_FINALIZE:
                snprintf(line_buf, sizeof(line_buf),
                         "\033[38;2;107;107;107m%-4d\033[0m: "
                         "\033[38;2;139;0;0m%-15s\033[0m "
                         "\033[38;2;184;134;11m%-5d\033[0m",
                         line++, op_names[opcode], operands[0]);
                line++;
                pc++;
                break;

            case OP_JUMP_IF_FALSE:
            case OP_JUMP:
            case OP_LOOP:
            {
                int offset = (int16_t)((operands[0] << 8) | operands[1]);
                if (opcode == OP_LOOP)
                    offset &= OP_LOOP_OFFSET_MASK;
                int target = instr->offset + offset;

                snprintf(line_buf, sizeof(line_buf),
                         offset < 0
                             ? "\033[38;2;107;107;107m%-4d\033[0m: \033[38;2;139;0;0m%-14s\033[0m "
                               "\033[38;2;184;134;11m%-6d\033[0m \033[38;2;34;139;34m[<< %-3d]\033[0m\n"
                             : "\033[38;2;107;107;107m%-4d\033[0m: \033[38;2;139;0;0m%-14s\033[0m "
                               "\033[38;2;184;134;11m%-6d\033[0m \033[38;2;34;139;34m[>> %-3d]\033[0m\n",
                         line++, op_names[opcode], offset, target);
                line += 2;
                pc += 2;

                dis_emit(line_buf);
                continue;
            }

            case OP_LOAD_CONST:
            case OP_PUSH_LIST:
            case OP_PUSH_MAP:
            case OP_GET_MEMBER:
            case OP_SET_MEMBER:
                snprintf(line_buf, sizeof(line_buf),
                         "\033[38;2;107;107;107m%-4d\033[0m: "
                         "\033[38;2;139;0;0m%-15s\033[0m "
                         "\033[38;2;184;134;11m%-5d\033[0m",
                         line++, op_names[opcode], (int16_t)((operands[0] << 8) | operands[1]));
                line += 2;
                pc += 2;
                break;

            case OP_LIST_APPEND:
            case OP_LIST_EXTEND:
            case OP_LIST_FINALIZE:
            case OP_COMP_END:
            case OP_MAP_SET:
            case OP_MAP_EXTEND:
                snprintf(line_buf, sizeof(line_buf),
                         "\033[38;2;107;107;107m%-4d\033[0m: "
                         "\033[38;2;139;0;0m%-15s\033[0m",
                         line++, op_names[opcode]);
                break;

            case OP_PUSH_CLOSURE:
                snprintf(line_buf, sizeof(line_buf),
                         "\033[38;2;107;107;107m%-4d\033[0m: "
                         "\033[38;2;139;0;0m%-15s\033[0m "
                         "\033[38;2;184;134;11m%d %3d\033[0m",
                         line++, op_names[opcode], operands[0], operands[1]);
                line += 2;
                pc += 2;
                break;

            default:
                snprintf(line_buf, sizeof(line_buf),
                         "\033[38;2;107;107;107m%-4d\033[0m: "
                         "\033[38;2;139;0;0m%-15s\033[0m",
                         line++, op_names[opcode]);
                break;
            }
#endif

            if (descr && strcmp(descr, "") != 0)
            {
                if (strlen(descr) > 20)
                {
                    char short_descr[21];
                    strncpy(short_descr, descr, 20);
                    short_descr[20] = '\0';
#ifdef __EMSCRIPTEN__
                    strcat(line_buf, " [");
                    strcat(line_buf, short_descr);
                    strcat(line_buf, "...]\n");
#else
                    strcat(line_buf, " \033[38;2;34;139;34m[");
                    strcat(line_buf, short_descr);
                    strcat(line_buf, "...]\033[0m\n");
#endif
                }
                else
                {
#ifdef __EMSCRIPTEN__
                    strcat(line_buf, " [");
                    strcat(line_buf, descr);
                    strcat(line_buf, "]\n");
#else
                    strcat(line_buf, " \033[38;2;34;139;34m[");
                    strcat(line_buf, descr);
                    strcat(line_buf, "]\033[0m\n");
#endif
                }
            }
            else
                strcat(line_buf, "\n");

            dis_emit(line_buf);
        }
    }
}

void set_errorSource(const char *source_name)
{
    error_source = source_name;
}

void p_error(const char *message, int line, int column)
{
    if (global_errorHandler)
        global_errorHandler(message, line, column);
    else
    {

        if (error_source && error_source[0] != '\0')
            fprintf(stderr, "[Parsing Error] in %s at line %d, column %d: %s\n",
                    error_source, line, column, message);
        else
            fprintf(stderr, "[Parsing Error] at line %d, column %d: %s\n",
                    line, column, message);
        exit(EXIT_FAILURE);
    }
}

void p_errorf(int line, int column, const char *format, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    if (global_errorHandler)
    {
        global_errorHandler(buffer, line, column);
    }
    else
    {
        fflush(stdout);

        if (error_source && error_source[0] != '\0')
            fprintf(stderr, "\n\033[1;31m[PARSE ERROR] in %s at line %d, column %d:\033[0m %s",
                    error_source, line, column, buffer);
        else
            fprintf(stderr, "\n\033[1;31m[PARSE ERROR] at line %d, column %d:\033[0m %s",
                    line, column, buffer);
        fprintf(stderr, "\n");

        exit(EXIT_FAILURE);
    }
}

void free_compiler(compiler_t *comp)
{
    list_free(comp->code);

    list_free(comp->constants);

    list_free(comp->names);

    list_free(comp->builtin_names);
    if (comp->declared_globals)
        ht_free(comp->declared_globals);
    free(comp->source_name);

    while (!stack_isEmpty(comp->contexts))
    {
        context_t *context = pop_context(comp);
        free_context(context);
    }
    stack_free(comp->contexts);

    while (!stack_isEmpty(comp->loops))
    {
        loop_t *loop = pop_loopContext(comp);
        free_loop(loop);
    }
    stack_free(comp->loops);

    stack_free(comp->objects);

    ht_free(comp->instrs);

    free(comp);
}

void reset_compiler(compiler_t *comp)
{
    list_free(comp->code);
    list_free(comp->names);
    if (comp->declared_globals)
        ht_free(comp->declared_globals);

    while (!stack_isEmpty(comp->contexts))
    {
        context_t *context = pop_context(comp);
        free_context(context);
    }
    stack_free(comp->contexts);

    while (!stack_isEmpty(comp->loops))
    {
        loop_t *loop = pop_loopContext(comp);
        free_loop(loop);
    }
    stack_free(comp->loops);

    stack_free(comp->objects);
    free(comp->source_name);

    ht_free(comp->instrs);

    comp->code = list_create(sizeof(uint8_t));
    comp->names = list_create(sizeof(String));
    memset(&comp->global_cache, 0, sizeof(comp->global_cache));
    comp->declared_globals = ht_create(sizeof(bool));

    comp->locals = stack_create(sizeof(local_t));
    comp->contexts = stack_create(sizeof(context_t *));
    comp->loops = stack_create(sizeof(loop_t *));
    comp->objects = stack_create(sizeof(String));
    comp->name = "";

    comp->current = create_context(false, comp->code, NULL);

    comp->instrs = ht_create(sizeof(list_t));

    comp->is_lookUp = false;
    comp->is_upvalue = false;
    comp->is_repl = false;
    comp->source_name = NULL;

    stack_push(comp->contexts, &comp->current);
}
