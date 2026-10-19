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
static void _dis_emit(const char *text)
{
    EM_ASM({ console.log(UTF8ToString($0)); }, text);
}
#else
static void _dis_emitPlain(FILE *file, const char *text)
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

static void _dis_emit(const char *text)
{
    if (dis_output)
        _dis_emitPlain(dis_output, text);
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
    [0x3d] = "IMPORT_OP",
    [0x3e] = "GET_EXPORT",
    [0x3f] = "IMPORT_ALL",
    [0x40] = "IMPORT_DEFAULT",
    [0x41] = "LOAD_SUPER",
    [0x42] = "TENSOR_GET",
    [0x43] = "TENSOR_SET",
    [0x45] = "CALL_SPREAD",
    [0x47] = "MAP_EXTEND",
    [0x49] = "COMP_APPEND",
    [0x4a] = "PUSH_SET",
    [0x4b] = "PUSH_TUPLE",
    [0x4c] = "GET_MEMBER",
    [0x4d] = "SET_MEMBER",
    [0x4e] = "COMP_BEGIN",
    [0x4f] = "COMP_END",
    [0x50] = "PUSH_CLASS",
    [0x51] = "GET_SLOT",
    [0x52] = "SET_SLOT",
    [0x53] = "SWITCH_COMPARE",
};

static Context *_create_context(bool is_function, List *code, char *fun_name)
{
    static int f_count = 0;
    Context *context = malloc(sizeof(Context));

    context->upvalues = list_create(sizeof(Upvalue));
    context->locals = stack_create(sizeof(Local *));
    context->instrs = list_create(sizeof(Instruction));
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

static void _free_instr(Instruction *instr)
{
    if (!instr)
        return;

    free(instr->descr);
    free(instr->fun_name);
    free(instr->operands);
}

static void _free_context(Context *context)
{
    list_free(context->upvalues);

    // locals stack stores Local* pointer VALUES
    // stack_pop returns pointer to the slot holding the Local*
    // dereference to get the actual Local*, then free that
    while (!_stack_isEmpty(context->locals))
    {
        Local *local = *(Local **)_stack_pop(context->locals);
        free(local->name);
        free(local); // Local was heap-alloc'd in add_localConst — safe to free
    }
    stack_free(context->locals);

    if (context->instrs)
    {
        while (!list_isEmpty(context->instrs))
        {
            Instruction *instr = (Instruction *)list_pop(context->instrs);
            _free_instr(instr);
        }
        list_free(context->instrs);
    }

    if (context->param_names)
        list_free(context->param_names);

    free(context->fun_name);
    free(context);
}

static void _free_loop(Loop *loop)
{
    stack_free(loop->breaks);
    free(loop);
}

static void _free_loop(Loop *loop);

static Context *_current_context(Compiler *comp)
{
    Context **context = (Context **)_stack_peek(comp->contexts);
    return context ? *context : NULL;
}

static Context *_context_at(Compiler *comp, int depth)
{
    Context **context = (Context **)stack_getAt(comp->contexts, depth);
    return context ? *context : NULL;
}

static Context *_pop_context(Compiler *comp)
{
    Context **context = (Context **)_stack_pop(comp->contexts);
    return context ? *context : NULL;
}

static Loop *_current_loop(Compiler *comp)
{
    Loop **loop = (Loop **)_stack_peek(comp->loops);
    return loop ? *loop : NULL;
}

static Loop *_pop_loopContext(Compiler *comp)
{
    Loop **loop = (Loop **)_stack_pop(comp->loops);
    return loop ? *loop : NULL;
}

Compiler *init_compiler()
{

    Compiler *comp = (Compiler *)malloc(sizeof(Compiler));

    comp->code = list_create(sizeof(uint8_t));
    comp->constants = list_create(sizeof(Value));
    comp->string_constants = ht_create(sizeof(int));

    _list_add(comp->constants, &NEW_NUM(NAN));
    _list_add(comp->constants, &NEW_NUM(INFINITY));

    _list_add(comp->constants, &NEW_BOOL(TRUE));
    _list_add(comp->constants, &NEW_BOOL(FALSE));

    comp->names = list_create(sizeof(String));
    memset(&comp->global_cache, 0, sizeof(comp->global_cache));
    comp->builtin_names = list_create(sizeof(String));
    comp->declared_globals = ht_create(sizeof(bool));

    for (int i = 0; i < BUILTIN_CONST_COUNT; i++)
        _list_add(comp->builtin_names, new_string(builtin_constants[i].name));

    for (int i = 0; i < BUILTIN_FUNC_COUNT; i++)
        _list_add(comp->builtin_names, new_string(builtin_functions[i].name));

    comp->locals = stack_create(sizeof(Local));
    comp->contexts = stack_create(sizeof(Context *));
    comp->loops = stack_create(sizeof(Loop *));
    comp->objects = stack_create(sizeof(String));
    comp->name = "";

    comp->current = _create_context(FALSE, comp->code, NULL);

    comp->instrs = ht_create(sizeof(List));

    comp->is_lookUp = FALSE;
    comp->is_upvalue = FALSE;
    comp->is_repl = FALSE;
    comp->source_name = NULL;

    _stack_push(comp->contexts, &comp->current);

    return comp;
}

void add_code(Compiler *comp, Byte _byte)
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
        comp->code->data = realloc(comp->code->data, cap * sizeof(Byte));
        if (comp->code->data == NULL)
        {
            fprintf(stderr, "Memory allocation failed\n");
            exit(EXIT_FAILURE);
        }
        comp->code->capacity = cap;
    }

    ((Byte *)comp->code->data)[size++] = _byte;
    comp->code->size = size;
}

bool is_localScope(Compiler *comp)
{
    return comp->current->depth > 0 || comp->current->is_function;
}

void push_object(Compiler *comp)
{
    if (!comp->is_lookUp)
        _stack_push(comp->objects, new_string(comp->name));
}

void pop_object(Compiler *comp)
{
    if (!comp->is_lookUp)
        _stack_pop(comp->objects);
}

bool is_object(Compiler *comp)
{
    return !_stack_isEmpty(comp->objects);
}

bool is_lookUp(Compiler *comp)
{
    return comp->is_lookUp;
}

bool look_up(Compiler *comp, bool value)
{
    bool look_up = comp->is_lookUp;

    comp->is_lookUp = value;

    return look_up;
}

void print_locals(Compiler *comp)
{
    printf("Locals stack (top to bottom):\n");

    Local *local = *(Local **)_stack_peek(comp->current->locals);
    printf("Local: name = %s, depth = %d, is_captured = %s\n",
           local->name,
           local->depth,
           local->is_captured ? "true" : "false");

    printf("\n");
}

void add_localConst(Compiler *comp, char *name, bool is_const)
{
    PiStack *locals = comp->current->locals;

    // Check for name conflict ONLY in current block
    for (int i = _stack_size(locals) - 1; i >= 0; i--)
    {
        Local *local = *(Local **)stack_getAt(locals, i);

        if (local->depth < comp->current->depth)
            break;

        if (strcmp(local->name, name) == 0)
        {
            p_errorf(comp->current_line, comp->current_col,
                     "Name already declared in this scope: [%s]", name);
        }
    }

    Local *local = malloc(sizeof(Local));
    local->name = strdup(name);
    local->depth = comp->current->depth;
    local->is_captured = FALSE;
    local->is_const = is_const;

    _stack_push(locals, &local);
}

void add_local(Compiler *comp, char *name)
{
    add_localConst(comp, name, FALSE);
}

int get_local(Compiler *comp, char *name)
{
    int index = -1;
    int depth = _stack_size(comp->contexts) - 1;
    comp->is_upvalue = FALSE;

    index = resolve_local(comp, depth, name);
    if (index != -1)
        return index;
    else
    {
        index = resolve_upvalue(comp, depth, name);
        comp->is_upvalue = TRUE;
    }
    return index;
}

int get_localSize(Compiler *comp, int depth)
{
    int size = 0;

    PiStack *locals = comp->current->locals;
    int l_size = _stack_size(locals);

    for (int i = l_size - 1; i >= 0; i--)
    {
        Local *local = *(Local **)stack_getAt(locals, i);
        if (local->depth >= depth)
            size++;
        else
            break;
    }
    return size;
}

int resolve_local(Compiler *comp, int depth, char *name)
{
    int index = -1;
    Local *local;
    Context *context = _context_at(comp, depth);
    for (int i = _stack_size(context->locals) - 1; i >= 0; i--)
    {
        local = *(Local **)stack_getAt(context->locals, i);
        if (strcmp(local->name, name) == 0)
        {
            index = i;
            break;
        }
    }
    return index;
}

int resolve_upvalue(Compiler *comp, int depth, char *name)
{
    if (depth == 0)
        return -1;

    int index = resolve_local(comp, depth - 1, name);
    if (index != -1)
        return add_upvalue(comp, depth, index, TRUE);

    int upvalue = resolve_upvalue(comp, depth - 1, name);
    if (upvalue != -1)
        return add_upvalue(comp, depth, upvalue, FALSE);

    return -1;
}

int add_upvalue(Compiler *comp, int depth, int index, bool is_local)
{
    Context *current = _context_at(comp, depth);

    int size = LIST_SIZE(current->upvalues);
    for (int i = 0; i < size; i++)
    {
        Upvalue *_upvalue = (Upvalue *)_list_getAt(current->upvalues, i);

        if (_upvalue->index == index && _upvalue->is_local == is_local)
            return i;
    }

    Upvalue *upvalue = malloc(sizeof(Upvalue));
    upvalue->index = index;
    upvalue->is_local = is_local;
    _list_add(current->upvalues, upvalue);

    return size;
}

bool is_builtin(Compiler *comp, const char *name)
{
    for (int i = 0; i < comp->builtin_names->size; i++)
    {
        char *existing = string_get(comp->builtin_names, i);
        if (strcmp(existing, name) == 0)
            return TRUE;
    }
    return FALSE;
}

static bool _global_isConst(Compiler *comp, const char *name)
{
    bool *is_const = comp->declared_globals ? (bool *)_ht_get(comp->declared_globals, name) : NULL;
    return is_const != NULL && *is_const;
}

static bool _local_isConstAt(Compiler *comp, int depth, int index)
{
    Context *context = _context_at(comp, depth);
    if (!context || index < 0 || index >= _stack_size(context->locals))
        return FALSE;

    Local *local = *(Local **)stack_getAt(context->locals, index);
    return local && local->is_const;
}

static bool _resolved_upvalueIsConst(Compiler *comp, const char *name)
{
    int depth = _stack_size(comp->contexts) - 2;
    for (int d = depth; d >= 0; d--)
    {
        Context *context = _context_at(comp, d);
        for (int i = _stack_size(context->locals) - 1; i >= 0; i--)
        {
            Local *local = *(Local **)stack_getAt(context->locals, i);
            if (strcmp(local->name, name) == 0)
                return local->is_const;
        }
    }

    return FALSE;
}

static void _check_constAssignment(Compiler *comp, const char *name, int local_index, bool is_upvalue)
{
    if (local_index != -1)
    {
        bool is_const = is_upvalue
                            ? _resolved_upvalueIsConst(comp, name)
                            : _local_isConstAt(comp, _stack_size(comp->contexts) - 1, local_index);
        if (is_const)
            p_errorf(comp->current_line, comp->current_col,
                     "Cannot reassign const binding [%s]", name);
        return;
    }

    if (_global_isConst(comp, name))
        p_errorf(comp->current_line, comp->current_col,
                 "Cannot reassign const binding [%s]", name);
}

void add_variableConst(Compiler *comp, char *name, bool is_const)
{
    int g_index = -1;
    if (is_localScope(comp))
    {
        add_localConst(comp, name, is_const);
    }
    else
    {
        if (_ht_get(comp->declared_globals, name) != NULL)
            p_errorf(comp->current_line, comp->current_col, "Name already exists [%s]", name);
        if (is_builtin(comp, name))
            p_errorf(comp->current_line, comp->current_col,
                     "Name already exists [%s]; this name is reserved by a builtin.", name);

        ht_put(comp->declared_globals, name, &is_const);

        g_index = store_name(comp, name);
        emit_8u(comp, OP_STORE_GLOBAL, name, g_index);
    }
}

void add_variable(Compiler *comp, char *name)
{
    add_variableConst(comp, name, FALSE);
}

static void _store_variableWithMode(Compiler *comp, char *name, bool allow_const_init)
{
    if (is_localScope(comp))
    {
        int index = get_local(comp, name);
        bool is_upvalue = comp->is_upvalue;
        if (index != -1)
        {
            if (!allow_const_init)
                _check_constAssignment(comp, name, index, is_upvalue);
            emit_8u(comp, is_upvalue ? OP_STORE_UPVALUE : OP_STORE_LOCAL, name, index);
        }
        else
        {
            if (!allow_const_init)
                _check_constAssignment(comp, name, -1, FALSE);
            int g_index = store_name(comp, name);
            emit_8u(comp, OP_STORE_GLOBAL, name, g_index);
        }
    }
    else
    {
        if (!allow_const_init)
            _check_constAssignment(comp, name, -1, FALSE);
        int g_index = store_name(comp, name);
        emit_8u(comp, OP_STORE_GLOBAL, name, g_index);
    }
}

void store_variable(Compiler *comp, char *name)
{
    _store_variableWithMode(comp, name, FALSE);
}

void store_variableInit(Compiler *comp, char *name)
{
    _store_variableWithMode(comp, name, TRUE);
}

void load_variable(Compiler *comp, char *name)
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

int name_index(Compiler *comp, char *name)
{
    for (int i = 0; i < comp->names->size; i++)
    {
        char *_name = string_get(comp->names, i);
        if (strcmp(_name, name) == 0)
            return i;
    }
    return -1;
}

int store_name(Compiler *comp, char *name)
{
    int index = name_index(comp, name);
    if (index != -1)
        return index;

    _list_add(comp->names, new_string(name));

    // Return the new index
    return comp->names->size - 1;
}

void remove_locals(Compiler *comp, int size)
{
    while (size-- > 0)
        _stack_pop(comp->current->locals);
}

void push_scope(Compiler *comp)
{
    comp->current->depth++;
}

void pop_scope(Compiler *comp)
{
    int size = emit_pop(comp, comp->current->depth);

    remove_locals(comp, size);

    comp->current->depth--;
}

void push_loop(Compiler *comp, int address, bool is_for)
{
    Loop *loop = (Loop *)malloc(sizeof(Loop));

    loop->_continue = address;
    loop->depth = comp->current->depth;
    loop->breaks = stack_create(sizeof(int));
    loop->is_for = is_for;

    _stack_push(comp->loops, &loop);
}

void pop_loop(Compiler *comp, int address)
{
    Loop *loop = _pop_loopContext(comp);
    PiStack *breaks = loop->breaks;

    int16_t offset = address - comp->code->size;

    emit_16u(comp, OP_JUMP, "", offset);

    while (_stack_isEmpty(breaks) == FALSE)
        patch_jump(comp, POP_INT(breaks));

    _free_loop(loop);
}

void push_break(Compiler *comp, int address)
{
    PUSH_INT(_current_loop(comp)->breaks, address);
}

int get_continue(Compiler *comp)
{
    return _current_loop(comp)->_continue;
}
bool is_forLoop(Compiler *comp)
{
    return _current_loop(comp)->is_for;
}

bool in_loop(Compiler *comp)
{
    return !_stack_isEmpty(comp->loops);
}

int loop_depth(Compiler *comp)
{
    return _current_loop(comp)->depth;
}

static bool _code_usesLocalSlot(List *code, uint8_t slot)
{
    uint8_t *bytecode = (uint8_t *)code->data;
    int length = code->size;

    for (int i = 0; i < length;)
    {
        uint8_t op = bytecode[i++];

        if ((op == OP_LOAD_LOCAL || op == OP_STORE_LOCAL) &&
            i < length && bytecode[i] == slot)
            return TRUE;

        i += _operand_count(op);
    }

    return FALSE;
}

void push_function(Compiler *comp, char *name)
{
    if (!comp->is_lookUp)
    {
        _current_context(comp)->depth = comp->current->depth;
        Context *context = _create_context(TRUE, list_create(sizeof(uint8_t)), name);
        _stack_push(comp->contexts, &context);

        comp->current = _current_context(comp);
        comp->code = comp->current->code;
        comp->locals = comp->current->locals;
    }
}

void pop_function(Compiler *comp, int params)
{
    if (!comp->is_lookUp)
    {
        char *name = comp->current->fun_name;

        ht_put(comp->instrs, name, comp->current->instrs);

        int uv_size = LIST_SIZE(comp->current->upvalues);
        List *upvalues = comp->current->upvalues;

        ObjCode *code = (ObjCode *)new_code(comp->code);

        code->need_args = _code_usesLocalSlot(comp->code, (uint8_t)params);
        code->need_kwargs = _code_usesLocalSlot(comp->code, (uint8_t)(params + 1));

        code->method_need_args = _code_usesLocalSlot(comp->code, (uint8_t)(params + 1));
        code->method_need_kwargs = _code_usesLocalSlot(comp->code, (uint8_t)(params + 2));

        int c_index = store_const(comp, NEW_OBJ(code));

        Context *context = _pop_context(comp);

        code->param_names = context->param_names;
        context->param_names = NULL;

        comp->current = _current_context(comp);
        comp->code = comp->current->code;
        comp->locals = comp->current->locals;

        free(context);

        int n_index = store_stringConst(comp, name);

        emit_16u(comp, OP_LOAD_CONST, name, n_index);

        char code_descr[32];
        snprintf(code_descr, sizeof(code_descr), "<code: 0x%04X>", code->hash);

        emit_16u(comp, OP_LOAD_CONST, code_descr, c_index);

        for (int i = 0; i < uv_size; i++)
        {
            Upvalue *upvalue = (Upvalue *)_list_getAt(upvalues, i);
            int index = store_const(comp, NEW_NUM(upvalue->index));
            emit_16u(comp, OP_LOAD_CONST, itos(upvalue->index), index);
            index = store_const(comp, NEW_BOOL(upvalue->is_local));
            emit_16u(comp, OP_LOAD_CONST, upvalue->is_local ? "true" : "false", index);
        }

        if (uv_size > 0)
            emit_16u(comp, OP_PUSH_CLOSURE, name, (params << 8) | uv_size);
        else
            emit_8u(comp, OP_PUSH_FUNCTION, name, (Byte)params);
    }
}
int store_const(Compiler *comp, Value value)
{
    if (IS_STR(value))
    {
        const char *text = AS_CSTRING(value);
        int *existing = _ht_get(comp->string_constants, text);
        if (existing)
            return *existing;
        int index = comp->constants->size;
        _list_add(comp->constants, &value);
        ht_put(comp->string_constants, text, &index);
        return index;
    }

    Value _value;
    for (int i = 0; i < comp->constants->size; i++)
    {
        _value = *(Value *)_list_getAt(comp->constants, i);
        if (equals(_value, value))
            return i;
    }
    _list_add(comp->constants, &value);
    return comp->constants->size - 1;
}

int store_stringConst(Compiler *comp, const char *text)
{
    int *existing = _ht_get(comp->string_constants, text);
    if (existing)
        return *existing;
    return store_const(comp, NEW_OBJ(new_pistring(strdup(text))));
}

int store_tokenConst(Compiler *comp, Token token)
{
    if (token.type != TK_STR && token.type != TK_ID)
        return store_const(comp, new_value(token));

    char *text = string_fromToken(token);
    int index = store_stringConst(comp, text);
    free(text);
    return index;
}

static int _emit(Compiler *comp, OpCode opcode, char *descr, int num_operands, int line, int column, ...)
{
    if (comp->code == NULL || comp->instrs == NULL || comp->is_lookUp)
        return -1;

    /* Fold the bytecode shape produced for literal arithmetic, e.g.
     * LOAD_CONST, LOAD_CONST, BINARY_OP into one LOAD_CONST.  Restrict this
     * to immediate numeric/boolean values so dynamic object behavior and
     * side effects are never evaluated during compilation. */
    if (opcode == OP_BINARY && num_operands == 1 && comp->code->size >= 6)
    {
        uint8_t *code = (uint8_t *)comp->code->data;
        int offset = comp->code->size - 6;
        if (code[offset] == OP_LOAD_CONST &&
            code[offset + 3] == OP_LOAD_CONST)
        {
            int left_index = ((int)code[offset + 1] << 8) | code[offset + 2];
            int right_index = ((int)code[offset + 4] << 8) | code[offset + 5];
            int op = (uint8_t)0;

            va_list read;
            va_start(read, column);
            op = va_arg(read, int);
            va_end(read);

            if (left_index >= 0 && right_index >= 0 &&
                left_index < comp->constants->size &&
                right_index < comp->constants->size && op <= 7)
            {
                Value left = *(Value *)_list_getAt(comp->constants, left_index);
                Value right = *(Value *)_list_getAt(comp->constants, right_index);
                bool literals = (IS_NUM(left) || IS_BOOL(left)) &&
                                (IS_NUM(right) || IS_BOOL(right));

                if (literals)
                {
                    double l = IS_NUM(left) ? AS_NUM(left) : (AS_BOOL(left) ? 1.0 : 0.0);
                    double r = IS_NUM(right) ? AS_NUM(right) : (AS_BOOL(right) ? 1.0 : 0.0);
                    Value folded;

                    switch (op)
                    {
                    case 0: folded = NEW_NUM(l + r); break;
                    case 1: folded = NEW_NUM(l - r); break;
                    case 2: folded = NEW_NUM(l * r); break;
                    case 3: folded = NEW_NUM(r == 0.0 ? INFINITY : l / r); break;
                    case 4: folded = ((int)r == 0) ? NEW_NAN() : NEW_NUM((int)l % (int)r); break;
                    case 5: folded = NEW_BOOL(l && r); break;
                    case 6: folded = NEW_BOOL(l || r); break;
                    case 7: folded = NEW_NUM(pow(l, r)); break;
                    default: folded = NEW_NIL(); break;
                    }

                    while (comp->current->instrs->size > 0 &&
                           ((Instruction *)_list_getAt(comp->current->instrs,
                                                 comp->current->instrs->size - 1))->offset >= offset)
                    {
                        Instruction *last = (Instruction *)list_pop(comp->current->instrs);
                        _free_instr(last);
                    }
                    comp->code->size = offset;

                    int folded_index = store_const(comp, folded);
                    char folded_descr[64];
                    if (IS_BOOL(folded))
                        snprintf(folded_descr, sizeof(folded_descr), "%s",
                                 AS_BOOL(folded) ? "true" : "false");
                    else
                        snprintf(folded_descr, sizeof(folded_descr), "%g",
                                 AS_NUM(folded));

                    return _emit(comp, OP_LOAD_CONST, folded_descr, 2, line, column,
                                 (folded_index >> 8) & 0xff, folded_index & 0xff);
                }
            }
        }
    }

    int size = LIST_SIZE(comp->code);

    uint8_t _opcode = (uint8_t)opcode;
    _list_add(comp->code, &_opcode);

    uint8_t *operands = malloc(sizeof(uint8_t) * num_operands);
    if (!operands)
        return -1;

    va_list args;
    va_start(args, column);
    for (int i = 0; i < num_operands; i++)
    {
        uint8_t operand = (uint8_t)va_arg(args, int);
        _list_add(comp->code, &operand);
        operands[i] = operand;
    }
    va_end(args);

    Instruction instr = {0};
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

    _list_add(comp->current->instrs, &instr);

    return comp->code->size - 1;
}

int emit(Compiler *comp, OpCode opcode)
{
    return _emit(comp, opcode, "", 0, comp->current_line, comp->current_col);
}

int emit_8u(Compiler *comp, OpCode opcode, char *descr, int operand)
{
    return _emit(comp, opcode, descr, 1, comp->current_line, comp->current_col, operand);
}

int emit_16u(Compiler *comp, OpCode opcode, char *descr, int operand)
{
    Byte op1 = (Byte)((operand >> 8) & 0xff);
    Byte op2 = (Byte)(operand & 0xff);
    return _emit(comp, opcode, descr, 2, comp->current_line, comp->current_col, op1, op2);
}

int emit_16uX2(Compiler *comp, OpCode opcode, char *descr, int first, int second)
{
    Byte op1 = (Byte)((first >> 8) & 0xff);
    Byte op2 = (Byte)(first & 0xff);
    Byte op3 = (Byte)((second >> 8) & 0xff);
    Byte op4 = (Byte)(second & 0xff);
    return _emit(comp, opcode, descr, 4, comp->current_line, comp->current_col,
                 op1, op2, op3, op4);
}

bool last_constantCondition(Compiler *comp, bool *value)
{
    if (!comp || !comp->code || comp->code->size < 3 || !value)
        return FALSE;

    uint8_t *code = (uint8_t *)comp->code->data;
    int offset = comp->code->size - 3;
    if (code[offset] != OP_LOAD_CONST)
        return FALSE;

    int index = ((int)code[offset + 1] << 8) | code[offset + 2];
    if (index < 0 || index >= comp->constants->size)
        return FALSE;

    Value constant = *(Value *)_list_getAt(comp->constants, index);
    if (!IS_BOOL(constant) && !IS_NIL(constant) && !IS_NUM(constant))
        return FALSE;

    *value = as_bool(constant);
    return TRUE;
}

void discard_lastConstant(Compiler *comp)
{
    if (!comp || !comp->code || !comp->current || !comp->current->instrs ||
        comp->code->size < 3 || comp->current->instrs->size == 0)
        return;

    int offset = comp->code->size - 3;
    uint8_t *code = (uint8_t *)comp->code->data;
    Instruction *last = (Instruction *)_list_getAt(comp->current->instrs,
                                           comp->current->instrs->size - 1);
    if (code[offset] != OP_LOAD_CONST || last->offset != offset)
        return;

    last = (Instruction *)list_pop(comp->current->instrs);
    _free_instr(last);
    comp->code->size = offset;
}

int emit_pop(Compiler *comp, int depth)
{
    int size = get_localSize(comp, depth);
    if (size > 1)
        emit_8u(comp, OP_POP_N, "", (uint8_t)size);
    else if (size == 1)
        emit(comp, OP_POP);
    return size;
}

int emit_jump(Compiler *comp, int address)
{
    emit_16u(comp, OP_JUMP, "", address);
    return comp->code->size - 1;
}

void patch_jumpWithFlags(Compiler *comp, int address, uint16_t flags)
{
    if (!comp->is_lookUp)
    {

        uint8_t *code = (uint8_t *)comp->code->data;
        int offset = comp->code->size - (address - 2);
        uint16_t encoded = ((uint16_t)offset & OP_LOOP_OFFSET_MASK) | flags;

        code[address - 1] = (encoded >> 8) & 0xff;
        code[address] = encoded & 0xff;

        for (int i = LIST_SIZE(comp->current->instrs) - 1; i >= 0; i--)
        {
            Instruction *instr = _list_getAt(comp->current->instrs, i);
            if (instr->offset == address - 2)
            {
                instr->operands[0] = (encoded >> 8) & 0xff;
                instr->operands[1] = encoded & 0xff;
                break;
            }
        }
    }
}

void patch_jump(Compiler *comp, int address)
{
    patch_jumpWithFlags(comp, address, 0);
}

void patch_switchCompare(Compiler *comp, int address)
{
    if (!comp || comp->is_lookUp)
        return;

    uint8_t *code = (uint8_t *)comp->code->data;
    int operand_start = address - 3;
    int offset = comp->code->size - operand_start;
    uint16_t encoded = (uint16_t)offset;
    code[address - 1] = (encoded >> 8) & 0xff;
    code[address] = encoded & 0xff;

    for (int i = LIST_SIZE(comp->current->instrs) - 1; i >= 0; i--)
    {
        Instruction *instr = _list_getAt(comp->current->instrs, i);
        if (instr->offset == address - 4)
        {
            instr->operands[2] = (encoded >> 8) & 0xff;
            instr->operands[3] = encoded & 0xff;
            break;
        }
    }
}

int code_size(Compiler *comp)
{
    return comp->code->size;
}

void dis(Compiler *comp)
{
    _dis_emit("disassembling...\n");

    if (_stack_size(comp->contexts) > 0)
    {
        Context *global_ctx = _context_at(comp, 0);
        ht_put(comp->instrs, "<global>", global_ctx->instrs);
    }

    // Use iterator over the instruction table
    HT_Iterator it = _ht_iterator(comp->instrs);
    while (_ht_next(&it))
    {
        const char *scope_name = it.key;
        List *instrs = (List *)it.value; // stored as void*, cast back

        char header_buf[256];
#ifdef __EMSCRIPTEN__
        snprintf(header_buf, sizeof(header_buf), "\n== Disassembly of %s ==\n\n",
                 strcmp(scope_name, "<global>") == 0 ? "global scope" : scope_name);
#else
        snprintf(header_buf, sizeof(header_buf), "\n\033[1;36m== Disassembly of %s ==\033[0m\n\n",
                 strcmp(scope_name, "<global>") == 0 ? "global scope" : scope_name);
#endif
        _dis_emit(header_buf);

        if (!instrs)
            continue;

        int line = 0, pc = 0;
        for (int j = 0; j < instrs->size; j++)
        {
            Instruction *instr = (Instruction *)_list_getAt(instrs, j);
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
            case OP_MAP_EXTEND:
            case OP_LIST_EXTEND:
                snprintf(line_buf, sizeof(line_buf), "%-4d: %-15s %-5d      ",
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
                         offset < 0 ? "%-4d: %-14s %-6d       [<< %-3d]\n"
                                    : "%-4d: %-14s %-6d       [>> %-3d]\n",
                         line++, op_names[opcode], offset, target);
                line += 2;
                pc += 2;

                _dis_emit(line_buf);
                continue;
            }

            case OP_LOAD_CONST:
            case OP_PUSH_LIST:
            case OP_PUSH_MAP:
            case OP_PUSH_SET:
            case OP_PUSH_TUPLE:
            case OP_GET_MEMBER:
            case OP_SET_MEMBER:
            case OP_GET_SLOT:
            case OP_SET_SLOT:
                snprintf(line_buf, sizeof(line_buf), "%-4d: %-15s %-5d      ",
                         line++, op_names[opcode], (int16_t)((operands[0] << 8) | operands[1]));
                line += 2;
                pc += 2;
                break;

            case OP_PUSH_CLASS:
                snprintf(line_buf, sizeof(line_buf), "%-4d: %-15s members=%d fields=%d",
                         line++, op_names[opcode],
                         (operands[0] << 8) | operands[1],
                         (operands[2] << 8) | operands[3]);
                line += 4;
                pc += 4;
                break;

            case OP_SWITCH_COMPARE:
            {
                int constant = (operands[0] << 8) | operands[1];
                int offset = (int16_t)((operands[2] << 8) | operands[3]);
                int target = instr->offset + 1 + offset;
                snprintf(line_buf, sizeof(line_buf), "%-4d: %-15s %-5d %-5d [>> %d]",
                         line++, op_names[opcode], constant, offset, target);
                line += 4;
                pc += 4;
                break;
            }

            case OP_COMP_END:
                snprintf(line_buf, sizeof(line_buf), "%-4d: %-15s",
                         line++, op_names[opcode]);
                break;

            case OP_PUSH_CLOSURE:
                snprintf(line_buf, sizeof(line_buf), "%-4d: %-15s %-5d %-5d",
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
            case OP_MAP_EXTEND:
            case OP_LIST_EXTEND:
                snprintf(line_buf, sizeof(line_buf),
                         "\033[38;2;107;107;107m%-4d\033[0m: "
                         "\033[38;2;139;0;0m%-15s\033[0m "
                         "\033[38;2;184;134;11m%-5d      \033[0m",
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
                               "\033[38;2;184;134;11m%-6d       \033[0m\033[38;2;34;139;34m[<< %-3d]\033[0m\n"
                             : "\033[38;2;107;107;107m%-4d\033[0m: \033[38;2;139;0;0m%-14s\033[0m "
                               "\033[38;2;184;134;11m%-6d       \033[0m\033[38;2;34;139;34m[>> %-3d]\033[0m\n",
                         line++, op_names[opcode], offset, target);
                line += 2;
                pc += 2;

                _dis_emit(line_buf);
                continue;
            }

            case OP_LOAD_CONST:
            case OP_PUSH_LIST:
            case OP_PUSH_MAP:
            case OP_PUSH_SET:
            case OP_PUSH_TUPLE:
            case OP_GET_MEMBER:
            case OP_SET_MEMBER:
            case OP_GET_SLOT:
            case OP_SET_SLOT:
                snprintf(line_buf, sizeof(line_buf),
                         "\033[38;2;107;107;107m%-4d\033[0m: "
                         "\033[38;2;139;0;0m%-15s\033[0m "
                         "\033[38;2;184;134;11m%-5d      \033[0m",
                         line++, op_names[opcode], (int16_t)((operands[0] << 8) | operands[1]));
                line += 2;
                pc += 2;
                break;

            case OP_PUSH_CLASS:
                snprintf(line_buf, sizeof(line_buf),
                         "\033[38;2;107;107;107m%-4d\033[0m: "
                         "\033[38;2;139;0;0m%-15s\033[0m "
                         "\033[38;2;184;134;11mmembers=%d fields=%d\033[0m",
                         line++, op_names[opcode],
                         (operands[0] << 8) | operands[1],
                         (operands[2] << 8) | operands[3]);
                line += 4;
                pc += 4;
                break;

            case OP_SWITCH_COMPARE:
            {
                int constant = (operands[0] << 8) | operands[1];
                int offset = (int16_t)((operands[2] << 8) | operands[3]);
                int target = instr->offset + 1 + offset;
                snprintf(line_buf, sizeof(line_buf),
                         "\033[38;2;107;107;107m%-4d\033[0m: "
                         "\033[38;2;139;0;0m%-15s\033[0m "
                         "\033[38;2;184;134;11m%-5d %-5d [>> %d]\033[0m",
                         line++, op_names[opcode], constant, offset, target);
                line += 4;
                pc += 4;
                break;
            }

            case OP_COMP_END:
                snprintf(line_buf, sizeof(line_buf),
                         "\033[38;2;107;107;107m%-4d\033[0m: "
                         "\033[38;2;139;0;0m%-15s\033[0m",
                         line++, op_names[opcode]);
                break;

            case OP_PUSH_CLOSURE:
                snprintf(line_buf, sizeof(line_buf),
                         "\033[38;2;107;107;107m%-4d\033[0m: "
                         "\033[38;2;139;0;0m%-15s\033[0m "
                         "\033[38;2;184;134;11m%-5d %-5d\033[0m",
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

            _dis_emit(line_buf);
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

void free_compiler(Compiler *comp)
{
    list_free(comp->code);

    list_free(comp->constants);
    ht_free(comp->string_constants);

    list_free(comp->names);

    list_free(comp->builtin_names);
    if (comp->declared_globals)
        ht_free(comp->declared_globals);
    free(comp->source_name);

    while (!_stack_isEmpty(comp->contexts))
    {
        Context *context = _pop_context(comp);
        _free_context(context);
    }
    stack_free(comp->contexts);

    while (!_stack_isEmpty(comp->loops))
    {
        Loop *loop = _pop_loopContext(comp);
        _free_loop(loop);
    }
    stack_free(comp->loops);

    stack_free(comp->objects);

    ht_free(comp->instrs);

    free(comp);
}

void reset_compiler(Compiler *comp)
{
    // Keep string_constants alongside the retained constant pool for REPL reuse.
    list_free(comp->code);
    list_free(comp->names);
    if (comp->declared_globals)
        ht_free(comp->declared_globals);

    while (!_stack_isEmpty(comp->contexts))
    {
        Context *context = _pop_context(comp);
        _free_context(context);
    }
    stack_free(comp->contexts);

    while (!_stack_isEmpty(comp->loops))
    {
        Loop *loop = _pop_loopContext(comp);
        _free_loop(loop);
    }
    stack_free(comp->loops);

    stack_free(comp->objects);
    free(comp->source_name);

    ht_free(comp->instrs);

    comp->code = list_create(sizeof(uint8_t));
    comp->names = list_create(sizeof(String));
    memset(&comp->global_cache, 0, sizeof(comp->global_cache));
    comp->declared_globals = ht_create(sizeof(bool));

    comp->locals = stack_create(sizeof(Local));
    comp->contexts = stack_create(sizeof(Context *));
    comp->loops = stack_create(sizeof(Loop *));
    comp->objects = stack_create(sizeof(String));
    comp->name = "";

    comp->current = _create_context(FALSE, comp->code, NULL);

    comp->instrs = ht_create(sizeof(List));

    comp->is_lookUp = FALSE;
    comp->is_upvalue = FALSE;
    comp->is_repl = FALSE;
    comp->source_name = NULL;

    _stack_push(comp->contexts, &comp->current);
}
