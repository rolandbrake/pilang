#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "pi_io.h"

#include "../common.h"
#include "pi_builtin.h"

/**
 * @brief Appends a string to the given buffer.
 *
 * This function appends a string to the given buffer. It takes care to not
 * overflow the buffer. If the buffer is full, it does nothing.
 *
 * @param buffer The buffer to append to.
 * @param offset A pointer to the offset of the buffer.
 * @param text The string to append.
 */
static void append(char *buffer, int *offset, const char *text)
{
    int remaining = BUFFER_SIZE - *offset - 1;
    if (remaining <= 0)
        return;

    // Calculate how much of the string can be written into the buffer
    int written = snprintf(buffer + *offset, remaining, "%s", text);
    if (written > 0)
    {
        // Update the offset to reflect the amount of characters written
        if (written >= remaining)
            *offset = BUFFER_SIZE - 1;
        else
            *offset += written;
    }
}

/**
 * @brief Appends a single character to the given buffer.
 *
 * This function appends a single character to the given buffer. It takes
 * care to not overflow the buffer. If the buffer is full, it does
 * nothing.
 *
 * @param buffer The buffer to append to.
 * @param offset A pointer to the offset of the buffer.
 * @param c The character to append.
 */
static void append_char(char *buffer, int *offset, char c)
{
    // Check if the buffer is full
    if (*offset >= BUFFER_SIZE - 1)
        return;

    // Append the character
    buffer[*offset] = c;
    (*offset)++;

    // Null terminate the buffer
    buffer[*offset] = '\0';
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

            char *arg_text = as_string(argv[index + 1]);
            if (!arg_text)
                arg_text = strdup("<unknown>");

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

/**
 * @brief Prints a string on the screen.
 *
 * This function takes one or three arguments: the text to be printed, and
 * optionally the x and y coordinates of the text position, and the text
 * color index. The text color index is wrapped within 32. An error is
 * raised if less than one argument is provided.
 *
 * @param vm The virtual machine instance.
 * @param argc The number of arguments (1 to 3).
 * @param argv The arguments: text (string), x (integer, optional), y (integer, optional), and text_color (integer, optional).
 * @return A nil value indicating completion.
 */
Value pi_print(vm_t *vm, int argc, Value *argv)
{
    (void)vm;

    for (int i = 0; i < argc; i++)
    {
        char *text = as_string(argv[i]);
        if (!text)
            text = strdup("<unknown>");

        if (i > 0)
            putchar(' ');
        fputs(text, stdout);
        free(text);
    }

    fflush(stdout);
    return NEW_NIL();
}

/**
 * @brief Prints a string on the screen followed by a newline character.
 *
 * This function takes one or four arguments: the text to be printed, and
 * optionally the x and y coordinates of the text position, and the text
 * color index. The text color index is wrapped within 32. An error is
 * raised if less than one argument is provided.
 *
 * @param vm The virtual machine instance.
 * @param argc The number of arguments (1 to 4).
 * @param argv The arguments: text (string), x (integer, optional), y (integer, optional), and text_color (integer, optional).
 * @return A nil value indicating completion.
 */
Value pi_println(vm_t *vm, int argc, Value *argv)
{
    pi_print(vm, argc, argv);
    putchar('\n');
    fflush(stdout);

    return NEW_NIL();
}

/**
 * @brief Prints a formatted string on the screen.
 *
 * This function takes one or more arguments: the format string, and
 * optionally any number of values to be formatted into the string.
 * The format string is expected to contain placeholders in the form of
 * {index:color} where index is the 0-based index of the value to be
 * formatted, and color is the text color index to use for the formatted
 * value.
 *
 * The format string is also expected to contain newline characters (\n) which
 * will move the cursor to the next line.
 *
 * The function is case-insensitive.
 *
 * @param vm The virtual machine instance.
 * @param argc The number of arguments (1 to N).
 * @param argv The arguments: format (string), and optionally values to be formatted.
 * @return A nil value indicating completion.
 */
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

/**
 * @brief Prints a message to the console.
 *
 * This function takes a message as a string and prints it to the console.
 * It also takes an optional flag string that specifies the type of log message:
 *   - "e" for error log messages
 *   - "w" for warning log messages
 *
 * If no flag is provided, the message is simply printed to the console.
 *
 * @param vm The virtual machine instance.
 * @param argc The argument count; expects at least 1 argument.
 * @param argv The argument values; expects the first argument to be a string.
 * @return A nil value indicating completion.
 */
Value pi_log(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1)
        vm_error(vm, "[log] expects message.");

    char *msg = as_string(argv[0]);

    const char *flag = "i";

    if (argc >= 2 && IS_STRING(argv[1]))
        flag = AS_CSTRING(argv[1]);

    // Error log message
    if (strcmp(flag, "e") == 0)
        printf(ANSI_RED "%s" ANSI_RESET "\n", msg);
    // Warning log message
    else if (strcmp(flag, "w") == 0)
        printf(ANSI_YELLOW "%s" ANSI_RESET "\n", msg);
    // Normal log message
    else
        printf("%s\n", msg);
    free(msg);

    return NEW_NIL();
}

/**
 * @brief Prompts the user for input and returns it as a string.
 *
 * @param vm The virtual machine instance.
 * @param argc Number of arguments (should be 1).
 * @param argv Arguments: [prompt string]
 * @return The input line as a string.
 */
Value pi_input(vm_t *vm, int argc, Value *argv)
{
    if (argc != 1 || !IS_STRING(argv[0]))
        vm_error(vm, "[input] expects a single string argument as a prompt.");

    PiString *prompt = AS_STRING(argv[0]);
    printf("%s", prompt->chars);
    fflush(stdout);

    char buffer[BUFFER_SIZE];
    if (!fgets(buffer, BUFFER_SIZE, stdin))
        vm_error(vm, "[input] Failed to read input.");

    // Remove trailing newline if exists
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
    if (argc != 0)
        vm_error(vm, "[readline] expects no arguments.");

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
