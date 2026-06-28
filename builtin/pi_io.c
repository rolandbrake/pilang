#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifdef _WIN32
#include <conio.h>
#include <io.h>
#include <windows.h>
#else
#include <errno.h>
#include <sys/select.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>
#endif

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

static void write_stdoutText(const char *text)
{
#ifdef _WIN32
    HANDLE out = (HANDLE)_get_osfhandle(_fileno(stdout));
    DWORD mode = 0;

    if (out != INVALID_HANDLE_VALUE && GetConsoleMode(out, &mode))
    {
        int wide_len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
        if (wide_len > 0)
        {
            wchar_t *wide = malloc(sizeof(wchar_t) * (size_t)wide_len);
            if (!wide)
                error("[io] Memory allocation failed.");

            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, wide, wide_len);

            DWORD written = 0;
            WriteConsoleW(out, wide, (DWORD)(wide_len - 1), &written, NULL);
            free(wide);
            return;
        }
    }
#endif

    fputs(text, stdout);
}

static void write_stdoutChar(char ch)
{
    char text[2] = {ch, '\0'};
    write_stdoutText(text);
}

static bool stdin_isInteractive(void)
{
#ifdef _WIN32
    return _isatty(_fileno(stdin)) != 0;
#else
    return isatty(STDIN_FILENO) != 0;
#endif
}

enum
{
    INPUT_KEY_NONE = -1,
    INPUT_KEY_LEFT = 256,
    INPUT_KEY_RIGHT,
    INPUT_KEY_DELETE,
    INPUT_KEY_HOME,
    INPUT_KEY_END
};

static unsigned long input_nowMillis(void)
{
#ifdef _WIN32
    return (unsigned long)GetTickCount();
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (unsigned long)(tv.tv_sec * 1000UL + tv.tv_usec / 1000UL);
#endif
}

static void input_moveLeft(size_t count)
{
    if (count == 0)
        return;

    char code[32];
    snprintf(code, sizeof(code), "\033[%zuD", count);
    write_stdoutText(code);
}

static void input_moveRight(size_t count)
{
    if (count == 0)
        return;

    char code[32];
    snprintf(code, sizeof(code), "\033[%zuC", count);
    write_stdoutText(code);
}

static void input_drawCursor(const char *buffer, size_t cursor, size_t len)
{
    (void)buffer;
    (void)cursor;
    (void)len;
    write_stdoutText("\033[41m \033[0m\b");
    fflush(stdout);
}

static void input_eraseCursor(const char *buffer, size_t cursor, size_t len)
{
    char text[2] = {' ', '\0'};
    if (cursor < len)
        text[0] = buffer[cursor];
    write_stdoutText(text);
    write_stdoutText("\b");
    fflush(stdout);
}

static void input_redrawTail(const char *buffer, size_t cursor, size_t len, bool clear_extra)
{
    if (cursor < len)
        write_stdoutText(buffer + cursor);

    if (clear_extra)
        write_stdoutChar(' ');

    input_moveLeft((len - cursor) + (clear_extra ? 1 : 0));
    fflush(stdout);
}

static void input_setCursorVisible(const char *buffer, size_t cursor, size_t len, bool *visible, bool show)
{
    if (*visible == show)
        return;

    if (show)
        input_drawCursor(buffer, cursor, len);
    else
        input_eraseCursor(buffer, cursor, len);

    *visible = show;
}

static void read_linePlain(vm_t *vm, const char *name, char *buffer, size_t size)
{
    if (!fgets(buffer, (int)size, stdin))
        vm_errorf(vm, "[%s] Failed to read input.", name);

    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n')
        buffer[len - 1] = '\0';
}

#ifndef _WIN32
static bool input_enableRawMode(struct termios *old_term)
{
    if (tcgetattr(STDIN_FILENO, old_term) == -1)
        return false;

    struct termios raw_term = *old_term;
    raw_term.c_lflag &= ~(ICANON | ECHO);
    raw_term.c_cc[VMIN] = 0;
    raw_term.c_cc[VTIME] = 0;
    return tcsetattr(STDIN_FILENO, TCSANOW, &raw_term) != -1;
}
#endif

static int input_readKey(int timeout_ms)
{
#ifdef _WIN32
    DWORD started = GetTickCount();
    while (!_kbhit())
    {
        if ((int)(GetTickCount() - started) >= timeout_ms)
            return INPUT_KEY_NONE;
        Sleep(1);
    }

    int ch = _getch();
    if (ch == 0 || ch == 224)
    {
        ch = _getch();
        if (ch == 75)
            return INPUT_KEY_LEFT;
        if (ch == 77)
            return INPUT_KEY_RIGHT;
        if (ch == 83)
            return INPUT_KEY_DELETE;
        if (ch == 71)
            return INPUT_KEY_HOME;
        if (ch == 79)
            return INPUT_KEY_END;
        return INPUT_KEY_NONE;
    }

    return ch;
#else
    fd_set set;
    FD_ZERO(&set);
    FD_SET(STDIN_FILENO, &set);

    struct timeval timeout;
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;

    int ready;
    do
    {
        ready = select(STDIN_FILENO + 1, &set, NULL, NULL, &timeout);
    } while (ready == -1 && errno == EINTR);

    if (ready <= 0)
        return INPUT_KEY_NONE;

    unsigned char ch = 0;
    ssize_t nread = read(STDIN_FILENO, &ch, 1);
    if (nread <= 0)
        return INPUT_KEY_NONE;

    if (ch == '\033')
    {
        unsigned char seq[3] = {0, 0, 0};
        if (read(STDIN_FILENO, &seq[0], 1) != 1)
            return '\033';
        if (read(STDIN_FILENO, &seq[1], 1) != 1)
            return '\033';

        if (seq[0] == '[')
        {
            if (seq[1] == 'D')
                return INPUT_KEY_LEFT;
            if (seq[1] == 'C')
                return INPUT_KEY_RIGHT;
            if (seq[1] == 'H')
                return INPUT_KEY_HOME;
            if (seq[1] == 'F')
                return INPUT_KEY_END;
            if (seq[1] == '3')
            {
                (void)read(STDIN_FILENO, &seq[2], 1);
                return INPUT_KEY_DELETE;
            }
        }

        return INPUT_KEY_NONE;
    }

    return ch;
#endif
}

static void read_lineWithCursor(vm_t *vm, const char *name, char *buffer, size_t size)
{
    if (size == 0)
        return;

    buffer[0] = '\0';

    if (!stdin_isInteractive())
    {
        read_linePlain(vm, name, buffer, size);
        return;
    }

#ifndef _WIN32
    struct termios old_term;
    if (!input_enableRawMode(&old_term))
    {
        read_linePlain(vm, name, buffer, size);
        return;
    }
#endif

    size_t len = 0;
    size_t cursor = 0;
    bool cursor_visible = false;
    unsigned long last_blink = input_nowMillis();
    input_setCursorVisible(buffer, cursor, len, &cursor_visible, true);

    while (true)
    {
        int ch = input_readKey(50);
        unsigned long now = input_nowMillis();
        if (ch == INPUT_KEY_NONE)
        {
            if (now - last_blink >= 500)
            {
                input_setCursorVisible(buffer, cursor, len, &cursor_visible, !cursor_visible);
                last_blink = now;
            }
            continue;
        }

        input_setCursorVisible(buffer, cursor, len, &cursor_visible, false);
        last_blink = now;

        if (ch == '\r' || ch == '\n')
        {
            write_stdoutChar('\n');
            break;
        }

        if (ch == 3)
        {
#ifndef _WIN32
            tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
#endif
            write_stdoutChar('\n');
            vm_errorf(vm, "[%s] Input interrupted.", name);
        }

        if (ch == INPUT_KEY_LEFT)
        {
            if (cursor > 0)
            {
                input_moveLeft(1);
                cursor--;
            }
            input_setCursorVisible(buffer, cursor, len, &cursor_visible, true);
            continue;
        }

        if (ch == INPUT_KEY_RIGHT)
        {
            if (cursor < len)
            {
                input_moveRight(1);
                cursor++;
            }
            input_setCursorVisible(buffer, cursor, len, &cursor_visible, true);
            continue;
        }

        if (ch == INPUT_KEY_HOME)
        {
            input_moveLeft(cursor);
            cursor = 0;
            input_setCursorVisible(buffer, cursor, len, &cursor_visible, true);
            continue;
        }

        if (ch == INPUT_KEY_END)
        {
            input_moveRight(len - cursor);
            cursor = len;
            input_setCursorVisible(buffer, cursor, len, &cursor_visible, true);
            continue;
        }

        if (ch == '\b' || ch == 127)
        {
            if (cursor > 0)
            {
                cursor--;
                input_moveLeft(1);
                memmove(buffer + cursor, buffer + cursor + 1, len - cursor);
                len--;
                buffer[len] = '\0';
                input_redrawTail(buffer, cursor, len, true);
            }
            input_setCursorVisible(buffer, cursor, len, &cursor_visible, true);
            continue;
        }

        if (ch == INPUT_KEY_DELETE)
        {
            if (cursor < len)
            {
                memmove(buffer + cursor, buffer + cursor + 1, len - cursor);
                len--;
                buffer[len] = '\0';
                input_redrawTail(buffer, cursor, len, true);
            }
            input_setCursorVisible(buffer, cursor, len, &cursor_visible, true);
            continue;
        }

        if (isprint((unsigned char)ch) || (unsigned char)ch >= 128)
        {
            if (len + 1 >= size)
            {
                input_setCursorVisible(buffer, cursor, len, &cursor_visible, true);
                continue;
            }

            memmove(buffer + cursor + 1, buffer + cursor, len - cursor + 1);
            buffer[cursor] = (char)ch;
            len++;
            write_stdoutText(buffer + cursor);
            cursor++;
            input_moveLeft(len - cursor);
            buffer[len] = '\0';
            input_setCursorVisible(buffer, cursor, len, &cursor_visible, true);
        }
    }

#ifndef _WIN32
    tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
#endif
}

static char *display_valueString(vm_t *vm, Value value, bool nested);

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

static char *display_quoteString(const char *text)
{
    DisplayBuilder builder;
    builder_init(&builder, "\"");

    for (const char *cursor = text; *cursor != '\0'; cursor++)
    {
        switch (*cursor)
        {
        case '\\':
            builder_append(&builder, "\\\\");
            break;
        case '"':
            builder_append(&builder, "\\\"");
            break;
        case '\n':
            builder_append(&builder, "\\n");
            break;
        case '\r':
            builder_append(&builder, "\\r");
            break;
        case '\t':
            builder_append(&builder, "\\t");
            break;
        default:
        {
            char ch[2] = {*cursor, '\0'};
            builder_append(&builder, ch);
            break;
        }
        }
    }

    builder_append(&builder, "\"");
    return builder_finish(&builder);
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
        char *quoted_key = display_quoteString(key);
        char *value = stored ? display_valueString(vm, *stored, true) : strdup("nil");

        if (i > 0)
            builder_append(&builder, ", ");

        builder_append(&builder, quoted_key);
        builder_append(&builder, ": ");
        builder_append(&builder, value);
        free(quoted_key);
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
        char *text = display_valueString(vm, item, true);

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
        char *text = display_valueString(vm, item, true);

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
        char *text = display_valueString(vm, set_get(set, i), true);

        if (i > 0)
            builder_append(&builder, ", ");

        builder_append(&builder, text);
        free(text);
    }

    builder_append(&builder, "}");
    return builder_finish(&builder);
}

static char *display_valueString(vm_t *vm, Value value, bool nested)
{
    // Instances can customize their printed representation by defining format().
    // Returning the same instance avoids infinite recursion.
    if (IS_MAP(value) && AS_MAP(value)->is_instance)
    {
        Value formatted = vm_callMethodNoArgs(vm, value, "format");
        if (!(IS_MAP(formatted) && AS_MAP(formatted) == AS_MAP(value)))
            return display_valueString(vm, formatted, nested);
    }

    if (nested && IS_STRING(value))
        return display_quoteString(AS_CSTRING(value));
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

char *pi_displayString(vm_t *vm, Value value)
{
    return display_valueString(vm, value, false);
}

typedef struct
{
    bool has_number;
    bool always_sign;
    int precision;
    char number_type;
    char align;
    int width;
    char style[128];
    int style_offset;
} FormatSpec;

static bool string_equalsIgnoreCase(const char *left, const char *right)
{
    while (*left != '\0' && *right != '\0')
    {
        if (tolower((unsigned char)*left) != tolower((unsigned char)*right))
            return false;
        left++;
        right++;
    }

    return *left == '\0' && *right == '\0';
}

static bool parse_intText(const char *text, int *value)
{
    if (*text == '\0')
        return false;

    int result = 0;
    for (const char *p = text; *p != '\0'; p++)
    {
        if (!isdigit((unsigned char)*p))
            return false;
        if (result > (INT_MAX - (*p - '0')) / 10)
            return false;
        result = result * 10 + (*p - '0');
    }

    *value = result;
    return true;
}

static bool parse_hexByte(const char *text, int *value)
{
    int result = 0;
    for (int i = 0; i < 2; i++)
    {
        char c = text[i];
        int digit;
        if (c >= '0' && c <= '9')
            digit = c - '0';
        else if (c >= 'a' && c <= 'f')
            digit = 10 + c - 'a';
        else if (c >= 'A' && c <= 'F')
            digit = 10 + c - 'A';
        else
            return false;
        result = result * 16 + digit;
    }

    *value = result;
    return true;
}

static void spec_appendStyle(FormatSpec *spec, const char *format, ...)
{
    int remaining = (int)sizeof(spec->style) - spec->style_offset - 1;
    if (remaining <= 0)
        return;

    va_list args;
    va_start(args, format);
    int written = vsnprintf(spec->style + spec->style_offset, remaining, format, args);
    va_end(args);

    if (written > 0)
    {
        if (written >= remaining)
            spec->style_offset = (int)sizeof(spec->style) - 1;
        else
            spec->style_offset += written;
    }
}

static int ansi_namedColor(const char *name)
{
    if (string_equalsIgnoreCase(name, "black"))
        return 0;
    if (string_equalsIgnoreCase(name, "red"))
        return 1;
    if (string_equalsIgnoreCase(name, "green"))
        return 2;
    if (string_equalsIgnoreCase(name, "yellow"))
        return 3;
    if (string_equalsIgnoreCase(name, "blue"))
        return 4;
    if (string_equalsIgnoreCase(name, "magenta"))
        return 5;
    if (string_equalsIgnoreCase(name, "cyan"))
        return 6;
    if (string_equalsIgnoreCase(name, "white"))
        return 7;
    if (string_equalsIgnoreCase(name, "default"))
        return 9;
    return -1;
}

static bool parse_colorSpec(FormatSpec *spec, const char *value, bool foreground)
{
    int channel = foreground ? 38 : 48;

    if (value[0] == '#' && strlen(value) == 7)
    {
        int r, g, b;
        if (!parse_hexByte(value + 1, &r) ||
            !parse_hexByte(value + 3, &g) ||
            !parse_hexByte(value + 5, &b))
            return false;
        spec_appendStyle(spec, "\033[%d;2;%d;%d;%dm", channel, r, g, b);
        return true;
    }

    int color_index = -1;
    if (parse_intText(value, &color_index))
    {
        if (color_index < 0 || color_index > 255)
            return false;
        spec_appendStyle(spec, "\033[%d;5;%dm", channel, color_index);
        return true;
    }

    color_index = ansi_namedColor(value);
    if (color_index < 0)
        return false;

    if (color_index == 9)
        spec_appendStyle(spec, "\033[%dm", foreground ? 39 : 49);
    else
        spec_appendStyle(spec, "\033[%dm", (foreground ? 30 : 40) + color_index);
    return true;
}

static bool parse_alignmentToken(FormatSpec *spec, const char *token)
{
    if ((token[0] != '<' && token[0] != '>' && token[0] != '^') || token[1] == '\0')
        return false;

    int width;
    if (!parse_intText(token + 1, &width))
        return false;

    spec->align = token[0];
    spec->width = width;
    return true;
}

static bool parse_numberToken(FormatSpec *spec, const char *token)
{
    const char *p = token;
    bool always_sign = false;
    int precision = -1;
    char type = '\0';

    if (*p == '+')
    {
        always_sign = true;
        p++;
    }

    if (*p == '.')
    {
        int parsed_precision = 0;
        bool has_digit = false;
        p++;
        while (isdigit((unsigned char)*p))
        {
            has_digit = true;
            parsed_precision = (parsed_precision * 10) + (*p - '0');
            p++;
        }
        if (!has_digit)
            return false;
        precision = parsed_precision;
    }

    if (*p != '\0')
    {
        if (p[1] != '\0')
            return false;
        if (*p != 'f' && *p != 'd' && *p != 'o' && *p != 'x' && *p != 'b' && *p != '%')
            return false;
        type = *p;
    }

    if (!always_sign && precision < 0 && type == '\0')
        return false;

    spec->has_number = true;
    spec->always_sign = always_sign;
    spec->precision = precision;
    spec->number_type = type;
    return true;
}

static void parse_formatSpec(const char *spec_text, int spec_len, FormatSpec *spec)
{
    memset(spec, 0, sizeof(*spec));
    spec->precision = -1;
    spec->style[0] = '\0';

    int i = 0;
    while (i < spec_len)
    {
        while (i < spec_len && isspace((unsigned char)spec_text[i]))
            i++;
        if (i >= spec_len)
            break;

        char token[64];
        int token_len = 0;
        while (i < spec_len && !isspace((unsigned char)spec_text[i]))
        {
            if (token_len < (int)sizeof(token) - 1)
                token[token_len++] = spec_text[i];
            i++;
        }
        token[token_len] = '\0';

        if (token_len == 0)
            continue;

        if (strcmp(token, "bold") == 0)
            spec_appendStyle(spec, "\033[1m");
        else if (strcmp(token, "italic") == 0)
            spec_appendStyle(spec, "\033[3m");
        else if (strcmp(token, "underline") == 0)
            spec_appendStyle(spec, "\033[4m");
        else if (strncmp(token, "fg:", 3) == 0)
        {
            if (!parse_colorSpec(spec, token + 3, true))
                spec_appendStyle(spec, "");
        }
        else if (strncmp(token, "bg:", 3) == 0)
        {
            if (!parse_colorSpec(spec, token + 3, false))
                spec_appendStyle(spec, "");
        }
        else if (!parse_alignmentToken(spec, token) && !parse_numberToken(spec, token))
        {
            // Unknown tokens are ignored so future format options stay forwards-compatible.
        }
    }
}

static bool value_isIntegral(double value)
{
    return isfinite(value) && floor(value) == value;
}

static char *format_binaryInteger(unsigned long long value)
{
    char digits[sizeof(unsigned long long) * CHAR_BIT + 1];
    int pos = (int)sizeof(digits) - 1;
    digits[pos] = '\0';

    do
    {
        digits[--pos] = (value & 1ULL) ? '1' : '0';
        value >>= 1;
    } while (value != 0);

    return strdup(digits + pos);
}

static char *format_numericValue(vm_t *vm, FormatSpec *spec, Value value)
{
    if (!is_numeric(value))
        vm_errorf(vm, "[format] numeric spec requires a numeric value, got %s.", type_name(value));

    double number = as_number(value);
    char buffer[128];
    int precision = spec->precision >= 0 ? spec->precision : 6;
    char sign_flag[2] = {spec->always_sign ? '+' : '\0', '\0'};

    switch (spec->number_type)
    {
    case 'd':
    case 'o':
    case 'x':
    case 'b':
    {
        if (!value_isIntegral(number))
            vm_error(vm, "[format] integer base spec requires an integral number.");

        long long signed_value = (long long)number;
        if (spec->number_type == 'd')
            snprintf(buffer, sizeof(buffer), "%s%lld",
                     spec->always_sign && signed_value >= 0 ? "+" : "", signed_value);
        else if (spec->number_type == 'o')
            snprintf(buffer, sizeof(buffer), "%llo", (unsigned long long)signed_value);
        else if (spec->number_type == 'x')
            snprintf(buffer, sizeof(buffer), "%llx", (unsigned long long)signed_value);
        else
            return format_binaryInteger((unsigned long long)signed_value);
        return strdup(buffer);
    }
    case 'f':
        snprintf(buffer, sizeof(buffer), "%%%s.%df", sign_flag, precision);
        break;
    case '%':
        snprintf(buffer, sizeof(buffer), "%%%s.%df%%%%", sign_flag, precision);
        number *= 100.0;
        break;
    default:
        if (spec->precision >= 0)
            snprintf(buffer, sizeof(buffer), "%%%s.%dg", sign_flag, precision);
        else
            snprintf(buffer, sizeof(buffer), "%%%sg", sign_flag);
        break;
    }

    char *text = malloc(128);
    if (!text)
        error("[format] Memory allocation failed.");
    snprintf(text, 128, buffer, number);
    return text;
}

static char *format_applyAlignment(const char *text, FormatSpec *spec)
{
    int len = (int)strlen(text);
    if (spec->width <= len)
        return strdup(text);

    int padding = spec->width - len;
    int left = 0;
    int right = 0;

    if (spec->align == '<')
        right = padding;
    else if (spec->align == '^')
    {
        left = padding / 2;
        right = padding - left;
    }
    else
        left = padding;

    char *result = malloc((size_t)spec->width + 1);
    if (!result)
        error("[format] Memory allocation failed.");

    int offset = 0;
    for (int i = 0; i < left; i++)
        result[offset++] = ' ';
    memcpy(result + offset, text, (size_t)len);
    offset += len;
    for (int i = 0; i < right; i++)
        result[offset++] = ' ';
    result[offset] = '\0';
    return result;
}

static char *format_valueWithSpec(vm_t *vm, Value value, FormatSpec *spec)
{
    char *text = spec->has_number
                     ? format_numericValue(vm, spec, value)
                     : pi_displayString(vm, value);
    char *aligned = format_applyAlignment(text, spec);
    free(text);

    if (spec->style_offset == 0)
        return aligned;

    size_t len = strlen(spec->style) + strlen(aligned) + strlen(ANSI_RESET) + 1;
    char *styled = malloc(len);
    if (!styled)
        error("[format] Memory allocation failed.");

    snprintf(styled, len, "%s%s%s", spec->style, aligned, ANSI_RESET);
    free(aligned);
    return styled;
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

            int spec_start = -1;
            int spec_len = 0;
            if (fmt[j] == ':')
            {
                j++;
                spec_start = j;
                while (fmt[j] != '\0' && fmt[j] != '}')
                    j++;
                spec_len = j - spec_start;
            }

            if (!has_digit || fmt[j] != '}')
            {
                append_char(out, &offset, fmt[i]);
                continue;
            }

            if ((index + 1) >= argc)
                vm_errorf(vm, "[format] placeholder {%d} is out of range.", index);

            char *arg_text;
            if (spec_start >= 0)
            {
                FormatSpec spec;
                parse_formatSpec(fmt + spec_start, spec_len, &spec);
                arg_text = format_valueWithSpec(vm, argv[index + 1], &spec);
            }
            else
            {
                arg_text = pi_displayString(vm, argv[index + 1]);
            }

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

        write_stdoutText(text);
        free(text);
    }

    fflush(stdout);
    return NEW_NIL();
}

Value pi_println(vm_t *vm, int argc, Value *argv)
{
    pi_print(vm, argc, argv);
    write_stdoutChar('\n');
    fflush(stdout);

    return NEW_NIL();
}

Value pi_printf(vm_t *vm, int argc, Value *argv)
{
    char out[BUFFER_SIZE];
    if (argc < 1 || !IS_STRING(argv[0]))
        vm_error(vm, "[printf] expects at least a format string.");

    format_text(vm, argc, argv, out);

    write_stdoutText(out);
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

    char *line = malloc(strlen(msg) + 32);
    if (!line)
        error("[log] Memory allocation failed.");

    if (strcmp(flag, "e") == 0)
        sprintf(line, ANSI_RED "%s" ANSI_RESET "\n", msg);
    else if (strcmp(flag, "w") == 0)
        sprintf(line, ANSI_YELLOW "%s" ANSI_RESET "\n", msg);
    else
        sprintf(line, "%s\n", msg);

    write_stdoutText(line);
    free(line);

    free(msg);
    return NEW_NIL();
}

Value pi_input(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_STRING(argv[0]))
        vm_error(vm, "[input] expects a single string argument as a prompt.");

    PiString *prompt = AS_STRING(argv[0]);
    write_stdoutText(prompt->chars);
    fflush(stdout);

    char buffer[BUFFER_SIZE];
    read_lineWithCursor(vm, "input", buffer, sizeof(buffer));

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
    read_lineWithCursor(vm, "readline", buffer, sizeof(buffer));

    return NEW_OBJ(new_pistring(strdup(buffer)));
}

Value io_prompt(vm_t *vm, int argc, Value *argv)
{
    if (argc > 0)
        pi_print(vm, argc, argv);

    char buffer[BUFFER_SIZE];
    read_lineWithCursor(vm, "prompt", buffer, sizeof(buffer));

    return NEW_OBJ(new_pistring(strdup(buffer)));
}

Value io_clear(vm_t *vm, int argc, Value *argv)
{
    (void)vm;
    (void)argc;
    (void)argv;

    write_stdoutText("\033[2J\033[H");
    fflush(stdout);
    return NEW_NIL();
}

Value io_pos(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_NUM(argv[0]) || !IS_NUM(argv[1]))
        vm_error(vm, "[io.pos] expects x and y numbers.");

    int x = (int)AS_NUM(argv[0]);
    int y = (int)AS_NUM(argv[1]);
    bool clear = argc >= 3 ? as_bool(argv[2]) : false;

    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;

    char code[64];
    snprintf(code, sizeof(code), "\033[%d;%dH%s", y + 1, x + 1, clear ? "\033[J" : "");
    write_stdoutText(code);
    fflush(stdout);
    return NEW_NIL();
}

Value io_cursor(vm_t *vm, int argc, Value *argv)
{
    bool visible = true;
    if (argc >= 1)
        visible = as_bool(argv[0]);

    write_stdoutText(visible ? "\033[?25h" : "\033[?25l");
    fflush(stdout);
    return NEW_NIL();
}

Value io_key(vm_t *vm, int argc, Value *argv)
{
    int timeout_ms = -1;
    if (argc >= 1)
    {
        if (!IS_NUM(argv[0]))
            vm_error(vm, "[io.key] timeout must be a millisecond number.");
        timeout_ms = (int)AS_NUM(argv[0]);
    }

#ifdef _WIN32
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    if (input == INVALID_HANDLE_VALUE || !GetConsoleMode(input, &mode))
    {
        DWORD started = GetTickCount();
        while (1)
        {
            DWORD available = 0;
            if (input != INVALID_HANDLE_VALUE && PeekNamedPipe(input, NULL, 0, NULL, &available, NULL) && available > 0)
            {
                char ch;
                if (_read(_fileno(stdin), &ch, 1) == 1)
                {
                    if (ch == '\r' || ch == '\n')
                        return NEW_NIL();
                    char text[2] = {ch, '\0'};
                    return NEW_OBJ(new_pistring(strdup(text)));
                }
            }

            if (timeout_ms >= 0 && (int)(GetTickCount() - started) >= timeout_ms)
                return NEW_NIL();
            Sleep(1);
        }
    }

    DWORD started = GetTickCount();
    while (!_kbhit())
    {
        if (timeout_ms >= 0 && (int)(GetTickCount() - started) >= timeout_ms)
            return NEW_NIL();
        Sleep(1);
    }

    int ch = _getch();
    if (ch == 0 || ch == 224)
    {
        ch = _getch();
        if (ch == 72)
            return NEW_OBJ(new_pistring(strdup("up")));
        if (ch == 80)
            return NEW_OBJ(new_pistring(strdup("down")));
        if (ch == 75)
            return NEW_OBJ(new_pistring(strdup("left")));
        if (ch == 77)
            return NEW_OBJ(new_pistring(strdup("right")));
    }

    char text[2] = {(char)ch, '\0'};
    return NEW_OBJ(new_pistring(strdup(text)));
#else
    struct termios old_term;
    struct termios raw_term;
    if (tcgetattr(STDIN_FILENO, &old_term) == -1)
    {
        int ch = getchar();
        if (ch == EOF)
            return NEW_NIL();
        char text[2] = {(char)ch, '\0'};
        return NEW_OBJ(new_pistring(strdup(text)));
    }

    raw_term = old_term;
    raw_term.c_lflag &= ~(ICANON | ECHO);
    raw_term.c_cc[VMIN] = 0;
    raw_term.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw_term);

    fd_set set;
    FD_ZERO(&set);
    FD_SET(STDIN_FILENO, &set);

    struct timeval timeout;
    struct timeval *timeout_ptr = NULL;
    if (timeout_ms >= 0)
    {
        timeout.tv_sec = timeout_ms / 1000;
        timeout.tv_usec = (timeout_ms % 1000) * 1000;
        timeout_ptr = &timeout;
    }

    int ready;
    do
    {
        ready = select(STDIN_FILENO + 1, &set, NULL, NULL, timeout_ptr);
    } while (ready == -1 && errno == EINTR);

    if (ready <= 0)
    {
        tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
        return NEW_NIL();
    }

    char ch;
    ssize_t nread = read(STDIN_FILENO, &ch, 1);

    if (nread > 0 && ch == '\033')
    {
        char seq[2];
        ssize_t first = read(STDIN_FILENO, &seq[0], 1);
        ssize_t second = read(STDIN_FILENO, &seq[1], 1);
        tcsetattr(STDIN_FILENO, TCSANOW, &old_term);

        if (first == 1 && second == 1 && seq[0] == '[')
        {
            if (seq[1] == 'A')
                return NEW_OBJ(new_pistring(strdup("up")));
            if (seq[1] == 'B')
                return NEW_OBJ(new_pistring(strdup("down")));
            if (seq[1] == 'C')
                return NEW_OBJ(new_pistring(strdup("right")));
            if (seq[1] == 'D')
                return NEW_OBJ(new_pistring(strdup("left")));
        }

        return NEW_OBJ(new_pistring(strdup("\033")));
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &old_term);

    if (nread <= 0)
        return NEW_NIL();

    char text[2] = {ch, '\0'};
    return NEW_OBJ(new_pistring(strdup(text)));
#endif
}

static BuiltinConst io_consts[] = {

};

static BuiltinFunc io_functions[] = {
    {"format", io_format},
    {"clear", io_clear},
    {"pos", io_pos},
    {"cursor", io_cursor},
    {"key", io_key},
    {"readline", io_readline},
    {"prompt", io_prompt},
};

DEFINE_BUILTIN_MODULE(module_io, "io", io_functions, io_consts);
