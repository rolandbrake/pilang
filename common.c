
#include <stdint.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include "common.h"
#include "pi_vm.h"

error_handlerFn global_errorHandler = NULL;
int pi_cli_argc = 0;
char **pi_cli_argv = NULL;

bool is_absolute_path(const char *path)
{
    if (!path || !path[0])
        return false;

#ifdef _WIN32
    return (isalpha((unsigned char)path[0]) && path[1] == ':') ||
           (path[0] == '\\' && path[1] == '\\');
#else
    return path[0] == '/';
#endif
}

char *resolve_path(vm_t *vm, const char *path)
{
    if (is_absolute_path(path) || !vm || !vm->current_path)
        return strdup(path);

    size_t length = strlen(vm->current_path) + 1 + strlen(path) + 1;
    char *resolved = malloc(length);
    if (resolved)
        snprintf(resolved, length, "%s/%s", vm->current_path, path);
    return resolved;
}

char *itos(int num)
{
    // Determine the length of the number
    int length = 0;
    int temp = num;

    // Handle the case of 0 explicitly
    if (temp == 0)
        length = 1;
    else
        // Calculate the length of the number
        while (temp != 0)
        {
            length++;
            temp /= 10;
        }

    // Allocate memory for the string (including space for the null terminator)
    char *str = (char *)malloc((length + 1) * sizeof(char));

    // Handle negative numbers
    if (num < 0)
    {
        str[0] = '-';
        num = -num;
    }

    // Convert each digit to a character
    int i = length - 1;
    do
    {
        str[i] = (num % 10) + '0';
        num /= 10;
        i--;
    } while (num != 0);

    // Null-terminate the string
    str[length] = '\0';

    return str;
}

void error(const char *format, ...)
{
    va_list args;
    va_start(args, format);

    fflush(stdout); // Make sure stdout is flushed before printing to stderr
    fprintf(stderr, "[Error] ");
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");

    va_end(args);
    exit(EXIT_FAILURE);
}

/**
 * Sets a custom error handler function to be called on parsing errors.
 *
 * @param handler A function pointer to the custom error handler. If NULL,
 *                the default behavior (printing to stderr and exiting) is restored.
 */
void set_errorHandler(error_handlerFn handler)
{
    global_errorHandler = handler;
}

