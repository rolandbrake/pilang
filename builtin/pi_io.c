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
    if (argc < 1 || !IS_STRING(argv[0]))
        vm_error(vm, "[printf] expects at least a format string.");

    const char *fmt = AS_CSTRING(argv[0]);
    char out[BUFFER_SIZE];
    int offset = 0;
    out[0] = '\0';

    for (int i = 0; fmt[i] != '\0'; i++)
    {
        if (fmt[i] == '{')
        {
            // Escaped "{{" -> "{"
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

            // Optional ":color" part is accepted but ignored in stdout mode.
            if (fmt[j] == ':')
            {
                j++;
                while (isdigit((unsigned char)fmt[j]))
                    j++;
            }

            if (!has_digit || fmt[j] != '}')
            {
                // Invalid placeholder syntax, keep it literal.
                append_char(out, &offset, fmt[i]);
                continue;
            }

            if ((index + 1) >= argc)
                vm_errorf(vm, "[printf] placeholder {%d} is out of range.", index);

            char *arg_text = as_string(argv[index + 1]);
            if (!arg_text)
                arg_text = strdup("<unknown>");
            append(out, &offset, arg_text);
            free(arg_text);

            i = j;
            continue;
        }

        // Escaped "}}" -> "}"
        if (fmt[i] == '}' && fmt[i + 1] == '}')
        {
            append_char(out, &offset, '}');
            i++;
            continue;
        }

        append_char(out, &offset, fmt[i]);
    }

    fputs(out, stdout);
    fflush(stdout);
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

/**
 * @brief Opens a file and returns a file handler.
 *
 * @param vm The virtual machine instance.
 * @param argc Number of arguments (should be 1 or 2).
 * @param argv Arguments: [file path], [file mode]
 * @return A file handler object.
 */
Value pi_open(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_STRING(argv[0]))
        vm_error(vm, "[open] expects a single string argument as a file path.");

    char *mode = "r";
    if (argc >= 2)
    {
        if (IS_STRING(argv[1]))
            mode = AS_CSTRING(argv[1]);
        else
            vm_error(vm, "[open] expects a string argument as a file mode.");
    }

    PiString *path = AS_STRING(argv[0]);
    FILE *file = fopen(path->chars, mode);

    if (!file)
        vm_errorf(vm, "[open] Failed to open file: %s", path->chars);

    // Extract filename from path
    const char *fullpath = path->chars;
    const char *last = strrchr(fullpath, '/');
#ifdef _WIN32
    // Windows uses backslashes as path separators
    const char *_last = strrchr(fullpath, '\\');
    if (!last || (_last && _last > last))
        last = _last;
#endif
    const char *filename = last ? last + 1 : fullpath;

    // Make a copy of filename and mode, since they must be owned by ObjFile
    char *_filename = strdup(filename);
    char *_mode = strdup(mode);
    ObjFile *f = (ObjFile *)new_file(file, _filename, _mode);

    return NEW_OBJ(f);
}

/**
 * @brief Reads a string from the file at the current position.
 *
 * @param vm The virtual machine instance.
 * @param argc Number of arguments (should be 1).
 * @param argv Argument: [file handler]
 * @return true if successful, otherwise raises an error.
 */

Value pi_read(vm_t *vm, int argc, Value *argv)
{
    if (argc != 1 || OBJ_TYPE(argv[0]) != OBJ_FILE)
        vm_error(vm, "[read] expects a single file handler as argument.");

    ObjFile *file = AS_FILE(argv[0]);

    if (file->closed)
        vm_error(vm, "[read] File is closed.");

    size_t buffer_size = BUFFER_SIZE;
    size_t capacity = buffer_size;
    size_t length = 0;

    char *content = malloc(capacity);
    if (!content)
        vm_error(vm, "[read] Out of memory.");

    while (!feof(file->fp))
    {
        if (length + buffer_size > capacity)
        {
            capacity *= 2;
            char *new_content = realloc(content, capacity);
            if (!new_content)
            {
                free(content);
                vm_error(vm, "[read] Out of memory during read.");
            }
            content = new_content;
        }

        size_t bytes = fread(content + length, 1, buffer_size, file->fp);
        if (ferror(file->fp))
        {
            free(content);
            vm_errorf(vm, "[read] Failed to read file: %s", file->filename);
        }
        length += bytes;
    }

    content[length] = '\0';

    Value result = NEW_OBJ(new_pistring(strdup(content)));
    free(content); // assuming new_pistring makes a copy
    return result;
}

/**
 * @brief Writes a string to the file at the current position.
 *
 * @param vm The virtual machine instance.
 * @param argc Number of arguments (should be 2).
 * @param argv Arguments: [file handler, string to write]
 * @return true if successful, otherwise raises an error.
 */
Value pi_write(vm_t *vm, int argc, Value *argv)
{

    if (argc != 2 || OBJ_TYPE(argv[0]) != OBJ_FILE)
        vm_error(vm, "[write] expects a file handler and a string as arguments.");

    if (!IS_STRING(argv[1]))
        vm_error(vm, "[write] second argument must be a string.");

    ObjFile *file = AS_FILE(argv[0]);

    if (file->closed)
        vm_error(vm, "[write] File is closed.");

    char *str = AS_CSTRING(argv[1]);

    size_t written = fwrite(str, 1, strlen(str), file->fp);

    if (written < strlen(str) || ferror(file->fp))
        vm_errorf(vm, "[write] Failed to write to file: %s", file->filename);

    return NEW_BOOL(true); // or return number of bytes written if you want
}

/**
 * @brief Sets the file position to the given number of bytes from the beginning of the file.
 *
 * @param vm The virtual machine instance.
 * @param argc Number of arguments (should be 2).
 * @param argv Arguments: [file handler, byte position as number]
 * @return true if successful, otherwise raises an error.
 */
Value pi_seek(vm_t *vm, int argc, Value *argv)
{

    if (argc != 2 || OBJ_TYPE(argv[0]) != OBJ_FILE)
        vm_error(vm, "[seek] expects a file handler and a number as arguments.");

    ObjFile *file = AS_FILE(argv[0]);

    if (file->closed)
        vm_error(vm, "[seek] File is closed.");

    if (!IS_NUM(argv[1]))
        vm_error(vm, "[seek] second argument must be a number.");

    long pos = as_number(argv[1]);
    if (fseek(file->fp, pos, SEEK_SET) != 0)
        vm_errorf(vm, "[seek] Failed to seek in file: %s", file->filename);

    return NEW_BOOL(true);
}

/**
 * @brief Closes the file stream and marks it as closed.
 *
 * @param vm The virtual machine instance.
 * @param argc Number of arguments (should be 1).
 * @param argv Argument: [file handler]
 * @return true if successful, otherwise raises an error.
 */
Value pi_close(vm_t *vm, int argc, Value *argv)
{

    if (argc != 1 || OBJ_TYPE(argv[0]) != OBJ_FILE)
        vm_error(vm, "[close] expects a file handler as argument.");

    ObjFile *file = AS_FILE(argv[0]);

    if (fclose(file->fp) != 0)
        vm_errorf(vm, "[close] Failed to close file: %s", file->filename);

    file->closed = true;
    return NEW_BOOL(true);
}

static BuiltinConst io_consts[] = {
    {"BUFFER_SIZE", NEW_NUM(BUFFER_SIZE)},
    {"SEEK_SET", NEW_NUM(SEEK_SET)},
    {"SEEK_CUR", NEW_NUM(SEEK_CUR)},
    {"SEEK_END", NEW_NUM(SEEK_END)},
};

static BuiltinFunc io_functions[] = {
    {"open", pi_open},
    {"read", pi_read},
    {"write", pi_write},
    {"seek", pi_seek},
    {"close", pi_close}};

DEFINE_BUILTIN_MODULE(io_module, "io", io_functions, io_consts);