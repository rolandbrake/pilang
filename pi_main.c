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

#ifndef TARGET_FPS
#define TARGET_FPS 60
#endif

vm_t *vm;
compiler_t *browser_comp = NULL;
parser_t *browser_parser = NULL;
int frame_count = 0;
double fps = 0.0;
clock_t start_time;
char *source = NULL;

bool paused = false;
bool browser_hadError = false;
bool browser_isExecuting = false;
bool browser_initialized = false;

static void browser_errorHandler(const char *message, int line, int column);

static void browser_initRuntime(void)
{
    if (browser_initialized)
        return;

    pi_cli_argc = 0;
    pi_cli_argv = NULL;
    set_errorHandler(browser_errorHandler);

    if (!source)
        source = strdup("");

    browser_initialized = true;
}

static void browser_notifyFinished(bool ok)
{
    EM_ASM({
        if (typeof onExecutionFinished == 'function')
            onExecutionFinished(!!$0);
    }, ok ? 1 : 0);
}

static void browser_reportError(const char *message, int line, int column)
{
    EM_ASM({
        const message = UTF8ToString($0);
        if (typeof console !== 'undefined' && console.error)
            console.error(message + ($1 >= 0 ? ` (line ${$1})` : ""));
        if (typeof onExecutionError == 'function')
            onExecutionError(message, $1, $2);
    }, message, line, column);
}

static void browser_printExecutionTime(void)
{
    clock_t end_time = clock();
    double time_taken = ((double)(end_time - start_time)) * 1000.0 / CLOCKS_PER_SEC;
    printf("Execution Time: %.4f ms\n", time_taken);
}

static void browser_errorHandler(const char *message, int line, int column)
{
    browser_hadError = true;
    paused = false;

    if (vm)
        vm->running = false;

    browser_reportError(message, line, column);
}

static void browser_cleanupExecution(void)
{
    if (vm)
    {
        free_vm(vm);
        vm = NULL;
    }

    if (browser_parser)
    {
        free_parser(browser_parser);
        browser_parser = NULL;
    }

    if (browser_comp)
    {
        free_compiler(browser_comp);
        browser_comp = NULL;
    }
}

static int browser_prepareExecution(void)
{
    browser_initRuntime();
    browser_cleanupExecution();
    browser_hadError = false;
    browser_isExecuting = false;
    paused = false;

    init_scanner(source ? source : "");
    token_t *tokens = scan();

    browser_comp = init_compiler();
    browser_comp->source_name = strdup("<browser>");
    browser_parser = init_parser(browser_comp, tokens, MODE_FILE);
    parse(browser_parser);

    if (browser_hadError)
        return 1;

    browser_comp->is_repl = true;
    vm = init_vm(browser_comp, "", true);
    return 0;
}

void main_loop()
{

    if (vm && vm->running)
    {
        while (vm->running && !paused)
            run(vm);

        if (!vm->running && !paused)
        {
            browser_printExecutionTime();
            browser_notifyFinished(!browser_hadError);
        }
    }
}

EMSCRIPTEN_KEEPALIVE
void set_source(const char *_source)
{
    browser_initRuntime();
    if (source)
        free(source);
    source = _source ? strdup(_source) : strdup("");
}

EMSCRIPTEN_KEEPALIVE
void stop_execution(void)
{
    browser_initRuntime();

    if (!vm)
        return;
    vm->running = false;
    paused = false;

    if (browser_isExecuting)
        return;

    browser_printExecutionTime();
    browser_notifyFinished(!browser_hadError);
}

EMSCRIPTEN_KEEPALIVE
void pause_execution(void)
{
    browser_initRuntime();
    if (!vm)
        return;

    vm->running = false;

    paused = true;
}

EMSCRIPTEN_KEEPALIVE
void resume_execution(void)
{
    browser_initRuntime();
    if (vm)
    {
        browser_isExecuting = true;
        vm->running = true;
        paused = false;

        while (vm->running && !paused)
            run(vm);

        browser_isExecuting = false;
        if (!vm->running && !paused)
        {
            browser_printExecutionTime();
            browser_notifyFinished(!browser_hadError);
        }
    }
}



EMSCRIPTEN_KEEPALIVE
int execute_source(void)
{
    if (browser_prepareExecution() != 0)
        return 1;

    start_time = clock();
    browser_isExecuting = true;

#ifdef DEBUG_BUILD
    dis(browser_comp);
#endif

    while (vm && vm->running && !paused)
        run(vm);

    if (paused)
    {
        browser_isExecuting = false;
        return 0;
    }

    if (!browser_hadError)
    {
        browser_printExecutionTime();
    }

    browser_isExecuting = false;
    browser_notifyFinished(!browser_hadError);
    return browser_hadError ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
int disassemble_source(void)
{
    if (browser_prepareExecution() != 0)
        return 1;

    dis(browser_comp);
    browser_notifyFinished(!browser_hadError);
    return browser_hadError ? 1 : 0;
}

int main(int argc, char *argv[])
{
    pi_cli_argc = argc;
    pi_cli_argv = argv;

    browser_initRuntime();
    return 0;
}

#else // Native version below

// Native (non-Emscripten) includes and main
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

// Add SDL includes
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

#ifndef TARGET_FPS
#define TARGET_FPS 60
#endif

static void print_usage(const char *program)
{
    printf("Pilangv0.0.1\n");
    printf("Usage:\n");
    printf("  %s run <file>     Run the specified Pilangfile\n", program);
    printf("  %s <file>         Shorthand for 'run <file>'\n", program);
    printf("  %s help           Display this help message\n", program);
    printf("  %s min <file>     Minimize the specified Pilangfile (coming soon)\n", program);
    printf("  %s fmt <file>     Format the specified Pilangfile (coming soon)\n", program);
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
    comp->source_name = strdup((entry_name && entry_name[0] != '\0')
                                   ? entry_name
                                   : (mode == MODE_REPL ? "<repl>" : "<source>"));
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

    // Cleanup
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

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0)
        error("SDL_Init failed: %s", SDL_GetError());
    

    if (TTF_Init() != 0)
        error("TTF_Init failed: %s", TTF_GetError());

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
        printf("Pilangv0.0.1\n");
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
