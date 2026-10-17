#include "pi_loon.h"
#include "pi_builtin.h"

#include "../pi_object.h"
#include "../pi_table.h"

/* Native LOON parser shared by the c native implementation and the `loon` module. */

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    const char *text;
    size_t pos, line, column;
    LoonError *error;
} Parser;

static void _fail(Parser *p, const char *format, ...)
{
    if (p->error && p->error->message[0] == '\0')
    {
        va_list args;
        p->error->line = p->line;
        p->error->column = p->column;
        va_start(args, format);
        snprintf(p->error->message, sizeof(p->error->message), "loon: ");
        size_t len = strlen(p->error->message);
        vsnprintf(p->error->message + len,
                  sizeof(p->error->message) - len, format, args);
        va_end(args);
    }
}

static char _peek(Parser *p) { return p->text[p->pos]; }

static char _take(Parser *p)
{
    char c = p->text[p->pos++];
    if (c == '\n')
    {
        p->line++;
        p->column = 1;
    }
    else
        p->column++;
    return c;
}

static void _spaces(Parser *p)
{
    while (_peek(p) == ' ' || _peek(p) == '\t' || _peek(p) == '\r')
        _take(p);
}

static LoonValue *_value_new(LoonType type)
{
    LoonValue *value = calloc(1, sizeof(*value));
    if (value)
        value->type = type;
    return value;
}

void loon_free(LoonValue *value)
{
    if (!value)
        return;
    if (value->type == LOON_STRING)
        free(value->as.string);
    else if (value->type == LOON_ARRAY)
    {
        for (size_t i = 0; i < value->as.array.count; i++)
            loon_free(value->as.array.items[i]);
        free(value->as.array.items);
    }
    else if (value->type == LOON_OBJECT)
    {
        for (size_t i = 0; i < value->as.object.count; i++)
        {
            free(value->as.object.members[i].key);
            loon_free(value->as.object.members[i].value);
        }
        free(value->as.object.members);
    }
    free(value);
}

static bool _append_item(LoonValue *array, LoonValue *item)
{
    size_t count = array->as.array.count;
    LoonValue **items = realloc(array->as.array.items, (count + 1) * sizeof(*items));
    if (!items)
        return FALSE;
    array->as.array.items = items;
    items[count] = item;
    array->as.array.count++;
    return TRUE;
}

static bool _add_member(LoonValue *object, char *key, LoonValue *value)
{
    size_t count = object->as.object.count;
    LoonMember *members = realloc(object->as.object.members, (count + 1) * sizeof(*members));
    if (!members)
        return FALSE;
    object->as.object.members = members;
    members[count] = (LoonMember){key, value};
    object->as.object.count++;
    return TRUE;
}

static char *_slice(const char *start, size_t length)
{
    char *result = malloc(length + 1);
    if (!result)
        return NULL;
    memcpy(result, start, length);
    result[length] = '\0';
    return result;
}

static char *_parse_string(Parser *p)
{
    if (_take(p) != '"')
        return NULL;
    char *out = NULL;
    size_t count = 0;
    while (_peek(p) && _peek(p) != '"')
    {
        char c = _take(p);
        if (c == '\\')
        {
            c = _take(p);
            if (c == 'n')
                c = '\n';
            else if (c == 'r')
                c = '\r';
            else if (c == 't')
                c = '\t';
            else if (c != '"' && c != '\\' && c != '/')
            {
                _fail(p, "invalid string escape");
                free(out);
                return NULL;
            }
        }
        if (!c)
        {
            _fail(p, "unterminated string");
            free(out);
            return NULL;
        }
        char *next = realloc(out, count + 2);
        if (!next)
        {
            _fail(p, "out of memory");
            free(out);
            return NULL;
        }
        out = next;
        out[count++] = c;
        out[count] = '\0';
    }
    if (_peek(p) != '"')
    {
        _fail(p, "unterminated string");
        free(out);
        return NULL;
    }
    _take(p);
    return out ? out : _slice("", 0);
}

static LoonValue *_parse_value(Parser *p);
static char *_parse_key(Parser *p);
static LoonValue *_parse_block(Parser *p, size_t indent);
static LoonValue *_parse_BracedObject(Parser *p);

static LoonValue *_parse_InlineArray(Parser *p)
{
    LoonValue *array = _value_new(LOON_ARRAY);
    if (!array)
        return NULL;
    _take(p);
    _spaces(p);
    if (_peek(p) == ']')
    {
        _take(p);
        return array;
    }
    while (_peek(p))
    {
        LoonValue *item = _parse_value(p);
        if (!item || !_append_item(array, item))
        {
            loon_free(item);
            loon_free(array);
            return NULL;
        }
        _spaces(p);
        if (_peek(p) == ']')
        {
            _take(p);
            return array;
        }
        if (_peek(p) != ',')
        {
            _fail(p, "expected ',' or ']'");
            loon_free(array);
            return NULL;
        }
        _take(p);
        _spaces(p);
    }
    _fail(p, "unterminated array");
    loon_free(array);
    return NULL;
}

static LoonValue *_parse_InlineObject(Parser *p)
{
    LoonValue *object = _value_new(LOON_OBJECT);
    if (!object)
        return NULL;
    _take(p);
    _spaces(p);
    if (_peek(p) == '}')
    {
        _take(p);
        return object;
    }
    while (_peek(p))
    {
        char *key = _parse_key(p);
        if (!key)
        {
            loon_free(object);
            return NULL;
        }
        _spaces(p);
        if (_peek(p) != ':')
        {
            _fail(p, "expected ':' after object key");
            free(key);
            loon_free(object);
            return NULL;
        }
        _take(p);
        _spaces(p);
        LoonValue *item = _parse_value(p);
        if (!item || !_add_member(object, key, item))
        {
            free(key);
            loon_free(item);
            loon_free(object);
            return NULL;
        }
        _spaces(p);
        if (_peek(p) == '}')
        {
            _take(p);
            return object;
        }
        if (_peek(p) != ',')
        {
            _fail(p, "expected ',' or '}'");
            loon_free(object);
            return NULL;
        }
        _take(p);
        _spaces(p);
    }
    _fail(p, "unterminated object");
    loon_free(object);
    return NULL;
}

static LoonValue *_parse_value(Parser *p)
{
    _spaces(p);
    char c = _peek(p);
    if (c == '"')
    {
        char *s = _parse_string(p);
        if (!s)
            return NULL;
        LoonValue *v = _value_new(LOON_STRING);
        if (v)
            v->as.string = s;
        else
            free(s);
        return v;
    }
    if (c == '[')
        return _parse_InlineArray(p);
    if (c == '{' && p->text[p->pos + 1] == '\n')
        return _parse_BracedObject(p);
    if (c == '{')
        return _parse_InlineObject(p);
    const char *start = p->text + p->pos;
    while (_peek(p) && !isspace((unsigned char)_peek(p)) && _peek(p) != ',' && _peek(p) != ']' && _peek(p) != '}')
        _take(p);
    size_t length = (size_t)((p->text + p->pos) - start);
    if (length == 0)
    {
        _fail(p, "expected value");
        return NULL;
    }
    LoonValue *v = NULL;
    if (length == 4 && strncmp(start, "null", 4) == 0)
        v = _value_new(LOON_NULL);
    else if (length == 4 && strncmp(start, "true", 4) == 0)
    {
        v = _value_new(LOON_BOOL);
        if (v)
            v->as.boolean = TRUE;
    }
    else if (length == 5 && strncmp(start, "false", 5) == 0)
    {
        v = _value_new(LOON_BOOL);
        if (v)
            v->as.boolean = FALSE;
    }
    else
    {
        char *word = _slice(start, length), *end = NULL;
        double n = word ? strtod(word, &end) : 0;
        if (!word || !end || *end)
        {
            _fail(p, "invalid value '%.*s'", (int)length, start);
            free(word);
            return NULL;
        }
        free(word);
        v = _value_new(LOON_NUMBER);
        if (v)
            v->as.number = n;
    }
    return v;
}

static char *_parse_key(Parser *p)
{
    if (_peek(p) == '"')
        return _parse_string(p);
    const char *start = p->text + p->pos;
    while (isalnum((unsigned char)_peek(p)) || _peek(p) == '_' || _peek(p) == '-')
        _take(p);
    size_t length = (size_t)((p->text + p->pos) - start);
    if (!length)
    {
        _fail(p, "expected object key");
        return NULL;
    }
    return _slice(start, length);
}

static bool _line_indent(Parser *p, size_t *indent)
{
    *indent = 0;
    while (_peek(p) == ' ')
    {
        _take(p);
        (*indent)++;
    }
    if (_peek(p) == '\t')
    {
        _fail(p, "tabs are not allowed for indentation");
        return FALSE;
    }
    return TRUE;
}

static bool _line_end(Parser *p)
{
    _spaces(p);
    if (_peek(p) == '\n')
    {
        _take(p);
        return TRUE;
    }
    return _peek(p) == '\0';
}

static LoonValue *_parse_BlockArray(Parser *p, size_t indent)
{
    LoonValue *array = _value_new(LOON_ARRAY);
    if (!array)
        return NULL;
    _take(p); /* [ */
    if (!_line_end(p))
    {
        _fail(p, "block array '[' must end its line");
        loon_free(array);
        return NULL;
    }
    while (_peek(p))
    {
        if (_peek(p) == '\n')
        {
            _take(p);
            continue;
        }
        size_t current;
        if (!_line_indent(p, &current))
        {
            loon_free(array);
            return NULL;
        }
        if (current + 2 == indent && _peek(p) == ']')
        {
            _take(p);
            if (!_line_end(p))
            {
                _fail(p, "unexpected content after ']'");
                loon_free(array);
                return NULL;
            }
            return array;
        }
        if (current < indent)
        {
            p->pos -= current;
            p->column -= current;
            break;
        }
        if (current != indent)
        {
            _fail(p, "expected indentation of %zu spaces", indent);
            loon_free(array);
            return NULL;
        }
        if (_peek(p) == ']')
        {
            _take(p);
            if (!_line_end(p))
            {
                _fail(p, "unexpected content after ']'");
                loon_free(array);
                return NULL;
            }
            return array;
        }
        LoonValue *item = _parse_value(p);
        if (!item || !_line_end(p) || !_append_item(array, item))
        {
            loon_free(item);
            loon_free(array);
            return NULL;
        }
    }
    _fail(p, "unterminated block array");
    loon_free(array);
    return NULL;
}

static LoonValue *_parse_table(Parser *p, size_t indent)
{
    LoonValue *rows = _value_new(LOON_ARRAY);
    char **columns = NULL;
    size_t count = 0;
    if (!rows)
        return NULL;
    _take(p); /* ( */
    while (_peek(p) && _peek(p) != ')')
    {
        _spaces(p);
        char *column = _parse_key(p);
        if (!column)
            goto failed;
        for (size_t i = 0; i < count; i++)
            if (strcmp(columns[i], column) == 0)
            {
                _fail(p, "duplicate table column '%s'", column);
                free(column);
                goto failed;
            }
        char **next = realloc(columns, (count + 1) * sizeof(*next));
        if (!next)
        {
            free(column);
            goto failed;
        }
        columns = next;
        columns[count++] = column;
        _spaces(p);
        if (_peek(p) == ',')
            _take(p);
        else if (_peek(p) != ')')
        {
            _fail(p, "expected ',' or ')' in table header");
            goto failed;
        }
    }
    if (!count || _peek(p) != ')')
    {
        _fail(p, "table requires one or more columns");
        goto failed;
    }
    _take(p);
    _spaces(p);
    if (_peek(p) != ':')
    {
        _fail(p, "expected ':' after table header");
        goto failed;
    }
    _take(p);
    if (!_line_end(p))
    {
        _fail(p, "unexpected content after table header");
        goto failed;
    }
    while (_peek(p))
    {
        if (_peek(p) == '\n')
        {
            _take(p);
            continue;
        }
        size_t current;
        if (!_line_indent(p, &current))
            goto failed;
        if (current < indent)
        {
            p->pos -= current;
            p->column -= current;
            break;
        }
        if (current != indent)
        {
            _fail(p, "expected indentation of %zu spaces", indent);
            goto failed;
        }
        LoonValue *row = _value_new(LOON_OBJECT);
        if (!row)
            goto failed;
        for (size_t i = 0; i < count; i++)
        {
            LoonValue *cell = _parse_value(p);
            char *key = _slice(columns[i], strlen(columns[i]));
            if (!cell || !key || !_add_member(row, key, cell))
            {
                free(key);
                loon_free(cell);
                loon_free(row);
                goto failed;
            }
            _spaces(p);
            if (i + 1 < count)
            {
                if (_peek(p) != ',')
                {
                    _fail(p, "table row has too few values");
                    loon_free(row);
                    goto failed;
                }
                _take(p);
                _spaces(p);
            }
        }
        if (!_line_end(p))
        {
            _fail(p, "table row has too many values");
            loon_free(row);
            goto failed;
        }
        if (!_append_item(rows, row))
        {
            loon_free(row);
            goto failed;
        }
    }
    for (size_t i = 0; i < count; i++)
        free(columns[i]);
    free(columns);
    return rows;
failed:
    for (size_t i = 0; i < count; i++)
        free(columns[i]);
    free(columns);
    loon_free(rows);
    return NULL;
}

static LoonValue *_parse_BracedObject(Parser *p)
{
    LoonValue *object = _value_new(LOON_OBJECT);
    size_t field_indent = 0;
    if (!object)
        return NULL;
    _take(p);
    if (!_line_end(p))
    {
        _fail(p, "block object '{' must end its line");
        loon_free(object);
        return NULL;
    }
    while (_peek(p))
    {
        if (_peek(p) == '\n')
        {
            _take(p);
            continue;
        }
        size_t current;
        if (!_line_indent(p, &current))
        {
            loon_free(object);
            return NULL;
        }
        if (_peek(p) == '}' && field_indent && current + 2 == field_indent)
        {
            _take(p);
            _spaces(p);
            if (_peek(p) && _peek(p) != '\n')
            {
                _fail(p, "unexpected content after '}'");
                loon_free(object);
                return NULL;
            }
            return object;
        }
        if (!field_indent)
            field_indent = current;
        if (current != field_indent)
        {
            _fail(p, "expected indentation of %zu spaces", field_indent);
            loon_free(object);
            return NULL;
        }
        char *key = _parse_key(p);
        if (!key || _peek(p) != ':')
        {
            free(key);
            _fail(p, "expected ':' after object key");
            loon_free(object);
            return NULL;
        }
        _take(p);
        _spaces(p);
        LoonValue *item;
        if (_peek(p) == '\n')
        {
            _take(p);
            item = _parse_block(p, field_indent + 2);
        }
        else
        {
            item = _parse_value(p);
            if (!_line_end(p))
            {
                loon_free(item);
                item = NULL;
            }
        }
        if (!item || !_add_member(object, key, item))
        {
            free(key);
            loon_free(item);
            loon_free(object);
            return NULL;
        }
    }
    _fail(p, "unterminated block object");
    loon_free(object);
    return NULL;
}

static LoonValue *_parse_block(Parser *p, size_t indent)
{
    LoonValue *object = _value_new(LOON_OBJECT);
    if (!object)
        return NULL;
    while (_peek(p))
    {
        if (_peek(p) == '\n')
        {
            _take(p);
            continue;
        }
        size_t current;
        if (!_line_indent(p, &current))
        {
            loon_free(object);
            return NULL;
        }
        if (_peek(p) == '\n')
        {
            _take(p);
            continue;
        }
        if (current < indent)
        {
            p->pos -= current;
            p->column -= current;
            break;
        }
        if (current != indent)
        {
            _fail(p, "expected indentation of %zu spaces", indent);
            loon_free(object);
            return NULL;
        }
        if (_peek(p) == '(')
        {
            LoonValue *table = _parse_table(p, indent + 2);
            if (!table || !_add_member(object, _slice("rows", 4), table))
            {
                loon_free(table);
                loon_free(object);
                return NULL;
            }
            continue;
        }
        char *key = _parse_key(p);
        if (!key)
        {
            loon_free(object);
            return NULL;
        }
        if (_peek(p) == '(')
        {
            LoonValue *table = _parse_table(p, indent + 2);
            if (!table || !_add_member(object, key, table))
            {
                free(key);
                loon_free(table);
                loon_free(object);
                return NULL;
            }
            continue;
        }
        if (_peek(p) != ':')
        {
            _fail(p, "expected ':' after key");
            free(key);
            loon_free(object);
            return NULL;
        }
        _take(p);
        _spaces(p);
        LoonValue *item;
        if (_peek(p) == '\n' || !_peek(p))
        {
            if (_peek(p) == '\n')
                _take(p);
            if (!_peek(p))
            {
                _fail(p, "key '%s' has no value", key);
                free(key);
                loon_free(object);
                return NULL;
            }
            Parser lookahead = *p;
            size_t child_indent = 0;
            if (!_line_indent(&lookahead, &child_indent))
            {
                free(key);
                loon_free(object);
                return NULL;
            }
            if (child_indent == indent + 2 && _peek(&lookahead) == '[')
            {
                *p = lookahead;
                item = _parse_BlockArray(p, indent + 4);
            }
            else
                item = _parse_block(p, indent + 2);
        }
        else if (_peek(p) == '[' && p->text[p->pos + 1] == '\n')
            item = _parse_BlockArray(p, indent + 2);
        else
        {
            item = _parse_value(p);
            _spaces(p);
            if (_peek(p) && _peek(p) != '\n')
            {
                _fail(p, "unexpected trailing content");
                loon_free(item);
                item = NULL;
            }
            if (_peek(p) == '\n')
                _take(p);
        }
        if (!item || !_add_member(object, key, item))
        {
            free(key);
            loon_free(item);
            loon_free(object);
            return NULL;
        }
    }
    return object;
}

LoonValue *loon_parse(const char *source, LoonError *error)
{
    if (error)
        memset(error, 0, sizeof(*error));
    if (!source)
    {
        if (error)
            snprintf(error->message, sizeof(error->message), "source is null");
        return NULL;
    }
    Parser p = {.text = source, .line = 1, .column = 1, .error = error};
    LoonValue *result;
    if (_peek(&p) == '(')
    {
        result = _parse_table(&p, 2);
    }
    else
        result = _parse_block(&p, 0);
    if (!result || (error && error->message[0]))
    {
        loon_free(result);
        return NULL;
    }
    return result;
}

const LoonValue *loon_objectGet(const LoonValue *value, const char *key)
{
    if (!value || value->type != LOON_OBJECT || !key)
        return NULL;
    for (size_t i = 0; i < value->as.object.count; i++)
        if (strcmp(value->as.object.members[i].key, key) == 0)
            return value->as.object.members[i].value;
    return NULL;
}

static Value _loon_toValue(VM *vm, const LoonValue *value)
{
    switch (value->type)
    {
    case LOON_NULL:
        return NEW_NIL();
    case LOON_BOOL:
        return NEW_BOOL(value->as.boolean);
    case LOON_NUMBER:
        return NEW_NUM(value->as.number);
    case LOON_STRING:
        return NEW_OBJ(add_obj(vm, new_pistring(strdup(value->as.string))));
    case LOON_ARRAY:
    {
        List *items = list_create(sizeof(Value));
        for (size_t i = 0; items && i < value->as.array.count; i++)
        {
            Value item = _loon_toValue(vm, value->as.array.items[i]);
            _list_add(items, &item);
        }
        return items ? NEW_OBJ(add_obj(vm, new_list(items))) : NEW_NIL();
    }
    case LOON_OBJECT:
    {
        PiMap *map = (PiMap *)add_obj(vm, new_map(ht_create(sizeof(Value))));
        for (size_t i = 0; i < value->as.object.count; i++)
        {
            LoonMember *member = &value->as.object.members[i];
            Value item = _loon_toValue(vm, member->value);
            map_setValueByKey(map, member->key, item);
        }
        return NEW_OBJ(map);
    }
    }
    return NEW_NIL();
}

Value loon_parseValue(VM *vm, int argc, Value *argv)
{
    if (argc != 1 || !IS_STR(argv[0]))
    {
        vm_error(vm, "[loon.parse] expects one string argument.");
        return NEW_NIL();
    }

    LoonError error = {0};
    LoonValue *document = loon_parse(AS_CSTRING(argv[0]), &error);
    if (!document)
    {
        vm_errorf(vm, "[loon.parse] line %zu, column %zu: %s", error.line,
                  error.column, error.message[0] ? error.message : "parse failed");
        return NEW_NIL();
    }

    Value result = _loon_toValue(vm, document);
    loon_free(document);
    return result;
}

static BuiltinFunc loon_funcs[] = {{"parse", loon_parseValue}};
static BuiltinConst loon_consts[] = {};

DEFINE_BUILTIN_MODULE(module_loon, "loon", loon_funcs, loon_consts);
