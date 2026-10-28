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
char *bin_ops[] = {"+", "-", "*", "/", "%", "&&", "||", "**", "&", "|", "^",
                   "<<", ">>", ">>>", ".", "is"};
char *unary_ops[] = {"+", "-", "!", "~", "#", "++", "--", "typeof"};

static void _program(Parser *parser);
static void _declaration(Parser *parser);
static void _var_decl(Parser *parser, bool is_const);
static void _func_decl(Parser *parser);
static void _class_decl(Parser *parser);
static void _statement(Parser *parser);
static void _expr_state(Parser *parser);
static void _destructure_ListAssignStatement(Parser *parser);
static void _destructure_tupleAssignStatement(Parser *parser);
static bool _is_tupleDestructureAssign(Parser *parser);
static void _block(Parser *parser);
static void _if_stmt(Parser *parser);
static void _switch_stmt(Parser *parser);
static void _while_stmt(Parser *parser);
static void _for_stmt(Parser *parser);
static void _break_stmt(Parser *parser);
static void _continue_stmt(Parser *parser);
static void _return_stmt(Parser *parser);
static void _variable(Parser *parser, bool is_const);
static void _expr(Parser *parser);
static void _assignment(Parser *parser, bool emit_load);
static void _pipeline_expr(Parser *parser);
static void _cond_expr(Parser *parser);
static void _or_expr(Parser *parser);
static void _and_expr(Parser *parser);
static void _in_expr(Parser *parser);
static void _range_expr(Parser *parser);
static void _bitOr_expr(Parser *parser);
static void _xor_expr(Parser *parser);
static void _bitAnd_expr(Parser *parser);
static void _shift_expr(Parser *parser);
static void _equality_expr(Parser *parser);
static void _compare_expr(Parser *parser);
static void _add_expr(Parser *parser);
static void _dot_expr(Parser *parser);
static void _mult_expr(Parser *parser);
static void _exp_expr(Parser *parser);
static void _member_expr(Parser *parser);
static void _unary_expr(Parser *parser);
static void _primary(Parser *parser);

static void _emit_spreadListLiteral(Parser *parser);
static void _emit_spreadMapLiteral(Parser *parser);
static void _emit_classMap(Parser *parser, const char *class_name, const char *parent_name);

static void _emit_nilReturn(Compiler *comp)
{
    emit(comp, OP_RETURN_NIL);
}

static void _emit_defaultReturn(Compiler *comp)
{
    _emit_nilReturn(comp);
}

static void _emit_listComprehension(Parser *parser);
static int _emit_literalFill(Parser *parser, double _previous, double endpoint, Token token);

static bool _call_hasSpreadArgs(Parser *parser);

static bool _list_hasSpreadItems(Parser *parser);
static bool _list_isComprehension(Parser *parser);

static bool _map_hasSpreadItems(Parser *parser);
static bool _is_mapEntry(Parser *parser);
static void _emit_setLiteral(Parser *parser);
static bool _has_accessContinuation(Parser *parser, Token token);

static Token _peek(Parser *parser);
static Token _peek_next(Parser *parser);
static Token _previous(Parser *parser);
static bool _check(Parser *parser, PiTokenType type);
static bool _match(Parser *parser, PiTokenType type);
static Token _consume(Parser *parser, PiTokenType type, const char *message);
static void _advance(Parser *parser);
static bool _is_functionLiteral(Parser *parser, int index);
static bool _is_objectLiteral(Parser *parser, int index);
static char *_get_pendingFunctionName(Parser *parser);
static bool _numeric_literalSegment(Parser *parser, int start, int end, double *value);
static double _consume_fillEndpoint(Parser *parser);

void set_pos(Parser *parser, Token token);

typedef struct
{
    int start;
    int end;
} Segment;

#define MAX_PIPELINE_STAGES 64
#define MAX_PIPELINE_ARGS 64

typedef struct
{
    Token op;
    Segment callee;
    Segment args[MAX_PIPELINE_ARGS];
    int arg_count;
} PipelineStage;

typedef struct
{
    Segment input;
    PipelineStage stages[MAX_PIPELINE_STAGES];
    int stage_count;
} Pipeline;

typedef struct
{
    Token name;
    Segment iterable;
} ComprehensionIterator;

typedef struct
{
    Segment result;
    Segment iterators;
    Segment conditions;
    int end_index;
    bool has_conditions;
} ListComprehension;

static bool _is_functionLiteral(Parser *parser, int index)
{
    PiTokenType type = parser->tokens[index].type;

    if (type == TK_FUN)
        return TRUE;

    if (type == TK_ID && parser->tokens[index + 1].type == TK_RARROW)
        return TRUE;

    if (type != TK_LPAREN)
        return FALSE;

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

    return FALSE;
}

static bool _is_objectLiteral(Parser *parser, int index)
{
    return parser->tokens[index].type == TK_LBRACE;
}

static char *_get_pendingFunctionName(Parser *parser)
{
    char *name = parser->fun_name;

    parser->fun_name = NULL;

    return name ? strdup(name) : NULL;
}

static bool _is_integerLiteralValue(double value)
{
    return isfinite(value) && floor(value) == value;
}

static void _emit_numberLiteralConst(Parser *parser, double value)
{
    char descr[64];
    snprintf(descr, sizeof(descr), "%.15g", value);

    int index = store_const(parser->comp, NEW_NUM(value));
    emit_16u(parser->comp, OP_LOAD_CONST, descr, index);
}

static int _emit_literalFill(Parser *parser, double _previous, double endpoint, Token token)
{
    if (!_is_integerLiteralValue(_previous) || !_is_integerLiteralValue(endpoint))
        p_error("Fill expansion requires integer numeric literal endpoints.",
                token.line, token.column);

    int step = _previous <= endpoint ? 1 : -1;
    int count = 0;

    for (double value = _previous + step;
         step > 0 ? value <= endpoint : value >= endpoint;
         value += step)
    {
        if (count >= UINT16_MAX)
            p_error("Fill expansion is too large for one literal.",
                    token.line, token.column);

        _emit_numberLiteralConst(parser, value);
        count++;
    }

    return count;
}

static bool _numeric_literalSegment(Parser *parser, int start, int end, double *value)
{
    if (start + 1 == end && parser->tokens[start].type == TK_NUM)
    {
        *value = tk_double(parser->tokens[start]);
        return TRUE;
    }

    if (start + 2 == end &&
        parser->tokens[start].type == TK_MINUS &&
        parser->tokens[start + 1].type == TK_NUM)
    {
        double number = tk_double(parser->tokens[start + 1]);
        *value = parser->tokens[start + 1].is_negative ? number : -number;
        return TRUE;
    }

    return FALSE;
}

static double _consume_fillEndpoint(Parser *parser)
{
    bool negative = _match(parser, TK_MINUS);
    Token token = _consume(parser, TK_NUM, "Expect integer numeric literal after ', ...,'.");
    double value = tk_double(token);
    return negative ? -value : value;
}

static void _emit_spreadListLiteral(Parser *parser)
{
    emit_16u(parser->comp, OP_PUSH_LIST, "", 0);
    int source_count = 0;
    int pending_items = 0;

    if (_match(parser, TK_RBRACKET))
        return;

    do
    {
        if (_check(parser, TK_RBRACKET))
            break;

        if (_match(parser, TK_ELLIPSIS))
        {
            if (pending_items > 0)
            {
                emit_16u(parser->comp, OP_PUSH_LIST, "", pending_items);
                pending_items = 0;
                source_count++;
            }
            _cond_expr(parser);
            source_count++;
        }
        else
        {
            _cond_expr(parser);
            pending_items++;
        }
    } while (_match(parser, TK_COMMA));

    _consume(parser, TK_RBRACKET, "Expect ']' at the end of list literal.");
    if (pending_items > 0)
    {
        emit_16u(parser->comp, OP_PUSH_LIST, "", pending_items);
        source_count++;
    }
    if (source_count > 255)
        p_error("List literal cannot contain more than 255 spread segments.", _peek(parser).line, _peek(parser).column);
    emit_8u(parser->comp, OP_LIST_EXTEND, "", source_count);
}

static bool _call_hasSpreadArgs(Parser *parser)
{
    int index = parser->current;
    int paren_depth = 1;   // The parentheses of the function call
    int bracket_depth = 0; // The brackets of a list literal
    int brace_depth = 0;   // The braces of a dictionary literal

    while (parser->tokens[index].type != TK_EOF)
    {
        Token token = parser->tokens[index++];

        switch (token.type)
        {
        case TK_LPAREN:
            paren_depth++;
            break;
        case TK_RPAREN:
            paren_depth--;
            if (paren_depth == 0)
                return FALSE;
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
                return TRUE;
            break;
        default:
            break;
        }
    }

    return FALSE;
}

static bool _list_hasSpreadItems(Parser *parser)
{
    int index = parser->current;
    int paren_depth = 0;
    int bracket_depth = 1;
    int brace_depth = 0;

    while (parser->tokens[index].type != TK_EOF)
    {
        Token token = parser->tokens[index++];

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
                return FALSE;
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
                return TRUE;
            }
            break;
        default:
            break;
        }
    }

    return FALSE;
}

static bool _map_hasSpreadItems(Parser *parser)
{
    int index = parser->current;
    int paren_depth = 0;
    int bracket_depth = 0;
    int brace_depth = 1;

    while (parser->tokens[index].type != TK_EOF)
    {
        Token token = parser->tokens[index++];

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
                return FALSE;
            break;
        case TK_ELLIPSIS:
            if (paren_depth == 0 && bracket_depth == 0 && brace_depth == 1)
            {
                if (parser->tokens[index].type == TK_COMMA)
                    break;
                return TRUE;
            }
            break;
        default:
            break;
        }
    }

    return FALSE;
}

static bool _is_mapEntry(Parser *parser)
{
    if (_check(parser, TK_ELLIPSIS))
        return TRUE;

    Token token = _peek(parser);
    switch (token.type)
    {
    case TK_STR:
    case TK_ID:
    case TK_NUM:
    case TK_FALSE:
    case TK_TRUE:
    {
        Token next_token = parser->tokens[parser->current + 1];
        return next_token.type == TK_COLON || next_token.type == TK_LPAREN;
    }
    default:
        return FALSE;
    }
}

static void _emit_setLiteral(Parser *parser)
{
    int size = 0;
    if (_check(parser, TK_RBRACE))
    {
        _consume(parser, TK_RBRACE, "Expect '}' at the end of set literal.");
        emit_16u(parser->comp, OP_PUSH_SET, "", 0);
        return;
    }

    do
    {
        if (_check(parser, TK_RBRACE))
            break;

        int item_start = parser->current;
        _cond_expr(parser);
        size++;

        double previous_number = 0;
        bool previous_is_number = _numeric_literalSegment(parser, item_start, parser->current,
                                                          &previous_number);

        while (_check(parser, TK_COMMA) && _peek_next(parser).type == TK_ELLIPSIS)
        {
            _consume(parser, TK_COMMA, "Expect ',' before '...' in set fill expansion.");
            _consume(parser, TK_ELLIPSIS, "Expect '...' in set fill expansion.");
            Token ellipsis = _previous(parser);
            if (!previous_is_number)
                p_error("Fill expansion requires an integer numeric literal before ', ...,'.",
                        ellipsis.line, ellipsis.column);

            _consume(parser, TK_COMMA, "Expect ',' after '...' in set fill expansion.");
            double endpoint = _consume_fillEndpoint(parser);
            size += _emit_literalFill(parser, previous_number, endpoint, ellipsis);
            previous_number = endpoint;
            previous_is_number = TRUE;
        }
    } while (_match(parser, TK_COMMA));

    _consume(parser, TK_RBRACE, "Expect '}' at the end of set literal.");
    emit_16u(parser->comp, OP_PUSH_SET, "", size);
}

static bool _scan_listComprehension(Parser *parser, ListComprehension *comp)
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
        Token token = parser->tokens[index];

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
                    return FALSE;

                comp->result.start = parser->current;
                comp->result.end = first_colon;
                comp->iterators.start = first_colon + 1;
                comp->iterators.end = (second_colon == -1) ? index : second_colon;
                comp->conditions.start = (second_colon == -1) ? index : second_colon + 1;
                comp->conditions.end = index;
                comp->has_conditions = second_colon != -1;
                comp->end_index = index;
                return TRUE;
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

    return FALSE;
}

static bool _list_isComprehension(Parser *parser)
{
    ListComprehension comp;

    return _scan_listComprehension(parser, &comp);
}

static void _compile_segmentExpr(Parser *parser, Segment segment, const char *message)
{
    if (segment.start >= segment.end)
    {
        Token token = parser->tokens[segment.start];
        p_error(message, token.line, token.column);
    }

    int saved = parser->current;

    Token saved_end = parser->tokens[segment.end];

    parser->current = segment.start;

    parser->tokens[segment.end].type = TK_EOF;

    _pipeline_expr(parser);

    parser->tokens[segment.end] = saved_end;

    if (parser->current != segment.end)
    {
        Token token = parser->tokens[parser->current];
        p_error(message, token.line, token.column);
    }

    parser->current = saved;
}

static int _parse_compIterators(Parser *parser, Segment segment, ComprehensionIterator *iters, int max_iters)
{
    int index = segment.start;
    int count = 0;

    while (index < segment.end)
    {
        if (count >= max_iters)
        {
            Token token = parser->tokens[index];
            p_errorf(token.line, token.column, "Too many iterators in list comprehension.");
        }

        Token name = parser->tokens[index++];
        if (name.type != TK_ID)
            p_errorf(name.line, name.column, "Expect iterator variable name in list comprehension.");

        if (index >= segment.end || parser->tokens[index].type != TK_IN)
        {
            Token token = parser->tokens[index < segment.end ? index : segment.end - 1];
            p_errorf(token.line, token.column, "Expect 'in' after iterator variable in list comprehension.");
        }
        index++;

        int expr_start = index;
        int paren_depth = 0;
        int bracket_depth = 0;
        int brace_depth = 0;

        while (index < segment.end)
        {
            Token token = parser->tokens[index];
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

static int _parse_compConditions(Parser *parser, Segment segment, Segment *conds, int max_conds)
{
    if (segment.start >= segment.end)
        return 0;

    int index = segment.start;
    int count = 0;

    while (index < segment.end)
    {
        if (count >= max_conds)
        {
            Token token = parser->tokens[index];
            p_errorf(token.line, token.column, "Too many conditions in list comprehension.");
        }

        int expr_start = index;
        int paren_depth = 0;
        int bracket_depth = 0;
        int brace_depth = 0;

        while (index < segment.end)
        {
            Token token = parser->tokens[index];
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

static void _emit_listCompLoops(Parser *parser, ListComprehension *comp,
                                ComprehensionIterator *iters, int iter_count,
                                Segment *conds, int cond_count,
                                int iter_index, int acc_slot)
{
    if (iter_index == iter_count)
    {
        // expressions and list comprehension expression, and append the resulting value to the list.
        int jumps[32];
        int jump_count = 0;

        for (int i = 0; i < cond_count; i++)
        {
            _compile_segmentExpr(parser, conds[i], "Invalid list comprehension condition.");
            // Emit a jump if false instruction to jump over the list comprehension expression if the condition is false
            jumps[jump_count++] = emit_16u(parser->comp, OP_JUMP_IF_FALSE, "", 0);
        }

        _compile_segmentExpr(parser, comp->result, "Invalid list comprehension expression.");
        emit_8u(parser->comp, OP_COMP_APPEND, "<comp>", acc_slot);

        for (int i = 0; i < jump_count; i++)
            patch_jump(parser->comp, jumps[i]);
        return;
    }

    Token iter_token = iters[iter_index].name;

    set_pos(parser, parser->tokens[iters[iter_index].iterable.start]);

    _compile_segmentExpr(parser, iters[iter_index].iterable, "Invalid list comprehension iterator expression.");
    emit(parser->comp, OP_PUSH_ITER);

    set_pos(parser, iter_token);

    int address = emit_16u(parser->comp, OP_LOOP, "", 0);

    push_scope(parser->comp);
    add_variable(parser->comp, token_value(iter_token));
    push_loop(parser->comp, address - 2, TRUE);

    _emit_listCompLoops(parser, comp, iters, iter_count, conds, cond_count, iter_index + 1, acc_slot);

    pop_scope(parser->comp);
    pop_loop(parser->comp, address - 2);

    patch_jump(parser->comp, address);
}

static void _emit_listComprehension(Parser *parser)
{
    ListComprehension comp;
    if (!_scan_listComprehension(parser, &comp))
        return;

    ComprehensionIterator iters[16];
    Segment conds[32];
    int iter_count = _parse_compIterators(parser, comp.iterators, iters, 16);
    int cond_count = comp.has_conditions ? _parse_compConditions(parser, comp.conditions, conds, 32) : 0;

    if (iter_count == 0)
    {
        Token token = parser->tokens[comp.iterators.start];
        p_errorf(token.line, token.column, "List comprehension requires at least one iterator.");
    }

    // Create local metadata for the result list so iterator locals keep their
    // normal relative slots inside the comprehension.
    char hidden_name[32];
    snprintf(hidden_name, sizeof(hidden_name), "<comp_%d>", comp.result.start);
    add_local(parser->comp, hidden_name);
    int acc_slot = get_local(parser->comp, hidden_name);
    emit_8u(parser->comp, OP_COMP_BEGIN, "<comp>", acc_slot);

    _emit_listCompLoops(parser, &comp, iters, iter_count, conds, cond_count, 0, acc_slot);

    emit(parser->comp, OP_COMP_END);
    remove_locals(parser->comp, 1);
    parser->current = comp.end_index;
    _consume(parser, TK_RBRACKET, "Expect ']' after list comprehension.");
}

static Token _peek(Parser *parser)
{
    return parser->tokens[parser->current];
}

static Token _peek_next(Parser *parser)
{
    return parser->tokens[parser->current + 1];
}

static bool _is_atEnd(Parser *parser)
{
    return _peek(parser).type == TK_EOF;
}

static Token _previous(Parser *parser)
{
    return parser->tokens[parser->current - 1];
}

static bool _is_delimiter(Token token)
{
    return token.type == TK_SEMICOLON;
}

static Token _next(Parser *parser)
{
    if (!_is_atEnd(parser))
    {
        parser->current++;

        Token tok = _peek(parser);
        if (!_is_delimiter(tok))
            parser->last = tok;
    }
    return _previous(parser);
}

static bool _check(Parser *parser, PiTokenType type)
{
    return !_is_atEnd(parser) && _peek(parser).type == type;
}

static bool _match_n(Parser *parser, int t_count, ...)
{
    va_list args;
    va_start(args, t_count);

    for (int i = 0; i < t_count; i++)
    {
        PiTokenType type = va_arg(args, PiTokenType);
        if (_check(parser, type))
        {
            _next(parser);
            va_end(args);
            return TRUE;
        }
    }
    va_end(args);
    return FALSE;
}

static bool _match(Parser *parser, PiTokenType type)
{
    if (_check(parser, type))
    {
        _next(parser);
        return TRUE;
    }
    return FALSE;
}

static bool _is_listDestructureAssign(Parser *parser)
{
    if (!_check(parser, TK_LBRACKET))
        return FALSE;

    int current = parser->current;
    _next(parser);

    if (_check(parser, TK_RBRACKET))
    {
        parser->current = current;
        return FALSE;
    }

    while (TRUE)
    {
        if (!_check(parser, TK_ID))
        {
            parser->current = current;
            return FALSE;
        }
        _next(parser);

        if (!_match(parser, TK_COMMA))
            break;
    }

    bool _is_assign = _match(parser, TK_RBRACKET) && _check(parser, TK_ASSIGN);
    parser->current = current;
    return _is_assign;
}

static bool _is_tupleDestructureAssign(Parser *parser)
{
    if (!_check(parser, TK_LPAREN))
        return FALSE;

    int current = parser->current;
    _next(parser);

    if (!_check(parser, TK_ID))
    {
        parser->current = current;
        return FALSE;
    }
    _next(parser);

    bool has_comma = FALSE;
    while (_match(parser, TK_COMMA))
    {
        has_comma = TRUE;
        if (_check(parser, TK_RPAREN))
            break;
        if (!_check(parser, TK_ID))
        {
            parser->current = current;
            return FALSE;
        }
        _next(parser);
    }

    bool _is_assign = has_comma && _match(parser, TK_RPAREN) && _check(parser, TK_ASSIGN);
    parser->current = current;
    return _is_assign;
}

static bool _check_n(Parser *parser, int t_count, ...)
{
    if (_is_atEnd(parser))
        return FALSE;
    va_list args;
    va_start(args, t_count);
    for (int i = 0; i < t_count; i++)
    {
        PiTokenType type = va_arg(args, PiTokenType);
        if (_peek(parser).type == type)
        {
            va_end(args);
            return TRUE;
        }
    }
    va_end(args);
    return FALSE;
}

static Token _consume(Parser *parser, PiTokenType type, const char *message)
{
    if (_check(parser, type))
    {
        Token token = _next(parser);
        return token;
    }
    else if (message != NULL)
        p_error(message, _peek(parser).line, _peek(parser).column);
    else
        p_error("Unexpected token", _peek(parser).line, _peek(parser).column);

    if (global_errorHandler)
    {
        Token token = _peek(parser);

        while (!_is_atEnd(parser))
            _advance(parser);

        return token;
    }

    exit(EXIT_FAILURE);
}

static void _advance(Parser *parser)
{
    if (!_is_atEnd(parser))
        parser->current++;
}

static void _skip(Parser *parser, int steps)
{
    parser->current += steps;
}

static bool _consume_ifExist(Parser *parser, int t_count, ...)
{
    bool consumed = FALSE;
    va_list args;
    va_start(args, t_count);

    while (TRUE)
    {
        bool matched = FALSE;

        for (int i = 0; i < t_count; ++i)
        {
            PiTokenType type = va_arg(args, PiTokenType);
            if (_check(parser, type))
            {
                _advance(parser);
                consumed = TRUE;
                matched = TRUE;
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

void set_pos(Parser *parser, Token token)
{
    parser->comp->current_line = token.line;

    parser->comp->current_col = token.column;
}

static bool _is_lineBreak(Parser *parser)
{
    return _previous(parser).line < _peek(parser).line || _peek(parser).type == TK_EOF;
}

bool need_delimiter(Parser *parser)
{
    if (!_consume_ifExist(parser, 1, TK_SEMICOLON))
    {
        if (!_is_lineBreak(parser))
        {
            if (!_check(parser, TK_RBRACE))
                return TRUE;
        }
    }

    return FALSE;
}
static bool _is_assign(Parser *parser)
{
    if (parser->is_store &&
        (parser->force_store ||
         _check_n(parser, 11, TK_ASSIGN, TK_PLUS_ASSIGN, TK_MINUS_ASSIGN, TK_DIV_ASSIGN, TK_MULT_ASSIGN,
                  TK_MOD_ASSIGN, TK_BITOR_ASSIGN, TK_XOR_ASSIGN, TK_BITAND_ASSIGN, TK_INCR, TK_DECR)))
    {
        parser->is_store = FALSE;
        parser->force_store = FALSE;
        return TRUE;
    }

    return FALSE;
}

static bool _has_accessContinuation(Parser *parser, Token token)
{
    return (_check(parser, TK_DOT) &&
            _peek_next(parser).type == TK_ID &&
            _peek(parser).line == _peek_next(parser).line) ||
           (_peek(parser).line == token.line &&
            _check_n(parser, 2, TK_LBRACKET, TK_LPAREN));
}

static bool _is_memberAssignment(Parser *parser, Token token)
{
    bool is_chained_access = parser->is_store && parser->force_store &&
                             _has_accessContinuation(parser, token);
    return !is_chained_access && _is_assign(parser);
}

void mark_tokens(Parser *parser, int start, int end)
{
    // Iterate over the range of tokens and mark them as skipped
    for (int i = start; i < end; i++)
        parser->tokens[i].skip = TRUE;
}

static void _skip_letDecl(Parser *parser)
{
    int depth = 0;

    while (!_is_atEnd(parser))
    {
        Token tok = _peek(parser);

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

        if (depth == 0 && (tok.type == TK_SEMICOLON || _is_lineBreak(parser)))
        {
            if (tok.type == TK_SEMICOLON)
                _next(parser);
            break;
        }

        _next(parser);
    }
}

Parser *init_parser(Compiler *comp, Token *tokens, ParserMode mode)
{
    Parser *parser = (Parser *)malloc(sizeof(Parser));

    parser->tokens = tokens;

    parser->access = FALSE;
    parser->current = 0;
    parser->is_store = FALSE;
    parser->force_store = FALSE;
    parser->is_return = FALSE;
    parser->has_walrus = FALSE;
    parser->object_member = FALSE;
    parser->fun_name = NULL;
    parser->object_name = NULL;
    parser->instance_field_names = NULL;
    parser->instance_fields = NULL;

    parser->comp = comp;
    set_errorSource(comp ? comp->source_name : NULL);

    parser->mode = mode;
    if (mode == MODE_REPL)
        parser->comp->is_repl = TRUE;

    parser->had_error = FALSE;

    return parser;
}

void parse(Parser *parser)
{
    if (parser->mode == MODE_REPL)
    {
        // In REPL mode, parse only a single expression statement.
        // if (!is_atEnd(parser))
        //     expr_state(parser);

        // In REPL mode, parse a full declaration (fun, class, let, or expression)
        if (!_is_atEnd(parser))
            _declaration(parser);
    }
    else
    {
        // In file mode, parse the entire program.
        _program(parser);
    }

    emit(parser->comp, OP_HALT);

    // Runtime diagnostics need the global instruction metadata even when
    // debug disassembly is disabled.
    ht_put(parser->comp->instrs, "<global>", parser->comp->current->instrs);
}

static void _declarations(Parser *parser)
{
    int depth = 0;

    // First pass: Hoist functions and collect globals
    while (!_is_atEnd(parser))
    {
        // Track block depth to ignore inner declarations
        if (_check(parser, TK_LBRACE))
            depth++;
        else if (_check(parser, TK_RBRACE))
            depth--;

        // Skip inner block declarations
        if (depth > 0)
        {
            _next(parser);
            continue;
        }

        if (_match(parser, TK_FUN) && !_match(parser, TK_LPAREN))
        {
            int start = parser->current - 1;
            _func_decl(parser);
            int end = parser->current;
            mark_tokens(parser, start, end);
        }
        else if (_match(parser, TK_LET) || _match(parser, TK_CONST))
            _skip_letDecl(parser);
        else
            _next(parser);
    }

    parser->current = 0;

    // Second pass: Parse remaining code (skipping processed tokens)
    while (!_is_atEnd(parser))
    {
        if (parser->tokens[parser->current].skip)
            _next(parser);
        else
            // statement(parser); // Parse remaining statements
            _declaration(parser);
    }
}

static void _program(Parser *parser)
{
    _declarations(parser);
}

static void _declaration(Parser *parser)
{
    if (_match(parser, TK_LET))
        _var_decl(parser, FALSE);
    else if (_match(parser, TK_CONST))
        _var_decl(parser, TRUE);
    else if (_match(parser, TK_FUN))
        _func_decl(parser);
    else if (_match(parser, TK_CLASS))
        _class_decl(parser);
    else
    {
        _statement(parser); // Parse as a statement
        parser->is_return = FALSE;
    }
}

static void _var_decl(Parser *parser, bool is_const)
{
    do
    {
        _variable(parser, is_const);
    } while (_match(parser, TK_COMMA));
    _consume_ifExist(parser, 1, TK_SEMICOLON);
}

static void _variable(Parser *parser, bool is_const)
{
    Token token = _consume(parser, TK_ID, "Expect variable name");
    char *name = token_value(token);
    bool reserved_local_function = FALSE;

    // parser->comp->name = strdup(name);

    if (_match(parser, TK_ASSIGN))
    {
        char *prev_fun = parser->fun_name;
        char *prev_obj = parser->object_name;
        bool function_literal = _is_functionLiteral(parser, parser->current);

        if (function_literal && is_localScope(parser->comp))
        {
            emit(parser->comp, OP_PUSH_NIL);
            add_localConst(parser->comp, name, is_const);
            reserved_local_function = TRUE;
        }

        if (function_literal)
            parser->fun_name = name;
        if (_is_objectLiteral(parser, parser->current))
            parser->object_name = name;

        if (reserved_local_function)
        {
            _cond_expr(parser);
            store_variableInit(parser->comp, name);
        }
        else
        {
            _assignment(parser, TRUE);
        }

        parser->fun_name = prev_fun;
        parser->object_name = prev_obj;
    }

    else
        emit(parser->comp, OP_PUSH_NIL);

    if (!reserved_local_function)
        add_variableConst(parser->comp, name, is_const);
}

static List *_param_list(Parser *parser)
{
    int size = 0;
    Token name;
    List *params = list_create(sizeof(String));

    set_pos(parser, _previous(parser));

    if (!_check(parser, TK_RPAREN))
    {
        do
        {
            if (size >= 32)
                p_error("Can't have more than 32 parameters.", _peek(parser).line, _peek(parser).column);

            name = _consume(parser, TK_ID, "Expect parameter name.");
            _list_add(params, new_string(token_value(name)));

            // TODO: Handle default values check in the future
            if (_match(parser, TK_ASSIGN))
            {
                /*
                 * Compile defaults as zero-argument functions. Function
                 * declarations are hoisted, so evaluating the expression here
                 * would run it before preceding imports and initializers.
                 */
                push_function(parser->comp, "<default>");
                _expr(parser);
                emit(parser->comp, OP_RETURN);
                pop_function(parser->comp, 0);
            }
            else
                emit(parser->comp, OP_PUSH_NIL);

            size++;

        } while (_match(parser, TK_COMMA));
    }

    return params;
}

static void _emit_spreadMapLiteral(Parser *parser)
{
    emit_16u(parser->comp, OP_PUSH_MAP, "", 0);
    int source_count = 0;
    int pending_entries = 0;

    if (_match(parser, TK_RBRACE))
    {
        return;
    }

    do
    {
        if (_check(parser, TK_RBRACE))
            break;

        if (_match(parser, TK_ELLIPSIS))
        {
            if (pending_entries > 0)
            {
                emit_16u(parser->comp, OP_PUSH_MAP, "", pending_entries);
                pending_entries = 0;
                source_count++;
            }
            _cond_expr(parser);
            source_count++;
            continue;
        }

        char *key;
        int index = 0;

        if (_match_n(parser, 5, TK_STR, TK_ID, TK_NUM, TK_FALSE, TK_TRUE))
        {
            key = tk_string(_previous(parser));
            index = store_stringConst(parser->comp, key);
        }
        else
            p_error("Unexpected key expression.", _peek(parser).line, _peek(parser).column);

        if (_match(parser, TK_LPAREN))
        {
            List *params = _param_list(parser);
            int size = LIST_SIZE(params);
            _consume(parser, TK_RPAREN, "Expect ')' before function body.");
            _consume(parser, TK_LBRACE, "Expect '{' before function body.");

            push_function(parser->comp, key);
            parser->comp->current->param_names = params;

            if (is_object(parser->comp))
                add_local(parser->comp, "this");

            for (int i = 0; i < size; i++)
                add_local(parser->comp, string_get(params, i));
            add_local(parser->comp, "args");
            add_local(parser->comp, "kw_args");

            if (_match(parser, TK_RBRACE))
            {
                _emit_defaultReturn(parser->comp);
            }
            else
            {
                while (!_check(parser, TK_RBRACE) && !_is_atEnd(parser))
                    _declaration(parser);

                if (!parser->is_return)
                {
                    _emit_defaultReturn(parser->comp);

                    parser->is_return = FALSE;
                }
            }

            parser->is_return = FALSE;
            pop_function(parser->comp, size);
            _consume(parser, TK_RBRACE, "Expect '}' after function body.");
        }
        else
        {
            if (strcmp(key, "constructor") == 0)
                p_error("Constructor is a reserved keyword.", _peek(parser).line, _peek(parser).column);
            _consume(parser, TK_COLON, "Expect ':' after object key expression.");
            char *prev_obj = parser->object_name;
            parser->object_name = NULL;
            _cond_expr(parser);
            parser->object_name = prev_obj;
        }

        emit_16u(parser->comp, OP_LOAD_CONST, key, index);
        free(key);
        pending_entries++;
    } while (_match(parser, TK_COMMA) && !_check(parser, TK_RBRACE));

    _consume(parser, TK_RBRACE, "Expect '}' at the end of map literal.");
    if (pending_entries > 0)
    {
        emit_16u(parser->comp, OP_PUSH_MAP, "", pending_entries);
        source_count++;
    }
    if (source_count > 0)
    {
        if (source_count > 255)
            p_error("Map literal cannot contain more than 255 spread segments.", _peek(parser).line, _peek(parser).column);
        emit_8u(parser->comp, OP_MAP_EXTEND, "", source_count);
    }
}

static void _emit_classMap(Parser *parser, const char *class_name, const char *parent_name)
{
    char *prev_obj = parser->object_name;
    HT_Table *prev_field_names = parser->instance_field_names;
    List *prev_fields = parser->instance_fields;
    parser->object_name = (char *)class_name;
    parser->instance_field_names = ht_create(sizeof(uint16_t));
    parser->instance_fields = list_create(sizeof(char *));

    push_object(parser->comp);

    int size = 0;
    while (!_check(parser, TK_RBRACE) && !_is_atEnd(parser))
    {
        Token key_tok = _consume(parser, TK_ID, "Expect class member name.");
        char *key = token_value(key_tok);
        int index = store_stringConst(parser->comp, key);

        if (_match(parser, TK_LPAREN))
        {
            List *params = _param_list(parser);
            int param_count = LIST_SIZE(params);
            _consume(parser, TK_RPAREN, "Expect ')' before method body.");
            _consume(parser, TK_LBRACE, "Expect '{' before method body.");

            push_function(parser->comp, key);
            parser->comp->current->param_names = params;

            add_local(parser->comp, "this");

            for (int i = 0; i < param_count; i++)
                add_local(parser->comp, string_get(params, i));
            add_local(parser->comp, "args");
            add_local(parser->comp, "kw_args");

            if (_match(parser, TK_RBRACE))
            {
                _emit_defaultReturn(parser->comp);
            }
            else
            {
                while (!_check(parser, TK_RBRACE) && !_is_atEnd(parser))
                    _declaration(parser);

                if (!parser->is_return)
                {
                    _emit_defaultReturn(parser->comp);
                }
            }

            parser->is_return = FALSE;
            pop_function(parser->comp, param_count);
            _consume(parser, TK_RBRACE, "Expect '}' after method body.");
        }
        else
        {
            if (strcmp(key, "constructor") == 0)
                p_error("Constructor is a reserved keyword.", _peek(parser).line, _peek(parser).column);

            _consume(parser, TK_ASSIGN, "Expect '=' after static class member name.");

            bool prev_object_member = parser->object_member;
            char *prev_fun = parser->fun_name;
            char *member_prev_obj = parser->object_name;
            parser->object_member = TRUE;
            parser->object_name = NULL;

            if (_is_functionLiteral(parser, parser->current))
                parser->fun_name = key;

            _cond_expr(parser);
            parser->object_member = prev_object_member;
            parser->fun_name = prev_fun;
            parser->object_name = member_prev_obj;
        }

        emit_16u(parser->comp, OP_LOAD_CONST, key, index);
        free(key);
        size++;

        bool had_comma = _consume_ifExist(parser, 1, TK_COMMA);
        if (!had_comma && need_delimiter(parser))
            p_error("Expected delimiter between class members.", _peek(parser).line, _peek(parser).column);
    }

    _consume(parser, TK_RBRACE, "Expect '}' after class body.");
    pop_object(parser->comp);

    /* PUSH_CLASS consumes member pairs followed by the class name and
       superclass.  Object is resolved normally, so a missing superclass is
       represented by the language-level Object value. */
    int field_count = LIST_SIZE(parser->instance_fields);
    for (int i = 0; i < field_count; i++)
    {
        char *field_name = *(char **)_list_getAt(parser->instance_fields, i);
        int field_index = store_stringConst(parser->comp, field_name);
        emit_16u(parser->comp, OP_LOAD_CONST, field_name, field_index);
    }

    int name_index = store_stringConst(parser->comp, class_name);
    emit_16u(parser->comp, OP_LOAD_CONST, (char *)class_name, name_index);
    load_variable(parser->comp, (char *)parent_name);
    emit_16uX2(parser->comp, OP_PUSH_CLASS, "", size, field_count);

    ht_free(parser->instance_field_names);
    list_free(parser->instance_fields);
    parser->instance_field_names = prev_field_names;
    parser->instance_fields = prev_fields;
    parser->object_name = prev_obj;
}

static void _class_decl(Parser *parser)
{
    Token name_tok = _consume(parser, TK_ID, "Expect class name.");
    char *class_name = token_value(name_tok);
    char *parent_name = "Object";

    if (_match(parser, TK_COLON))
    {
        Token parent_tok = _consume(parser, TK_ID, "Expect parent class name after ':'.");
        parent_name = token_value(parent_tok);
    }

    _consume(parser, TK_LBRACE, "Expect '{' before class body.");

    _emit_classMap(parser, class_name, parent_name);
    add_variable(parser->comp, class_name);
}
static void _func_decl(Parser *parser)
{
    Token token = _previous(parser);

    if (_match(parser, TK_ID))
    {
        Token id_token = _previous(parser);
        char *name = token_value(id_token);

        if (is_localScope(parser->comp))
            add_local(parser->comp, name);

        _consume(parser, TK_LPAREN, "Expect '(' after function name.");
        List *params = _param_list(parser);
        int size = LIST_SIZE(params);
        _consume(parser, TK_RPAREN, "Expect ')' before function body.");
        _consume(parser, TK_LBRACE, "Expect '{' before function body.");
        token = _previous(parser);

        push_function(parser->comp, name);
        parser->comp->current->param_names = params;

        for (int i = 0; i < size; i++)
            add_local(parser->comp, string_get(params, i));
        add_local(parser->comp, "args");
        add_local(parser->comp, "kw_args");

        bool hit_finalReturn = FALSE;

        while (!_check(parser, TK_RBRACE) && !_is_atEnd(parser))
        {
            if (hit_finalReturn)
            {
                Token _token = _peek(parser);
                p_errorf(_token.line, _token.column,
                         "Unreachable code after final return statement");
            }

            if (_check(parser, TK_RETURN))
            {
                _declaration(parser);
                hit_finalReturn = TRUE;
                continue;
            }

            _declaration(parser);
        }

        // Implicit return if no return seen
        if (!parser->is_return)
        {
            //  Important: Mark where the implicit return comes from
            Token rbrace = _peek(parser);
            set_pos(parser, rbrace);
            _emit_nilReturn(parser->comp);
        }

        parser->is_return = FALSE;

        _consume(parser, TK_RBRACE, "Expect '}' after function body.");

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

    _consume_ifExist(parser, 1, TK_SEMICOLON);
}

static void _debug(Parser *parser)
{
    emit(parser->comp, OP_DEBUG);
    _consume_ifExist(parser, 1, TK_SEMICOLON);
}

static char *_import_joinParts(Token *parts, int count)
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

static void _emit_importModule(Parser *parser, Token *parts, int count)
{
    char *module_path = _import_joinParts(parts, count);
    int module_index = store_stringConst(parser->comp, module_path);
    emit_16u(parser->comp, OP_LOAD_CONST, module_path, module_index);
    free(module_path);
    emit(parser->comp, OP_IMPORT);
}

static void _emit_importBinding(Parser *parser, Token export_tok, Token alias_tok)
{
    char *alias_name = token_value(alias_tok);
    int export_index = store_tokenConst(parser->comp, export_tok);

    emit_16u(parser->comp, OP_LOAD_CONST, alias_name, export_index);
    emit(parser->comp, OP_GET_EXPORT);

    store_variable(parser->comp, alias_name);
    free(alias_name);
}

static void _emit_importAlias(Parser *parser, Token alias_tok)
{
    char *alias_name = token_value(alias_tok);
    store_variable(parser->comp, alias_name);
    free(alias_name);
}

static void _import_item(Parser *parser)
{
    Token parts[256];
    int count = 0;
    bool import_all = FALSE;
    bool import_braced = FALSE;
    bool has_alias = FALSE;
    Token alias_tok;

    parts[count++] = _consume(parser, TK_ID, "Expect module name after 'import'.");

    while (_match(parser, TK_DOT))
    {
        if (_check(parser, TK_MULT))
        {
            _next(parser);
            import_all = TRUE;
            break;
        }

        if (_check(parser, TK_LBRACE))
        {
            import_braced = TRUE;
            break;
        }

        if (count >= 256)
            p_error("Import path is too long.", _peek(parser).line, _peek(parser).column);
        parts[count++] = _consume(parser, TK_ID, "Expect identifier after '.'.");
    }

    if (!import_all && !import_braced && _match(parser, TK_COLON))
    {
        alias_tok = _consume(parser, TK_ID, "Expect alias name after ':'.");
        has_alias = TRUE;

        if (_match(parser, TK_DOT))
        {
            if (_check(parser, TK_MULT))
            {
                _next(parser);
                import_all = TRUE;
            }
            else if (_check(parser, TK_LBRACE))
            {
                import_braced = TRUE;
            }
            else
            {
                p_error("Expect '*' or '{' after aliased import selector.", _peek(parser).line, _peek(parser).column);
            }
        }
    }

    if (import_all)
    {
        _emit_importModule(parser, parts, count);
        if (has_alias)
        {
            emit(parser->comp, OP_DUP_TOP);
            _emit_importAlias(parser, alias_tok);
        }
        emit(parser->comp, OP_IMPORT_ALL);
        return;
    }

    if (import_braced)
    {
        _emit_importModule(parser, parts, count); // leaves module on stack
        if (has_alias)
        {
            emit(parser->comp, OP_DUP_TOP);
            _emit_importAlias(parser, alias_tok);
        }

        _consume(parser, TK_LBRACE, "Expect '{' after module path.");
        do
        {
            Token export_tok = _consume(parser, TK_ID, "Expect export name inside import list.");
            Token alias_tok = export_tok;
            if (_match(parser, TK_COLON))
                alias_tok = _consume(parser, TK_ID, "Expect alias name after ':'.");

            emit(parser->comp, OP_DUP_TOP);
            _emit_importBinding(parser, export_tok, alias_tok);

        } while (_match(parser, TK_COMMA));

        _consume(parser, TK_RBRACE, "Expect '}' after import list.");
        emit(parser->comp, OP_POP); // discard module left on stack
        return;
    }

    if (has_alias)
    {
        // import module:alias
        if (count == 1)
        {
            _emit_importModule(parser, parts, count);
            _emit_importAlias(parser, alias_tok);
            return;
        }

        // import path.to.mod.elem:alias
        Token export_tok = parts[count - 1];

        _emit_importModule(parser, parts, count - 1);
        _emit_importBinding(parser, export_tok, alias_tok);
        return;
    }

    // Plain module import: bind to a same-name export, else the module.
    _emit_importModule(parser, parts, count);
    char *binding_name = token_value(parts[count - 1]);
    int name_index = store_tokenConst(parser->comp, parts[count - 1]);
    emit_16u(parser->comp, OP_LOAD_CONST, binding_name, name_index);
    emit(parser->comp, OP_IMPORT_DEFAULT);
    store_variable(parser->comp, binding_name);
    free(binding_name);
}

static void _import_stmt(Parser *parser)
{
    Token tok = _previous(parser); // 'import'
    set_pos(parser, tok);

    do
    {
        _import_item(parser);
    } while (_match(parser, TK_COMMA));

    _consume_ifExist(parser, 1, TK_SEMICOLON);
}

static void _statement(Parser *parser)
{
    if (_is_listDestructureAssign(parser))
        _destructure_ListAssignStatement(parser);
    else if (_is_tupleDestructureAssign(parser))
        _destructure_tupleAssignStatement(parser);
    else if (_match(parser, TK_LBRACE))
    {
        // Look ahead to check if it's an object literal (key: value format)
        int current = parser->current;

        if (_match_n(parser, 5, TK_STR, TK_ID, TK_NUM, TK_FALSE, TK_TRUE) && _match(parser, TK_COLON))
        {
            parser->current = current - 1;
            _primary(parser);
        }
        else
        {
            // Otherwise, parse as a block
            parser->current = current;
            _block(parser);
        }
    }
    else if (_match(parser, TK_IF))
        _if_stmt(parser);
    else if (_match(parser, TK_SWITCH))
        _switch_stmt(parser);
    else if (_match(parser, TK_WHILE))
        _while_stmt(parser);
    else if (_match(parser, TK_FOR))
        _for_stmt(parser);
    else if (_match(parser, TK_BREAK))
        _break_stmt(parser);
    else if (_match(parser, TK_CONTINUE))
        _continue_stmt(parser);
    else if (_match(parser, TK_RETURN))
        _return_stmt(parser);
    else if (_match(parser, TK_DEBUG))
        _debug(parser);
    else if (_match(parser, TK_IMPORT))
        _import_stmt(parser);
    else
        _expr_state(parser);

    // parser->is_return = false;
}

static void _destructure_ListAssignStatement(Parser *parser)
{
    List *targets = list_create(sizeof(char *));
    _consume(parser, TK_LBRACKET, "Expect '[' to start destructuring assignment.");
    do
    {
        Token name_tok = _consume(parser, TK_ID, "Expect identifier in destructuring assignment.");
        char *name = token_value(name_tok);
        _list_add(targets, &name);
    } while (_match(parser, TK_COMMA));

    _consume(parser, TK_RBRACKET, "Expect ']' after destructuring targets.");
    _consume(parser, TK_ASSIGN, "Expect '=' after destructuring targets.");
    _expr(parser);

    int size = LIST_SIZE(targets);
    for (int i = 0; i < size; i++)
    {
        char *name = *(char **)_list_getAt(targets, i);
        int index = store_const(parser->comp, NEW_NUM(i));

        emit(parser->comp, OP_DUP_TOP);
        emit_16u(parser->comp, OP_LOAD_CONST, name, index);
        emit(parser->comp, OP_GET_ITEM);
        store_variable(parser->comp, name);
    }

    emit(parser->comp, OP_POP);

    if (need_delimiter(parser))
        p_error("Expected delemiter between statements.", _peek(parser).line, _peek(parser).column);
}

static void _destructure_tupleAssignStatement(Parser *parser)
{
    List *targets = list_create(sizeof(char *));
    _consume(parser, TK_LPAREN, "Expect '(' to start tuple destructuring assignment.");
    do
    {
        Token name_tok = _consume(parser, TK_ID, "Expect identifier in tuple destructuring assignment.");
        char *name = token_value(name_tok);
        _list_add(targets, &name);
    } while (_match(parser, TK_COMMA) && !_check(parser, TK_RPAREN));

    _consume(parser, TK_RPAREN, "Expect ')' after tuple destructuring targets.");
    _consume(parser, TK_ASSIGN, "Expect '=' after tuple destructuring targets.");
    _expr(parser);

    int size = LIST_SIZE(targets);
    for (int i = 0; i < size; i++)
    {
        char *name = *(char **)_list_getAt(targets, i);
        int index = store_const(parser->comp, NEW_NUM(i));

        emit(parser->comp, OP_DUP_TOP);
        emit_16u(parser->comp, OP_LOAD_CONST, name, index);
        emit(parser->comp, OP_GET_ITEM);
        store_variable(parser->comp, name);
    }

    emit(parser->comp, OP_POP);

    if (need_delimiter(parser))
        p_error("Expected delemiter between statements.", _peek(parser).line, _peek(parser).column);
}

static void _store_pairFromTop(Parser *parser, char *first, char *second)
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

static void _block(Parser *parser)
{
    push_scope(parser->comp);

    while (!_check(parser, TK_RBRACE) && !_is_atEnd(parser) && !parser->is_return)
        _declaration(parser);

    if (parser->is_return && !_check(parser, TK_RBRACE))
        p_error("Unreachable code after return statement.", _peek(parser).line, _peek(parser).column);

    parser->is_return = FALSE;

    pop_scope(parser->comp);

    _consume(parser, TK_RBRACE, "Expect '}' after block.");
}

static void _condition(Parser *parser)
{
    // Parentheses are ordinary grouping expressions. Parsing the complete
    // condition here lets `(a == true) && b == false` continue after `)`.
    _pipeline_expr(parser);
}
static void _if_stmt(Parser *parser)
{
    Token start = _peek(parser); // capture for accurate position
    _condition(parser);

    bool constant_condition;
    if (last_constantCondition(parser->comp, &constant_condition))
    {
        discard_lastConstant(parser->comp);

        if (constant_condition)
        {
            if (_match(parser, TK_LBRACE))
                _block(parser);
            else
            {
                _statement(parser);
                parser->is_return = FALSE;
            }

            if (_match(parser, TK_ELSE))
            {
                bool previous_lookup = look_up(parser->comp, TRUE);
                if (_match(parser, TK_LBRACE))
                    _block(parser);
                else
                    _statement(parser);
                look_up(parser->comp, previous_lookup);
                parser->is_return = FALSE;
            }
        }
        else
        {
            bool previous_lookup = look_up(parser->comp, TRUE);
            if (_match(parser, TK_LBRACE))
                _block(parser);
            else
                _statement(parser);
            look_up(parser->comp, previous_lookup);
            parser->is_return = FALSE;

            if (_match(parser, TK_ELSE))
            {
                if (_match(parser, TK_LBRACE))
                    _block(parser);
                else
                    _statement(parser);
                parser->is_return = FALSE;
            }
        }
        return;
    }

    set_pos(parser, start);
    int then_jump = emit_16u(parser->comp, OP_JUMP_IF_FALSE, "", 0);

    if (_match(parser, TK_LBRACE))
        _block(parser);
    else
    {
        _statement(parser);
        parser->is_return = FALSE;
    }

    int end_jumps[256];
    int jump_count = 0;

    if (_check(parser, TK_ELIF) || _check(parser, TK_ELSE))
    {
        set_pos(parser, _peek(parser));
        end_jumps[jump_count++] = emit_16u(parser->comp, OP_JUMP, "", 0);
    }

    patch_jump(parser->comp, then_jump);

    while (_match(parser, TK_ELIF))
    {
        Token elif_tok = _previous(parser);
        _condition(parser);

        set_pos(parser, elif_tok);
        then_jump = emit_16u(parser->comp, OP_JUMP_IF_FALSE, "", 0);

        if (_match(parser, TK_LBRACE))
            _block(parser);
        else
        {
            _statement(parser);
            parser->is_return = FALSE;
        }

        if (_check(parser, TK_ELIF) || _check(parser, TK_ELSE))
        {
            set_pos(parser, _peek(parser));
            end_jumps[jump_count++] = emit_16u(parser->comp, OP_JUMP, "", 0);
        }

        patch_jump(parser->comp, then_jump);
    }

    if (_match(parser, TK_ELSE))
    {
        Token else_tok = _previous(parser);
        set_pos(parser, else_tok);

        patch_jump(parser->comp, then_jump);

        if (_match(parser, TK_LBRACE))
            _block(parser);
        else
        {
            _statement(parser);
            parser->is_return = FALSE;
        }
    }
    else
        patch_jump(parser->comp, then_jump);

    for (int i = 0; i < jump_count; i++)
        patch_jump(parser->comp, end_jumps[i]);
}

static void _switch_branchBody(Parser *parser)
{
    if (_match(parser, TK_LBRACE))
        _block(parser);
    else
    {
        _statement(parser);
        parser->is_return = FALSE;
    }
}

static bool _is_defaultSwitchCase(Parser *parser)
{
    if (!_check(parser, TK_ID))
        return FALSE;

    Token token = _peek(parser);
    return token.length == 1 && token.start[0] == '_' &&
           parser->tokens[parser->current + 1].type == TK_COLON;
}

static int _find_switchCaseColon(Parser *parser)
{
    int index = parser->current;
    int paren_depth = 0;
    int bracket_depth = 0;
    int brace_depth = 0;
    int ternary_depth = 0;

    while (parser->tokens[index].type != TK_EOF)
    {
        Token token = parser->tokens[index];

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

static bool _switch_literalEquality(Parser *parser, int subject_start, int subject_end,
                                    Segment _condition, int *constant_index)
{
    int condition_length = _condition.end - _condition.start;

    /* A literal case implicitly compares against the switch subject. */
    if (condition_length == 1)
    {
        Token literal = parser->tokens[_condition.start];
        switch (literal.type)
        {
        case TK_NUM:
        case TK_STR:
        case TK_TRUE:
        case TK_FALSE:
        case TK_NIL:
        case TK_INF:
        case TK_NAN:
            *constant_index = store_tokenConst(parser->comp, literal);
            return TRUE;
        default:
            return FALSE;
        }
    }

    /* Preserve the explicit form: subject == literal. */
    if (subject_end != subject_start + 1 || condition_length != 3)
        return FALSE;

    Token subject = parser->tokens[subject_start];
    Token left = parser->tokens[_condition.start];
    Token op = parser->tokens[_condition.start + 1];
    Token literal = parser->tokens[_condition.start + 2];

    if (subject.type != TK_ID || left.type != TK_ID || op.type != TK_EQUAL ||
        subject.length != left.length ||
        strncmp(subject.start, left.start, subject.length) != 0)
        return FALSE;

    switch (literal.type)
    {
    case TK_NUM:
    case TK_STR:
    case TK_TRUE:
    case TK_FALSE:
    case TK_NIL:
    case TK_INF:
    case TK_NAN:
        *constant_index = store_tokenConst(parser->comp, literal);
        return TRUE;
    default:
        return FALSE;
    }
}

static void _switch_stmt(Parser *parser)
{
    Token switch_tok = _previous(parser);
    bool condition_switch = _check(parser, TK_LBRACE);

    int subject_start = -1;
    int subject_end = -1;
    bool subject_on_stack = FALSE;
    if (!condition_switch)
    {
        set_pos(parser, switch_tok);
        subject_start = parser->current;
        _expr(parser);
        subject_end = parser->current;
        subject_on_stack = TRUE;
    }

    _consume(parser, TK_LBRACE, "Expect '{' before switch cases.");

    int end_jumps[256];
    int jump_count = 0;
    bool has_case = FALSE;
    bool has_default = FALSE;

    while (!_check(parser, TK_RBRACE) && !_is_atEnd(parser))
    {
        if (has_default)
            p_error("Default switch case '_' must be the last case.",
                    _peek(parser).line, _peek(parser).column);

        has_case = TRUE;

        if (_is_defaultSwitchCase(parser))
        {
            has_default = TRUE;
            _next(parser); // _
            _consume(parser, TK_COLON, "Expect ':' after switch default case.");
            if (subject_on_stack)
            {
                emit(parser->comp, OP_POP);
                subject_on_stack = FALSE;
            }
            _switch_branchBody(parser);
            break;
        }

        int colon_index = _find_switchCaseColon(parser);
        if (colon_index < 0)
            p_error("Expect ':' after switch case condition.",
                    _peek(parser).line, _peek(parser).column);

        Token case_tok = _peek(parser);
        Segment _condition = {parser->current, colon_index};
        int constant_index = -1;
        bool fast_equality = subject_on_stack &&
                             _switch_literalEquality(parser, subject_start, subject_end,
                                                     _condition, &constant_index);
        if (!fast_equality)
        {
            if (subject_on_stack)
            {
                emit(parser->comp, OP_POP);
                subject_on_stack = FALSE;
            }
            _compile_segmentExpr(parser, _condition, "Invalid switch case condition.");
        }
        parser->current = colon_index;
        _consume(parser, TK_COLON, "Expect ':' after switch case condition.");

        set_pos(parser, case_tok);
        int next_case_jump;
        if (fast_equality)
            next_case_jump = emit_16uX2(parser->comp, OP_SWITCH_COMPARE, "", constant_index, 0);
        else
            next_case_jump = emit_16u(parser->comp, OP_JUMP_IF_FALSE, "", 0);

        _switch_branchBody(parser);
        if (jump_count >= 256)
            p_error("Too many switch cases.", case_tok.line, case_tok.column);

        end_jumps[jump_count++] = emit_16u(parser->comp, OP_JUMP, "", 0);
        if (fast_equality)
            patch_switchCompare(parser->comp, next_case_jump);
        else
            patch_jump(parser->comp, next_case_jump);
    }

    if (!has_case)
        p_error("Switch statement requires at least one case.",
                switch_tok.line, switch_tok.column);

    _consume(parser, TK_RBRACE, "Expect '}' after switch cases.");

    if (subject_on_stack)
        emit(parser->comp, OP_POP);

    for (int i = 0; i < jump_count; i++)
        patch_jump(parser->comp, end_jumps[i]);

    parser->is_return = FALSE;
}

static void _while_stmt(Parser *parser)
{
    int jump = code_size(parser->comp);

    Token cond_start = _peek(parser);

    _condition(parser);

    set_pos(parser, cond_start);

    // Emit a conditional jump instruction to exit the loop if the condition is false
    int address = emit_16u(parser->comp, OP_JUMP_IF_FALSE, "", 0);

    push_loop(parser->comp, jump, FALSE);

    if (_match(parser, TK_LBRACE))
        _block(parser);
    else
    {
        _statement(parser);
        parser->is_return = FALSE;
    }

    pop_loop(parser->comp, jump);
    patch_jump(parser->comp, address);
}

static void _for_stmt(Parser *parser)
{
    bool has_parens = _match(parser, TK_LPAREN);

    Token first = _consume(parser, TK_ID, "Invalid for-loop left-hand side. Expect identifier.");
    Token second = {0};
    bool has_pair = FALSE;

    if (_match(parser, TK_COMMA))
    {
        has_pair = TRUE;
        second = _consume(parser, TK_ID, "Expect identifier after ',' in for-loop left-hand side.");
    }

    _consume(parser, TK_IN, "Expect 'in' keyword after loop variable.");

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

    Token cond_tok = _peek(parser);
    _cond_expr(parser);

    if (has_parens)
        _consume(parser, TK_RPAREN, "Expect ')' after iterable expression.");

    set_pos(parser, cond_tok); // associate with iterable expression
    emit(parser->comp, OP_PUSH_ITER);

    set_pos(parser, first); // mark the loop start
    int address = emit_16u(parser->comp, OP_LOOP, "", has_pair ? OP_LOOP_TARGET_PAIR_FLAG : 0);

    if (has_pair)
    {
        _store_pairFromTop(parser, token_value(first), token_value(second));
    }
    else
    {
        store_variable(parser->comp, token_value(first));
    }
    push_loop(parser->comp, address - 2, TRUE);

    if (_match(parser, TK_LBRACE))
    {
        while (!_check(parser, TK_RBRACE) && !_is_atEnd(parser))
            _declaration(parser);

        _consume(parser, TK_RBRACE, "Expect '}' after block.");
    }
    else
    {
        _statement(parser);
        parser->is_return = FALSE;
    }

    pop_scope(parser->comp);
    pop_loop(parser->comp, address - 2);
    patch_jumpWithFlags(parser->comp, address, has_pair ? OP_LOOP_TARGET_PAIR_FLAG : 0);
}

static void _break_stmt(Parser *parser)
{
    Token tok = _previous(parser); // 'break' token
    set_pos(parser, tok);

    if (!in_loop(parser->comp))
        p_errorf(tok.line, tok.column, "'break' used outside of a loop");

    bool for_loop = is_forLoop(parser->comp);
    if (for_loop)
        emit(parser->comp, OP_POP_ITER);

    emit_pop(parser->comp, loop_depth(parser->comp) + (for_loop ? 0 : 1));
    push_break(parser->comp, emit_jump(parser->comp, 0));

    // Mark this point as a return-like exit to check for unreachable code
    parser->is_return = TRUE;

    if (need_delimiter(parser))
        p_error("Expected delimiter or newline after 'break'.", tok.line, tok.column);
}

static void _continue_stmt(Parser *parser)
{
    Token tok = _previous(parser); // 'continue' token
    set_pos(parser, tok);

    if (!in_loop(parser->comp))
        p_errorf(tok.line, tok.column, "'continue' used outside of a loop");

    int address = get_continue(parser->comp);
    bool for_loop = is_forLoop(parser->comp);
    emit_pop(parser->comp, loop_depth(parser->comp) + (for_loop ? 0 : 1));
    emit_jump(parser->comp, address - code_size(parser->comp));

    parser->is_return = TRUE;

    if (need_delimiter(parser))
        p_error("Expected delemiter or newline after 'continue'.", tok.line, tok.column);
}

static void _return_stmt(Parser *parser)
{
    Token tok = _previous(parser); // 'return' token
    set_pos(parser, tok);
    bool emitted_return = FALSE;

    if (_match(parser, TK_SEMICOLON) || _is_lineBreak(parser))
    {
        _emit_nilReturn(parser->comp);
        emitted_return = TRUE;
    }
    else
        _expr(parser); // return with value

    if (!emitted_return)
        emit(parser->comp, OP_RETURN);
    parser->is_return = TRUE;

    if (need_delimiter(parser))
        p_error("Expected delemiter or newline after return.", tok.line, tok.column);
}

static void _expr_state(Parser *parser)
{

    Token token = _peek(parser);
    int start_line = token.line;
    bool prev_lookUp, _is_assign = FALSE;
    int current = parser->current;

    if (token.type == TK_ID)
    {
        Token op_token = parser->tokens[current + 1];
        Token after = parser->tokens[current + 2];
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
            _skip(parser, 2);

            if (need_delimiter(parser))
                p_error("Expected delemiter between statements.", _peek(parser).line, _peek(parser).column);
            return;
        }
    }

    if (token.type == TK_LPAREN)
    {
        prev_lookUp = look_up(parser->comp, TRUE);
        _primary(parser);
        look_up(parser->comp, prev_lookUp);
        parser->current = current;
    }

    current = parser->current;

    prev_lookUp = look_up(parser->comp, TRUE);

    _cond_expr(parser);
    token = _peek(parser);
    if (token.line == start_line && token.type >= TK_ASSIGN && token.type <= TK_MOD_ASSIGN)
        _is_assign = TRUE;
    look_up(parser->comp, prev_lookUp);

    parser->current = current;

    _expr(parser);

    // The assignment expression is handled separately
    if (!_is_assign)
    {
        if (parser->comp->is_repl)
            emit(parser->comp, OP_PRINT);
        else
            emit(parser->comp, OP_POP);
    }

    // Check for statement separation
    if (need_delimiter(parser))
        p_error("Expected delemiter between statements.", _peek(parser).line, _peek(parser).column);
}

static void _expr(Parser *parser)
{
    _assignment(parser, FALSE);
}

static Assignment *_init_assign(int left, int right, PiTokenType op)
{
    Assignment *assign = malloc(sizeof(Assignment));

    assign->left = left;
    assign->right = right;
    assign->op = op;

    return assign;
}

static void _assignment(Parser *parser, bool emit_load)
{
    PiTokenType op;
    PiStack *assigns = stack_create(sizeof(Assignment));
    int left = parser->current, right;
    bool prev_lookUp = look_up(parser->comp, TRUE);

    // First pass: collect assignments in the stack without emitting bytecode
    // This is done to handle the case where there are multiple assignments in
    // a single expression, e.g. "a = b = c = d = 0".
    _cond_expr(parser);
    while (_match_n(parser, 9, TK_ASSIGN, TK_PLUS_ASSIGN, TK_MINUS_ASSIGN, TK_DIV_ASSIGN, TK_MULT_ASSIGN,
                    TK_MOD_ASSIGN, TK_BITOR_ASSIGN, TK_XOR_ASSIGN, TK_BITAND_ASSIGN))
    {

        op = _previous(parser).type;

        right = parser->current;

        _stack_push(assigns, _init_assign(left, right, op));

        _pipeline_expr(parser);
        left = right;
    }

    look_up(parser->comp, prev_lookUp);

    if (_stack_isEmpty(assigns))
    {
        parser->current = left;
        _pipeline_expr(parser); // Re-evaluate as a non-assignment expression
    }
    else
    {
        int current = parser->current;
        Assignment *assign;

        // Second pass: pop each assignment and generate bytecode
        while (!_stack_isEmpty(assigns))
        {

            assign = _stack_pop(assigns);

            op = assign->op;
            left = assign->left;
            right = assign->right;

            Token lhs = parser->tokens[left];

            if (parser->tokens[left].type != TK_ID)
                p_error("Invalid assignment target", parser->tokens[left].line, parser->tokens[left].column);

            // Sync the runtime error position to LHS token
            set_pos(parser, lhs);

            if (op != TK_ASSIGN)
            {
                // Load LHS for compound assignments
                parser->current = left;
                _cond_expr(parser);
            }

            parser->current = right;
            char *prev_fun = parser->fun_name;
            char *prev_obj = parser->object_name;

            if (op == TK_ASSIGN && _is_functionLiteral(parser, right))
                parser->fun_name = token_value(lhs);
            if (op == TK_ASSIGN && _is_objectLiteral(parser, right))
                parser->object_name = token_value(lhs);

            _pipeline_expr(parser);
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
            parser->is_store = TRUE;
            _cond_expr(parser);

            // free(assign);
        }

        if (emit_load)
        {
            parser->current = left;
            _cond_expr(parser);
        }

        parser->current = current;
    }
}

static bool _is_pipelineBoundary(Parser *parser, int index, int start,
                                 int paren_depth, int bracket_depth, int brace_depth)
{
    if (paren_depth != 0 || bracket_depth != 0 || brace_depth != 0)
        return FALSE;

    Token token = parser->tokens[index];
    if (token.type == TK_PIPELINE)
        return TRUE;

    if (index > start && token.line > parser->tokens[index - 1].line)
        return TRUE;

    switch (token.type)
    {
    case TK_EOF:
    case TK_SEMICOLON:
    case TK_COMMA:
    case TK_COLON:
    case TK_RPAREN:
    case TK_RBRACKET:
    case TK_RBRACE:
        return TRUE;
    default:
        return FALSE;
    }
}

static int _find_pipelineStageEnd(Parser *parser, int start)
{
    int paren_depth = 0;
    int bracket_depth = 0;
    int brace_depth = 0;

    int index = start;
    for (; parser->tokens[index].type != TK_EOF; index++)
    {
        if (_is_pipelineBoundary(parser, index, start, paren_depth, bracket_depth, brace_depth))
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

static int _find_pipelineCallParen(Parser *parser, int start, int end)
{
    if (end <= start || parser->tokens[end - 1].type != TK_RPAREN)
        return -1;

    for (int index = start; index < end; index++)
        if (parser->tokens[index].type == TK_LPAREN && parser->tokens[index].close_at == end - 1)
            return index;

    return -1;
}

static int _split_pipelineArgs(Parser *parser, int start, int end,
                               Segment *args, int max_args)
{
    int count = 0;
    int arg_start = start;
    int paren_depth = 0;
    int bracket_depth = 0;
    int brace_depth = 0;

    for (int index = start; index <= end; index++)
    {
        bool at_end = index == end;
        Token token = at_end ? parser->tokens[end] : parser->tokens[index];

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

            args[count++] = (Segment){arg_start, index};
            arg_start = index + 1;
        }
    }

    return count;
}

static void _parse_pipelineStage(Parser *parser, PipelineStage *stage)
{
    int start = parser->current;
    int end = _find_pipelineStageEnd(parser, start);

    if (start >= end)
        p_error("Expect pipeline stage after '=>'.",
                parser->tokens[start].line, parser->tokens[start].column);

    int call_paren = _find_pipelineCallParen(parser, start, end);
    if (call_paren >= 0)
    {
        stage->callee = (Segment){start, call_paren};
        stage->arg_count = _split_pipelineArgs(parser, call_paren + 1, end - 1,
                                               stage->args, MAX_PIPELINE_ARGS);
    }
    else
    {
        stage->callee = (Segment){start, end};
        stage->arg_count = 0;
    }

    if (stage->callee.start >= stage->callee.end)
        p_error("Expect callable pipeline stage.",
                parser->tokens[start].line, parser->tokens[start].column);

    parser->current = end;
}

static void _emit_pipelineNested(Parser *parser, Pipeline *pipe, int stage_index)
{
    PipelineStage *stage = &pipe->stages[stage_index];

    _compile_segmentExpr(parser, stage->callee, "Invalid pipeline stage callable.");

    if (stage_index == 0)
        _compile_segmentExpr(parser, pipe->input, "Invalid pipeline input expression.");
    else
        _emit_pipelineNested(parser, pipe, stage_index - 1);

    for (int i = 0; i < stage->arg_count; i++)
        _compile_segmentExpr(parser, stage->args[i], "Invalid pipeline stage argument.");

    Token callee = parser->tokens[stage->callee.start];
    set_pos(parser, stage->op);
    emit_8u(parser->comp, OP_CALL_FUNCTION, token_value(callee), (uint8_t)(stage->arg_count + 1));
}

static void _pipeline_expr(Parser *parser)
{
    int start = parser->current;
    bool prev_lookUp = look_up(parser->comp, TRUE);

    _cond_expr(parser);
    bool has_pipeline = _check(parser, TK_PIPELINE);
    int first_pipeline = parser->current;

    look_up(parser->comp, prev_lookUp);
    parser->current = start;

    if (!has_pipeline)
    {
        _cond_expr(parser);
        return;
    }

    Pipeline pipe = {0};
    pipe.input = (Segment){start, first_pipeline};
    parser->current = first_pipeline;

    while (_match(parser, TK_PIPELINE))
    {
        if (pipe.stage_count >= MAX_PIPELINE_STAGES)
            p_error("Too many pipeline stages.",
                    _previous(parser).line, _previous(parser).column);

        PipelineStage *stage = &pipe.stages[pipe.stage_count++];
        stage->op = _previous(parser);
        _parse_pipelineStage(parser, stage);
    }

    _emit_pipelineNested(parser, &pipe, pipe.stage_count - 1);
}

static void _cond_expr(Parser *parser)
{

    _or_expr(parser);

    if (_match(parser, TK_QUESTION))
    {
        int then_jump = emit_16u(parser->comp, OP_JUMP_IF_FALSE, "", 0);

        // Sync current token for better runtime error info
        set_pos(parser, _peek(parser));

        _cond_expr(parser);

        _consume(parser, TK_COLON, "Expect ':' after '?'");
        int else_jump = emit_16u(parser->comp, OP_JUMP, "", 0);

        patch_jump(parser->comp, then_jump);

        _cond_expr(parser);

        patch_jump(parser->comp, else_jump);
    }
}

static void _or_expr(Parser *parser)
{
    _and_expr(parser);
    while (_match(parser, TK_OR))
    {
        Token op_token = _previous(parser);
        _and_expr(parser);
        set_pos(parser, op_token);
        emit_8u(parser->comp, OP_BINARY, bin_ops[6], 6);
    }
}

static void _and_expr(Parser *parser)
{
    _in_expr(parser);
    while (_match(parser, TK_AND))
    {
        Token op_token = _previous(parser);
        _in_expr(parser);
        set_pos(parser, op_token);
        emit_8u(parser->comp, OP_BINARY, bin_ops[5], 5);
    }
}

static void _in_expr(Parser *parser)
{
    _range_expr(parser);
    while (_match(parser, TK_IN))
    {
        Token op_token = _previous(parser);
        _range_expr(parser);
        set_pos(parser, op_token);
        emit_8u(parser->comp, OP_COMPARE, comp_ops[6], 6);
    }
}

static void _range_expr(Parser *parser)
{
    _bitOr_expr(parser);
    if (_match(parser, TK_DBDOTS))
    {
        Token op_token = _previous(parser);
        _bitOr_expr(parser);
        if (_match(parser, TK_COLON))
            _expr(parser);
        else
            emit(parser->comp, OP_PUSH_NIL);
        set_pos(parser, op_token);
        emit(parser->comp, OP_PUSH_RANGE);
    }
}

static void _bitOr_expr(Parser *parser)
{
    _xor_expr(parser);
    while (_match(parser, TK_BITOR))
    {
        Token op_token = _previous(parser);
        _xor_expr(parser);
        set_pos(parser, op_token);
        emit_8u(parser->comp, OP_BINARY, bin_ops[9], 9);
    }
}

static void _xor_expr(Parser *parser)
{
    _bitAnd_expr(parser);
    while (_match(parser, TK_XOR))
    {
        Token op_token = _previous(parser);
        _bitAnd_expr(parser);
        set_pos(parser, op_token);
        emit_8u(parser->comp, OP_BINARY, bin_ops[10], 10);
    }
}

static void _bitAnd_expr(Parser *parser)
{
    _shift_expr(parser);
    while (_match(parser, TK_BITAND))
    {
        Token op_token = _previous(parser);
        _shift_expr(parser);
        set_pos(parser, op_token);
        emit_8u(parser->comp, OP_BINARY, bin_ops[8], 8);
    }
}

static void _shift_expr(Parser *parser)
{
    _equality_expr(parser);

    while (_match_n(parser, 3, TK_LSHIFT, TK_RSHIFT, TK_URSHIFT))
    {
        PiTokenType op = _previous(parser).type;
        Token op_token = _previous(parser);
        _equality_expr(parser);
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

static void _equality_expr(Parser *parser)
{
    _compare_expr(parser);
    while (_match_n(parser, 3, TK_NOT_EQUAL, TK_EQUAL, TK_IS))
    {
        PiTokenType op = _previous(parser).type;
        Token op_token = _previous(parser);
        _compare_expr(parser);
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

static void _compare_expr(Parser *parser)
{
    _add_expr(parser);

    int last_value_pos = -1;
    int comparison_count = 0;

    while (_match_n(parser, 6, TK_EQUAL, TK_NOT_EQUAL, TK_GREATER,
                    TK_LESS, TK_GREATER_EQUAL, TK_LESS_EQUAL))
    {
        PiTokenType op = _previous(parser).type;
        Token op_token = _previous(parser);

        if (last_value_pos != -1)
        {
            // Rewind and reparse the previous right-hand side (e.g., `b`)
            parser->current = last_value_pos;
            _add_expr(parser); // push `b` again
            _next(parser);     // advance past the operator
        }

        // Save position before parsing the next expression (e.g., `c`)
        last_value_pos = parser->current;
        _add_expr(parser); // parse right-hand side
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

static void _add_expr(Parser *parser)
{
    _dot_expr(parser);
    while (_match_n(parser, 2, TK_PLUS, TK_MINUS))
    {

        Token op = _previous(parser);
        _dot_expr(parser);
        set_pos(parser, op);
        if (op.type == TK_PLUS)
            emit_8u(parser->comp, OP_BINARY, bin_ops[0], 0);
        else
            emit_8u(parser->comp, OP_BINARY, bin_ops[1], 1);
    }
}

static void _dot_expr(Parser *parser)
{
    _mult_expr(parser);
    while (_match(parser, TK_DOT_PROD))
    {
        Token op = _previous(parser);
        _mult_expr(parser);
        set_pos(parser, op);
        emit_8u(parser->comp, OP_BINARY, bin_ops[14], 14);
    }
}

static void _mult_expr(Parser *parser)
{
    _exp_expr(parser);
    while (_match_n(parser, 3, TK_MULT, TK_DIV, TK_MOD))
    {
        Token op = _previous(parser);
        _exp_expr(parser);
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

static void _exp_expr(Parser *parser)
{
    _unary_expr(parser);
    while (_match(parser, TK_POWER))
    {
        Token op = _previous(parser);
        _exp_expr(parser);
        set_pos(parser, op);
        emit_8u(parser->comp, OP_BINARY, bin_ops[7], 7);
    }
}

static void _unary_expr(Parser *parser)
{
    PiTokenType op;
    int current;

    if (_match_n(parser, 8, TK_PLUS, TK_MINUS, TK_NOT, TK_BITNEG, TK_HASH, TK_INCR, TK_DECR, TK_TYPEOF))
    {
        op = _previous(parser).type;
        Token op_token = _previous(parser);

        // Handle negative number literals directly
        if (op == TK_MINUS && _peek(parser).type == TK_NUM)
        {
            parser->tokens[parser->current].is_negative = TRUE;
            _member_expr(parser);
            return;
        }

        if (op == TK_INCR || op == TK_DECR)
        {
            current = parser->current;
            _member_expr(parser);
            set_pos(parser, op_token);

            Token target = _previous(parser);

            if (target.type == TK_NUM || target.type == TK_STR || target.type == TK_TRUE ||
                target.type == TK_FALSE || target.type == TK_NIL)
                p_error("Increment/Decrement operations cannot be applied to calls or literals.",
                        target.line, target.column);

            int type = (op == TK_INCR) ? 5 : 6;
            emit_8u(parser->comp, OP_UNARY, unary_ops[type], type);

            // DUP: one copy to store, one to leave as expression result
            emit(parser->comp, OP_DUP_TOP);

            parser->current = current;
            parser->is_store = TRUE;
            parser->force_store = TRUE;
            _member_expr(parser);
            parser->is_store = FALSE;
            parser->force_store = FALSE;
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
            _unary_expr(parser);
            set_pos(parser, op_token);
            if (type != -1)
                emit_8u(parser->comp, OP_UNARY, unary_ops[type], type);
        }
    }
    else
    {
        current = parser->current;
        _member_expr(parser);
        Token operand = _previous(parser);

        // Handle post-increment / post-decrement
        if (_match_n(parser, 2, TK_INCR, TK_DECR))
        {
            op = _previous(parser).type;
            Token op_token = _previous(parser);

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
                parser->is_store = TRUE;
                _member_expr(parser);
                _advance(parser);

                parser->is_store = FALSE;
            }
        }
    }
}

static bool _slice_expr(Parser *parser)
{
    int index;
    bool is_slice = FALSE;
    Token token = _peek(parser);

    if (_check(parser, TK_COLON))
    {
        // Missing start: emit INFINITY so get_slice's isinf() check fires.
        emit_16u(parser->comp, OP_LOAD_CONST, "inf", 1);
        is_slice = TRUE;
        _next(parser); // consume the leading ':'
    }
    else
        _cond_expr(parser);

    // FIX: flip operands so match() is only called when is_slice is false
    if (is_slice || _match(parser, TK_COLON))
    {
        is_slice = TRUE;
        token = _previous(parser);

        if (!_check(parser, TK_RBRACKET) && !_check(parser, TK_COLON) && !_check(parser, TK_COMMA))
            _cond_expr(parser);
        else
            emit_16u(parser->comp, OP_LOAD_CONST, "inf", 1); // missing end: INFINITY

        if (_match(parser, TK_COLON))
        {
            if (!_check(parser, TK_RBRACKET) && !_check(parser, TK_COMMA))
                _cond_expr(parser);
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

static void _register_instanceField(Parser *parser, char *name)
{
    if (!parser->instance_field_names || ht_has(parser->instance_field_names, name))
        return;

    int count = LIST_SIZE(parser->instance_fields);
    if (count >= UINT16_MAX)
        p_error("Class has too many instance fields.", _peek(parser).line, _peek(parser).column);
    uint16_t index = (uint16_t)count;
    ht_put(parser->instance_field_names, name, &index);
    _list_add(parser->instance_fields, &name);
}

static void _member_expr(Parser *parser)
{
    _primary(parser);
    bool direct_access = TRUE;

    while (TRUE)
    {
        Token token = _previous(parser);

        if (token.line < _peek(parser).line &&
            !_has_accessContinuation(parser, token))
            break;

        set_pos(parser, token);
        if (_match(parser, TK_DOT))
        {
            // Handle property access using dot notation
            Token name = _consume(parser, TK_ID, "Expect property name after '.'");

            bool super_constructor = token.type == TK_SUPER ||
                                     (parser->current >= 3 &&
                                      parser->tokens[parser->current - 3].type == TK_SUPER);
            if (super_constructor && strcmp(token_value(name), "constructor") == 0)
                p_error("Call the superclass constructor with super(...), not super.constructor(...).",
                        name.line, name.column);

            bool assign = _is_memberAssignment(parser, name);
            char *member_name = token_value(name);
            if (assign && direct_access && parser->instance_field_names &&
                token.type == TK_ID && strcmp(tk_string(token), "this") == 0)
                _register_instanceField(parser, member_name);

            int index = store_tokenConst(parser->comp, name);

            if (assign)
                emit_16u(parser->comp, OP_SET_MEMBER, token_value(name), index);
            else
                emit_16u(parser->comp, OP_GET_MEMBER, token_value(name), index);

            direct_access = FALSE;
        }

        // Handle property access using bracket notation and slicing for lists and tensors
        else if (_match(parser, TK_LBRACKET))
        {
            _slice_expr(parser);

            if (_check(parser, TK_COMMA))
            {
                /*  Tensor indexing: tensor[i, j:k, m, ...]  */
                int ndim = 1;
                while (_match(parser, TK_COMMA))
                {
                    if (_check(parser, TK_RBRACKET))
                        break; /* trailing comma */

                    if (ndim >= MAX_TENSOR_DIMS)
                        p_error("Too many tensor dimensions",
                                _peek(parser).line, _peek(parser).column);

                    _slice_expr(parser); /* return value discarded, VM checks at runtime */
                    ndim++;
                }

                // Check for chained access before deciding whether this is an assignment to a tensor element or just an access
                _consume(parser, TK_RBRACKET, "Expect ']' after tensor index");
                bool assign = _is_memberAssignment(parser, _previous(parser));

                /* VM pops ndim values and checks each at runtime for slice vs index */
                emit_8u(parser->comp, assign ? OP_TENSOR_SET : OP_TENSOR_GET, "", (uint8_t)ndim);
            }
            else
            {
                /*  List / 1-D indexing: list[i] or list[a:b:c]  */
                _consume(parser, TK_RBRACKET, "Expect ']' after index");
                // Always emit GET_ITEM or SET_ITEM - the slice object is already on the stack
                bool assign = _is_memberAssignment(parser, _previous(parser));
                emit(parser->comp, assign ? OP_SET_ITEM : OP_GET_ITEM);
            }
        }

        // handle function call
        else if (_match(parser, TK_LPAREN))
        {
            int args = 0;
            int named = 0;
            bool saw_named = FALSE;
            bool saw_spread = _call_hasSpreadArgs(parser);

            if (token.type == TK_SUPER)
            {
                int ctor_index = store_stringConst(parser->comp, "constructor");
                emit_16u(parser->comp, OP_GET_MEMBER, "constructor", ctor_index);
            }

            if (!_check(parser, TK_RPAREN))
            {
                int spread_source_count = 0;
                int pending_args = 0;
                bool spread_args_finalized = FALSE;
                if (saw_spread)
                    emit_16u(parser->comp, OP_PUSH_LIST, "", 0);

                do
                {
                    if (_match(parser, TK_ELLIPSIS))
                    {
                        if (saw_named)
                        {
                            Token err = _previous(parser);
                            p_errorf(err.line, err.column,
                                     "Positional arguments must come before named arguments.");
                        }

                        if (pending_args > 0)
                        {
                            emit_16u(parser->comp, OP_PUSH_LIST, "", pending_args);
                            pending_args = 0;
                            spread_source_count++;
                        }
                        _expr(parser);
                        spread_source_count++;
                    }
                    else if (_check(parser, TK_ID) && _peek_next(parser).type == TK_ASSIGN)
                    {
                        Token key_tok = _consume(parser, TK_ID, "Expect identifier for named argument.");
                        Token eq_tok = _consume(parser, TK_ASSIGN, "Expect '=' after named argument.");
                        (void)eq_tok;

                        if (!saw_named)
                            saw_named = TRUE;

                        if (saw_spread && !spread_args_finalized)
                        {
                            if (pending_args > 0)
                            {
                                emit_16u(parser->comp, OP_PUSH_LIST, "", pending_args);
                                pending_args = 0;
                                spread_source_count++;
                            }
                            if (spread_source_count > 255)
                                p_error("Spread argument list cannot contain more than 255 sources.", key_tok.line, key_tok.column);
                            emit_8u(parser->comp, OP_LIST_EXTEND, "", spread_source_count);
                            spread_args_finalized = TRUE;
                        }

                        if (named >= 256)
                            p_errorf(key_tok.line, key_tok.column, "Too many named arguments.");

                        char *key = token_value(key_tok);
                        named++;

                        _expr(parser); // parse value

                        int index = store_stringConst(parser->comp, key);
                        emit_16u(parser->comp, OP_LOAD_CONST, key, index);
                        free(key);
                    }
                    else
                    {
                        if (saw_named)
                        {
                            Token err = _peek(parser);
                            p_errorf(err.line, err.column,
                                     "Positional arguments must come before named arguments.");
                        }
                        _expr(parser);
                        if (saw_spread)
                            pending_args++;
                        else
                            args++;
                    }
                } while (_match(parser, TK_COMMA));

                if (saw_spread && !spread_args_finalized)
                {
                    if (pending_args > 0)
                    {
                        emit_16u(parser->comp, OP_PUSH_LIST, "", pending_args);
                        pending_args = 0;
                        spread_source_count++;
                    }
                    if (spread_source_count > 255)
                        p_error("Spread argument list cannot contain more than 255 sources.", _peek(parser).line, _peek(parser).column);
                    emit_8u(parser->comp, OP_LIST_EXTEND, "", spread_source_count);
                }
            }
            _consume(parser, TK_RPAREN, "Expect ')' after function call");
            set_pos(parser, token);
            char *name = strcmp(token_value(token), ")") == 0 ? "<FUN>" : token_value(token);
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

static void _arrow_func(Parser *parser)
{
    if (_match(parser, TK_LBRACE))
    {
        Token token = _previous(parser);

        if (_check(parser, TK_RBRACE))
        {
            set_pos(parser, token);

            _emit_defaultReturn(parser->comp);
            parser->is_return = TRUE;
        }
        else
        {
            while (!_check(parser, TK_RBRACE) && !_is_atEnd(parser))
                _declaration(parser);
        }

        if (!parser->is_return)
        {
            Token token = _peek(parser);

            set_pos(parser, token);

            _emit_defaultReturn(parser->comp);
            parser->is_return = FALSE;
        }

        Token rbrace = _consume(parser, TK_RBRACE, "Expect '}' after function body.");
        set_pos(parser, rbrace);
    }
    else
    {
        Token token = _peek(parser);
        _expr(parser);

        set_pos(parser, token);
        emit(parser->comp, OP_RETURN);
    }
}
static void _primary(Parser *parser)
{
    if (_match_n(parser, 7, TK_NUM, TK_STR, TK_TRUE, TK_FALSE, TK_NIL, TK_INF, TK_NAN))
    {
        Token token = _previous(parser);
        set_pos(parser, token);

        if (token.type == TK_NAN)
            emit_16u(parser->comp, OP_LOAD_CONST, "NAN", 0);
        else if (token.type == TK_INF)
            emit_16u(parser->comp, OP_LOAD_CONST, "INF", 0);
        else
        {
            int index = store_tokenConst(parser->comp, token);
            emit_16u(parser->comp, OP_LOAD_CONST, token_value(token), index);
        }
    }
    else if (_match(parser, TK_LPAREN))
    {
        int _current = parser->current;
        set_pos(parser, _previous(parser));

        if (is_lookUp(parser->comp))
        {
            // Lookahead mode: scan past the parenthesized expression without emitting
            // Empty parens () are valid in lookahead - just skip past them
            if (_check(parser, TK_RPAREN))
            {
                _next(parser);
                if (_match(parser, TK_RARROW))
                {
                    if (_match(parser, TK_LBRACE))
                    {
                        int depth = 1;
                        while (depth > 0 && !_is_atEnd(parser))
                        {
                            if (_check(parser, TK_RBRACE))
                                depth--;
                            else if (_check(parser, TK_LBRACE))
                                depth++;
                            if (depth == 0)
                                break;
                            _next(parser);
                        }
                        if (depth != 0)
                            p_error("Unmatched '{' in arrow function.", _peek(parser).line, _peek(parser).column);
                        _consume(parser, TK_RBRACE, "Expect '}' after arrow function.");
                    }
                    else
                        _expr(parser);
                }
                return;
            }

            // Scan past the contents of the parens
            int depth = 1;
            while (depth > 0 && !_is_atEnd(parser))
            {
                if (_check(parser, TK_RPAREN))
                    depth--;
                else if (_check(parser, TK_LPAREN))
                    depth++;
                _next(parser);
            }

            if (depth != 0)
                p_error("Unmatched '(' in grouping expression.", _peek(parser).line, _peek(parser).column);

            if (_match(parser, TK_RARROW))
            {
                if (_match(parser, TK_LBRACE))
                {
                    depth = 1;
                    while (depth > 0 && !_is_atEnd(parser))
                    {
                        if (_check(parser, TK_RBRACE))
                            depth--;
                        else if (_check(parser, TK_LBRACE))
                            depth++;
                        if (depth == 0)
                            break;
                        _next(parser);
                    }
                    if (depth != 0)
                        p_error("Unmatched '{' in arrow function.", _peek(parser).line, _peek(parser).column);
                    _consume(parser, TK_RBRACE, "Expect '}' after arrow function.");
                }
                else
                    _expr(parser);
            }
        }
        else
        {
            // Empty tuple: ()
            if (_check(parser, TK_RPAREN))
            {
                _consume(parser, TK_RPAREN, "Expect ')' after empty tuple.");

                // Check for arrow function: () -> ...
                if (_match(parser, TK_RARROW))
                {
                    bool method_value = parser->object_member;
                    parser->object_member = FALSE;
                    push_function(parser->comp, _get_pendingFunctionName(parser));
                    List *empty_params = list_create(sizeof(String));
                    parser->comp->current->param_names = empty_params;

                    if (method_value)
                        add_local(parser->comp, "this");
                    add_local(parser->comp, "args");
                    add_local(parser->comp, "kw_args");

                    _arrow_func(parser);

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
            while (!_check(parser, TK_RPAREN))
                _next(parser);

            _next(parser);

            if (_match(parser, TK_RARROW))
            {
                //  Arrow function: (params) -> body
                parser->current = _current;
                List *params = _param_list(parser);
                int size = LIST_SIZE(params);
                _consume(parser, TK_RPAREN, "Expect ')' after expression.");
                _consume(parser, TK_RARROW, "Expect '->' after function parameters.");

                bool method_value = parser->object_member;
                parser->object_member = FALSE;
                push_function(parser->comp, _get_pendingFunctionName(parser));
                parser->comp->current->param_names = params;

                if (method_value)
                    add_local(parser->comp, "this");
                for (int i = 0; i < size; i++)
                    add_local(parser->comp, string_get(params, i));
                add_local(parser->comp, "args");
                add_local(parser->comp, "kw_args");

                _arrow_func(parser);

                pop_function(parser->comp, size);
                parser->object_member = method_value;
            }
            else
            {
                //  Grouped expression or tuple literal
                parser->current = _current;

                _cond_expr(parser);

                if (_match(parser, TK_COMMA))
                {
                    // At least one comma means tuple literal.
                    // (expr,)  is a single-element tuple.
                    // (expr, expr, ...) is a multi-element tuple.
                    // A trailing comma after the last element is allowed.
                    int size = 1;
                    double previous_number = 0;
                    bool previous_is_number = _numeric_literalSegment(parser, _current, parser->current - 1,
                                                                      &previous_number);
                    while (!_check(parser, TK_RPAREN) && !_is_atEnd(parser))
                    {
                        if (_match(parser, TK_ELLIPSIS))
                        {
                            Token ellipsis = _previous(parser);
                            if (!previous_is_number)
                                p_error("Fill expansion requires an integer numeric literal before ', ...,'.",
                                        ellipsis.line, ellipsis.column);

                            _consume(parser, TK_COMMA, "Expect ',' after '...' in tuple fill expansion.");
                            double endpoint = _consume_fillEndpoint(parser);
                            size += _emit_literalFill(parser, previous_number, endpoint, ellipsis);
                            previous_number = endpoint;
                            previous_is_number = TRUE;
                            if (!_match(parser, TK_COMMA))
                                break;
                            continue;
                        }

                        int item_start = parser->current;
                        _cond_expr(parser);
                        size++;
                        previous_is_number = _numeric_literalSegment(parser, item_start, parser->current,
                                                                     &previous_number);
                        if (!_match(parser, TK_COMMA))
                            break;
                    }
                    _consume(parser, TK_RPAREN, "Expect ')' after tuple literal.");
                    emit_16u(parser->comp, OP_PUSH_TUPLE, "", size);
                }
                else
                {
                    //  Grouped expression: (expr) -
                    _consume(parser, TK_RPAREN, "Expect ')' after expression.");
                }
            }
        }
    }
    else if (_match(parser, TK_SUPER))
    {
        if (_check(parser, TK_DOT) &&
            parser->tokens[parser->current + 1].type == TK_ID &&
            strcmp(tk_string(parser->tokens[parser->current + 1]), "constructor") == 0)
            p_error("Call the superclass constructor with super(...), not super.constructor(...).",
                    _previous(parser).line, _previous(parser).column);
        set_pos(parser, _previous(parser));
        emit(parser->comp, OP_LOAD_SUPER);
    }
    else if (_match(parser, TK_ID))
    {
        char *name = tk_string(_previous(parser));
        set_pos(parser, _previous(parser));

        // Lookahead: skip arrow function body without emitting
        if (is_lookUp(parser->comp) && _match(parser, TK_RARROW))
        {
            if (_match(parser, TK_LBRACE))
            {
                int depth = 1;
                while (depth > 0 && !_is_atEnd(parser))
                {
                    if (_check(parser, TK_RBRACE))
                        depth--;
                    else if (_check(parser, TK_LBRACE))
                        depth++;
                    if (depth == 0)
                        break;
                    _next(parser);
                }
                if (depth != 0)
                    p_error("Unmatched '{' in arrow function.", _peek(parser).line, _peek(parser).column);
                _consume(parser, TK_RBRACE, "Expect '}' after arrow function.");
            }
            else
                _expr(parser);

            return;
        }

        // Single-param arrow function: name -> body
        if (_match(parser, TK_RARROW))
        {
            emit(parser->comp, OP_PUSH_NIL);

            bool method_value = parser->object_member;
            parser->object_member = FALSE;
            push_function(parser->comp, _get_pendingFunctionName(parser));
            List *single_params = list_create(sizeof(String));
            _list_add(single_params, new_string(name));
            parser->comp->current->param_names = single_params;

            if (method_value)
                add_local(parser->comp, "this");
            add_local(parser->comp, name);
            add_local(parser->comp, "args");
            add_local(parser->comp, "kw_args");

            _arrow_func(parser);

            pop_function(parser->comp, 1);
            parser->object_member = method_value;
        }
        // Walrus operator: name <- expr
        else if (_match(parser, TK_LARROW))
        {
            if (parser->has_walrus)
                p_error("Chained '<-' operators are not allowed",
                        _peek(parser).line, _peek(parser).column);

            parser->has_walrus = TRUE;
            _cond_expr(parser);
            parser->has_walrus = FALSE;

            emit(parser->comp, OP_DUP_TOP);
            store_variable(parser->comp, name);
            return;
        }
        else
        {
            bool is_chained_access = parser->is_store && parser->force_store &&
                                     _peek(parser).line == _previous(parser).line &&
                                     _check_n(parser, 3, TK_DOT, TK_LBRACKET, TK_LPAREN);

            if (is_object(parser->comp) && strcmp(name, "super") == 0)
            {
                emit(parser->comp, OP_LOAD_SUPER);
                return;
            }
            if (!is_chained_access && _is_assign(parser))
                store_variable(parser->comp, name);
            else
                load_variable(parser->comp, name);
        }
    }
    else if (_match(parser, TK_LBRACKET))
    {
        int size = 0;
        set_pos(parser, _previous(parser));

        if (_match(parser, TK_RBRACKET))
            emit_16u(parser->comp, OP_PUSH_LIST, "", 0);

        else if (_list_isComprehension(parser))
            _emit_listComprehension(parser);

        else if (_list_hasSpreadItems(parser))
            _emit_spreadListLiteral(parser);

        else
        {
            do
            {
                if (!_check(parser, TK_RBRACKET))
                {
                    int item_start = parser->current;
                    _cond_expr(parser);
                    size++;
                    double previous_number = 0;
                    bool previous_is_number = _numeric_literalSegment(parser, item_start, parser->current,
                                                                      &previous_number);

                    while (_check(parser, TK_COMMA) && _peek_next(parser).type == TK_ELLIPSIS)
                    {
                        _consume(parser, TK_COMMA, "Expect ',' before '...' in list fill expansion.");
                        _consume(parser, TK_ELLIPSIS, "Expect '...' in list fill expansion.");
                        Token ellipsis = _previous(parser);
                        if (!previous_is_number)
                            p_error("Fill expansion requires an integer numeric literal before ', ...,'.",
                                    ellipsis.line, ellipsis.column);

                        _consume(parser, TK_COMMA, "Expect ',' after '...' in list fill expansion.");
                        double endpoint = _consume_fillEndpoint(parser);
                        size += _emit_literalFill(parser, previous_number, endpoint, ellipsis);
                        previous_number = endpoint;
                        previous_is_number = TRUE;
                    }
                }
                else
                    break; // trailing comma
            } while (_match(parser, TK_COMMA));
            _consume(parser, TK_RBRACKET, "Expect ']' at the end of list literal.");
            emit_16u(parser->comp, OP_PUSH_LIST, "", size);
        }
    }
    else if (_match(parser, TK_LBRACE))
    {
        set_pos(parser, _previous(parser));
        if (is_lookUp(parser->comp))
        {
            int depth = 1;
            while (depth > 0 && !_is_atEnd(parser))
            {
                if (_check(parser, TK_LBRACE))
                    depth++;
                else if (_check(parser, TK_RBRACE))
                    depth--;
                _next(parser);
                if (depth == 0)
                    break;
            }
            if (depth != 0)
                p_error("Unmatched '}' in map.", _peek(parser).line, _peek(parser).column);
            return;
        }

        if (_match(parser, TK_RBRACE))
        {
            push_object(parser->comp);
            pop_object(parser->comp);
            emit_16u(parser->comp, OP_PUSH_MAP, "", 0);
        }
        else if (!_is_mapEntry(parser) && !_map_hasSpreadItems(parser))
        {
            _emit_setLiteral(parser);
        }
        else
        {
            push_object(parser->comp);
            if (_map_hasSpreadItems(parser))
            {
                _emit_spreadMapLiteral(parser);
                pop_object(parser->comp);
            }
            else
            {
                char *key;
                int index = 0;
                int size = 0;
                do
                {
                    if (_match_n(parser, 5, TK_STR, TK_ID, TK_NUM, TK_FALSE, TK_TRUE))
                    {
                        key = tk_string(_previous(parser));
                        index = store_stringConst(parser->comp, key);
                    }
                    else
                    {
                        p_error("Unexpected key expression.", _peek(parser).line, _peek(parser).column);
                    }

                    if (_match(parser, TK_LPAREN))
                    {
                        // Method shorthand: { key(params) { body } }
                        List *params = _param_list(parser);
                        int param_size = LIST_SIZE(params);
                        _consume(parser, TK_RPAREN, "Expect ')' before function body.");
                        _consume(parser, TK_LBRACE, "Expect '{' before function body.");

                        push_function(parser->comp, key);
                        parser->comp->current->param_names = params;

                        if (is_object(parser->comp))
                            add_local(parser->comp, "this");

                        for (int i = 0; i < param_size; i++)
                            add_local(parser->comp, string_get(params, i));
                        add_local(parser->comp, "args");
                        add_local(parser->comp, "kw_args");

                        if (_match(parser, TK_RBRACE))
                        {
                            _emit_defaultReturn(parser->comp);
                        }
                        else
                        {
                            while (!_check(parser, TK_RBRACE) && !_is_atEnd(parser))
                                _declaration(parser);

                            if (!parser->is_return)
                            {
                                _emit_defaultReturn(parser->comp);
                                parser->is_return = FALSE;
                            }
                        }

                        parser->is_return = FALSE;
                        pop_function(parser->comp, param_size);
                        _consume(parser, TK_RBRACE, "Expect '}' after function body.");
                    }
                    else
                    {
                        if (strcmp(key, "constructor") == 0)
                            p_error("Constructor is a reserved keyword.", _peek(parser).line, _peek(parser).column);
                        _consume(parser, TK_COLON, "Expect ':' after object key expression.");

                        bool prev_object_member = parser->object_member;
                        char *prev_fun = parser->fun_name;
                        char *prev_obj = parser->object_name;
                        /* Maps store plain function values; they do not bind `this`. */
                        parser->object_member = FALSE;
                        parser->object_name = NULL;

                        if (_is_functionLiteral(parser, parser->current))
                            parser->fun_name = key;

                        _cond_expr(parser);
                        parser->object_member = prev_object_member;
                        parser->fun_name = prev_fun;
                        parser->object_name = prev_obj;
                    }

                    emit_16u(parser->comp, OP_LOAD_CONST, key, index);
                    free(key);
                    size++;
                } while (_match(parser, TK_COMMA) && !_check(parser, TK_RBRACE));

                _consume(parser, TK_RBRACE, "Expect '}' at the end of map literal.");
                pop_object(parser->comp);
                emit_16u(parser->comp, OP_PUSH_MAP, "", size);
            }
        }
    }
    // Anonymous function expressions: fun(params) { body }
    else if (_match(parser, TK_FUN))
    {
        set_pos(parser, _previous(parser));

        if (is_lookUp(parser->comp))
        {
            int depth = 0;

            _consume(parser, TK_LPAREN, "Expect '(' after function name.");

            depth = 1;
            while (depth > 0 && !_is_atEnd(parser))
            {
                if (_check(parser, TK_LPAREN))
                    depth++;
                else if (_check(parser, TK_RPAREN))
                    depth--;
                _next(parser);
            }
            if (depth != 0)
                p_error("Unmatched '(' in anonymous function.", _peek(parser).line, _peek(parser).column);

            _consume(parser, TK_LBRACE, "Expect '{' before function body.");

            depth = 1;
            while (depth > 0 && !_is_atEnd(parser))
            {
                if (_check(parser, TK_LBRACE))
                    depth++;
                else if (_check(parser, TK_RBRACE))
                    depth--;
                _next(parser);
            }
            if (depth != 0)
                p_error("Unmatched '{' in anonymous function.", _peek(parser).line, _peek(parser).column);

            return;
        }
        Compiler *comp = parser->comp;
        // Function expressions do not have their name set until we parse the parameter list, since the name may be needed for recursion within the function body. So we use a placeholder name for now and set the actual name after parsing the parameters.
        _consume(parser, TK_LPAREN, "Expect '(' after function name.");
        List *params = _param_list(parser);
        int size = LIST_SIZE(params);
        _consume(parser, TK_RPAREN, "Expect ')' before function body.");
        _consume(parser, TK_LBRACE, "Expect '{' before function body.");

        bool method_value = parser->object_member;
        parser->object_member = FALSE;
        push_function(comp, _get_pendingFunctionName(parser));
        comp->current->param_names = params;

        if (method_value)
            add_local(comp, "this");
        for (int i = 0; i < size; i++)
            add_local(comp, string_get(params, i));
        add_local(comp, "args");
        add_local(comp, "kw_args");

        if (_check(parser, TK_RBRACE))
        {
            _emit_defaultReturn(comp);
            parser->is_return = TRUE;
        }
        else
        {
            while (!_check(parser, TK_RBRACE) && !_is_atEnd(parser))
                _declaration(parser);

            if (!parser->is_return)
            {
                _emit_defaultReturn(comp);
                parser->is_return = TRUE;
            }
        }

        pop_function(comp, size);
        parser->is_return = FALSE;
        parser->object_member = method_value;

        _consume(parser, TK_RBRACE, "Expect '}' after function body.");
    }
    else
        p_error("Expect expression.", _previous(parser).line, _previous(parser).column);
}

void free_parser(Parser *parser)
{
    free(parser->tokens);
    free(parser);
}
