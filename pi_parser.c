#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include "pi_parser.h"
#include "pi_compiler.h"
#include "pi_opcode.h"
#include "pi_object.h"
#include "pi_string.h"

char *comp_ops[] = {"==", "!=", ">", "<", ">=", "<=", "in"};
char *bin_ops[] = {"+", "-", "*", "/", "%", "&&", "||", "**", "&", "|", "^", "<<", ">>", ">>>", ".", "is"};
char *unary_ops[] = {"+", "-", "!", "~", "#", "++", "--", "typeof"};

static void program(parser_t *parser);
static void declaration(parser_t *parser);
static void var_decl(parser_t *parser, bool is_const);
static void func_decl(parser_t *parser);
static void class_decl(parser_t *parser);
static void statement(parser_t *parser);
static void expr_state(parser_t *parser);
static void destructure_assignStatement(parser_t *parser);
static void block(parser_t *parser);
static void if_stmt(parser_t *parser);
static void switch_stmt(parser_t *parser);
static void while_stmt(parser_t *parser);
static void for_stmt(parser_t *parser);
static void break_stmt(parser_t *parser);
static void continue_stmt(parser_t *parser);
static void return_stmt(parser_t *parser);
static void print(parser_t *parser);
static void variable(parser_t *parser, bool is_const);
static void expr(parser_t *parser);
static void assignment(parser_t *parser, bool emit_load);
static void pipeline_expr(parser_t *parser);
static void cond_expr(parser_t *parser);
static void or_expr(parser_t *parser);
static void and_expr(parser_t *parser);
static void in_expr(parser_t *parser);
static void range_expr(parser_t *parser);
static void bitOr_expr(parser_t *parser);
static void xor_expr(parser_t *parser);
static void bitAnd_expr(parser_t *parser);
static void shift_expr(parser_t *parser);
static void equality_expr(parser_t *parser);
static void compare_expr(parser_t *parser);
static void add_expr(parser_t *parser);
static void dot_expr(parser_t *parser);
static void mult_expr(parser_t *parser);
static void exp_expr(parser_t *parser);
static void member_expr(parser_t *parser);
static void unary_expr(parser_t *parser);
static void primary(parser_t *parser);

static void emit_spreadListLiteral(parser_t *parser);
static void emit_spreadMapLiteral(parser_t *parser);
static void emit_classMap(parser_t *parser, const char *class_name);
static void emit_boundMethodCall(parser_t *parser, const char *receiver, const char *method, int argc);
static void emit_listComprehension(parser_t *parser);
static int emit_literalFill(parser_t *parser, double previous, double endpoint, token_t token);

static bool call_hasSpreadArgs(parser_t *parser);

static bool list_hasSpreadItems(parser_t *parser);
static bool list_isComprehension(parser_t *parser);

static bool map_hasSpreadItems(parser_t *parser);
static bool is_mapEntry(parser_t *parser);
static void emit_setLiteral(parser_t *parser);
static bool has_accessContinuation(parser_t *parser, token_t token);

static token_t peek(parser_t *parser);
static token_t peek_next(parser_t *parser);
static token_t previous(parser_t *parser);
static bool check(parser_t *parser, tk_type type);
static bool match(parser_t *parser, tk_type type);
static token_t consume(parser_t *parser, tk_type type, const char *message);
static void advance(parser_t *parser);
void set_pos(parser_t *parser, token_t token);
static bool is_functionLiteral(parser_t *parser, int index);
static bool is_objectLiteral(parser_t *parser, int index);
static char *get_pendingFunctionName(parser_t *parser);
static void emit_mapFinalize(parser_t *parser);
static bool numeric_literalSegment(parser_t *parser, int start, int end, double *value);
static double consume_fillEndpoint(parser_t *parser);

typedef struct
{
    int start;
    int end;
} segment_t;

#define MAX_PIPELINE_STAGES 64
#define MAX_PIPELINE_ARGS 64

typedef struct
{
    token_t op;
    segment_t callee;
    segment_t args[MAX_PIPELINE_ARGS];
    int arg_count;
} pipeline_stage_t;

typedef struct
{
    segment_t input;
    pipeline_stage_t stages[MAX_PIPELINE_STAGES];
    int stage_count;
} pipeline_t;

typedef struct
{
    token_t name;
    segment_t iterable;
} comp_iter_t;

typedef struct
{
    segment_t result;
    segment_t iterators;
    segment_t conditions;
    int end_index;
    bool has_conditions;
} list_comp_t;

static bool is_functionLiteral(parser_t *parser, int index)
{
    tk_type type = parser->tokens[index].type;

    if (type == TK_FUN)
        return true;

    if (type == TK_ID && parser->tokens[index + 1].type == TK_RARROW)
        return true;

    if (type != TK_LPAREN)
        return false;

    int depth = 1;

    for (int i = index + 1; parser->tokens[i].type != TK_EOF; i++)
    {
        if (parser->tokens[i].type == TK_LPAREN)
            depth++;
        else if (parser->tokens[i].type == TK_RPAREN)
            depth--;

        if (depth == 0)
            return parser->tokens[i + 1].type == TK_RARROW;
    }

    return false;
}

static bool is_objectLiteral(parser_t *parser, int index)
{
    return parser->tokens[index].type == TK_LBRACE;
}

static char *get_pendingFunctionName(parser_t *parser)
{
    char *name = parser->fun_name;

    parser->fun_name = NULL;

    return name ? strdup(name) : NULL;
}

static void emit_mapFinalize(parser_t *parser)
{
    int name_index = 0xFF;
    char *descr = "";

    if (parser->object_name != NULL)
    {
        name_index = store_name(parser->comp, parser->object_name);
        descr = parser->object_name;
    }

    emit_8u(parser->comp, OP_MAP_FINALIZE, descr, name_index);
}

static void emit_boundMethodCall(parser_t *parser, const char *receiver, const char *method, int argc)
{
    load_variable(parser->comp, (char *)receiver);

    int method_index = store_const(parser->comp, NEW_OBJ(new_pistring((char *)method)));
    emit_16u(parser->comp, OP_LOAD_CONST, (char *)method, method_index);
    emit(parser->comp, OP_GET_MEMBER);
}

static bool is_integerLiteralValue(double value)
{
    return isfinite(value) && floor(value) == value;
}

static void emit_numberLiteralConst(parser_t *parser, double value)
{
    char descr[64];
    snprintf(descr, sizeof(descr), "%.15g", value);

    int index = store_const(parser->comp, NEW_NUM(value));
    emit_16u(parser->comp, OP_LOAD_CONST, descr, index);
}

static int emit_literalFill(parser_t *parser, double previous, double endpoint, token_t token)
{
    if (!is_integerLiteralValue(previous) || !is_integerLiteralValue(endpoint))
        p_error("Fill expansion requires integer numeric literal endpoints.",
                token.line, token.column);

    int step = previous <= endpoint ? 1 : -1;
    int count = 0;

    for (double value = previous + step;
         step > 0 ? value <= endpoint : value >= endpoint;
         value += step)
    {
        if (count >= UINT16_MAX)
            p_error("Fill expansion is too large for one literal.",
                    token.line, token.column);

        emit_numberLiteralConst(parser, value);
        count++;
    }

    return count;
}

static bool numeric_literalSegment(parser_t *parser, int start, int end, double *value)
{
    if (start + 1 == end && parser->tokens[start].type == TK_NUM)
    {
        *value = tk_double(parser->tokens[start]);
        return true;
    }

    if (start + 2 == end &&
        parser->tokens[start].type == TK_MINUS &&
        parser->tokens[start + 1].type == TK_NUM)
    {
        double number = tk_double(parser->tokens[start + 1]);
        *value = parser->tokens[start + 1].is_negative ? number : -number;
        return true;
    }

    return false;
}

static double consume_fillEndpoint(parser_t *parser)
{
    bool negative = match(parser, TK_MINUS);
    token_t token = consume(parser, TK_NUM, "Expect integer numeric literal after ', ...,'.");
    double value = tk_double(token);
    return negative ? -value : value;
}

static void emit_spreadListLiteral(parser_t *parser)
{
    emit_16u(parser->comp, OP_PUSH_LIST, "", 0);

    if (match(parser, TK_RBRACKET))
    {
        emit(parser->comp, OP_LIST_FINALIZE);
        return;
    }

    do
    {
        if (check(parser, TK_RBRACKET))
            break;

        bool is_spread = match(parser, TK_ELLIPSIS);

        cond_expr(parser);

        emit(parser->comp, is_spread ? OP_LIST_EXTEND : OP_LIST_APPEND);
    } while (match(parser, TK_COMMA));

    consume(parser, TK_RBRACKET, "Expect ']' at the end of list literal.");
    emit(parser->comp, OP_LIST_FINALIZE);
}

static bool call_hasSpreadArgs(parser_t *parser)
{
    int index = parser->current;
    int paren_depth = 1;   // The parentheses of the function call
    int bracket_depth = 0; // The brackets of a list literal
    int brace_depth = 0;   // The braces of a dictionary literal

    while (parser->tokens[index].type != TK_EOF)
    {
        token_t token = parser->tokens[index++];

        switch (token.type)
        {
        case TK_LPAREN:
            paren_depth++;
            break;
        case TK_RPAREN:
            paren_depth--;
            if (paren_depth == 0)
                return false;
            break;
        case TK_LBRACKET:
            bracket_depth++;
            break;
        case TK_RBRACKET:
            if (bracket_depth > 0)
                bracket_depth--;
            break;
        case TK_LBRACE:
            brace_depth++;
            break;
        case TK_RBRACE:
            if (brace_depth > 0)
                brace_depth--;
            break;
        case TK_ELLIPSIS:
            if (paren_depth == 1 && bracket_depth == 0 && brace_depth == 0)
                return true;
            break;
        default:
            break;
        }
    }

    return false;
}

static bool list_hasSpreadItems(parser_t *parser)
{
    int index = parser->current;
    int paren_depth = 0;
    int bracket_depth = 1;
    int brace_depth = 0;

    while (parser->tokens[index].type != TK_EOF)
    {
        token_t token = parser->tokens[index++];

        switch (token.type)
        {
        case TK_LPAREN:
            paren_depth++;
            break;
        case TK_RPAREN:
            if (paren_depth > 0)
                paren_depth--;
            break;
        case TK_LBRACKET:
            bracket_depth++;
            break;
        case TK_RBRACKET:
            bracket_depth--;
            if (bracket_depth == 0)
                return false;
            break;
        case TK_LBRACE:
            brace_depth++;
            break;
        case TK_RBRACE:
            if (brace_depth > 0)
                brace_depth--;
            break;
        case TK_ELLIPSIS:
            if (paren_depth == 0 && bracket_depth == 1 && brace_depth == 0)
            {
                if (parser->tokens[index].type == TK_COMMA)
                    break;
                return true;
            }
            break;
        default:
            break;
        }
    }

    return false;
}

static bool map_hasSpreadItems(parser_t *parser)
{
    int index = parser->current;
    int paren_depth = 0;
    int bracket_depth = 0;
    int brace_depth = 1;

    while (parser->tokens[index].type != TK_EOF)
    {
        token_t token = parser->tokens[index++];

        switch (token.type)
        {
        case TK_LPAREN:
            paren_depth++;
            break;
        case TK_RPAREN:
            if (paren_depth > 0)
                paren_depth--;
            break;
        case TK_LBRACKET:
            bracket_depth++;
            break;
        case TK_RBRACKET:
            if (bracket_depth > 0)
                bracket_depth--;
            break;
        case TK_LBRACE:
            brace_depth++;
            break;
        case TK_RBRACE:
            brace_depth--;
            if (brace_depth == 0)
                return false;
            break;
        case TK_ELLIPSIS:
            if (paren_depth == 0 && bracket_depth == 0 && brace_depth == 1)
            {
                if (parser->tokens[index].type == TK_COMMA)
                    break;
                return true;
            }
            break;
        default:
            break;
        }
    }

    return false;
}

static bool is_mapEntry(parser_t *parser)
{
    if (check(parser, TK_ELLIPSIS))
        return true;

    token_t token = peek(parser);
    switch (token.type)
    {
    case TK_STR:
    case TK_ID:
    case TK_NUM:
    case TK_FALSE:
    case TK_TRUE:
    {
        token_t next_token = parser->tokens[parser->current + 1];
        return next_token.type == TK_COLON || next_token.type == TK_LPAREN;
    }
    default:
        return false;
    }
}

static void emit_setLiteral(parser_t *parser)
{
    int size = 0;
    if (check(parser, TK_RBRACE))
    {
        consume(parser, TK_RBRACE, "Expect '}' at the end of set literal.");
        emit_16u(parser->comp, OP_PUSH_SET, "", 0);
        return;
    }

    do
    {
        if (check(parser, TK_RBRACE))
            break;

        int item_start = parser->current;
        cond_expr(parser);
        size++;

        double previous_number = 0;
        bool previous_is_number = numeric_literalSegment(parser, item_start, parser->current,
                                                         &previous_number);

        while (check(parser, TK_COMMA) && peek_next(parser).type == TK_ELLIPSIS)
        {
            consume(parser, TK_COMMA, "Expect ',' before '...' in set fill expansion.");
            consume(parser, TK_ELLIPSIS, "Expect '...' in set fill expansion.");
            token_t ellipsis = previous(parser);
            if (!previous_is_number)
                p_error("Fill expansion requires an integer numeric literal before ', ...,'.",
                        ellipsis.line, ellipsis.column);

            consume(parser, TK_COMMA, "Expect ',' after '...' in set fill expansion.");
            double endpoint = consume_fillEndpoint(parser);
            size += emit_literalFill(parser, previous_number, endpoint, ellipsis);
            previous_number = endpoint;
            previous_is_number = true;
        }
    } while (match(parser, TK_COMMA));

    consume(parser, TK_RBRACE, "Expect '}' at the end of set literal.");
    emit_16u(parser->comp, OP_PUSH_SET, "", size);
}

static bool scan_listComprehension(parser_t *parser, list_comp_t *comp)
{
    int index = parser->current;
    int paren_depth = 0;
    int bracket_depth = 1;
    int brace_depth = 0;
    int ternary_depth = 0;
    int first_colon = -1;
    int second_colon = -1;

    while (parser->tokens[index].type != TK_EOF)
    {
        token_t token = parser->tokens[index];

        switch (token.type)
        {
        case TK_LPAREN:
            paren_depth++;
            break;
        case TK_RPAREN:
            if (paren_depth > 0)
                paren_depth--;
            break;
        case TK_LBRACKET:
            bracket_depth++;
            break;
        case TK_RBRACKET:
            bracket_depth--;
            if (bracket_depth == 0)
            {
                if (first_colon == -1)
                    return false;

                comp->result.start = parser->current;
                comp->result.end = first_colon;
                comp->iterators.start = first_colon + 1;
                comp->iterators.end = (second_colon == -1) ? index : second_colon;
                comp->conditions.start = (second_colon == -1) ? index : second_colon + 1;
                comp->conditions.end = index;
                comp->has_conditions = second_colon != -1;
                comp->end_index = index;
                return true;
            }
            break;
        case TK_LBRACE:
            brace_depth++;
            break;
        case TK_RBRACE:
            if (brace_depth > 0)
                brace_depth--;
            break;
        case TK_QUESTION:
            if (paren_depth == 0 && bracket_depth == 1 && brace_depth == 0)
                ternary_depth++;
            break;
        case TK_COLON:
            if (paren_depth == 0 && bracket_depth == 1 && brace_depth == 0)
            {
                if (ternary_depth > 0)
                    ternary_depth--;
                else if (first_colon == -1)
                    first_colon = index;
                else if (second_colon == -1)
                    second_colon = index;
                else
                    p_errorf(token.line, token.column,
                             "List comprehensions support at most one iterator clause and one condition clause separator.");
            }
            break;
        default:
            break;
        }

        index++;
    }

    return false;
}

static bool list_isComprehension(parser_t *parser)
{
    list_comp_t comp;

    return scan_listComprehension(parser, &comp);
}

static void compile_segmentExpr(parser_t *parser, segment_t segment, const char *message)
{
    if (segment.start >= segment.end)
    {
        token_t token = parser->tokens[segment.start];
        p_error(message, token.line, token.column);
    }

    int saved = parser->current;

    token_t saved_end = parser->tokens[segment.end];

    parser->current = segment.start;

    parser->tokens[segment.end].type = TK_EOF;

    pipeline_expr(parser);

    parser->tokens[segment.end] = saved_end;

    if (parser->current != segment.end)
    {
        token_t token = parser->tokens[parser->current];
        p_error(message, token.line, token.column);
    }

    parser->current = saved;
}

static int parse_compIterators(parser_t *parser, segment_t segment, comp_iter_t *iters, int max_iters)
{
    int index = segment.start;
    int count = 0;

    while (index < segment.end)
    {
        if (count >= max_iters)
        {
            token_t token = parser->tokens[index];
            p_errorf(token.line, token.column, "Too many iterators in list comprehension.");
        }

        token_t name = parser->tokens[index++];
        if (name.type != TK_ID)
            p_errorf(name.line, name.column, "Expect iterator variable name in list comprehension.");

        if (index >= segment.end || parser->tokens[index].type != TK_IN)
        {
            token_t token = parser->tokens[index < segment.end ? index : segment.end - 1];
            p_errorf(token.line, token.column, "Expect 'in' after iterator variable in list comprehension.");
        }
        index++;

        int expr_start = index;
        int paren_depth = 0;
        int bracket_depth = 0;
        int brace_depth = 0;

        while (index < segment.end)
        {
            token_t token = parser->tokens[index];
            if (token.type == TK_COMMA && paren_depth == 0 && bracket_depth == 0 && brace_depth == 0)
                break;

            switch (token.type)
            {
            case TK_LPAREN:
                paren_depth++;
                break;
            case TK_RPAREN:
                if (paren_depth > 0)
                    paren_depth--;
                break;
            case TK_LBRACKET:
                bracket_depth++;
                break;
            case TK_RBRACKET:
                if (bracket_depth > 0)
                    bracket_depth--;
                break;
            case TK_LBRACE:
                brace_depth++;
                break;
            case TK_RBRACE:
                if (brace_depth > 0)
                    brace_depth--;
                break;
            default:
                break;
            }

            index++;
        }

        if (expr_start == index)
            p_errorf(name.line, name.column, "Expect iterable expression after 'in' in list comprehension.");

        iters[count].name = name;
        iters[count].iterable.start = expr_start;
        iters[count].iterable.end = index;
        count++;

        if (index < segment.end && parser->tokens[index].type == TK_COMMA)
            index++;
    }

    return count;
}

static int parse_compConditions(parser_t *parser, segment_t segment, segment_t *conds, int max_conds)
{
    if (segment.start >= segment.end)
        return 0;

    int index = segment.start;
    int count = 0;

    while (index < segment.end)
    {
        if (count >= max_conds)
        {
            token_t token = parser->tokens[index];
            p_errorf(token.line, token.column, "Too many conditions in list comprehension.");
        }

        int expr_start = index;
        int paren_depth = 0;
        int bracket_depth = 0;
        int brace_depth = 0;

        while (index < segment.end)
        {
            token_t token = parser->tokens[index];
            if (token.type == TK_COMMA && paren_depth == 0 && bracket_depth == 0 && brace_depth == 0)
                break;

            switch (token.type)
            {
            case TK_LPAREN:
                paren_depth++;
                break;
            case TK_RPAREN:
                if (paren_depth > 0)
                    paren_depth--;
                break;
            case TK_LBRACKET:
                bracket_depth++;
                break;
            case TK_RBRACKET:
                if (bracket_depth > 0)
                    bracket_depth--;
                break;
            case TK_LBRACE:
                brace_depth++;
                break;
            case TK_RBRACE:
                if (brace_depth > 0)
                    brace_depth--;
                break;
            default:
                break;
            }

            index++;
        }

        conds[count].start = expr_start;
        conds[count].end = index;
        count++;

        if (index < segment.end && parser->tokens[index].type == TK_COMMA)
            index++;
    }

    return count;
}

static void emit_listCompLoops(parser_t *parser, list_comp_t *comp,
                               comp_iter_t *iters, int iter_count,
                               segment_t *conds, int cond_count,
                               int iter_index, int acc_slot)
{
    if (iter_index == iter_count)
    {
        // expressions and list comprehension expression, and append the resulting value to the list.
        int jumps[32];
        int jump_count = 0;

        for (int i = 0; i < cond_count; i++)
        {
            compile_segmentExpr(parser, conds[i], "Invalid list comprehension condition.");
            // Emit a jump if false instruction to jump over the list comprehension expression if the condition is false
            jumps[jump_count++] = emit_16u(parser->comp, OP_JUMP_IF_FALSE, "", 0);
        }

        compile_segmentExpr(parser, comp->result, "Invalid list comprehension expression.");
        emit_8u(parser->comp, OP_COMP_APPEND, "<comp>", acc_slot);

        for (int i = 0; i < jump_count; i++)
            patch_jump(parser->comp, jumps[i]);
        return;
    }

    token_t iter_token = iters[iter_index].name;

    set_pos(parser, parser->tokens[iters[iter_index].iterable.start]);

    compile_segmentExpr(parser, iters[iter_index].iterable, "Invalid list comprehension iterator expression.");
    emit(parser->comp, OP_PUSH_ITER);

    set_pos(parser, iter_token);

    int address = emit_16u(parser->comp, OP_LOOP, "", 0);

    push_scope(parser->comp);
    add_variable(parser->comp, token_value(iter_token));
    push_loop(parser->comp, address - 2, true);

    emit_listCompLoops(parser, comp, iters, iter_count, conds, cond_count, iter_index + 1, acc_slot);

    pop_scope(parser->comp);
    pop_loop(parser->comp, address - 2);

    patch_jump(parser->comp, address);
}

static void emit_listComprehension(parser_t *parser)
{
    list_comp_t comp;
    if (!scan_listComprehension(parser, &comp))
        return;

    comp_iter_t iters[16];
    segment_t conds[32];
    int iter_count = parse_compIterators(parser, comp.iterators, iters, 16);
    int cond_count = comp.has_conditions ? parse_compConditions(parser, comp.conditions, conds, 32) : 0;

    if (iter_count == 0)
    {
        token_t token = parser->tokens[comp.iterators.start];
        p_errorf(token.line, token.column, "List comprehension requires at least one iterator.");
    }

    // Create local metadata for the result list so iterator locals keep their
    // normal relative slots inside the comprehension.
    char hidden_name[32];
    snprintf(hidden_name, sizeof(hidden_name), "<comp_%d>", comp.result.start);
    add_local(parser->comp, hidden_name);
    int acc_slot = get_local(parser->comp, hidden_name);
    emit_8u(parser->comp, OP_COMP_BEGIN, "<comp>", acc_slot);

    emit_listCompLoops(parser, &comp, iters, iter_count, conds, cond_count, 0, acc_slot);

    emit(parser->comp, OP_COMP_END);
    remove_locals(parser->comp, 1);
    parser->current = comp.end_index;
    consume(parser, TK_RBRACKET, "Expect ']' after list comprehension.");
}

static token_t peek(parser_t *parser)
{
    return parser->tokens[parser->current];
}

static token_t peek_next(parser_t *parser)
{
    return parser->tokens[parser->current + 1];
}

static bool is_atEnd(parser_t *parser)
{
    return peek(parser).type == TK_EOF;
}

static token_t previous(parser_t *parser)
{
    return parser->tokens[parser->current - 1];
}

static bool is_delimiter(parser_t *parser, token_t token)
{
    return token.type == TK_SEMICOLON;
}

static token_t next(parser_t *parser)
{
    if (!is_atEnd(parser))
    {
        parser->current++;

        token_t tok = peek(parser);
        if (!is_delimiter(parser, tok))
            parser->last = tok;
    }
    return previous(parser);
}

static bool check(parser_t *parser, tk_type type)
{
    return !is_atEnd(parser) && peek(parser).type == type;
}

static bool match_n(parser_t *parser, int t_count, ...)
{
    va_list args;
    va_start(args, t_count);

    for (int i = 0; i < t_count; i++)
    {
        tk_type type = va_arg(args, tk_type);
        if (check(parser, type))
        {
            next(parser);
            va_end(args);
            return true;
        }
    }
    va_end(args);
    return false;
}

static bool match(parser_t *parser, tk_type type)
{
    if (check(parser, type))
    {
        next(parser);
        return true;
    }
    return false;
}

static bool is_destructure_assign(parser_t *parser)
{
    if (!check(parser, TK_LBRACKET))
        return false;

    int current = parser->current;
    next(parser);

    if (check(parser, TK_RBRACKET))
    {
        parser->current = current;
        return false;
    }

    while (true)
    {
        if (!check(parser, TK_ID))
        {
            parser->current = current;
            return false;
        }
        next(parser);

        if (!match(parser, TK_COMMA))
            break;
    }

    bool is_assign = match(parser, TK_RBRACKET) && check(parser, TK_ASSIGN);
    parser->current = current;
    return is_assign;
}

static bool check_n(parser_t *parser, int t_count, ...)
{
    if (is_atEnd(parser))
        return false;
    va_list args;
    va_start(args, t_count);
    for (int i = 0; i < t_count; i++)
    {
        tk_type type = va_arg(args, tk_type);
        if (peek(parser).type == type)
        {
            va_end(args);
            return true;
        }
    }
    va_end(args);
    return false;
}

static token_t consume(parser_t *parser, tk_type type, const char *message)
{
    if (check(parser, type))
    {
        token_t token = next(parser);
        return token;
    }
    else if (message != NULL)
        p_error(message, peek(parser).line, peek(parser).column);
    else
        p_error("Unexpected token", peek(parser).line, peek(parser).column);

    if (global_errorHandler)
    {
        token_t token = peek(parser);

        while (!is_atEnd(parser))
            advance(parser);

        return token;
    }

    exit(EXIT_FAILURE);
}

static void advance(parser_t *parser)
{
    if (!is_atEnd(parser))
        parser->current++;
}

static void skip(parser_t *parser, int steps)
{
    parser->current += steps;
}

static bool consume_ifExist(parser_t *parser, int t_count, ...)
{
    bool consumed = false;
    va_list args;
    va_start(args, t_count);

    while (true)
    {
        bool matched = false;

        for (int i = 0; i < t_count; ++i)
        {
            tk_type type = va_arg(args, tk_type);
            if (check(parser, type))
            {
                advance(parser);
                consumed = true;
                matched = true;
                break;
            }
        }

        if (!matched)
            break;

        va_end(args);
        va_start(args, t_count);
    }

    va_end(args);
    return consumed;
}

void set_pos(parser_t *parser, token_t token)
{
    parser->comp->current_line = token.line;

    parser->comp->current_col = token.column;
}

static bool is_lineBreak(parser_t *parser)
{
    return previous(parser).line < peek(parser).line || peek(parser).type == TK_EOF;
}

bool need_delimiter(parser_t *parser)
{
    if (!consume_ifExist(parser, 1, TK_SEMICOLON))
    {
        if (!is_lineBreak(parser))
        {
            if (!check(parser, TK_RBRACE))
                return true;
        }
    }

    return false;
}
static bool is_assign(parser_t *parser)
{
    if (parser->is_store &&
        (parser->force_store ||
         check_n(parser, 11, TK_ASSIGN, TK_PLUS_ASSIGN, TK_MINUS_ASSIGN, TK_DIV_ASSIGN, TK_MULT_ASSIGN,
                 TK_MOD_ASSIGN, TK_BITOR_ASSIGN, TK_XOR_ASSIGN, TK_BITAND_ASSIGN, TK_INCR, TK_DECR)))
    {
        parser->is_store = false;
        parser->force_store = false;
        return true;
    }

    return false;
}

static bool has_accessContinuation(parser_t *parser, token_t token)
{
    return peek(parser).line == token.line &&
           check_n(parser, 3, TK_DOT, TK_LBRACKET, TK_LPAREN);
}

void mark_tokens(parser_t *parser, int start, int end)
{
    // Iterate over the range of tokens and mark them as skipped
    for (int i = start; i < end; i++)
        parser->tokens[i].skip = true;
}

static void skip_letDecl(parser_t *parser)
{
    int depth = 0;

    while (!is_atEnd(parser))
    {
        token_t tok = peek(parser);

        switch (tok.type)
        {
        case TK_LPAREN:
        case TK_LBRACKET:
        case TK_LBRACE:
            depth++;
            break;
        case TK_RPAREN:
        case TK_RBRACKET:
        case TK_RBRACE:
            if (depth > 0)
                depth--;
            break;
        default:
            break;
        }

        if (depth == 0 && (tok.type == TK_SEMICOLON || is_lineBreak(parser)))
        {
            if (tok.type == TK_SEMICOLON)
                next(parser);
            break;
        }

        next(parser);
    }
}

parser_t *init_parser(compiler_t *comp, token_t *tokens, ParserMode mode)
{
    parser_t *parser = (parser_t *)malloc(sizeof(parser_t));

    parser->tokens = tokens;

    parser->access = false;
    parser->current = 0;
    parser->is_store = false;
    parser->force_store = false;
    parser->is_return = false;
    parser->has_walrus = false;
    parser->object_member = false;
    parser->fun_name = NULL;
    parser->object_name = NULL;

    parser->comp = comp;
    set_errorSource(comp ? comp->source_name : NULL);

    parser->mode = mode;
    if (mode == MODE_REPL)
        parser->comp->is_repl = true;

    parser->had_error = false;

    return parser;
}

void parse(parser_t *parser)
{
    if (parser->mode == MODE_REPL)
    {
        // In REPL mode, parse only a single expression statement.
        // if (!is_atEnd(parser))
        //     expr_state(parser);

        // In REPL mode, parse a full declaration (fun, class, let, or expression)
        if (!is_atEnd(parser))
            declaration(parser);
    }
    else
    {
        // In file mode, parse the entire program.
        program(parser);
    }

    emit(parser->comp, OP_HALT);

    // Runtime diagnostics need the global instruction metadata even when
    // debug disassembly is disabled.
    ht_put(parser->comp->instrs, "<global>", parser->comp->current->instrs);
}

static void declarations(parser_t *parser)
{
    int depth = 0;

    // First pass: Hoist functions and collect globals
    while (!is_atEnd(parser))
    {
        // Track block depth to ignore inner declarations
        if (check(parser, TK_LBRACE))
            depth++;
        else if (check(parser, TK_RBRACE))
            depth--;

        // Skip inner block declarations
        if (depth > 0)
        {
            next(parser);
            continue;
        }

        if (match(parser, TK_FUN) && !match(parser, TK_LPAREN))
        {
            int start = parser->current - 1;
            func_decl(parser);
            int end = parser->current;
            mark_tokens(parser, start, end);
        }
        else if (match(parser, TK_LET) || match(parser, TK_CONST))
            skip_letDecl(parser);
        else
            next(parser);
    }

    parser->current = 0;

    // Second pass: Parse remaining code (skipping processed tokens)
    while (!is_atEnd(parser))
    {
        if (parser->tokens[parser->current].skip)
            next(parser);
        else
            // statement(parser); // Parse remaining statements
            declaration(parser);
    }
}

static void program(parser_t *parser)
{
    declarations(parser);
}

static void declaration(parser_t *parser)
{
    if (match(parser, TK_LET))
        var_decl(parser, false);
    else if (match(parser, TK_CONST))
        var_decl(parser, true);
    else if (match(parser, TK_FUN))
        func_decl(parser);
    else if (match(parser, TK_CLASS))
        class_decl(parser);
    else
    {
        statement(parser); // Parse as a statement
        parser->is_return = false;
    }
}

static void var_decl(parser_t *parser, bool is_const)
{
    do
    {
        variable(parser, is_const);
    } while (match(parser, TK_COMMA));
    consume_ifExist(parser, 1, TK_SEMICOLON);
}

static void variable(parser_t *parser, bool is_const)
{
    int index = -1;

    token_t token = consume(parser, TK_ID, "Expect variable name");
    char *name = token_value(token);
    bool reserved_local_function = false;

    // parser->comp->name = strdup(name);

    if (match(parser, TK_ASSIGN))
    {
        char *prev_fun = parser->fun_name;
        char *prev_obj = parser->object_name;
        bool function_literal = is_functionLiteral(parser, parser->current);

        if (function_literal && is_localScope(parser->comp))
        {
            emit(parser->comp, OP_PUSH_NIL);
            add_localConst(parser->comp, name, is_const);
            reserved_local_function = true;
        }

        if (function_literal)
            parser->fun_name = name;
        if (is_objectLiteral(parser, parser->current))
            parser->object_name = name;

        if (reserved_local_function)
        {
            cond_expr(parser);
            store_variableInit(parser->comp, name);
        }
        else
        {
            assignment(parser, true);
        }

        parser->fun_name = prev_fun;
        parser->object_name = prev_obj;
    }

    else
        emit(parser->comp, OP_PUSH_NIL);

    if (!reserved_local_function)
        add_variableConst(parser->comp, name, is_const);
}

static list_t *param_list(parser_t *parser)
{
    int size = 0;
    token_t name;
    list_t *params = list_create(sizeof(String));

    set_pos(parser, previous(parser));

    if (!check(parser, TK_RPAREN))
    {
        do
        {
            if (size >= 32)
                p_error("Can't have more than 32 parameters.", peek(parser).line, peek(parser).column);

            name = consume(parser, TK_ID, "Expect parameter name.");
            list_add(params, new_string(token_value(name)));

            if (match(parser, TK_ASSIGN))
                expr(parser);
            else
                emit(parser->comp, OP_PUSH_NIL);

            size++;

        } while (match(parser, TK_COMMA));
    }

    return params;
}

static void emit_spreadMapLiteral(parser_t *parser)
{
    emit_16u(parser->comp, OP_PUSH_MAP, "", 0);

    if (match(parser, TK_RBRACE))
    {
        emit_mapFinalize(parser);
        return;
    }

    do
    {
        if (check(parser, TK_RBRACE))
            break;

        if (match(parser, TK_ELLIPSIS))
        {
            cond_expr(parser);
            emit(parser->comp, OP_MAP_EXTEND);
            continue;
        }

        char *key;
        int index = 0;

        if (match_n(parser, 5, TK_STR, TK_ID, TK_NUM, TK_FALSE, TK_TRUE))
        {
            key = tk_string(previous(parser));
            index = store_const(parser->comp, NEW_OBJ(new_pistring(key)));
        }
        else
            p_error("Unexpected key expression.", peek(parser).line, peek(parser).column);

        if (match(parser, TK_LPAREN))
        {
            list_t *params = param_list(parser);
            int size = list_size(params);
            consume(parser, TK_RPAREN, "Expect ')' before function body.");
            consume(parser, TK_LBRACE, "Expect '{' before function body.");

            push_function(parser->comp, key);
            parser->comp->current->param_names = params;

            if (is_object(parser->comp))
                add_local(parser->comp, "this");

            for (int i = 0; i < size; i++)
                add_local(parser->comp, string_get(params, i));
            add_local(parser->comp, "args");
            add_local(parser->comp, "kw_args");

            if (match(parser, TK_RBRACE))
            {
                if (is_constructor(parser->comp))
                    emit_8u(parser->comp, OP_LOAD_LOCAL, "this", 0);
                else
                    emit(parser->comp, OP_PUSH_NIL);
                emit(parser->comp, OP_RETURN);
            }
            else
            {
                while (!check(parser, TK_RBRACE) && !is_atEnd(parser))
                    declaration(parser);

                if (!parser->is_return)
                {
                    if (is_constructor(parser->comp))
                        emit_8u(parser->comp, OP_LOAD_LOCAL, "this", 0);
                    else
                        emit(parser->comp, OP_PUSH_NIL);
                    emit(parser->comp, OP_RETURN);

                    parser->is_return = false;
                }
            }

            parser->is_return = false;
            pop_function(parser->comp, size);
            consume(parser, TK_RBRACE, "Expect '}' after function body.");
        }
        else
        {
            if (strcmp(key, "constructor") == 0)
                p_error("Constructor is a reserved keyword.", peek(parser).line, peek(parser).column);
            consume(parser, TK_COLON, "Expect ':' after object key expression.");
            char *prev_obj = parser->object_name;
            parser->object_name = NULL;
            cond_expr(parser);
            parser->object_name = prev_obj;
        }

        emit_16u(parser->comp, OP_LOAD_CONST, key, index);
        emit(parser->comp, OP_MAP_SET);
    } while (match(parser, TK_COMMA) && !check(parser, TK_RBRACE));

    consume(parser, TK_RBRACE, "Expect '}' at the end of map literal.");
    emit_mapFinalize(parser);
}

static void emit_classMap(parser_t *parser, const char *class_name)
{
    char *prev_obj = parser->object_name;
    parser->object_name = (char *)class_name;

    push_object(parser->comp);

    int size = 0;
    while (!check(parser, TK_RBRACE) && !is_atEnd(parser))
    {
        token_t key_tok = consume(parser, TK_ID, "Expect class member name.");
        char *key = token_value(key_tok);
        int index = store_const(parser->comp, NEW_OBJ(new_pistring(key)));

        if (match(parser, TK_LPAREN))
        {
            list_t *params = param_list(parser);
            int param_count = list_size(params);
            consume(parser, TK_RPAREN, "Expect ')' before method body.");
            consume(parser, TK_LBRACE, "Expect '{' before method body.");

            push_function(parser->comp, key);
            parser->comp->current->param_names = params;

            add_local(parser->comp, "this");

            for (int i = 0; i < param_count; i++)
                add_local(parser->comp, string_get(params, i));
            add_local(parser->comp, "args");
            add_local(parser->comp, "kw_args");

            if (match(parser, TK_RBRACE))
            {
                if (is_constructor(parser->comp))
                    emit_8u(parser->comp, OP_LOAD_LOCAL, "this", 0);
                else
                    emit(parser->comp, OP_PUSH_NIL);
                emit(parser->comp, OP_RETURN);
            }
            else
            {
                while (!check(parser, TK_RBRACE) && !is_atEnd(parser))
                    declaration(parser);

                if (!parser->is_return)
                {
                    if (is_constructor(parser->comp))
                        emit_8u(parser->comp, OP_LOAD_LOCAL, "this", 0);
                    else
                        emit(parser->comp, OP_PUSH_NIL);
                    emit(parser->comp, OP_RETURN);
                }
            }

            parser->is_return = false;
            pop_function(parser->comp, param_count);
            consume(parser, TK_RBRACE, "Expect '}' after method body.");
        }
        else
        {
            if (strcmp(key, "constructor") == 0)
                p_error("Constructor is a reserved keyword.", peek(parser).line, peek(parser).column);

            consume(parser, TK_ASSIGN, "Expect '=' after static class member name.");

            bool prev_object_member = parser->object_member;
            char *prev_fun = parser->fun_name;
            char *member_prev_obj = parser->object_name;
            parser->object_member = true;
            parser->object_name = NULL;

            if (is_functionLiteral(parser, parser->current))
                parser->fun_name = key;

            cond_expr(parser);
            parser->object_member = prev_object_member;
            parser->fun_name = prev_fun;
            parser->object_name = member_prev_obj;
        }

        emit_16u(parser->comp, OP_LOAD_CONST, key, index);
        size++;

        bool had_comma = consume_ifExist(parser, 1, TK_COMMA);
        if (!had_comma && need_delimiter(parser))
            p_error("Expected delimiter between class members.", peek(parser).line, peek(parser).column);
    }

    consume(parser, TK_RBRACE, "Expect '}' after class body.");
    pop_object(parser->comp);
    emit_16u(parser->comp, OP_PUSH_MAP, "", size);
    emit_mapFinalize(parser);

    parser->object_name = prev_obj;
}

static void class_decl(parser_t *parser)
{
    token_t name_tok = consume(parser, TK_ID, "Expect class name.");
    char *class_name = token_value(name_tok);
    char *parent_name = "Object";

    if (match(parser, TK_COLON))
    {
        token_t parent_tok = consume(parser, TK_ID, "Expect parent class name after ':'.");
        parent_name = token_value(parent_tok);
    }

    consume(parser, TK_LBRACE, "Expect '{' before class body.");

    emit_classMap(parser, class_name);
    add_variable(parser->comp, class_name);

    emit_boundMethodCall(parser, "Object", "extends", 2);
    load_variable(parser->comp, parent_name);
    load_variable(parser->comp, class_name);
    emit_8u(parser->comp, OP_CALL_FUNCTION, "extends", 2);
    emit(parser->comp, OP_POP);

    emit_boundMethodCall(parser, class_name, "setName", 1);
    int name_index = store_const(parser->comp, NEW_OBJ(new_pistring(class_name)));
    emit_16u(parser->comp, OP_LOAD_CONST, class_name, name_index);
    emit_8u(parser->comp, OP_CALL_FUNCTION, "setName", 1);
    emit(parser->comp, OP_POP);

    emit_boundMethodCall(parser, "Object", "lock", 2);
    load_variable(parser->comp, class_name);
    int lock_index = store_const(parser->comp, NEW_BOOL(true));
    emit_16u(parser->comp, OP_LOAD_CONST, "true", lock_index);
    emit_8u(parser->comp, OP_CALL_FUNCTION, "lock", 2);
    emit(parser->comp, OP_POP);

    emit_boundMethodCall(parser, "Object", "bracketAccess", 2);
    load_variable(parser->comp, class_name);
    int bracket_index = store_const(parser->comp, NEW_BOOL(false));
    emit_16u(parser->comp, OP_LOAD_CONST, "false", bracket_index);
    emit_8u(parser->comp, OP_CALL_FUNCTION, "bracketAccess", 2);
    emit(parser->comp, OP_POP);
}
static void func_decl(parser_t *parser)
{
    token_t token = previous(parser);

    if (match(parser, TK_ID))
    {
        token_t id_token = previous(parser);
        char *name = token_value(id_token);

        if (is_localScope(parser->comp))
            add_local(parser->comp, name);

        consume(parser, TK_LPAREN, "Expect '(' after function name.");
        list_t *params = param_list(parser);
        int size = list_size(params);
        consume(parser, TK_RPAREN, "Expect ')' before function body.");
        consume(parser, TK_LBRACE, "Expect '{' before function body.");
        token = previous(parser);

        push_function(parser->comp, name);
        parser->comp->current->param_names = params;

        for (int i = 0; i < size; i++)
            add_local(parser->comp, string_get(params, i));
        add_local(parser->comp, "args");
        add_local(parser->comp, "kw_args");

        bool hit_finalReturn = false;

        while (!check(parser, TK_RBRACE) && !is_atEnd(parser))
        {
            if (hit_finalReturn)
            {
                token_t _token = peek(parser);
                p_errorf(_token.line, _token.column,
                         "Unreachable code after final return statement");
            }

            if (check(parser, TK_RETURN))
            {
                declaration(parser);
                hit_finalReturn = true;
                continue;
            }

            declaration(parser);
        }

        // Implicit return if no return seen
        if (!parser->is_return)
        {
            //  Important: Mark where the implicit return comes from
            token_t rbrace = peek(parser);
            set_pos(parser, rbrace);
            emit(parser->comp, OP_PUSH_NIL);
            emit(parser->comp, OP_RETURN);
        }

        parser->is_return = false;

        consume(parser, TK_RBRACE, "Expect '}' after function body.");

        pop_function(parser->comp, size);

        if (!is_localScope(parser->comp))
        {
            // Mark function definition location before storing it
            set_pos(parser, id_token);
            emit_8u(parser->comp, OP_STORE_GLOBAL, name, store_name(parser->comp, name));
        }
    }
    else
        p_error("Expect function name", token.line, token.column);

    consume_ifExist(parser, 1, TK_SEMICOLON);
}

static void debug(parser_t *parser)
{
    emit(parser->comp, OP_DEBUG);
    consume_ifExist(parser, 1, TK_SEMICOLON);
}

static char *import_joinParts(token_t *parts, int count)
{
    int total = 0;
    for (int i = 0; i < count; i++)
        total += parts[i].length;

    total += (count - 1); // dots

    char *path = malloc((size_t)total + 1);
    int offset = 0;

    for (int i = 0; i < count; i++)
    {
        memcpy(path + offset, parts[i].start, (size_t)parts[i].length);
        offset += parts[i].length;
        if (i < count - 1)
            path[offset++] = '.';
    }

    path[offset] = '\0';
    return path;
}

static void emit_importModule(parser_t *parser, token_t *parts, int count)
{
    char *module_path = import_joinParts(parts, count);
    int module_index = store_const(parser->comp, NEW_OBJ(new_pistring(module_path)));
    emit_16u(parser->comp, OP_LOAD_CONST, module_path, module_index);
    emit(parser->comp, OP_IMPORT);
}

static void emit_importBinding(parser_t *parser, token_t export_tok, token_t alias_tok)
{
    char *alias_name = token_value(alias_tok);
    int export_index = store_const(parser->comp, new_value(export_tok));

    emit_16u(parser->comp, OP_LOAD_CONST, alias_name, export_index);
    emit(parser->comp, OP_GET_EXPORT);

    store_variable(parser->comp, alias_name);
    free(alias_name);
}

static void emit_importAlias(parser_t *parser, token_t alias_tok)
{
    char *alias_name = token_value(alias_tok);
    store_variable(parser->comp, alias_name);
    free(alias_name);
}

static void import_item(parser_t *parser)
{
    token_t parts[256];
    int count = 0;
    bool import_all = false;
    bool import_braced = false;
    bool has_alias = false;
    token_t alias_tok;

    parts[count++] = consume(parser, TK_ID, "Expect module name after 'import'.");

    while (match(parser, TK_DOT))
    {
        if (check(parser, TK_MULT))
        {
            next(parser);
            import_all = true;
            break;
        }

        if (check(parser, TK_LBRACE))
        {
            import_braced = true;
            break;
        }

        if (count >= 256)
            p_error("Import path is too long.", peek(parser).line, peek(parser).column);
        parts[count++] = consume(parser, TK_ID, "Expect identifier after '.'.");
    }

    if (!import_all && !import_braced && match(parser, TK_COLON))
    {
        alias_tok = consume(parser, TK_ID, "Expect alias name after ':'.");
        has_alias = true;

        if (match(parser, TK_DOT))
        {
            if (check(parser, TK_MULT))
            {
                next(parser);
                import_all = true;
            }
            else if (check(parser, TK_LBRACE))
            {
                import_braced = true;
            }
            else
            {
                p_error("Expect '*' or '{' after aliased import selector.", peek(parser).line, peek(parser).column);
            }
        }
    }

    if (import_all)
    {
        emit_importModule(parser, parts, count);
        if (has_alias)
        {
            emit(parser->comp, OP_DUP_TOP);
            emit_importAlias(parser, alias_tok);
        }
        emit(parser->comp, OP_IMPORT_ALL);
        return;
    }

    if (import_braced)
    {
        emit_importModule(parser, parts, count); // leaves module on stack
        if (has_alias)
        {
            emit(parser->comp, OP_DUP_TOP);
            emit_importAlias(parser, alias_tok);
        }

        consume(parser, TK_LBRACE, "Expect '{' after module path.");
        do
        {
            token_t export_tok = consume(parser, TK_ID, "Expect export name inside import list.");
            token_t alias_tok = export_tok;
            if (match(parser, TK_COLON))
                alias_tok = consume(parser, TK_ID, "Expect alias name after ':'.");

            emit(parser->comp, OP_DUP_TOP);
            emit_importBinding(parser, export_tok, alias_tok);

        } while (match(parser, TK_COMMA));

        consume(parser, TK_RBRACE, "Expect '}' after import list.");
        emit(parser->comp, OP_POP); // discard module left on stack
        return;
    }

    if (has_alias)
    {
        // import module:alias
        if (count == 1)
        {
            emit_importModule(parser, parts, count);
            emit_importAlias(parser, alias_tok);
            return;
        }

        // import path.to.mod.elem:alias
        token_t export_tok = parts[count - 1];

        emit_importModule(parser, parts, count - 1);
        emit_importBinding(parser, export_tok, alias_tok);
        return;
    }

    // Plain module import: bind to export if same-name function exists, else module.
    emit_importModule(parser, parts, count);
    char *binding_name = token_value(parts[count - 1]);
    int name_index = store_const(parser->comp, new_value(parts[count - 1]));
    emit_16u(parser->comp, OP_LOAD_CONST, binding_name, name_index);
    emit(parser->comp, OP_IMPORT_DEFAULT);
    store_variable(parser->comp, binding_name);
    free(binding_name);
}

static void import_stmt(parser_t *parser)
{
    token_t tok = previous(parser); // 'import'
    set_pos(parser, tok);

    do
    {
        import_item(parser);
    } while (match(parser, TK_COMMA));

    consume_ifExist(parser, 1, TK_SEMICOLON);
}

static void statement(parser_t *parser)
{
    if (is_destructure_assign(parser))
        destructure_assignStatement(parser);
    else if (match(parser, TK_LBRACE))
    {
        // Look ahead to check if it's an object literal (key: value format)
        int current = parser->current;

        if (match_n(parser, 5, TK_STR, TK_ID, TK_NUM, TK_FALSE, TK_TRUE) && match(parser, TK_COLON))
        {
            parser->current = current - 1;
            primary(parser);
        }
        else
        {
            // Otherwise, parse as a block
            parser->current = current;
            block(parser);
        }
    }
    else if (match(parser, TK_IF))
        if_stmt(parser);
    else if (match(parser, TK_SWITCH))
        switch_stmt(parser);
    else if (match(parser, TK_WHILE))
        while_stmt(parser);
    else if (match(parser, TK_FOR))
        for_stmt(parser);
    else if (match(parser, TK_BREAK))
        break_stmt(parser);
    else if (match(parser, TK_CONTINUE))
        continue_stmt(parser);
    else if (match(parser, TK_RETURN))
        return_stmt(parser);
    else if (match(parser, TK_DEBUG))
        debug(parser);
    else if (match(parser, TK_IMPORT))
        import_stmt(parser);
    else
        expr_state(parser);

    // parser->is_return = false;
}

static void destructure_assignStatement(parser_t *parser)
{
    list_t *targets = list_create(sizeof(char *));
    consume(parser, TK_LBRACKET, "Expect '[' to start destructuring assignment.");
    do
    {
        token_t name_tok = consume(parser, TK_ID, "Expect identifier in destructuring assignment.");
        char *name = token_value(name_tok);
        list_add(targets, &name);
    } while (match(parser, TK_COMMA));

    consume(parser, TK_RBRACKET, "Expect ']' after destructuring targets.");
    consume(parser, TK_ASSIGN, "Expect '=' after destructuring targets.");
    expr(parser);

    int size = list_size(targets);
    for (int i = 0; i < size; i++)
    {
        char *name = *(char **)list_getAt(targets, i);
        int index = store_const(parser->comp, NEW_NUM(i));

        emit(parser->comp, OP_DUP_TOP);
        emit_16u(parser->comp, OP_LOAD_CONST, name, index);
        emit(parser->comp, OP_GET_ITEM);
        store_variable(parser->comp, name);
    }

    emit(parser->comp, OP_POP);

    if (need_delimiter(parser))
        p_error("Expected delemiter between statements.", peek(parser).line, peek(parser).column);
}

static void store_pairFromTop(parser_t *parser, char *first, char *second)
{
    char *targets[2] = {first, second};
    for (int i = 0; i < 2; i++)
    {
        int index = store_const(parser->comp, NEW_NUM(i));
        emit(parser->comp, OP_DUP_TOP);
        emit_16u(parser->comp, OP_LOAD_CONST, targets[i], index);
        emit(parser->comp, OP_GET_ITEM);
        store_variable(parser->comp, targets[i]);
    }
}

static void block(parser_t *parser)
{
    push_scope(parser->comp);

    while (!check(parser, TK_RBRACE) && !is_atEnd(parser) && !parser->is_return)
        declaration(parser);

    if (parser->is_return && !check(parser, TK_RBRACE))
        p_error("Unreachable code after return statement.", peek(parser).line, peek(parser).column);

    parser->is_return = false;

    pop_scope(parser->comp);

    consume(parser, TK_RBRACE, "Expect '}' after block.");
}

static void print(parser_t *parser)
{
    primary(parser);
    emit(parser->comp, OP_PRINT);
    consume_ifExist(parser, 1, TK_SEMICOLON);
}

static void condition(parser_t *parser)
{
    // Parentheses are ordinary grouping expressions. Parsing the complete
    // condition here lets `(a == true) && b == false` continue after `)`.
    pipeline_expr(parser);
}
static void if_stmt(parser_t *parser)
{
    token_t start = peek(parser); // capture for accurate position
    condition(parser);

    set_pos(parser, start);
    int then_jump = emit_16u(parser->comp, OP_JUMP_IF_FALSE, "", 0);

    if (match(parser, TK_LBRACE))
        block(parser);
    else
    {
        statement(parser);
        parser->is_return = false;
    }

    int end_jumps[256];
    int jump_count = 0;

    if (check(parser, TK_ELIF) || check(parser, TK_ELSE))
    {
        set_pos(parser, peek(parser));
        end_jumps[jump_count++] = emit_16u(parser->comp, OP_JUMP, "", 0);
    }

    patch_jump(parser->comp, then_jump);

    while (match(parser, TK_ELIF))
    {
        token_t elif_tok = previous(parser);
        condition(parser);

        set_pos(parser, elif_tok);
        then_jump = emit_16u(parser->comp, OP_JUMP_IF_FALSE, "", 0);

        if (match(parser, TK_LBRACE))
            block(parser);
        else
        {
            statement(parser);
            parser->is_return = false;
        }

        if (check(parser, TK_ELIF) || check(parser, TK_ELSE))
        {
            set_pos(parser, peek(parser));
            end_jumps[jump_count++] = emit_16u(parser->comp, OP_JUMP, "", 0);
        }

        patch_jump(parser->comp, then_jump);
    }

    if (match(parser, TK_ELSE))
    {
        token_t else_tok = previous(parser);
        set_pos(parser, else_tok);

        patch_jump(parser->comp, then_jump);

        if (match(parser, TK_LBRACE))
            block(parser);
        else
        {
            statement(parser);
            parser->is_return = false;
        }
    }
    else
        patch_jump(parser->comp, then_jump);

    for (int i = 0; i < jump_count; i++)
        patch_jump(parser->comp, end_jumps[i]);
}

static void switch_branchBody(parser_t *parser)
{
    if (match(parser, TK_LBRACE))
        block(parser);
    else
    {
        statement(parser);
        parser->is_return = false;
    }
}

static bool is_defaultSwitchCase(parser_t *parser)
{
    if (!check(parser, TK_ID))
        return false;

    token_t token = peek(parser);
    return token.length == 1 && token.start[0] == '_' &&
           parser->tokens[parser->current + 1].type == TK_COLON;
}

static int find_switchCaseColon(parser_t *parser)
{
    int index = parser->current;
    int paren_depth = 0;
    int bracket_depth = 0;
    int brace_depth = 0;
    int ternary_depth = 0;

    while (parser->tokens[index].type != TK_EOF)
    {
        token_t token = parser->tokens[index];

        switch (token.type)
        {
        case TK_LPAREN:
            paren_depth++;
            break;
        case TK_RPAREN:
            if (paren_depth > 0)
                paren_depth--;
            break;
        case TK_LBRACKET:
            bracket_depth++;
            break;
        case TK_RBRACKET:
            if (bracket_depth > 0)
                bracket_depth--;
            break;
        case TK_LBRACE:
            brace_depth++;
            break;
        case TK_RBRACE:
            if (brace_depth > 0)
                brace_depth--;
            else
                return -1;
            break;
        case TK_QUESTION:
            if (paren_depth == 0 && bracket_depth == 0 && brace_depth == 0)
                ternary_depth++;
            break;
        case TK_COLON:
            if (paren_depth == 0 && bracket_depth == 0 && brace_depth == 0)
            {
                if (ternary_depth > 0)
                    ternary_depth--;
                else
                    return index;
            }
            break;
        default:
            break;
        }

        index++;
    }

    return -1;
}

static void switch_stmt(parser_t *parser)
{
    token_t switch_tok = previous(parser);

    set_pos(parser, switch_tok);
    expr(parser);
    emit(parser->comp, OP_POP);

    consume(parser, TK_LBRACE, "Expect '{' before switch cases.");

    int end_jumps[256];
    int jump_count = 0;
    bool has_case = false;
    bool has_default = false;

    while (!check(parser, TK_RBRACE) && !is_atEnd(parser))
    {
        if (has_default)
            p_error("Default switch case '_' must be the last case.",
                    peek(parser).line, peek(parser).column);

        has_case = true;

        if (is_defaultSwitchCase(parser))
        {
            has_default = true;
            next(parser); // _
            consume(parser, TK_COLON, "Expect ':' after switch default case.");
            switch_branchBody(parser);
            break;
        }

        int colon_index = find_switchCaseColon(parser);
        if (colon_index < 0)
            p_error("Expect ':' after switch case condition.",
                    peek(parser).line, peek(parser).column);

        token_t case_tok = peek(parser);
        segment_t condition = {parser->current, colon_index};
        compile_segmentExpr(parser, condition, "Invalid switch case condition.");
        parser->current = colon_index;
        consume(parser, TK_COLON, "Expect ':' after switch case condition.");

        set_pos(parser, case_tok);
        int next_case_jump = emit_16u(parser->comp, OP_JUMP_IF_FALSE, "", 0);

        switch_branchBody(parser);
        if (jump_count >= 256)
            p_error("Too many switch cases.", case_tok.line, case_tok.column);

        end_jumps[jump_count++] = emit_16u(parser->comp, OP_JUMP, "", 0);
        patch_jump(parser->comp, next_case_jump);
    }

    if (!has_case)
        p_error("Switch statement requires at least one case.",
                switch_tok.line, switch_tok.column);

    consume(parser, TK_RBRACE, "Expect '}' after switch cases.");

    for (int i = 0; i < jump_count; i++)
        patch_jump(parser->comp, end_jumps[i]);

    parser->is_return = false;
}

static void while_stmt(parser_t *parser)
{
    int jump = code_size(parser->comp);

    token_t cond_start = peek(parser);

    condition(parser);

    set_pos(parser, cond_start);

    // Emit a conditional jump instruction to exit the loop if the condition is false
    int address = emit_16u(parser->comp, OP_JUMP_IF_FALSE, "", 0);

    push_loop(parser->comp, jump, false);

    if (match(parser, TK_LBRACE))
        block(parser);
    else
    {
        statement(parser);
        parser->is_return = false;
    }

    pop_loop(parser->comp, jump);
    patch_jump(parser->comp, address);
}

static void for_stmt(parser_t *parser)
{
    bool has_parens = match(parser, TK_LPAREN);

    token_t first = consume(parser, TK_ID, "Invalid for-loop left-hand side. Expect identifier.");
    token_t second = {0};
    bool has_pair = false;

    if (match(parser, TK_COMMA))
    {
        has_pair = true;
        second = consume(parser, TK_ID, "Expect identifier after ',' in for-loop left-hand side.");
    }

    consume(parser, TK_IN, "Expect 'in' keyword after loop variable.");

    push_scope(parser->comp);
    add_variable(parser->comp, token_value(first));
    set_pos(parser, first);
    if (!has_pair)
        emit(parser->comp, OP_PUSH_NIL);
    if (has_pair)
    {
        add_variable(parser->comp, token_value(second));
        set_pos(parser, second);
    }

    token_t cond_tok = peek(parser);
    cond_expr(parser);

    if (has_parens)
        consume(parser, TK_RPAREN, "Expect ')' after iterable expression.");

    set_pos(parser, cond_tok); // associate with iterable expression
    emit(parser->comp, OP_PUSH_ITER);

    set_pos(parser, first); // mark the loop start
    int address = emit_16u(parser->comp, OP_LOOP, "", has_pair ? OP_LOOP_TARGET_PAIR_FLAG : 0);

    if (has_pair)
    {
        store_pairFromTop(parser, token_value(first), token_value(second));
    }
    else
    {
        store_variable(parser->comp, token_value(first));
    }
    push_loop(parser->comp, address - 2, true);

    if (match(parser, TK_LBRACE))
    {
        while (!check(parser, TK_RBRACE) && !is_atEnd(parser))
            declaration(parser);

        consume(parser, TK_RBRACE, "Expect '}' after block.");
    }
    else
    {
        statement(parser);
        parser->is_return = false;
    }

    pop_scope(parser->comp);
    pop_loop(parser->comp, address - 2);
    patch_jumpWithFlags(parser->comp, address, has_pair ? OP_LOOP_TARGET_PAIR_FLAG : 0);
}

static void break_stmt(parser_t *parser)
{
    token_t tok = previous(parser); // 'break' token
    set_pos(parser, tok);

    if (!in_loop(parser->comp))
        p_errorf(tok.line, tok.column, "'break' used outside of a loop");

    if (is_forLoop(parser->comp))
        emit(parser->comp, OP_POP_ITER);

    emit_pop(parser->comp, loop_depth(parser->comp));
    push_break(parser->comp, emit_jump(parser->comp, 0));

    // Mark this point as a return-like exit to check for unreachable code
    parser->is_return = true;

    if (need_delimiter(parser))
        p_error("Expected delimiter or newline after 'break'.", tok.line, tok.column);
}

static void continue_stmt(parser_t *parser)
{
    token_t tok = previous(parser); // 'continue' token
    set_pos(parser, tok);

    if (!in_loop(parser->comp))
        p_errorf(tok.line, tok.column, "'continue' used outside of a loop");

    int address = get_continue(parser->comp);
    emit_pop(parser->comp, loop_depth(parser->comp));
    emit_jump(parser->comp, address - code_size(parser->comp));

    parser->is_return = true;

    if (need_delimiter(parser))
        p_error("Expected delemiter or newline after 'continue'.", tok.line, tok.column);
}

static void return_stmt(parser_t *parser)
{
    token_t tok = previous(parser); // 'return' token
    set_pos(parser, tok);

    if (is_constructor(parser->comp))
    {
        if (!check(parser, TK_SEMICOLON) && !is_lineBreak(parser))
            p_error("Constructors cannot return a value.", peek(parser).line, peek(parser).column);

        emit_8u(parser->comp, OP_LOAD_LOCAL, "this", 0);
    }
    else
    {
        if (match(parser, TK_SEMICOLON) || is_lineBreak(parser))
        {
            int index = store_const(parser->comp, NEW_NIL());
            emit_16u(parser->comp, OP_LOAD_CONST, "nil", index);
        }
        else
            expr(parser); // return with value
    }

    emit(parser->comp, OP_RETURN);
    parser->is_return = true;

    if (need_delimiter(parser))
        p_error("Expected delemiter or newline after return.", tok.line, tok.column);
}

static void expr_state(parser_t *parser)
{

    token_t token = peek(parser);
    int start_line = token.line;
    bool prev_lookUp, is_assign = false;
    int current = parser->current;

    if (token.type == TK_ID)
    {
        token_t op_token = parser->tokens[current + 1];
        token_t after = parser->tokens[current + 2];
        bool standalone_postfix = op_token.line == token.line &&
                                  (op_token.type == TK_INCR || op_token.type == TK_DECR) &&
                                  (after.line != token.line ||
                                   after.type == TK_SEMICOLON ||
                                   after.type == TK_RBRACE ||
                                   after.type == TK_EOF);

        if (standalone_postfix)
        {
            char *name = token_value(token);
            int type = (op_token.type == TK_INCR) ? 5 : 6;

            set_pos(parser, token);
            load_variable(parser->comp, name);
            set_pos(parser, op_token);
            emit_8u(parser->comp, OP_UNARY, unary_ops[type], type);
            set_pos(parser, token);
            store_variable(parser->comp, name);

            free(name);
            skip(parser, 2);

            if (need_delimiter(parser))
                p_error("Expected delemiter between statements.", peek(parser).line, peek(parser).column);
            return;
        }
    }

    if (token.type == TK_LPAREN)
    {
        prev_lookUp = look_up(parser->comp, true);
        primary(parser);
        look_up(parser->comp, prev_lookUp);
        parser->current = current;
    }

    current = parser->current;

    prev_lookUp = look_up(parser->comp, true);

    cond_expr(parser);
    token = peek(parser);
    if (token.line == start_line && token.type >= TK_ASSIGN && token.type <= TK_MOD_ASSIGN)
        is_assign = true;
    look_up(parser->comp, prev_lookUp);

    parser->current = current;

    expr(parser);

    // The assignment expression is handled separately
    if (!is_assign)
    {
        if (parser->comp->is_repl)
            emit(parser->comp, OP_PRINT);
        else
            emit(parser->comp, OP_POP);
    }

    // Check for statement separation
    if (need_delimiter(parser))
        p_error("Expected delemiter between statements.", peek(parser).line, peek(parser).column);
}

static void expr(parser_t *parser)
{
    assignment(parser, false);
}

static assign_t *init_assign(int left, int right, tk_type op)
{
    assign_t *assign = malloc(sizeof(assign_t));

    assign->left = left;
    assign->right = right;
    assign->op = op;

    return assign;
}

static void assignment(parser_t *parser, bool emit_load)
{
    tk_type op;
    pistack_t *assigns = stack_create(sizeof(assign_t));
    int left = parser->current, right;
    bool prev_lookUp = look_up(parser->comp, true);

    // First pass: collect assignments in the stack without emitting bytecode
    // This is done to handle the case where there are multiple assignments in
    // a single expression, e.g. "a = b = c = d = 0".
    cond_expr(parser);
    while (match_n(parser, 9, TK_ASSIGN, TK_PLUS_ASSIGN, TK_MINUS_ASSIGN, TK_DIV_ASSIGN, TK_MULT_ASSIGN,
                   TK_MOD_ASSIGN, TK_BITOR_ASSIGN, TK_XOR_ASSIGN, TK_BITAND_ASSIGN))
    {

        op = previous(parser).type;

        right = parser->current;

        stack_push(assigns, init_assign(left, right, op));

        pipeline_expr(parser);
        left = right;
    }

    look_up(parser->comp, prev_lookUp);

    if (stack_isEmpty(assigns))
    {
        parser->current = left;
        pipeline_expr(parser); // Re-evaluate as a non-assignment expression
    }
    else
    {
        int current = parser->current;
        assign_t *assign;

        // Second pass: pop each assignment and generate bytecode
        while (!stack_isEmpty(assigns))
        {

            assign = stack_pop(assigns);

            op = assign->op;
            left = assign->left;
            right = assign->right;

            token_t lhs = parser->tokens[left];

            if (parser->tokens[left].type != TK_ID)
                p_error("Invalid assignment target", parser->tokens[left].line, parser->tokens[left].column);

            // Sync the runtime error position to LHS token
            set_pos(parser, lhs);

            if (op != TK_ASSIGN)
            {
                // Load LHS for compound assignments
                parser->current = left;
                cond_expr(parser);
            }

            parser->current = right;
            char *prev_fun = parser->fun_name;
            char *prev_obj = parser->object_name;

            if (op == TK_ASSIGN && is_functionLiteral(parser, right))
                parser->fun_name = token_value(lhs);
            if (op == TK_ASSIGN && is_objectLiteral(parser, right))
                parser->object_name = token_value(lhs);

            pipeline_expr(parser);
            parser->fun_name = prev_fun;
            parser->object_name = prev_obj;

            if (op != TK_ASSIGN)
            {
                switch (op)
                {
                case TK_PLUS_ASSIGN:
                    emit_8u(parser->comp, OP_BINARY, bin_ops[0], 0); // OP_BINARY_ADD
                    break;
                case TK_MINUS_ASSIGN:
                    emit_8u(parser->comp, OP_BINARY, bin_ops[1], 1); // OP_BINARY_SUB
                    break;
                case TK_MULT_ASSIGN:
                    emit_8u(parser->comp, OP_BINARY, bin_ops[2], 2); // OP_BINARY_MUL
                    break;
                case TK_DIV_ASSIGN:
                    emit_8u(parser->comp, OP_BINARY, bin_ops[3], 3); // OP_BINARY_DIV
                    break;
                case TK_MOD_ASSIGN:
                    emit_8u(parser->comp, OP_BINARY, bin_ops[4], 4); // OP_BINARY_MOD
                    break;
                case TK_BITOR_ASSIGN:
                    emit_8u(parser->comp, OP_BINARY, bin_ops[5], 5); // OP_BINARY_BITOR
                    break;
                case TK_XOR_ASSIGN:
                    emit_8u(parser->comp, OP_BINARY, bin_ops[6], 6); // OP_BINARY_XOR
                    break;
                case TK_BITAND_ASSIGN:
                    emit_8u(parser->comp, OP_BINARY, bin_ops[7], 7); // OP_BINARY_BITAND
                    break;
                default:
                    break;
                }
            }

            parser->current = left;
            parser->is_store = true;
            cond_expr(parser);

            // free(assign);
        }

        if (emit_load)
        {
            parser->current = left;
            cond_expr(parser);
        }

        parser->current = current;
    }
}

static bool is_pipelineBoundary(parser_t *parser, int index, int start,
                                int paren_depth, int bracket_depth, int brace_depth)
{
    if (paren_depth != 0 || bracket_depth != 0 || brace_depth != 0)
        return false;

    token_t token = parser->tokens[index];
    if (token.type == TK_PIPELINE)
        return true;

    if (index > start && token.line > parser->tokens[index - 1].line)
        return true;

    switch (token.type)
    {
    case TK_EOF:
    case TK_SEMICOLON:
    case TK_COMMA:
    case TK_COLON:
    case TK_RPAREN:
    case TK_RBRACKET:
    case TK_RBRACE:
        return true;
    default:
        return false;
    }
}

static int find_pipelineStageEnd(parser_t *parser, int start)
{
    int paren_depth = 0;
    int bracket_depth = 0;
    int brace_depth = 0;

    int index = start;
    for (; parser->tokens[index].type != TK_EOF; index++)
    {
        if (is_pipelineBoundary(parser, index, start, paren_depth, bracket_depth, brace_depth))
            return index;

        switch (parser->tokens[index].type)
        {
        case TK_LPAREN:
            paren_depth++;
            break;
        case TK_RPAREN:
            if (paren_depth > 0)
                paren_depth--;
            break;
        case TK_LBRACKET:
            bracket_depth++;
            break;
        case TK_RBRACKET:
            if (bracket_depth > 0)
                bracket_depth--;
            break;
        case TK_LBRACE:
            brace_depth++;
            break;
        case TK_RBRACE:
            if (brace_depth > 0)
                brace_depth--;
            break;
        default:
            break;
        }
    }

    return index;
}

static int find_pipelineCallParen(parser_t *parser, int start, int end)
{
    if (end <= start || parser->tokens[end - 1].type != TK_RPAREN)
        return -1;

    for (int index = start; index < end; index++)
        if (parser->tokens[index].type == TK_LPAREN && parser->tokens[index].closeAt == end - 1)
            return index;

    return -1;
}

static int split_pipelineArgs(parser_t *parser, int start, int end,
                              segment_t *args, int max_args)
{
    int count = 0;
    int arg_start = start;
    int paren_depth = 0;
    int bracket_depth = 0;
    int brace_depth = 0;

    for (int index = start; index <= end; index++)
    {
        bool at_end = index == end;
        token_t token = at_end ? parser->tokens[end] : parser->tokens[index];

        if (!at_end)
        {
            switch (token.type)
            {
            case TK_LPAREN:
                paren_depth++;
                break;
            case TK_RPAREN:
                if (paren_depth > 0)
                    paren_depth--;
                break;
            case TK_LBRACKET:
                bracket_depth++;
                break;
            case TK_RBRACKET:
                if (bracket_depth > 0)
                    bracket_depth--;
                break;
            case TK_LBRACE:
                brace_depth++;
                break;
            case TK_RBRACE:
                if (brace_depth > 0)
                    brace_depth--;
                break;
            default:
                break;
            }
        }

        if (at_end || (token.type == TK_COMMA && paren_depth == 0 && bracket_depth == 0 && brace_depth == 0))
        {
            if (arg_start == index)
            {
                arg_start = index + 1;
                continue;
            }

            if (count >= max_args)
                p_error("Too many pipeline stage arguments.",
                        parser->tokens[arg_start].line, parser->tokens[arg_start].column);

            if (parser->tokens[arg_start].type == TK_ELLIPSIS)
                p_error("Pipeline stage calls do not support spread arguments yet.",
                        parser->tokens[arg_start].line, parser->tokens[arg_start].column);

            if (parser->tokens[arg_start].type == TK_ID &&
                arg_start + 1 < index &&
                parser->tokens[arg_start + 1].type == TK_ASSIGN)
                p_error("Pipeline stage calls do not support named arguments yet.",
                        parser->tokens[arg_start].line, parser->tokens[arg_start].column);

            args[count++] = (segment_t){arg_start, index};
            arg_start = index + 1;
        }
    }

    return count;
}

static void parse_pipelineStage(parser_t *parser, pipeline_stage_t *stage)
{
    int start = parser->current;
    int end = find_pipelineStageEnd(parser, start);

    if (start >= end)
        p_error("Expect pipeline stage after '=>'.",
                parser->tokens[start].line, parser->tokens[start].column);

    int call_paren = find_pipelineCallParen(parser, start, end);
    if (call_paren >= 0)
    {
        stage->callee = (segment_t){start, call_paren};
        stage->arg_count = split_pipelineArgs(parser, call_paren + 1, end - 1,
                                              stage->args, MAX_PIPELINE_ARGS);
    }
    else
    {
        stage->callee = (segment_t){start, end};
        stage->arg_count = 0;
    }

    if (stage->callee.start >= stage->callee.end)
        p_error("Expect callable pipeline stage.",
                parser->tokens[start].line, parser->tokens[start].column);

    parser->current = end;
}

static void emit_pipelineNested(parser_t *parser, pipeline_t *pipe, int stage_index)
{
    pipeline_stage_t *stage = &pipe->stages[stage_index];

    compile_segmentExpr(parser, stage->callee, "Invalid pipeline stage callable.");

    if (stage_index == 0)
        compile_segmentExpr(parser, pipe->input, "Invalid pipeline input expression.");
    else
        emit_pipelineNested(parser, pipe, stage_index - 1);

    for (int i = 0; i < stage->arg_count; i++)
        compile_segmentExpr(parser, stage->args[i], "Invalid pipeline stage argument.");

    token_t callee = parser->tokens[stage->callee.start];
    set_pos(parser, stage->op);
    emit_8u(parser->comp, OP_CALL_FUNCTION, token_value(callee), (uint8_t)(stage->arg_count + 1));
}

static void pipeline_expr(parser_t *parser)
{
    int start = parser->current;
    bool prev_lookUp = look_up(parser->comp, true);

    cond_expr(parser);
    bool has_pipeline = check(parser, TK_PIPELINE);
    int first_pipeline = parser->current;

    look_up(parser->comp, prev_lookUp);
    parser->current = start;

    if (!has_pipeline)
    {
        cond_expr(parser);
        return;
    }

    pipeline_t pipe = {0};
    pipe.input = (segment_t){start, first_pipeline};
    parser->current = first_pipeline;

    while (match(parser, TK_PIPELINE))
    {
        if (pipe.stage_count >= MAX_PIPELINE_STAGES)
            p_error("Too many pipeline stages.",
                    previous(parser).line, previous(parser).column);

        pipeline_stage_t *stage = &pipe.stages[pipe.stage_count++];
        stage->op = previous(parser);
        parse_pipelineStage(parser, stage);
    }

    emit_pipelineNested(parser, &pipe, pipe.stage_count - 1);
}

static void cond_expr(parser_t *parser)
{

    or_expr(parser);

    if (match(parser, TK_QUESTION))
    {
        int then_jump = emit_16u(parser->comp, OP_JUMP_IF_FALSE, "", 0);

        // Sync current token for better runtime error info
        set_pos(parser, peek(parser));

        cond_expr(parser);

        token_t token = consume(parser, TK_COLON, "Expect ':' after '?'");
        int else_jump = emit_16u(parser->comp, OP_JUMP, "", 0);

        patch_jump(parser->comp, then_jump);

        cond_expr(parser);

        patch_jump(parser->comp, else_jump);
    }
}

static void or_expr(parser_t *parser)
{
    and_expr(parser);
    while (match(parser, TK_OR))
    {
        token_t op_token = previous(parser);
        and_expr(parser);
        set_pos(parser, op_token);
        emit_8u(parser->comp, OP_BINARY, bin_ops[6], 6);
    }
}

static void and_expr(parser_t *parser)
{
    in_expr(parser);
    while (match(parser, TK_AND))
    {
        token_t op_token = previous(parser);
        in_expr(parser);
        set_pos(parser, op_token);
        emit_8u(parser->comp, OP_BINARY, bin_ops[5], 5);
    }
}

static void in_expr(parser_t *parser)
{
    range_expr(parser);
    while (match(parser, TK_IN))
    {
        token_t op_token = previous(parser);
        range_expr(parser);
        set_pos(parser, op_token);
        emit_8u(parser->comp, OP_COMPARE, comp_ops[6], 6);
    }
}

static void range_expr(parser_t *parser)
{
    bitOr_expr(parser);
    if (match(parser, TK_DBDOTS))
    {
        token_t op_token = previous(parser);
        bitOr_expr(parser);
        if (match(parser, TK_COLON))
            expr(parser);
        else
            emit(parser->comp, OP_PUSH_NIL);
        set_pos(parser, op_token);
        emit(parser->comp, OP_PUSH_RANGE);
    }
}

static void bitOr_expr(parser_t *parser)
{
    xor_expr(parser);
    while (match(parser, TK_BITOR))
    {
        token_t op_token = previous(parser);
        xor_expr(parser);
        set_pos(parser, op_token);
        emit_8u(parser->comp, OP_BINARY, bin_ops[9], 9);
    }
}

static void xor_expr(parser_t *parser)
{
    bitAnd_expr(parser);
    while (match(parser, TK_XOR))
    {
        token_t op_token = previous(parser);
        bitAnd_expr(parser);
        set_pos(parser, op_token);
        emit_8u(parser->comp, OP_BINARY, bin_ops[10], 10);
    }
}

static void bitAnd_expr(parser_t *parser)
{
    shift_expr(parser);
    while (match(parser, TK_BITAND))
    {
        token_t op_token = previous(parser);
        shift_expr(parser);
        set_pos(parser, op_token);
        emit_8u(parser->comp, OP_BINARY, bin_ops[8], 8);
    }
}

static void shift_expr(parser_t *parser)
{
    equality_expr(parser);

    while (match_n(parser, 3, TK_LSHIFT, TK_RSHIFT, TK_URSHIFT))
    {
        tk_type op = previous(parser).type;
        token_t op_token = previous(parser);
        equality_expr(parser);
        set_pos(parser, op_token);

        switch (op)
        {
        case TK_LSHIFT:
            emit_8u(parser->comp, OP_BINARY, bin_ops[11], 11);
            break;
        case TK_RSHIFT:
            emit_8u(parser->comp, OP_BINARY, bin_ops[12], 12);
            break;
        case TK_URSHIFT:
            emit_8u(parser->comp, OP_BINARY, bin_ops[13], 13);
            break;
        default:
            break;
        }
    }
}

static void equality_expr(parser_t *parser)
{
    compare_expr(parser);
    while (match_n(parser, 3, TK_NOT_EQUAL, TK_EQUAL, TK_IS))
    {
        tk_type op = previous(parser).type;
        token_t op_token = previous(parser);
        compare_expr(parser);
        set_pos(parser, op_token);

        if (op == TK_NOT_EQUAL)
            // !=
            emit_8u(parser->comp, OP_COMPARE, comp_ops[3], 3);

        else if (op == TK_EQUAL)
            // ==
            emit_8u(parser->comp, OP_COMPARE, comp_ops[2], 2);

        else if (op == TK_IS)
            // is
            emit_8u(parser->comp, OP_BINARY, bin_ops[15], 15);
    }
}

static void compare_expr(parser_t *parser)
{
    add_expr(parser);

    int last_value_pos = -1;
    int comparison_count = 0;

    while (match_n(parser, 6, TK_EQUAL, TK_NOT_EQUAL, TK_GREATER,
                   TK_LESS, TK_GREATER_EQUAL, TK_LESS_EQUAL))
    {
        tk_type op = previous(parser).type;
        token_t op_token = previous(parser);

        if (last_value_pos != -1)
        {
            // Rewind and reparse the previous right-hand side (e.g., `b`)
            parser->current = last_value_pos;
            add_expr(parser); // push `b` again
            next(parser);     // advance past the operator
        }

        // Save position before parsing the next expression (e.g., `c`)
        last_value_pos = parser->current;
        add_expr(parser); // parse right-hand side
        set_pos(parser, op_token);

        int op_index = -1;
        switch (op)
        {
        case TK_EQUAL:
            op_index = 0;
            break;
        case TK_NOT_EQUAL:
            op_index = 1;
            break;
        case TK_GREATER:
            op_index = 2;
            break;
        case TK_LESS:
            op_index = 3;
            break;
        case TK_GREATER_EQUAL:
            op_index = 4;
            break;
        case TK_LESS_EQUAL:
            op_index = 5;
            break;
        default:
            break;
        }
        emit_8u(parser->comp, OP_COMPARE, comp_ops[op_index], op_index);

        if (comparison_count > 0)
            emit_8u(parser->comp, OP_BINARY, bin_ops[5], 5); // logical AND

        comparison_count++;
    }
}

static void add_expr(parser_t *parser)
{
    dot_expr(parser);
    while (match_n(parser, 2, TK_PLUS, TK_MINUS))
    {

        token_t op = previous(parser);
        dot_expr(parser);
        set_pos(parser, op);
        if (op.type == TK_PLUS)
            emit_8u(parser->comp, OP_BINARY, bin_ops[0], 0);
        else
            emit_8u(parser->comp, OP_BINARY, bin_ops[1], 1);
    }
}

static void dot_expr(parser_t *parser)
{
    mult_expr(parser);
    while (match(parser, TK_DOT_PROD))
    {
        token_t op = previous(parser);
        mult_expr(parser);
        set_pos(parser, op);
        emit_8u(parser->comp, OP_BINARY, bin_ops[14], 14);
    }
}

static void mult_expr(parser_t *parser)
{
    exp_expr(parser);
    while (match_n(parser, 3, TK_MULT, TK_DIV, TK_MOD))
    {
        token_t op = previous(parser);
        exp_expr(parser);
        set_pos(parser, op);
        switch (op.type)
        {
        case TK_MULT:
            emit_8u(parser->comp, OP_BINARY, bin_ops[2], 2);
            break;
        case TK_DIV:
            emit_8u(parser->comp, OP_BINARY, bin_ops[3], 3);
            break;
        case TK_MOD:
            emit_8u(parser->comp, OP_BINARY, bin_ops[4], 4);
            break;
        default:
            break;
        }
    }
}

static void exp_expr(parser_t *parser)
{
    unary_expr(parser);
    while (match(parser, TK_POWER))
    {
        token_t op = previous(parser);
        exp_expr(parser);
        set_pos(parser, op);
        emit_8u(parser->comp, OP_BINARY, bin_ops[7], 7);
    }
}

static void unary_expr(parser_t *parser)
{
    tk_type op;
    int current;

    if (match_n(parser, 8, TK_PLUS, TK_MINUS, TK_NOT, TK_BITNEG, TK_HASH, TK_INCR, TK_DECR, TK_TYPEOF))
    {
        op = previous(parser).type;
        token_t op_token = previous(parser);

        // Handle negative number literals directly
        if (op == TK_MINUS && peek(parser).type == TK_NUM)
        {
            parser->tokens[parser->current].is_negative = true;
            member_expr(parser);
            return;
        }

        if (op == TK_INCR || op == TK_DECR)
        {
            current = parser->current;
            member_expr(parser);
            set_pos(parser, op_token);

            token_t target = previous(parser);

            if (target.type == TK_NUM || target.type == TK_STR || target.type == TK_TRUE ||
                target.type == TK_FALSE || target.type == TK_NIL)
                p_error("Increment/Decrement operations cannot be applied to calls or literals.",
                        target.line, target.column);

            int type = (op == TK_INCR) ? 5 : 6;
            emit_8u(parser->comp, OP_UNARY, unary_ops[type], type);

            // DUP: one copy to store, one to leave as expression result
            emit(parser->comp, OP_DUP_TOP);

            parser->current = current;
            parser->is_store = true;
            parser->force_store = true;
            member_expr(parser);
            parser->is_store = false;
            parser->force_store = false;
        }
        else
        {
            int type = -1;
            switch (op)
            {
            case TK_PLUS:
                type = 0;
                break;
            case TK_MINUS:
                type = 1;
                break;
            case TK_NOT:
                type = 2;
                break;
            case TK_BITNEG:
                type = 3;
                break;
            case TK_HASH:
                type = 4;
                break;
            case TK_TYPEOF:
                type = 7;
                break;
            default:
                break;
            }
            unary_expr(parser);
            set_pos(parser, op_token);
            if (type != -1)
                emit_8u(parser->comp, OP_UNARY, unary_ops[type], type);
        }
    }
    else
    {
        current = parser->current;
        member_expr(parser);
        token_t operand = previous(parser);

        // Handle post-increment / post-decrement
        if (match_n(parser, 2, TK_INCR, TK_DECR))
        {
            op = previous(parser).type;
            token_t op_token = previous(parser);

            if (op_token.line != operand.line)
            {
                // Not on the same line, so not post-increment, put back the token
                parser->current--;
            }
            else
            {

                if (operand.type == TK_NUM || operand.type == TK_STR || operand.type == TK_TRUE ||
                    operand.type == TK_FALSE || operand.type == TK_NIL)
                    p_error("Increment/Decrement operations cannot be applied to literals.",
                            operand.line, operand.column);

                emit(parser->comp, OP_DUP_TOP);
                set_pos(parser, op_token);

                int type = (op == TK_INCR) ? 5 : 6;
                emit_8u(parser->comp, OP_UNARY, unary_ops[type], type);

                parser->current = current;
                parser->is_store = true;
                member_expr(parser);
                advance(parser);

                parser->is_store = false;
            }
        }
    }
}

static bool slice_expr(parser_t *parser)
{
    int index;
    bool is_slice = false;
    token_t token = peek(parser);

    if (check(parser, TK_COLON))
    {
        // Missing start: emit INFINITY so get_slice's isinf() check fires.
        emit_16u(parser->comp, OP_LOAD_CONST, "inf", 1);
        is_slice = true;
        next(parser); // consume the leading ':'
    }
    else
        cond_expr(parser);

    // FIX: flip operands so match() is only called when is_slice is false
    if (is_slice || match(parser, TK_COLON))
    {
        is_slice = true;
        token = previous(parser);

        if (!check(parser, TK_RBRACKET) && !check(parser, TK_COLON) && !check(parser, TK_COMMA))
            cond_expr(parser);
        else
            emit_16u(parser->comp, OP_LOAD_CONST, "inf", 1); // missing end: INFINITY

        if (match(parser, TK_COLON))
        {
            if (!check(parser, TK_RBRACKET) && !check(parser, TK_COMMA))
                cond_expr(parser);
            else
            {
                index = store_const(parser->comp, NEW_NUM(1));
                emit_16u(parser->comp, OP_LOAD_CONST, "1", index);
            }
        }
        else
        {
            index = store_const(parser->comp, NEW_NUM(1));
            emit_16u(parser->comp, OP_LOAD_CONST, "1", index);
        }

        set_pos(parser, token);
        emit(parser->comp, OP_PUSH_SLICE);
    }
    return is_slice;
}

static void member_expr(parser_t *parser)
{
    primary(parser);

    while (true)
    {
        token_t token = previous(parser);

        if (token.line < peek(parser).line)
            break;

        set_pos(parser, token);
        if (match(parser, TK_DOT))
        {
            token_t token = previous(parser);
            // Handle property access using dot notation
            token_t name = consume(parser, TK_ID, "Expect property name after '.'");

            int index = store_const(parser->comp, new_value(name));
            emit_16u(parser->comp, OP_LOAD_CONST, token_value(name), index);

            bool is_chained_access = parser->is_store && parser->force_store &&
                                     has_accessContinuation(parser, name);

            if (!is_chained_access && is_assign(parser))
                emit(parser->comp, OP_SET_MEMBER);
            else
                emit(parser->comp, OP_GET_MEMBER);
        }

        // Handle property access using bracket notation and slicing for lists and tensors
        else if (match(parser, TK_LBRACKET))
        {
            token_t token = previous(parser);
            bool is_slice = slice_expr(parser); // still need return value for 1D

            if (check(parser, TK_COMMA))
            {
                /*  Tensor indexing: tensor[i, j:k, m, ...]  */
                int ndim = 1;
                while (match(parser, TK_COMMA))
                {
                    if (check(parser, TK_RBRACKET))
                        break; /* trailing comma */

                    if (ndim >= MAX_TENSOR_DIMS)
                        p_error("Too many tensor dimensions",
                                peek(parser).line, peek(parser).column);

                    slice_expr(parser); /* return value discarded, VM checks at runtime */
                    ndim++;
                }

                // Check for chained access before deciding whether this is an assignment to a tensor element or just an access
                consume(parser, TK_RBRACKET, "Expect ']' after tensor index");
                bool is_chained_access = parser->is_store && parser->force_store &&
                                         has_accessContinuation(parser, previous(parser));
                bool assign = !is_chained_access && is_assign(parser);

                /* VM pops ndim values and checks each at runtime for slice vs index */
                emit_8u(parser->comp, assign ? OP_TENSOR_SET : OP_TENSOR_GET, "", (uint8_t)ndim);
            }
            else
            {
                /*  List / 1-D indexing: list[i] or list[a:b:c]  */
                consume(parser, TK_RBRACKET, "Expect ']' after index");
                bool is_chained_access = parser->is_store && parser->force_store &&
                                         has_accessContinuation(parser, previous(parser));

                // Always emit GET_ITEM or SET_ITEM - the slice object is already on the stack
                bool assign = !is_chained_access && is_assign(parser);
                emit(parser->comp, assign ? OP_SET_ITEM : OP_GET_ITEM);
            }
        }

        // handle function call
        else if (match(parser, TK_LPAREN))
        {
            int args = 0;
            int named = 0;
            bool saw_named = false;
            bool saw_spread = call_hasSpreadArgs(parser);

            if (token.type == TK_SUPER)
            {
                int ctor_index = store_const(parser->comp, NEW_OBJ(new_pistring("constructor")));
                emit_16u(parser->comp, OP_LOAD_CONST, "constructor", ctor_index);
                emit(parser->comp, OP_GET_MEMBER);
            }

            if (!check(parser, TK_RPAREN))
            {
                if (saw_spread)
                    emit_16u(parser->comp, OP_PUSH_LIST, "", 0);

                do
                {
                    if (match(parser, TK_ELLIPSIS))
                    {
                        if (saw_named)
                        {
                            token_t err = previous(parser);
                            p_errorf(err.line, err.column,
                                     "Positional arguments must come before named arguments.");
                        }

                        expr(parser);
                        emit(parser->comp, OP_LIST_EXTEND);
                    }
                    else if (check(parser, TK_ID) && peek_next(parser).type == TK_ASSIGN)
                    {
                        token_t key_tok = consume(parser, TK_ID, "Expect identifier for named argument.");
                        token_t eq_tok = consume(parser, TK_ASSIGN, "Expect '=' after named argument.");
                        (void)eq_tok;

                        if (!saw_named)
                            saw_named = true;

                        if (named >= 256)
                            p_errorf(key_tok.line, key_tok.column, "Too many named arguments.");

                        char *key = token_value(key_tok);
                        named++;

                        expr(parser); // parse value

                        int index = store_const(parser->comp, NEW_OBJ(new_pistring(key)));
                        emit_16u(parser->comp, OP_LOAD_CONST, key, index);
                    }
                    else
                    {
                        if (saw_named)
                        {
                            token_t err = peek(parser);
                            p_errorf(err.line, err.column,
                                     "Positional arguments must come before named arguments.");
                        }
                        expr(parser);
                        if (saw_spread)
                            emit(parser->comp, OP_LIST_APPEND);
                        else
                            args++;
                    }
                } while (match(parser, TK_COMMA));
            }
            consume(parser, TK_RPAREN, "Expect ')' after function call");
            set_pos(parser, token);
            char *name = strcmp(token_value(token), ")") == 0 ? "<FUN>" : token_value(token);
            if (saw_spread)
                emit(parser->comp, OP_LIST_FINALIZE);
            if (named > 0)
                emit_16u(parser->comp, OP_PUSH_MAP, "", named);
            if (saw_spread)
            {
                emit_8u(parser->comp, OP_CALL_SPREAD, name, named > 0 ? 1 : 0);
            }
            else if (named > 0)
            {
                // Use OP_CALL_FUNCTION_KW when named arguments are present
                emit_8u(parser->comp, OP_CALL_FUNCTION_KW, name, (uint8_t)args);
            }
            else
            {
                // Use regular OP_CALL_FUNCTION when only positional arguments
                emit_8u(parser->comp, OP_CALL_FUNCTION, name, (uint8_t)args);
            }
        }
        else
            break;
    }
}

static void arrow_func(parser_t *parser)
{
    if (match(parser, TK_LBRACE))
    {
        token_t token = previous(parser);

        if (check(parser, TK_RBRACE))
        {
            set_pos(parser, token);

            if (is_constructor(parser->comp))
                emit_8u(parser->comp, OP_LOAD_LOCAL, "this", 0);
            else
                emit(parser->comp, OP_PUSH_NIL);

            emit(parser->comp, OP_RETURN);
            parser->is_return = true;
        }
        else
        {
            while (!check(parser, TK_RBRACE) && !is_atEnd(parser))
                declaration(parser);
        }

        if (!parser->is_return)
        {
            token_t token = peek(parser);

            set_pos(parser, token);

            if (is_constructor(parser->comp))
                emit_8u(parser->comp, OP_LOAD_LOCAL, "this", 0);
            else
                emit(parser->comp, OP_PUSH_NIL);

            emit(parser->comp, OP_RETURN);
            parser->is_return = false;
        }

        token_t rbrace = consume(parser, TK_RBRACE, "Expect '}' after function body.");
        set_pos(parser, rbrace);
    }
    else
    {
        token_t token = peek(parser);
        expr(parser);

        set_pos(parser, token);
        emit(parser->comp, OP_RETURN);
    }
}
static void primary(parser_t *parser)
{
    if (match_n(parser, 7, TK_NUM, TK_STR, TK_TRUE, TK_FALSE, TK_NIL, TK_INF, TK_NAN))
    {
        token_t token = previous(parser);
        set_pos(parser, token);

        if (token.type == TK_NAN)
            emit_16u(parser->comp, OP_LOAD_CONST, "NAN", 0);
        else if (token.type == TK_INF)
            emit_16u(parser->comp, OP_LOAD_CONST, "INF", 0);
        else
        {
            int index = store_const(parser->comp, new_value(token));
            emit_16u(parser->comp, OP_LOAD_CONST, token_value(token), index);
        }
    }
    else if (match(parser, TK_LPAREN))
    {
        int _current = parser->current;
        set_pos(parser, previous(parser));

        if (is_lookUp(parser->comp))
        {
            // Lookahead mode: scan past the parenthesized expression without emitting
            // Empty parens () are valid in lookahead - just skip past them
            if (check(parser, TK_RPAREN))
            {
                next(parser);
                if (match(parser, TK_RARROW))
                {
                    if (match(parser, TK_LBRACE))
                    {
                        int depth = 1;
                        while (depth > 0 && !is_atEnd(parser))
                        {
                            if (check(parser, TK_RBRACE))
                                depth--;
                            else if (check(parser, TK_LBRACE))
                                depth++;
                            if (depth == 0)
                                break;
                            next(parser);
                        }
                        if (depth != 0)
                            p_error("Unmatched '{' in arrow function.", peek(parser).line, peek(parser).column);
                        consume(parser, TK_RBRACE, "Expect '}' after arrow function.");
                    }
                    else
                        expr(parser);
                }
                return;
            }

            // Scan past the contents of the parens
            int depth = 1;
            while (depth > 0 && !is_atEnd(parser))
            {
                if (check(parser, TK_RPAREN))
                    depth--;
                else if (check(parser, TK_LPAREN))
                    depth++;
                next(parser);
            }

            if (depth != 0)
                p_error("Unmatched '(' in grouping expression.", peek(parser).line, peek(parser).column);

            if (match(parser, TK_RARROW))
            {
                if (match(parser, TK_LBRACE))
                {
                    depth = 1;
                    while (depth > 0 && !is_atEnd(parser))
                    {
                        if (check(parser, TK_RBRACE))
                            depth--;
                        else if (check(parser, TK_LBRACE))
                            depth++;
                        if (depth == 0)
                            break;
                        next(parser);
                    }
                    if (depth != 0)
                        p_error("Unmatched '{' in arrow function.", peek(parser).line, peek(parser).column);
                    consume(parser, TK_RBRACE, "Expect '}' after arrow function.");
                }
                else
                    expr(parser);
            }
        }
        else
        {
            // Empty tuple: ()
            if (check(parser, TK_RPAREN))
            {
                consume(parser, TK_RPAREN, "Expect ')' after empty tuple.");

                // Check for arrow function: () -> ...
                if (match(parser, TK_RARROW))
                {
                    bool method_value = parser->object_member;
                    parser->object_member = false;
                    push_function(parser->comp, get_pendingFunctionName(parser));
                    list_t *empty_params = list_create(sizeof(String));
                    parser->comp->current->param_names = empty_params;

                    if (method_value)
                        add_local(parser->comp, "this");
                    add_local(parser->comp, "args");
                    add_local(parser->comp, "kw_args");

                    arrow_func(parser);

                    pop_function(parser->comp, 0);
                    parser->object_member = method_value;
                }
                else
                {
                    // Bare () with no arrow: empty tuple.
                    emit_16u(parser->comp, OP_PUSH_TUPLE, "", 0);
                }
                return;
            }

            // Scan ahead to determine whether this is an arrow function
            while (!check(parser, TK_RPAREN))
                next(parser);

            next(parser);

            if (match(parser, TK_RARROW))
            {
                //  Arrow function: (params) -> body
                parser->current = _current;
                list_t *params = param_list(parser);
                int size = list_size(params);
                consume(parser, TK_RPAREN, "Expect ')' after expression.");
                consume(parser, TK_RARROW, "Expect '->' after function parameters.");

                bool method_value = parser->object_member;
                parser->object_member = false;
                push_function(parser->comp, get_pendingFunctionName(parser));
                parser->comp->current->param_names = params;

                if (method_value)
                    add_local(parser->comp, "this");
                for (int i = 0; i < size; i++)
                    add_local(parser->comp, string_get(params, i));
                add_local(parser->comp, "args");
                add_local(parser->comp, "kw_args");

                arrow_func(parser);

                pop_function(parser->comp, size);
                parser->object_member = method_value;
            }
            else
            {
                //  Grouped expression or tuple literal
                parser->current = _current;

                cond_expr(parser);

                if (match(parser, TK_COMMA))
                {
                    // At least one comma means tuple literal.
                // (expr,)  is a single-element tuple.
                // (expr, expr, ...) is a multi-element tuple.
                // A trailing comma after the last element is allowed.
                int size = 1;
                double previous_number = 0;
                bool previous_is_number = numeric_literalSegment(parser, _current, parser->current - 1,
                                                                 &previous_number);
                while (!check(parser, TK_RPAREN) && !is_atEnd(parser))
                {
                    if (match(parser, TK_ELLIPSIS))
                    {
                        token_t ellipsis = previous(parser);
                        if (!previous_is_number)
                            p_error("Fill expansion requires an integer numeric literal before ', ...,'.",
                                    ellipsis.line, ellipsis.column);

                        consume(parser, TK_COMMA, "Expect ',' after '...' in tuple fill expansion.");
                        double endpoint = consume_fillEndpoint(parser);
                        size += emit_literalFill(parser, previous_number, endpoint, ellipsis);
                        previous_number = endpoint;
                        previous_is_number = true;
                        if (!match(parser, TK_COMMA))
                            break;
                        continue;
                    }

                    int item_start = parser->current;
                    cond_expr(parser);
                    size++;
                    previous_is_number = numeric_literalSegment(parser, item_start, parser->current,
                                                               &previous_number);
                    if (!match(parser, TK_COMMA))
                        break;
                }
                    consume(parser, TK_RPAREN, "Expect ')' after tuple literal.");
                    emit_16u(parser->comp, OP_PUSH_TUPLE, "", size);
                }
                else
                {
                    //  Grouped expression: (expr) -
                    consume(parser, TK_RPAREN, "Expect ')' after expression.");
                }
            }
        }
    }
    else if (match(parser, TK_SUPER))
    {
        set_pos(parser, previous(parser));
        emit(parser->comp, OP_LOAD_SUPER);
    }
    else if (match(parser, TK_ID))
    {
        char *name = tk_string(previous(parser));
        set_pos(parser, previous(parser));

        // Lookahead: skip arrow function body without emitting
        if (is_lookUp(parser->comp) && match(parser, TK_RARROW))
        {
            if (match(parser, TK_LBRACE))
            {
                int depth = 1;
                while (depth > 0 && !is_atEnd(parser))
                {
                    if (check(parser, TK_RBRACE))
                        depth--;
                    else if (check(parser, TK_LBRACE))
                        depth++;
                    if (depth == 0)
                        break;
                    next(parser);
                }
                if (depth != 0)
                    p_error("Unmatched '{' in arrow function.", peek(parser).line, peek(parser).column);
                consume(parser, TK_RBRACE, "Expect '}' after arrow function.");
            }
            else
                expr(parser);

            return;
        }

        // Single-param arrow function: name -> body
        if (match(parser, TK_RARROW))
        {
            emit(parser->comp, OP_PUSH_NIL);

            bool method_value = parser->object_member;
            parser->object_member = false;
            push_function(parser->comp, get_pendingFunctionName(parser));
            list_t *single_params = list_create(sizeof(String));
            list_add(single_params, new_string(name));
            parser->comp->current->param_names = single_params;

            if (method_value)
                add_local(parser->comp, "this");
            add_local(parser->comp, name);
            add_local(parser->comp, "args");
            add_local(parser->comp, "kw_args");

            arrow_func(parser);

            pop_function(parser->comp, 1);
            parser->object_member = method_value;
        }
        // Walrus operator: name <- expr
        else if (match(parser, TK_LARROW))
        {
            if (parser->has_walrus)
                p_error("Chained '<-' operators are not allowed",
                        peek(parser).line, peek(parser).column);

            parser->has_walrus = true;
            cond_expr(parser);
            parser->has_walrus = false;

            emit(parser->comp, OP_DUP_TOP);
            store_variable(parser->comp, name);
            return;
        }
        else
        {
            bool is_chained_access = parser->is_store && parser->force_store &&
                                     peek(parser).line == previous(parser).line &&
                                     check_n(parser, 3, TK_DOT, TK_LBRACKET, TK_LPAREN);

            if (is_object(parser->comp) && strcmp(name, "super") == 0)
            {
                emit(parser->comp, OP_LOAD_SUPER);
                return;
            }
            if (!is_chained_access && is_assign(parser))
                store_variable(parser->comp, name);
            else
                load_variable(parser->comp, name);
        }
    }
    else if (match(parser, TK_LBRACKET))
    {
        int size = 0;
        set_pos(parser, previous(parser));

        if (match(parser, TK_RBRACKET))
            emit_16u(parser->comp, OP_PUSH_LIST, "", 0);

        else if (list_isComprehension(parser))
            emit_listComprehension(parser);

        else if (list_hasSpreadItems(parser))
            emit_spreadListLiteral(parser);

        else
        {
            do
            {
                if (!check(parser, TK_RBRACKET))
                {
                    int item_start = parser->current;
                    cond_expr(parser);
                    size++;
                    double previous_number = 0;
                    bool previous_is_number = numeric_literalSegment(parser, item_start, parser->current,
                                                                     &previous_number);

                    while (check(parser, TK_COMMA) && peek_next(parser).type == TK_ELLIPSIS)
                    {
                        consume(parser, TK_COMMA, "Expect ',' before '...' in list fill expansion.");
                        consume(parser, TK_ELLIPSIS, "Expect '...' in list fill expansion.");
                        token_t ellipsis = previous(parser);
                        if (!previous_is_number)
                            p_error("Fill expansion requires an integer numeric literal before ', ...,'.",
                                    ellipsis.line, ellipsis.column);

                        consume(parser, TK_COMMA, "Expect ',' after '...' in list fill expansion.");
                        double endpoint = consume_fillEndpoint(parser);
                        size += emit_literalFill(parser, previous_number, endpoint, ellipsis);
                        previous_number = endpoint;
                        previous_is_number = true;
                    }
                }
                else
                    break; // trailing comma
            } while (match(parser, TK_COMMA));
            consume(parser, TK_RBRACKET, "Expect ']' at the end of list literal.");
            emit_16u(parser->comp, OP_PUSH_LIST, "", size);
        }
    }
    else if (match(parser, TK_LBRACE))
    {
        set_pos(parser, previous(parser));
        if (is_lookUp(parser->comp))
        {
            int depth = 1;
            while (depth > 0 && !is_atEnd(parser))
            {
                if (check(parser, TK_LBRACE))
                    depth++;
                else if (check(parser, TK_RBRACE))
                    depth--;
                next(parser);
                if (depth == 0)
                    break;
            }
            if (depth != 0)
                p_error("Unmatched '}' in map.", peek(parser).line, peek(parser).column);
            return;
        }

        if (match(parser, TK_RBRACE))
        {
            push_object(parser->comp);
            pop_object(parser->comp);
            emit_16u(parser->comp, OP_PUSH_MAP, "", 0);
            emit_mapFinalize(parser);
        }
        else if (!is_mapEntry(parser) && !map_hasSpreadItems(parser))
        {
            emit_setLiteral(parser);
        }
        else
        {
            push_object(parser->comp);
            if (map_hasSpreadItems(parser))
            {
                emit_spreadMapLiteral(parser);
                pop_object(parser->comp);
            }
            else
            {
                char *key;
                int index = 0;
                int size = 0;
                do
                {
                    if (match_n(parser, 5, TK_STR, TK_ID, TK_NUM, TK_FALSE, TK_TRUE))
                    {
                        key = tk_string(previous(parser));
                        index = store_const(parser->comp, NEW_OBJ(new_pistring(key)));
                    }
                    else
                    {
                        p_error("Unexpected key expression.", peek(parser).line, peek(parser).column);
                    }

                    if (match(parser, TK_LPAREN))
                    {
                        // Method shorthand: { key(params) { body } }
                        list_t *params = param_list(parser);
                        int param_size = list_size(params);
                        consume(parser, TK_RPAREN, "Expect ')' before function body.");
                        consume(parser, TK_LBRACE, "Expect '{' before function body.");

                        push_function(parser->comp, key);
                        parser->comp->current->param_names = params;

                        if (is_object(parser->comp))
                            add_local(parser->comp, "this");

                        for (int i = 0; i < param_size; i++)
                            add_local(parser->comp, string_get(params, i));
                        add_local(parser->comp, "args");
                        add_local(parser->comp, "kw_args");

                        if (match(parser, TK_RBRACE))
                        {
                            if (is_constructor(parser->comp))
                                emit_8u(parser->comp, OP_LOAD_LOCAL, "this", 0);
                            else
                                emit(parser->comp, OP_PUSH_NIL);
                            emit(parser->comp, OP_RETURN);
                        }
                        else
                        {
                            while (!check(parser, TK_RBRACE) && !is_atEnd(parser))
                                declaration(parser);

                            if (!parser->is_return)
                            {
                                if (is_constructor(parser->comp))
                                    emit_8u(parser->comp, OP_LOAD_LOCAL, "this", 0);
                                else
                                    emit(parser->comp, OP_PUSH_NIL);
                                emit(parser->comp, OP_RETURN);
                                parser->is_return = false;
                            }
                        }

                        parser->is_return = false;
                        pop_function(parser->comp, param_size);
                        consume(parser, TK_RBRACE, "Expect '}' after function body.");
                    }
                    else
                    {
                        if (strcmp(key, "constructor") == 0)
                            p_error("Constructor is a reserved keyword.", peek(parser).line, peek(parser).column);
                        consume(parser, TK_COLON, "Expect ':' after object key expression.");

                        bool prev_object_member = parser->object_member;
                        char *prev_fun = parser->fun_name;
                        char *prev_obj = parser->object_name;
                        parser->object_member = true;
                        parser->object_name = NULL;

                        if (is_functionLiteral(parser, parser->current))
                            parser->fun_name = key;

                        cond_expr(parser);
                        parser->object_member = prev_object_member;
                        parser->fun_name = prev_fun;
                        parser->object_name = prev_obj;
                    }

                    emit_16u(parser->comp, OP_LOAD_CONST, key, index);
                    size++;
                } while (match(parser, TK_COMMA) && !check(parser, TK_RBRACE));

                consume(parser, TK_RBRACE, "Expect '}' at the end of map literal.");
                pop_object(parser->comp);
                emit_16u(parser->comp, OP_PUSH_MAP, "", size);
                emit_mapFinalize(parser);
            }
        }
    }
    // Anonymous function expressions: fun(params) { body }
    else if (match(parser, TK_FUN))
    {
        set_pos(parser, previous(parser));

        if (is_lookUp(parser->comp))
        {
            int depth = 0;

            consume(parser, TK_LPAREN, "Expect '(' after function name.");

            depth = 1;
            while (depth > 0 && !is_atEnd(parser))
            {
                if (check(parser, TK_LPAREN))
                    depth++;
                else if (check(parser, TK_RPAREN))
                    depth--;
                next(parser);
            }
            if (depth != 0)
                p_error("Unmatched '(' in anonymous function.", peek(parser).line, peek(parser).column);

            consume(parser, TK_LBRACE, "Expect '{' before function body.");

            depth = 1;
            while (depth > 0 && !is_atEnd(parser))
            {
                if (check(parser, TK_LBRACE))
                    depth++;
                else if (check(parser, TK_RBRACE))
                    depth--;
                next(parser);
            }
            if (depth != 0)
                p_error("Unmatched '{' in anonymous function.", peek(parser).line, peek(parser).column);

            return;
        }
        compiler_t *comp = parser->comp;
        // Function expressions do not have their name set until we parse the parameter list, since the name may be needed for recursion within the function body. So we use a placeholder name for now and set the actual name after parsing the parameters.
        consume(parser, TK_LPAREN, "Expect '(' after function name.");
        list_t *params = param_list(parser);
        int size = list_size(params);
        consume(parser, TK_RPAREN, "Expect ')' before function body.");
        consume(parser, TK_LBRACE, "Expect '{' before function body.");

        bool method_value = parser->object_member;
        parser->object_member = false;
        push_function(comp, get_pendingFunctionName(parser));
        comp->current->param_names = params;

        if (method_value)
            add_local(comp, "this");
        for (int i = 0; i < size; i++)
            add_local(comp, string_get(params, i));
        add_local(comp, "args");
        add_local(comp, "kw_args");

        if (check(parser, TK_RBRACE))
        {
            if (is_constructor(comp))
                emit_8u(comp, OP_LOAD_LOCAL, "this", 0);
            else
                emit(comp, OP_PUSH_NIL);
            emit(comp, OP_RETURN);
            parser->is_return = true;
        }
        else
        {
            while (!check(parser, TK_RBRACE) && !is_atEnd(parser))
                declaration(parser);

            if (!parser->is_return)
            {
                if (is_constructor(comp))
                    emit_8u(comp, OP_LOAD_LOCAL, "this", 0);
                else
                    emit(comp, OP_PUSH_NIL);
                emit(comp, OP_RETURN);
                parser->is_return = true;
            }
        }

        pop_function(comp, size);
        parser->is_return = false;
        parser->object_member = method_value;

        consume(parser, TK_RBRACE, "Expect '}' after function body.");
    }
    else
        p_error("Expect expression.", previous(parser).line, previous(parser).column);
}

void free_parser(parser_t *parser)
{
    free(parser->tokens);
    free(parser);
}
