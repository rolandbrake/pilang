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
#include "pi_build.h"
#include "pi_module.h"
#include "pi_package.h"
#include "pi_vm.h"

#ifndef TARGET_FPS
#define TARGET_FPS 60
#endif

VM *vm;
Compiler *browser_comp = NULL;
Parser *browser_parser = NULL;
int frame_count = 0;
double fps = 0.0;
clock_t start_time;
char *source = NULL;

bool paused = FALSE;
bool browser_hadError = FALSE;
bool browser_isExecuting = FALSE;
bool browser_initialized = FALSE;

static void _browser_errorHandler(const char *message, int line, int column);

static void _browser_initRuntime(void)
{
    if (browser_initialized)
        return;

    pi_cli_argc = 0;
    pi_cli_argv = NULL;
    set_errorHandler(_browser_errorHandler);

    if (!source)
        source = strdup("");

    browser_initialized = TRUE;
}

static void _browser_notifyFinished(bool ok)
{
    EM_ASM({
        if (typeof on_execution_finished == 'function')
            on_execution_finished(!!$0); }, ok ? 1 : 0);
}

static void _browser_reportError(const char *message, int line, int column)
{
    EM_ASM({
        const message = UTF8ToString($0);
        if (typeof console !== 'undefined' && console.error)
            console.error(message + ($1 >= 0 ? ` (line ${$1})` : ""));
        if (typeof on_execution_error == 'function')
            on_execution_error(message, $1, $2); }, message, line, column);
}

static void _browser_printExecutionTime(void)
{
    clock_t end_time = clock();
    double time_taken = ((double)(end_time - start_time)) * 1000.0 / CLOCKS_PER_SEC;
    printf("Execution Time: %.4f ms\n", time_taken);
}

static void _browser_errorHandler(const char *message, int line, int column)
{
    browser_hadError = TRUE;
    paused = FALSE;

    if (vm)
        vm->running = FALSE;

    _browser_reportError(message, line, column);
}

static void _browser_cleanupExecution(void)
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

static int _browser_prepareExecution(void)
{
    _browser_initRuntime();
    _browser_cleanupExecution();
    browser_hadError = FALSE;
    browser_isExecuting = FALSE;
    paused = FALSE;

    init_scanner(source ? source : "");
    Token *tokens = scan();

    browser_comp = init_compiler();
    browser_comp->source_name = strdup("<browser>");
    browser_parser = init_parser(browser_comp, tokens, MODE_FILE);
    parse(browser_parser);

    if (browser_hadError)
        return 1;

    browser_comp->is_repl = TRUE;
    vm = init_vm(browser_comp, "", TRUE);
    return 0;
}

void main_loop()
{

    if (vm && vm->running)
    {
        while (vm->running && !paused)
            vm_run(vm);

        if (!vm->running && !paused)
        {
            _browser_printExecutionTime();
            _browser_notifyFinished(!browser_hadError);
        }
    }
}

EMSCRIPTEN_KEEPALIVE
void set_source(const char *_source)
{
    _browser_initRuntime();
    if (source)
        free(source);
    source = _source ? strdup(_source) : strdup("");
}

EMSCRIPTEN_KEEPALIVE
void stop_execution(void)
{
    _browser_initRuntime();

    if (!vm)
        return;
    vm->running = FALSE;
    paused = FALSE;

    if (browser_isExecuting)
        return;

    _browser_printExecutionTime();
    _browser_notifyFinished(!browser_hadError);
}

EMSCRIPTEN_KEEPALIVE
void pause_execution(void)
{
    _browser_initRuntime();
    if (!vm)
        return;

    vm->running = FALSE;

    paused = TRUE;
}

EMSCRIPTEN_KEEPALIVE
void resume_execution(void)
{
    _browser_initRuntime();
    if (vm)
    {
        browser_isExecuting = TRUE;
        vm->running = TRUE;
        paused = FALSE;

        while (vm->running && !paused)
            vm_run(vm);

        browser_isExecuting = FALSE;
        if (!vm->running && !paused)
        {
            _browser_printExecutionTime();
            _browser_notifyFinished(!browser_hadError);
        }
    }
}

EMSCRIPTEN_KEEPALIVE
int execute_source(void)
{
    if (_browser_prepareExecution() != 0)
        return 1;

    start_time = clock();
    browser_isExecuting = TRUE;

#ifdef DEBUG_BUILD
    dis(browser_comp);
#endif

    while (vm && vm->running && !paused)
        vm_run(vm);

    if (paused)
    {
        browser_isExecuting = FALSE;
        return 0;
    }

    if (!browser_hadError)
    {
        _browser_printExecutionTime();
    }

    browser_isExecuting = FALSE;
    _browser_notifyFinished(!browser_hadError);
    return browser_hadError ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
int disassemble_source(void)
{
    if (_browser_prepareExecution() != 0)
        return 1;

    dis(browser_comp);
    _browser_notifyFinished(!browser_hadError);
    return browser_hadError ? 1 : 0;
}

int main(int argc, char *argv[])
{
    pi_cli_argc = argc;
    pi_cli_argv = argv;

    _browser_initRuntime();
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
#ifdef _WIN32
#include <windows.h>
#endif
#include "pi_lex.h"
#include "pi_parser.h"
#include "pi_stack.h"
#include "pi_compiler.h"
#include "pi_build.h"
#include "pi_module.h"
#include "pi_package.h"
#include "pi_vm.h"
#include "common.h"

// Add SDL includes
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

#ifndef TARGET_FPS
#define TARGET_FPS 60
#endif

static bool package_colors;

static void _package_initOutput(void)
{
#ifdef _WIN32
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode;
    package_colors = output != INVALID_HANDLE_VALUE && GetConsoleMode(output, &mode) &&
                     SetConsoleMode(output, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif
}

static const char *_package_color(const char *color)
{
    return package_colors ? color : "";
}

static void _print_usage(const char *program)
{
    printf("Pilangv0.1.4\n");
    printf("Usage:\n");
    printf("  %s                            Start the interactive prompt\n", program);
    printf("  %s run [file] [args...]       Run main.pi or the specified Pilang file\n", program);
    printf("  %s <file> [args...]           Shorthand for 'run <file>'\n", program);
    printf("  %s dis <file>                 Print bytecode for the specified Pilang file\n", program);
    printf("  %s dis -o <output> <file>     Write bytecode to a file\n", program);
    printf("  %s build [file]               Build main.pi or the specified project\n", program);
    printf("  %s pack [target:win]          Validate package.loon and build its entry project\n", program);
    printf("  %s fmt <file>                 Format a file in place\n", program);
    printf("  %s min <file>                 Minimize a file in place\n", program);
    printf("  %s help                       Display this help message\n", program);
}

static void _handle_sigint(int sig)
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

static int _run_compiler(Compiler *comp, Parser *parser, const char *entry_name,
                        bool is_main, PX_ProjectBuild *project_build)
{
#ifdef DEBUG_BUILD
    dis(comp);
#endif

    VM *vm = init_vm(comp, entry_name, is_main);
    vm->project_build = project_build;

    clock_t start = clock();
    while (vm->running && !interrupt_requested)
        vm_run(vm);

    bool interrupted = interrupt_requested != 0;
    if (interrupted)
    {
        fprintf(stderr, "\n[ctrl+c] exectuion interrupted\n");
        interrupt_requested = 0;
    }

    clock_t end = clock();
    double time_taken = ((double)(end - start)) * 1000.0 / CLOCKS_PER_SEC;
    printf("Execution Time: %.4f ms\n", time_taken);

    if (parser)
        free_parser(parser);
    free_vm(vm);
    free_compiler(comp);
    return interrupted ? 130 : 0;
}

static int _run_source(const char *source, ParserMode mode, const char *entry_name,
                      bool is_main)
{
    init_scanner((char *)source);
    Token *tokens = scan();

    Compiler *comp = init_compiler();
    comp->source_name = strdup((entry_name && entry_name[0] != '\0')
                                   ? entry_name
                                   : (mode == MODE_REPL ? "<repl>" : "<source>"));
    Parser *parser = init_parser(comp, tokens, mode);
    parse(parser);
    return _run_compiler(comp, parser, entry_name, is_main, NULL);
}

static char *_build_pathFor(const char *filename)
{
    size_t length = strlen(filename);
    bool has_pi_extension = length >= 3 && strcmp(filename + length - 3, ".pi") == 0;
    size_t base_length = has_pi_extension ? length - 3 : length;
    char *path = malloc(base_length + 4);
    if (!path)
        return NULL;
    memcpy(path, filename, base_length);
    memcpy(path + base_length, ".px", 4);
    return path;
}

static bool _has_extension(const char *path, const char *extension);
static int _run_executableFile(const char *filename);

static int _run_file(const char *filename)
{
    if (_has_extension(filename, ".px"))
        return _run_executableFile(filename);

    char *source = read_file(filename);
    if (!source)
        return 1;

    int status;
    char *build_path = _build_pathFor(filename);
    PX_ProjectBuild *build = calloc(1, sizeof(*build));
    if (build_path && build && px_readProjectBuild(build_path, filename, source, build))
    {
        PX_BuiltModule *entry = NULL;
        for (uint32_t i = 0; i < build->module_count; i++)
            if (build->modules[i].is_entry)
                entry = &build->modules[i];

        if (entry && entry->compiler)
        {
            Compiler *comp = entry->compiler;
            entry->compiler = NULL;
            comp->source_name = strdup(filename);
            status = _run_compiler(comp, NULL, filename, TRUE, build);
            build = NULL; // run_compiler transfers ownership to the VM.
        }
        else
        {
            px_freeProjectBuild(build);
            status = _run_source(source, MODE_FILE, filename, TRUE);
        }
    }
    else
    {
        if (build)
            px_freeProjectBuild(build);
        status = _run_source(source, MODE_FILE, filename, TRUE);
    }

    free(build_path);
    free(build);
    free(source);
    return status;
}

static char *_source_dirName(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    const char *end = slash;
    if (!end || (backslash && backslash > end))
        end = backslash;
    if (!end)
        return strdup(".");
    size_t length = (size_t)(end - path);
    if (length == 2 && path[1] == ':')
        length++;
    char *directory = malloc(length + 1);
    if (!directory)
        return NULL;
    memcpy(directory, path, length);
    directory[length] = '\0';
    return directory;
}

static bool _build_hasModule(const PX_ProjectBuild *build, const char *path)
{
    return px_findBuiltModule((PX_ProjectBuild *)build, path) != NULL;
}

static bool _build_addModule(PX_ProjectBuild *build, const char *name,
                            char *path, Compiler *compiler, const char *source)
{
    uint32_t count = build->module_count;
    PX_BuiltModule *modules = realloc(build->modules, (size_t)(count + 1) * sizeof(*modules));
    if (!modules)
        return FALSE;
    build->modules = modules;
    build->modules[count] = (PX_BuiltModule){
        .name = strdup(name),
        .path = path,
        .compiler = compiler,
        .source_size = strlen(source),
        .source_hash = px_hashBytes(source, strlen(source)),
    };
    if (!build->modules[count].name)
        return FALSE;
    build->module_count++;
    return TRUE;
}

static bool _build_compileImports(PX_ProjectBuild *build, Compiler *compiler,
                                 const char *base_path);

static bool _build_compileImportsInCode(PX_ProjectBuild *build, Compiler *compiler,
                                       List *code, const char *base_path)
{
    uint8_t *bytes = code->data;
    for (int pc = 0; pc < code->size;)
    {
        OpCode op = (OpCode)bytes[pc++];
        if (op == OP_LOAD_CONST && pc + 1 < code->size &&
            bytes[pc + 2] == OP_IMPORT)
        {
            int index = (bytes[pc] << 8) | bytes[pc + 1];
            if (index >= 0 && index < compiler->constants->size)
            {
                Value value = LIST_AT(compiler->constants, index);
                if (IS_STRING(value))
                {
                    char *path = module_resolvePathFrom(base_path, AS_CSTRING(value));
                    if (path && !_build_hasModule(build, path))
                    {
                        char *source = read_file(path);
                        if (!source)
                        {
                            free(path);
                            return FALSE;
                        }
                        Compiler *module_compiler = init_compiler();
                        module_compiler->source_name = strdup(path);
                        init_scanner(source);
                        Parser *parser = init_parser(module_compiler, scan(), MODE_FILE);
                        parse(parser);
                        bool valid = !parser->had_error;
                        free_parser(parser);
                        if (!valid || !_build_addModule(build, AS_CSTRING(value), path,
                                                       module_compiler, source))
                        {
                            free(path);
                            free(source);
                            free_compiler(module_compiler);
                            return FALSE;
                        }
                        free(source);
                        char *module_base = _source_dirName(path);
                        if (!module_base || !_build_compileImports(build, module_compiler, module_base))
                        {
                            free(module_base);
                            return FALSE;
                        }
                        free(module_base);
                        path = NULL; // The project build owns the resolved path.
                    }
                    free(path);
                }
            }
        }
        pc += _operand_count(op);
    }

    return TRUE;
}

static bool _build_compileImports(PX_ProjectBuild *build, Compiler *compiler,
                                 const char *base_path)
{
    if (!_build_compileImportsInCode(build, compiler, compiler->code, base_path))
        return FALSE;

    /* Function and method bodies share the module constant table. Scan each
       body once, rather than recursing through that shared table. */
    for (int i = 0; i < compiler->constants->size; i++)
    {
        Value value = LIST_AT(compiler->constants, i);
        if (IS_OBJ_TYPE(value, OBJ_CODE) &&
            !_build_compileImportsInCode(build, compiler, AS_CODE(value)->data, base_path))
            return FALSE;
    }
    return TRUE;
}

static int _build_file(const char *filename)
{
    char *source = read_file(filename);
    char *build_path = _build_pathFor(filename);
    if (!source || !build_path)
    {
        free(source);
        free(build_path);
        return 1;
    }

    Compiler *compiler = init_compiler();
    compiler->source_name = strdup(filename);
    init_scanner(source);
    Parser *parser = init_parser(compiler, scan(), MODE_FILE);
    parse(parser);
    bool valid = !parser->had_error;
    free_parser(parser);

    PX_ProjectBuild modules = {0};
    char *base_path = _source_dirName(filename);
    if (valid && base_path)
        valid = _build_compileImports(&modules, compiler, base_path);

    bool written = valid && px_writeProjectBuild(build_path, filename, source, compiler, &modules);
    if (written)
        printf("Build completed: %s\n", build_path);
    else
        fprintf(stderr, "Could not write build '%s'.\n", build_path);

    free(base_path);
    px_freeProjectBuild(&modules);
    free_compiler(compiler);
    free(build_path);
    free(source);
    return written ? 0 : 1;
}

static bool _has_extension(const char *path, const char *extension)
{
    size_t path_length = strlen(path);
    size_t extension_length = strlen(extension);
    return path_length >= extension_length &&
           strcmp(path + path_length - extension_length, extension) == 0;
}

static int _run_executableFile(const char *filename)
{
    PX_ProjectBuild *build = calloc(1, sizeof(*build));
    if (!build || !px_readExecutableBuild(filename, build))
    {
        free(build);
        fprintf(stderr, "Could not load executable '%s'.\n", filename);
        return 1;
    }

    PX_BuiltModule *entry = NULL;
    for (uint32_t i = 0; i < build->module_count; i++)
        if (build->modules[i].is_entry)
            entry = &build->modules[i];

    if (!entry || !entry->compiler)
    {
        px_freeProjectBuild(build);
        free(build);
        fprintf(stderr, "Executable '%s' has no entry module.\n", filename);
        return 1;
    }

    Compiler *comp = entry->compiler;
    entry->compiler = NULL;
    comp->source_name = strdup(entry->path);
    int status = _run_compiler(comp, NULL, entry->path, TRUE, build);
    return status;
}

static int _run_embeddedBuild(const char *filename, uint64_t offset)
{
    PX_ProjectBuild *build = calloc(1, sizeof(*build));
    if (!build || !px_readEmbeddedBuild(filename, offset, build))
    {
        free(build);
        fprintf(stderr, "Could not load packaged application '%s'.\n", filename);
        return 1;
    }
    PX_BuiltModule *entry = NULL;
    for (uint32_t i = 0; i < build->module_count; i++)
        if (build->modules[i].is_entry)
            entry = &build->modules[i];
    if (!entry || !entry->compiler)
    {
        px_freeProjectBuild(build);
        free(build);
        return 1;
    }
    Compiler *comp = entry->compiler;
    entry->compiler = NULL;
    comp->source_name = strdup(entry->path);
    return _run_compiler(comp, NULL, entry->path, TRUE, build);
}

static int _disassemble_file(const char *filename, const char *output_filename)
{
    char *source = read_file(filename);
    if (!source)
        return 1;

    init_scanner(source);
    Token *tokens = scan();

    Compiler *comp = init_compiler();
    comp->source_name = strdup(filename);
    Parser *parser = init_parser(comp, tokens, MODE_FILE);
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

static bool _file_exists(const char *filename)
{
    FILE *file = fopen(filename, "rb");
    if (!file)
        return FALSE;
    fclose(file);
    return TRUE;
}

static char *_quote_arg(const char *arg)
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

static char *_utils_ToolPath(void)
{
#ifdef _WIN32
    char executable[MAX_PATH];
    DWORD length = GetModuleFileNameA(NULL, executable, sizeof(executable));
    if (length == 0 || length >= sizeof(executable))
        return NULL;

    char *separator = strrchr(executable, '\\');
    char *alternate_separator = strrchr(executable, '/');
    if (!separator || (alternate_separator && alternate_separator > separator))
        separator = alternate_separator;
    if (!separator)
        return NULL;

    const char *tool_name = "PiForminator.px";
    size_t directory_length = (size_t)(separator - executable) + 1;
    char *tool = malloc(directory_length + strlen(tool_name) + 1);
    if (!tool)
        return NULL;

    memcpy(tool, executable, directory_length);
    strcpy(tool + directory_length, tool_name);
    return tool;
#else
    return strdup("bin/PiForminator.px");
#endif
}

static int _run_utilsTool(const char *mode, const char *filename)
{
    char *tool = _utils_ToolPath();
    if (!tool || !_file_exists(tool))
    {
        fprintf(stderr, "Missing PiForminator.px beside the pilang executable. Build utils/PiForminator.pi first.\n");
        free(tool);
        return 1;
    }

    char *tool_argv[] = {tool, (char *)mode, (char *)filename};
    pi_cli_argc = 3;
    pi_cli_argv = tool_argv;
    int result = _run_executableFile(tool);
    free(tool);
    return result;
}

/* Returns the brace depth of a buffer: > 0 means input is incomplete. */
static int _repl_braceDepth(const char *buf)
{
    int depth = 0;
    bool in_string = FALSE;
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
                in_string = FALSE;
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
static int _run_repl(void)
{
#define C_RESET "\033[0m"
#define C_BOLD "\033[1m"
#define C_RED "\033[31m"
#define C_GREEN "\033[32m"
#define C_YELLOW "\033[33m"
#define C_CYAN "\033[36m"

    printf(
        C_CYAN C_BOLD "Pilang v0.1.4" C_RESET
                      "  " C_YELLOW "(type 'exit' or press ^C to quit)" C_RESET "\n");

    Compiler *comp = init_compiler();
    comp->source_name = strdup("<repl>");
    comp->is_repl = true;

    /* Bootstrap with an empty program so init_vm has a valid chunk. */
    init_scanner("");
    Token *boot_tokens = scan();
    Parser *boot_parser = init_parser(comp, boot_tokens, MODE_REPL);
    parse(boot_parser);
    free_parser(boot_parser);

    VM *repl_vm = init_vm(comp, "<repl>", false);

    /* Drain the bootstrap - hits OP_HALT immediately. */
    while (repl_vm->running && !interrupt_requested)
        vm_run(repl_vm);

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
        if (_repl_braceDepth(buf) > 0)
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
        Token *tokens = scan();
        Parser *parser = init_parser(comp, tokens, MODE_REPL);
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
                vm_run(repl_vm);

            if (interrupt_requested)
            {
                fprintf(stderr, "\n[ctrl+c] exectuion interrupted\n");
                interrupt_requested = 0;
                // clear the buffer so stale input isn't re-executed
                buf[0] = '\0';
                buf_len = 0;
                continuing = FALSE;
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
    static char *main_argv[] = {"main.pi"};
    pi_cli_argc = 0;
    pi_cli_argv = NULL;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0)
        error("SDL_Init failed: %s", SDL_GetError());

    if (TTF_Init() != 0)
        error("TTF_Init failed: %s", TTF_GetError());

    signal(SIGINT, _handle_sigint);

#ifdef _WIN32
    char executable[MAX_PATH];
    DWORD executable_length = GetModuleFileNameA(NULL, executable, sizeof(executable));
    uint64_t embedded_offset;
    bool embedded_console;
    if (executable_length > 0 && executable_length < sizeof(executable) &&
        package_embeddedOffset(executable, &embedded_offset, &embedded_console))
    {
        if (!embedded_console)
            FreeConsole();
        return _run_embeddedBuild(executable, embedded_offset);
    }
#endif

    /* No arguments -> drop into the interactive REPL. */
    if (argc < 2)
        return _run_repl();

    const char *command = argv[1];

    if (strcmp(command, "help") == 0 || strcmp(command, "--help") == 0 || strcmp(command, "-h") == 0)
    {
        _print_usage(argv[0]);
        return 0;
    }

    if (strcmp(command, "--version") == 0 || strcmp(command, "-v") == 0)
    {
        printf("Pilang: 0.1.3\n");
        return 0;
    }

    if (strcmp(command, "build") == 0)
    {
        const char *input = argc == 2 ? "main.pi" : argc == 3 ? argv[2]
                                                              : NULL;
        if (!input)
        {
            fprintf(stderr, "Usage: %s build [file]\n", argv[0]);
            return 1;
        }
        if (argc == 2 && !_file_exists(input))
        {
            fprintf(stderr, "Could not find main.pi in the current directory.\n");
            return 1;
        }
        pi_cli_argc = 1;
        pi_cli_argv = argc == 2 ? main_argv : &argv[2];
        return _build_file(input);
    }

    if (strcmp(command, "pack") == 0)
    {
        _package_initOutput();
        const char *target = "win";
        if (argc == 3 && strncmp(argv[2], "target:", 7) == 0)
            target = argv[2] + 7;
        else if (argc != 2)
        {
            fprintf(stderr, "Usage: %s pack [target:win]\n", argv[0]);
            return 1;
        }
        if (strcmp(target, "win") != 0)
        {
            fprintf(stderr, "%sPackaging failed:%s target '%s' is not supported yet.\n",
                    _package_color("\033[31m"), _package_color("\033[0m"), target);
            return 1;
        }
        PiPackage package;
        if (!package_load(&package, "win"))
        {
            fprintf(stderr, "%sPackaging failed:%s package.loon is invalid.\n",
                    _package_color("\033[31m"), _package_color("\033[0m"));
            return 1;
        }
        printf("%sPackaging%s %s %s (%s)\n", _package_color("\033[36m"),
               _package_color("\033[0m"), package.name, package.version, target);
        int status = _build_file(package.entry);
        if (status != 0)
        {
            fprintf(stderr, "%sPackaging failed:%s could not compile '%s'.\n",
                    _package_color("\033[31m"), _package_color("\033[0m"), package.entry);
            package_free(&package);
            return status;
        }
        const char *runtime_path = argv[0];
#ifdef _WIN32
        char runtime_path_buffer[MAX_PATH];
        DWORD runtime_length = GetModuleFileNameA(NULL, runtime_path_buffer, sizeof(runtime_path_buffer));
        if (runtime_length == 0 || runtime_length >= sizeof(runtime_path_buffer))
        {
            fprintf(stderr, "%sPackaging failed:%s could not locate the Pilang executable.\n",
                    _package_color("\033[31m"), _package_color("\033[0m"));
            package_free(&package);
            return 1;
        }
        runtime_path = runtime_path_buffer;
#endif
        char package_error[256];
        if (!package_bundle(runtime_path, &package, package_error, sizeof(package_error)))
        {
            fprintf(stderr, "%sPackaging failed:%s %s\n", _package_color("\033[31m"),
                    _package_color("\033[0m"), package_error[0] ? package_error : "unknown error");
            status = 1;
        }
        else
            printf("%sPackage created:%s %s\n", _package_color("\033[32m"),
                   _package_color("\033[0m"), package.output);
        package_free(&package);
        return status;
    }

    if (strcmp(command, "run") == 0)
    {
        if (argc == 2)
        {
            if (!_file_exists("main.pi"))
            {
                fprintf(stderr, "Could not find main.pi in the current directory.\n");
                return 1;
            }
            pi_cli_argc = 1;
            pi_cli_argv = main_argv;
            return _run_file("main.pi");
        }
        pi_cli_argc = argc - 2;
        pi_cli_argv = &argv[2];
        return _run_file(argv[2]);
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

        return _disassemble_file(input, output);
    }

    if (strcmp(command, "min") == 0 || strcmp(command, "fmt") == 0)
    {
        if (argc != 3)
        {
            fprintf(stderr, "Usage: %s %s <file>\n", argv[0], command);
            return 1;
        }
        return _run_utilsTool(command, argv[2]);
    }

    /* Shorthand: `pi <file>` is equivalent to `pi run <file>` */
    if (argc >= 2)
    {
        pi_cli_argc = argc - 1;
        pi_cli_argv = &argv[1];
        return _run_file(command);
    }

    fprintf(stderr, "Unknown command: %s\nUse '%s help' to see available commands.\n", command, argv[0]);
    return 1;
}

#endif
