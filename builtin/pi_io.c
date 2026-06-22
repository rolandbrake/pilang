#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "pi_io.h"

#include "../common.h"
#include "pi_builtin.h"
#include "../pi_func.h"

static void append(char *buffer, int *offset, const char *text)
{
    int remaining = BUFFER_SIZE - *offset - 1;
    if (remaining <= 0)
        return;

    int written = snprintf(buffer + *offset, remaining, "%s", text);
    if (written > 0)
    {
        if (written >= remaining)
            *offset = BUFFER_SIZE - 1;
        else
            *offset += written;
    }
}

static void append_char(char *buffer, int *offset, char c)
{
    if (*offset >= BUFFER_SIZE - 1)
        return;

    buffer[*offset] = c;
    (*offset)++;
    buffer[*offset] = '\0';
}

char *pi_displayString(vm_t *vm, Value value);

typedef struct
{
    char *data;
    size_t length;
    size_t capacity;
} DisplayBuilder;

static void builder_init(DisplayBuilder *builder, const char *initial)
{
    size_t len = strlen(initial);
    builder->capacity = len < 32 ? 32 : len + 1;
    builder->length = len;
    builder->data = malloc(builder->capacity);

    if (!builder->data)
        error("[display] Memory allocation failed.");

    memcpy(builder->data, initial, len + 1);
}

static void builder_append(DisplayBuilder *builder, const char *text)
{
    size_t len = strlen(text);
    size_t needed = builder->length + len + 1;

    if (needed > builder->capacity)
    {
        size_t capacity = builder->capacity;

        while (capacity < needed)
            capacity *= 2;

        char *data = realloc(builder->data, capacity);
        if (!data)
            error("[display] Memory allocation failed.");

        builder->data = data;
        builder->capacity = capacity;
    }

    memcpy(builder->data + builder->length, text, len + 1);
    builder->length += len;
}

static char *builder_finish(DisplayBuilder *builder)
{
    return builder->data;
}

static char *display_mapString(vm_t *vm, PiMap *map)
{
    int size = ht_length(map->table);
    if (size == 0)
        return strdup("{}");

    DisplayBuilder builder;
    builder_init(&builder, "{");
    ht_iter it = ht_iterator(map->table);
    for (int i = 0; ht_next(&it); i++)
    {
        char *key = it.key;
        Value *stored = it.value;
        char *value = stored ? pi_displayString(vm, *stored) : strdup("nil");

        if (i > 0)
            builder_append(&builder, ", ");

        builder_append(&builder, key);
        builder_append(&builder, ": ");
        builder_append(&builder, value);
        free(value);
    }

    builder_append(&builder, "}");
    return builder_finish(&builder);
}

static char *display_listString(vm_t *vm, PiList *list)
{
    DisplayBuilder builder;
    builder_init(&builder, "[");

    for (int i = 0; i < list->items->size; i++)
    {
        Value item = *(Value *)list_getAt(list->items, i);
        char *text = pi_displayString(vm, item);

        if (i > 0)
            builder_append(&builder, ", ");

        builder_append(&builder, text);
        free(text);
    }

    builder_append(&builder, "]");
    return builder_finish(&builder);
}

static char *display_tupleString(vm_t *vm, PiTuple *tuple)
{
    int size = LIST_SIZE(tuple->items);
    DisplayBuilder builder;
    builder_init(&builder, "(");

    for (int i = 0; i < size; i++)
    {
        Value item = *(Value *)list_getAt(tuple->items, i);
        char *text = pi_displayString(vm, item);

        if (i > 0)
            builder_append(&builder, ", ");

        builder_append(&builder, text);
        free(text);
    }

    // Keep Python-style single-item tuple output: (value,)
    if (size == 1)
        builder_append(&builder, ",");

    builder_append(&builder, ")");
    return builder_finish(&builder);
}

static char *display_setString(vm_t *vm, PiSet *set)
{
    int size = set_size(set);
    if (size == 0)
        return strdup("{}");

    DisplayBuilder builder;
    builder_init(&builder, "{");

    for (int i = 0; i < size; i++)
    {
        char *text = pi_displayString(vm, set_get(set, i));

        if (i > 0)
            builder_append(&builder, ", ");

        builder_append(&builder, text);
        free(text);
    }

    builder_append(&builder, "}");
    return builder_finish(&builder);
}

char *pi_displayString(vm_t *vm, Value value)
{
    // Instances can customize their printed representation by defining format().
    // Returning the same instance avoids infinite recursion.
    if (IS_MAP(value) && AS_MAP(value)->is_instance)
    {
        Value formatted = vm_callMethodNoArgs(vm, value, "format");
        if (!(IS_MAP(formatted) && AS_MAP(formatted) == AS_MAP(value)))
            return pi_displayString(vm, formatted);
    }

    if (IS_LIST(value))
        return display_listString(vm, AS_LIST(value));
    if (IS_MAP(value))
        return display_mapString(vm, AS_MAP(value));
    if (IS_SET(value))
        return display_setString(vm, AS_SET(value));
    if (IS_TUPLE(value))
        return display_tupleString(vm, AS_TUPLE(value));

    char *text = as_stringWithFormat(vm, value);
    return text ? text : strdup("<unknown>");
}

static void format_text(vm_t *vm, int argc, Value *argv, char *out)
{
    if (argc < 1 || !IS_STRING(argv[0]))
        vm_error(vm, "[format] expects at least a format string.");

    const char *fmt = AS_CSTRING(argv[0]);
    int offset = 0;
    out[0] = '\0';

    for (int i = 0; fmt[i] != '\0'; i++)
    {
        if (fmt[i] == '{')
        {
            if (fmt[i + 1] == '{')
            {
                append_char(out, &offset, '{');
                i++;
                continue;
            }

            int j = i + 1;
            int index = 0;
            bool has_digit = false;

            while (isdigit((unsigned char)fmt[j]))
            {
                has_digit = true;
                index = (index * 10) + (fmt[j] - '0');
                j++;
            }

            // Accept and skip a reserved "{index:...}" format section.
            // The current console output ignores style/color metadata.
            if (fmt[j] == ':')
            {
                j++;
                while (isdigit((unsigned char)fmt[j]))
                    j++;
            }

            if (!has_digit || fmt[j] != '}')
            {
                append_char(out, &offset, fmt[i]);
                continue;
            }

            if ((index + 1) >= argc)
                vm_errorf(vm, "[format] placeholder {%d} is out of range.", index);

            char *arg_text = pi_displayString(vm, argv[index + 1]);

            append(out, &offset, arg_text);
            free(arg_text);

            i = j;
            continue;
        }

        if (fmt[i] == '}' && fmt[i + 1] == '}')
        {
            append_char(out, &offset, '}');
            i++;
            continue;
        }

        append_char(out, &offset, fmt[i]);
    }
}

Value pi_print(vm_t *vm, int argc, Value *argv)
{
    char *text;
    for (int i = 0; i < argc; i++)
    {
        if (i > 0)
            putchar(' ');

        text = pi_displayString(vm, argv[i]);

        fputs(text, stdout);
        free(text);
    }

    fflush(stdout);
    return NEW_NIL();
}

Value pi_println(vm_t *vm, int argc, Value *argv)
{
    pi_print(vm, argc, argv);
    putchar('\n');
    fflush(stdout);

    return NEW_NIL();
}

Value pi_printf(vm_t *vm, int argc, Value *argv)
{
    char out[BUFFER_SIZE];
    if (argc < 1 || !IS_STRING(argv[0]))
        vm_error(vm, "[printf] expects at least a format string.");

    format_text(vm, argc, argv, out);

    fputs(out, stdout);
    fflush(stdout);
    return NEW_NIL();
}

Value pi_log(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1)
        vm_error(vm, "[log] expects message.");

    char *msg = as_string(argv[0]);

    const char *flag = "i";

    if (argc >= 2 && IS_STRING(argv[1]))
        flag = AS_CSTRING(argv[1]);

    if (strcmp(flag, "e") == 0)
        printf(ANSI_RED "%s" ANSI_RESET "\n", msg);
    else if (strcmp(flag, "w") == 0)
        printf(ANSI_YELLOW "%s" ANSI_RESET "\n", msg);
    else
        printf("%s\n", msg);

    free(msg);
    return NEW_NIL();
}

Value pi_input(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_STRING(argv[0]))
        vm_error(vm, "[input] expects a single string argument as a prompt.");

    PiString *prompt = AS_STRING(argv[0]);
    printf("%s", prompt->chars);
    fflush(stdout);

    char buffer[BUFFER_SIZE];
    if (!fgets(buffer, BUFFER_SIZE, stdin))
        vm_error(vm, "[input] Failed to read input.");

    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n')
        buffer[len - 1] = '\0';

    return NEW_OBJ(new_pistring(strdup(buffer)));
}

Value io_format(vm_t *vm, int argc, Value *argv)
{
    char out[BUFFER_SIZE];
    format_text(vm, argc, argv, out);
    return NEW_OBJ(new_pistring(strdup(out)));
}

Value io_readline(vm_t *vm, int argc, Value *argv)
{
    (void)argc;
    (void)argv;

    char buffer[BUFFER_SIZE];
    if (!fgets(buffer, BUFFER_SIZE, stdin))
        vm_error(vm, "[readline] Failed to read input.");

    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n')
        buffer[len - 1] = '\0';

    return NEW_OBJ(new_pistring(strdup(buffer)));
}

Value io_prompt(vm_t *vm, int argc, Value *argv)
{
    if (argc > 0)
        pi_print(vm, argc, argv);

    char buffer[BUFFER_SIZE];
    if (!fgets(buffer, BUFFER_SIZE, stdin))
        vm_error(vm, "[prompt] Failed to read input.");

    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n')
        buffer[len - 1] = '\0';

    return NEW_OBJ(new_pistring(strdup(buffer)));
}

static BuiltinConst io_consts[] = {

};

static BuiltinFunc io_functions[] = {
    {"format", io_format},
    {"readline", io_readline},
    {"prompt", io_prompt},
};

DEFINE_BUILTIN_MODULE(module_io, "io", io_functions, io_consts);
