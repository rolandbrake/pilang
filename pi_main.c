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
            onExecutionFinished(!!$0); }, ok ? 1 : 0);
}

static void browser_reportError(const char *message, int line, int column)
{
    EM_ASM({
        const message = UTF8ToString($0);
        if (typeof console !== 'undefined' && console.error)
            console.error(message + ($1 >= 0 ? ` (line ${$1})` : ""));
        if (typeof onExecutionError == 'function')
            onExecutionError(message, $1, $2); }, message, line, column);
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
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include "pi_lex.h"
#include "pi_parser.h"
#include "pi_stack.h"
#include "pi_compiler.h"
#include "pi_vm.h"
#include "common.h"

// Add SDL includes
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

#ifndef TARGET_FPS
#define TARGET_FPS 60
#endif

static void print_usage(const char *program)
{
    printf("Pilangv0.0.3\n");
    printf("Usage:\n");
    printf("  %s run <file> [args...]       Run the specified Pilang file\n", program);
    printf("  %s <file> [args...]           Shorthand for 'run <file>'\n", program);
    printf("  %s dis <file>                 Print bytecode for the specified Pilang file\n", program);
    printf("  %s dis -o <output> <file>     Write bytecode to a file\n", program);
    printf("  %s fmt <file>                 Format a file in place using utils/PiCli.js\n", program);
    printf("  %s min <file>                 Minimize a file in place using utils/PiCli.js\n", program);
    printf("  %s help                       Display this help message\n", program);
}

static void handle_sigint(int sig)
{
    (void)sig;
    interrupt_requested = 1;
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
    while (vm->running && !interrupt_requested)
        run(vm);

    bool interrupted = interrupt_requested != 0;
    if (interrupted)
    {
        fprintf(stderr, "\n[ctrl+c] exectuion interrupted\n");
        interrupt_requested = 0;
    }

    clock_t end = clock();

    double time_taken = ((double)(end - start)) * 1000.0 / CLOCKS_PER_SEC;
    printf("Execution Time: %.4f ms\n", time_taken);

    // Cleanup
    free_parser(parser);
    free_vm(vm);
    free_compiler(comp);

    return interrupted ? 130 : 0;
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

static int disassemble_file(const char *filename, const char *output_filename)
{
    char *source = read_file(filename);
    if (!source)
        return 1;

    init_scanner(source);
    token_t *tokens = scan();

    compiler_t *comp = init_compiler();
    comp->source_name = strdup(filename);
    parser_t *parser = init_parser(comp, tokens, MODE_FILE);
    parse(parser);

    FILE *output = NULL;
    if (output_filename)
    {
        output = fopen(output_filename, "wb");
        if (!output)
        {
            fprintf(stderr, "Could not open output file '%s': %s\n", output_filename, strerror(errno));
            free_parser(parser);
            free_compiler(comp);
            free(source);
            return 1;
        }
        dis_setOutput(output);
    }

    dis(comp);

    if (output)
    {
        dis_setOutput(NULL);
        fclose(output);
    }

    free_parser(parser);
    free_compiler(comp);
    free(source);
    return 0;
}

static bool file_exists(const char *filename)
{
    FILE *file = fopen(filename, "rb");
    if (!file)
        return false;
    fclose(file);
    return true;
}

static char *quote_arg(const char *arg)
{
    size_t extra = 2;
    for (const char *p = arg; *p; p++)
        if (*p == '"')
            extra++;

    size_t len = strlen(arg);
    char *quoted = malloc(len + extra + 1);
    if (!quoted)
        return NULL;

    char *out = quoted;
    *out++ = '"';
    for (const char *p = arg; *p; p++)
    {
        if (*p == '"')
            *out++ = '\\';
        *out++ = *p;
    }
    *out++ = '"';
    *out = '\0';
    return quoted;
}

static int run_utilsTool(const char *mode, const char *filename)
{
    const char *tool = "utils/PiCli.js";
    if (!file_exists(tool))
    {
        fprintf(stderr, "Missing %s. Cannot run '%s'.\n", tool, mode);
        return 1;
    }

    char *quoted_tool = quote_arg(tool);
    char *quoted_mode = quote_arg(mode);
    char *quoted_file = quote_arg(filename);

    if (!quoted_tool || !quoted_mode || !quoted_file)
    {
        fprintf(stderr, "Out of memory while preparing %s command.\n", mode);
        free(quoted_tool);
        free(quoted_mode);
        free(quoted_file);
        return 1;
    }

    size_t command_len = strlen("node ") + strlen(quoted_tool) + 1 + strlen(quoted_mode) + 1 + strlen(quoted_file) + 1;
    char *command = malloc(command_len);
    if (!command)
    {
        fprintf(stderr, "Out of memory while preparing %s command.\n", mode);
        free(quoted_tool);
        free(quoted_mode);
        free(quoted_file);
        return 1;
    }

    snprintf(command, command_len, "node %s %s %s", quoted_tool, quoted_mode, quoted_file);
    int status = system(command);

    free(command);
    free(quoted_tool);
    free(quoted_mode);
    free(quoted_file);

    if (status != 0)
        fprintf(stderr, "Command '%s' failed. Make sure Node.js and the utils formatter/minifier modules are available.\n", mode);
    return status == 0 ? 0 : 1;
}

/* Returns the brace depth of a buffer: > 0 means input is incomplete. */
static int repl_braceDepth(const char *buf)
{
    int depth = 0;
    bool in_string = false;
    char string_char = 0;

    for (const char *c = buf; *c; c++)
    {
        if (in_string)
        {
            if (*c == '\\')
            {
                c++;
                continue;
            } /* skip escape */
            if (*c == string_char)
                in_string = false;
            continue;
        }
        if (*c == '"' || *c == '\'')
        {
            in_string = true;
            string_char = *c;
            continue;
        }
        if (*c == '{')
            depth++;
        else if (*c == '}')
            depth--;
    }
    return depth;
}

// Runs a REPL.
static int run_repl(void)
{
#define C_RESET "\033[0m"
#define C_BOLD "\033[1m"
#define C_RED "\033[31m"
#define C_GREEN "\033[32m"
#define C_YELLOW "\033[33m"
#define C_CYAN "\033[36m"

    printf(
        C_CYAN C_BOLD "Pilang v0.0.3" C_RESET
                      "  " C_YELLOW "(type 'exit' or press ^C to quit)" C_RESET "\n");

    compiler_t *comp = init_compiler();
    comp->source_name = strdup("<repl>");
    comp->is_repl = true;

    /* Bootstrap with an empty program so init_vm has a valid chunk. */
    init_scanner("");
    token_t *boot_tokens = scan();
    parser_t *boot_parser = init_parser(comp, boot_tokens, MODE_REPL);
    parse(boot_parser);
    free_parser(boot_parser);

    vm_t *repl_vm = init_vm(comp, "<repl>", false);

    /* Drain the bootstrap - hits OP_HALT immediately. */
    while (repl_vm->running && !interrupt_requested)
        run(repl_vm);

    size_t buf_cap = 8192;
    char *buf = malloc(buf_cap);
    if (!buf)
    {
        fprintf(stderr, C_RED "Out of memory starting REPL.\n" C_RESET);
        free_vm(repl_vm);
        free_compiler(comp);
        return 1;
    }

    buf[0] = '\0';
    size_t buf_len = 0;
    bool continuing = false; /* true while inside an open block */

    char line[4096];

    for (;;)
    {
        fputs(
            continuing
                ? C_YELLOW "... " C_RESET
                : C_GREEN ">>> " C_RESET,
            stdout);
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin))
        {
            putchar('\n');
            break;
        }

        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n')
            line[--len] = '\0';

        /* Explicit backslash continuation (separate from brace tracking). */
        bool has_backslash = (len > 0 && line[len - 1] == '\\');
        if (has_backslash)
        {
            line[--len] = '\0';
            line[len++] = ' ';
            line[len] = '\0';
        }

        /* Grow accumulation buffer. */
        if (buf_len + len + 2 > buf_cap)
        {
            buf_cap = (buf_len + len + 2) * 2;
            char *tmp = realloc(buf, buf_cap);
            if (!tmp)
            {
                fprintf(stderr, C_RED "Out of memory.\n" C_RESET);
                break;
            }
            buf = tmp;
        }

        /* Append line (with a newline separator so the lexer sees line breaks). */
        memcpy(buf + buf_len, line, len);
        buf_len += len;
        buf[buf_len++] = '\n';
        buf[buf_len] = '\0';

        /* Keep accumulating if the user ended with a backslash. */
        if (has_backslash)
        {
            continuing = true;
            continue;
        }

        /* Keep accumulating while there are unclosed braces. */
        if (repl_braceDepth(buf) > 0)
        {
            continuing = true;
            continue;
        }

        continuing = false;

        /* Skip blank input. */
        const char *p = buf;
        while (*p == ' ' || *p == '\t' || *p == '\n')
            p++;
        if (*p == '\0')
        {
            buf[0] = '\0';
            buf_len = 0;
            continue;
        }

        /* ---- Built-in REPL commands ---- */
        /* Trim trailing newline for comparison. */
        char cmd[64] = {0};
        strncpy(cmd, buf, sizeof(cmd) - 1);
        /* Strip trailing whitespace/newline from cmd. */
        int cmd_len = (int)strlen(cmd);
        while (cmd_len > 0 && (cmd[cmd_len - 1] == '\n' || cmd[cmd_len - 1] == ' '))
            cmd[--cmd_len] = '\0';

        if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0)
            break;

        if (strcmp(cmd, "help") == 0)
        {
            printf(C_CYAN C_BOLD "REPL commands:\n" C_RESET);
            printf(C_GREEN "  exit / quit" C_RESET "   Leave the REPL\n");
            printf(C_GREEN "  help" C_RESET "          Show this message\n");
            printf(C_GREEN "  clear" C_RESET "         Clear the screen\n");
            buf[0] = '\0';
            buf_len = 0;
            continue;
        }

        if (strcmp(cmd, "clear") == 0)
        {
            fputs("\033[2J\033[H", stdout);
            buf[0] = '\0';
            buf_len = 0;
            continue;
        }

        /* ------------------------------------------------------------------
         * Compile the accumulated input into the persistent compiler chunk,
         * then run only the newly appended bytecode.
         * ------------------------------------------------------------------ */
        int entry_pc = comp->code->size;

        init_scanner(buf);
        token_t *tokens = scan();
        parser_t *parser = init_parser(comp, tokens, MODE_REPL);
        parse(parser);

        bool had_error = parser->had_error;
        free_parser(parser);

        if (!had_error)
        {
            repl_vm->code = comp->code;
            repl_vm->constants = comp->constants;
            repl_vm->names = comp->names;
            repl_vm->instrs = comp->instrs;
            repl_vm->pc = entry_pc;
            repl_vm->running = true;

            while (repl_vm->running && !interrupt_requested)
                run(repl_vm);

            if (interrupt_requested)
            {
                fprintf(stderr, "\n[ctrl+c] exectuion interrupted\n");
                interrupt_requested = 0;
                // clear the buffer so stale input isn't re-executed
                buf[0] = '\0';
                buf_len = 0;
                continuing = false;
            }
        }

        buf[0] = '\0';
        buf_len = 0;
    }

    free(buf);
    free_vm(repl_vm);
    free_compiler(comp);

#undef C_RESET
#undef C_BOLD
#undef C_RED
#undef C_GREEN
#undef C_YELLOW
#undef C_CYAN

    return 0;
}

int main(int argc, char *argv[])
{
    pi_cli_argc = 0;
    pi_cli_argv = NULL;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0)
        error("SDL_Init failed: %s", SDL_GetError());

    if (TTF_Init() != 0)
        error("TTF_Init failed: %s", TTF_GetError());

    signal(SIGINT, handle_sigint);

    /* No arguments -> drop into the interactive REPL */
    if (argc < 2)
        return run_repl();

    const char *command = argv[1];

    if (strcmp(command, "help") == 0 || strcmp(command, "--help") == 0 || strcmp(command, "-h") == 0)
    {
        print_usage(argv[0]);
        return 0;
    }

    if (strcmp(command, "--version") == 0 || strcmp(command, "-v") == 0)
    {
        printf("Pilangv0.0.3\n");
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

    if (strcmp(command, "dis") == 0)
    {
        const char *input = NULL;
        const char *output = NULL;

        if (argc == 3)
        {
            input = argv[2];
        }
        else if (argc == 5 && strcmp(argv[2], "-o") == 0)
        {
            output = argv[3];
            input = argv[4];
        }
        else if (argc == 5 && strcmp(argv[3], "-o") == 0)
        {
            input = argv[2];
            output = argv[4];
        }
        else
        {
            fprintf(stderr, "Usage: %s dis <file>\n       %s dis -o <output> <file>\n", argv[0], argv[0]);
            return 1;
        }

        return disassemble_file(input, output);
    }

    if (strcmp(command, "min") == 0 || strcmp(command, "fmt") == 0)
    {
        if (argc != 3)
        {
            fprintf(stderr, "Usage: %s %s <file>\n", argv[0], command);
            return 1;
        }
        return run_utilsTool(command, argv[2]);
    }

    /* Shorthand: `pi <file>` is equivalent to `pi run <file>` */
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
