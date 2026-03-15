#ifdef __EMSCRIPTEN__

// Emscripten-specific includes and code

#include <emscripten.h>
#include <stdint.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "pi_lex.h"
#include "pi_parser.h"
#include "pi_stack.h"
#include "pi_compiler.h"
#include "pi_vm.h"
#include "./builtin/pi_audio.h"

#ifndef TARGET_FPS
#define TARGET_FPS 60
#endif

vm_t *vm;
int frame_count = 0;
double fps = 0.0;
clock_t start_time;
char *source = NULL;

bool paused = false;

void main_loop()
{

    if (vm && vm->running)
        run(vm);

    emscripten_sleep(0);
}

EMSCRIPTEN_KEEPALIVE
void set_source(const char *_source)
{
    if (source)
        free(source);
    source = _source ? strdup(_source) : strdup("");
}

EMSCRIPTEN_KEEPALIVE
void stop_execution(void)
{

    if (!vm || !vm->running)
        return;
    printf("Stopping execution from [c]\n");
    vm->running = false;
    paused = false;
    clock_t end_time = clock();
    double time_taken = ((double)(end_time - start_time)) * 1000.0 / CLOCKS_PER_SEC;
    printf("Execution Time: %.4f ms\n", time_taken);

    EM_ASM({
        if (typeof onExecutionFinished == 'function')
            onExecutionFinished();
    });

    emscripten_cancel_main_loop();
}

EMSCRIPTEN_KEEPALIVE
void pause_execution(void)
{
    vm->running = false;

    paused = true;    
}

EMSCRIPTEN_KEEPALIVE
void resume_execution(void)
{
    if (vm)
    {
        vm->running = true;
        paused = false;
    }
}

EMSCRIPTEN_KEEPALIVE
void _init_audio(void)
{
    init_audio();
}

int main(int argc, char *argv[])
{
    pi_cli_argc = argc;
    pi_cli_argv = argv;

    init_audio();

    if (!source)
        source = strdup("");

    init_scanner(source);
    token_t *tokens = scan();
    compiler_t *comp = init_compiler();    
    parser_t *parser = init_parser(comp, tokens, MODE_FILE);
    parse(parser);    

    vm = init_vm(comp, "", true);

    emscripten_set_main_loop(main_loop, 0, 1);
    return 0;
}

#else // Native version below

// Native (non-Emscripten) includes and main
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "pi_lex.h"
#include "pi_parser.h"
#include "pi_stack.h"
#include "pi_compiler.h"
#include "pi_vm.h"
#include "common.h"
#include "pi_min.h"

#ifndef TARGET_FPS
#define TARGET_FPS 60
#endif

static void print_usage(const char *program)
{
    printf("PiScript v0.0.1\n");
    printf("Usage:\n");
    printf("  %s run <file>     Run the specified PiScript file\n", program);
    printf("  %s <file>         Shorthand for 'run <file>'\n", program);
    printf("  %s help           Display this help message\n", program);
    printf("  %s min <file>     Minimize the specified PiScript file (coming soon)\n", program);
    printf("  %s fmt <file>     Format the specified PiScript file (coming soon)\n", program);
}

char *read_file(const char *filename)
{
    FILE *file = fopen(filename, "rb");
    if (!file)
    {
        fprintf(stderr, "Could not open file '%s': %s\n", filename, strerror(errno));
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0)
    {
        fprintf(stderr, "Could not seek file '%s'\n", filename);
        fclose(file);
        return NULL;
    }

    long length = ftell(file);
    if (length < 0)
    {
        fprintf(stderr, "Could not read file size for '%s'\n", filename);
        fclose(file);
        return NULL;
    }

    if (fseek(file, 0, SEEK_SET) != 0)
    {
        fprintf(stderr, "Could not seek file '%s'\n", filename);
        fclose(file);
        return NULL;
    }

    char *buffer = (char *)malloc(length + 1);
    if (!buffer)
    {
        fprintf(stderr, "Out of memory while reading '%s'\n", filename);
        fclose(file);
        return NULL;
    }
    size_t bytes_read = fread(buffer, 1, (size_t)length, file);
    if (bytes_read != (size_t)length)
    {
        fprintf(stderr, "Failed to fully read '%s'\n", filename);
        free(buffer);
        fclose(file);
        return NULL;
    }
    buffer[length] = '\0';
    fclose(file);
    return buffer;
}

static int run_source(const char *source, ParserMode mode, const char *entry_name, bool is_main)
{
    init_scanner((char *)source);
    token_t *tokens = scan();

    compiler_t *comp = init_compiler();
    parser_t *parser = init_parser(comp, tokens, mode);
    parse(parser);

    
#ifdef DEBUG_BUILD
    dis(comp);
#endif

    vm_t *vm = init_vm(comp, entry_name, is_main);

    clock_t start = clock();
    while (vm->running)
        run(vm);
    clock_t end = clock();

    double time_taken = ((double)(end - start)) * 1000.0 / CLOCKS_PER_SEC;
    printf("Execution Time: %.4f ms\n", time_taken);

    free_parser(parser);
    free_vm(vm);
    free_compiler(comp);

    return 0;
}

static int run_file(const char *filename)
{
    char *source = read_file(filename);
    if (!source)
        return 1;

    int status = run_source(source, MODE_FILE, filename, true);
    free(source);
    return status;
}

int main(int argc, char *argv[])
{
    pi_cli_argc = 0;
    pi_cli_argv = NULL;

    if (argc < 2)
    {
        print_usage(argv[0]);
        return 1;
    }

    const char *command = argv[1];

    if (strcmp(command, "help") == 0 || strcmp(command, "--help") == 0 || strcmp(command, "-h") == 0)
    {
        print_usage(argv[0]);
        return 0;
    }

    if (strcmp(command, "--version") == 0 || strcmp(command, "-v") == 0)
    {
        printf("PiScript v0.0.1\n");
        return 0;
    }

    if (strcmp(command, "run") == 0)
    {
        if (argc < 3)
        {
            fprintf(stderr, "Usage: %s run <file> [args...]\n", argv[0]);
            return 1;
        }
        pi_cli_argc = argc - 2;
        pi_cli_argv = &argv[2];
        return run_file(argv[2]);
    }

    if (strcmp(command, "min") == 0 || strcmp(command, "fmt") == 0)
    {
        if (argc != 3)
        {
            fprintf(stderr, "Usage: %s %s <file>\n", argv[0], command);
            return 1;
        }
        printf("Command '%s' on file '%s' is not yet implemented.\n", command, argv[2]);
        return 0;
    }

    // Shorthand: allow `pi <file>` as equivalent to `pi run <file>`.
    if (argc >= 2)
    {
        pi_cli_argc = argc - 1;
        pi_cli_argv = &argv[1];
        return run_file(command);
    }

    fprintf(stderr, "Unknown command: %s\nUse '%s help' to see available commands.\n", command, argv[0]);
    return 1;
}

#endif
