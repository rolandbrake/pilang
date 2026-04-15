#include <errno.h>
// #include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pi_os.h"
#include "../pi_object.h"
#include "../table.h"
#include "pi_builtin.h"

#ifdef __EMSCRIPTEN__

static Value os_notSupported(vm_t *vm, const char *name)
{
    vm_errorf(vm, "[os.%s] is not supported in the Emscripten build.", name);
    return NEW_NIL();
}

Value os_run(vm_t *vm, int argc, Value *argv)
{
    (void)argc;
    (void)argv;
    return os_notSupported(vm, "run");
}
Value os_spawn(vm_t *vm, int argc, Value *argv)
{
    (void)argc;
    (void)argv;
    return os_notSupported(vm, "spawn");
}
Value os_which(vm_t *vm, int argc, Value *argv)
{
    (void)argc;
    (void)argv;
    return os_notSupported(vm, "which");
}
Value os_signal(vm_t *vm, int argc, Value *argv)
{
    (void)argc;
    (void)argv;
    return os_notSupported(vm, "signal");
}
Value os_kill(vm_t *vm, int argc, Value *argv)
{
    (void)argc;
    (void)argv;
    return os_notSupported(vm, "kill");
}
Value os_hostname(vm_t *vm, int argc, Value *argv)
{
    (void)argc;
    (void)argv;
    return os_notSupported(vm, "hostname");
}
Value os_cpus(vm_t *vm, int argc, Value *argv)
{
    (void)argc;
    (void)argv;
    return os_notSupported(vm, "cpus");
}
Value os_ram(vm_t *vm, int argc, Value *argv)
{
    (void)argc;
    (void)argv;
    return os_notSupported(vm, "ram");
}
Value os_user(vm_t *vm, int argc, Value *argv)
{
    (void)argc;
    (void)argv;
    return NEW_NIL();
}

#else

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <lmcons.h>
#include <processthreadsapi.h>
#include <shlwapi.h>
#else
#include <fcntl.h>
#include <limits.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/sysinfo.h>
#endif
#endif

static Value os_string(vm_t *vm, const char *text)
{
    return NEW_OBJ(add_obj(vm, new_pistring(strdup(text ? text : ""))));
}

static PiMap *os_resultMap(vm_t *vm, const char *stdout_text, const char *stderr_text, int code)
{
    PiMap *result = (PiMap *)add_obj(vm, new_map(ht_create(sizeof(Value)), false));
    Value stdout_val = os_string(vm, stdout_text);
    Value stderr_val = os_string(vm, stderr_text);
    Value code_val = NEW_NUM(code);

    ht_put(result->table, "stdout", &stdout_val);
    ht_put(result->table, "stderr", &stderr_val);
    ht_put(result->table, "code", &code_val);
    return result;
}

static char *read_allText(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return strdup("");

    if (fseek(fp, 0, SEEK_END) != 0)
    {
        fclose(fp);
        return strdup("");
    }

    long length = ftell(fp);
    if (length < 0)
    {
        fclose(fp);
        return strdup("");
    }

    if (fseek(fp, 0, SEEK_SET) != 0)
    {
        fclose(fp);
        return strdup("");
    }

    char *buffer = (char *)malloc((size_t)length + 1);
    if (!buffer)
    {
        fclose(fp);
        return strdup("");
    }

    size_t read = fread(buffer, 1, (size_t)length, fp);
    fclose(fp);
    buffer[read] = '\0';
    return buffer;
}

#ifdef _WIN32
static bool make_tempPath(char *buffer, DWORD size)
{
    char temp_dir[MAX_PATH];
    DWORD dir_len = GetTempPathA(MAX_PATH, temp_dir);
    if (dir_len == 0 || dir_len >= MAX_PATH)
        return false;

    UINT result = GetTempFileNameA(temp_dir, "pi", 0, buffer);
    return result != 0;
}

static Value run_processWindows(vm_t *vm, const char *command, bool wait)
{
    char stdout_path[MAX_PATH];
    char stderr_path[MAX_PATH];
    if (!make_tempPath(stdout_path, MAX_PATH) || !make_tempPath(stderr_path, MAX_PATH))
        vm_error(vm, "[os] failed to create temporary files.");

    SECURITY_ATTRIBUTES sa;
    ZeroMemory(&sa, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE stdout_file = CreateFileA(stdout_path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
    HANDLE stderr_file = CreateFileA(stderr_path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);

    if (stdout_file == INVALID_HANDLE_VALUE || stderr_file == INVALID_HANDLE_VALUE)
        vm_error(vm, "[os] failed to open temporary files.");

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = stdout_file;
    si.hStdError = stderr_file;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    size_t cmd_len = strlen(command) + 16;
    char *cmdline = (char *)malloc(cmd_len);
    snprintf(cmdline, cmd_len, "cmd.exe /C %s", command);

    BOOL ok = CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, CREATE_NO_WINDOW,
                             NULL, NULL, &si, &pi);
    free(cmdline);

    CloseHandle(stdout_file);
    CloseHandle(stderr_file);

    if (!ok)
    {
        DeleteFileA(stdout_path);
        DeleteFileA(stderr_path);
        vm_error(vm, "[os] failed to create process.");
    }

    if (!wait)
    {
        DWORD pid = pi.dwProcessId;
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        DeleteFileA(stdout_path);
        DeleteFileA(stderr_path);
        return NEW_NUM((double)pid);
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 1;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    char *stdout_text = read_allText(stdout_path);
    char *stderr_text = read_allText(stderr_path);
    DeleteFileA(stdout_path);
    DeleteFileA(stderr_path);

    PiMap *result = os_resultMap(vm, stdout_text, stderr_text, (int)exit_code);
    free(stdout_text);
    free(stderr_text);
    return NEW_OBJ(result);
}
#else
static bool make_tempPath(char *buffer, size_t size)
{
    snprintf(buffer, size, "/tmp/pilangXXXXXX");
    int fd = mkstemp(buffer);
    if (fd == -1)
        return false;
    close(fd);
    return true;
}

static Value run_processPosix(vm_t *vm, const char *command, bool wait)
{
    char stdout_path[PATH_MAX];
    char stderr_path[PATH_MAX];
    if (!make_tempPath(stdout_path, sizeof(stdout_path)) || !make_tempPath(stderr_path, sizeof(stderr_path)))
        vm_error(vm, "[os] failed to create temporary files.");

    pid_t pid = fork();
    if (pid < 0)
        vm_error(vm, "[os] failed to fork.");

    if (pid == 0)
    {
        int out_fd = open(stdout_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        int err_fd = open(stderr_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);

        if (out_fd >= 0)
            dup2(out_fd, STDOUT_FILENO);
        if (err_fd >= 0)
            dup2(err_fd, STDERR_FILENO);

        if (out_fd >= 0)
            close(out_fd);
        if (err_fd >= 0)
            close(err_fd);

        execl("/bin/sh", "sh", "-c", command, (char *)NULL);
        _exit(127);
    }

    if (!wait)
    {
        unlink(stdout_path);
        unlink(stderr_path);
        return NEW_NUM((double)pid);
    }

    int status = 0;
    waitpid(pid, &status, 0);

    char *stdout_text = read_allText(stdout_path);
    char *stderr_text = read_allText(stderr_path);
    unlink(stdout_path);
    unlink(stderr_path);

    int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    PiMap *result = os_resultMap(vm, stdout_text, stderr_text, exit_code);
    free(stdout_text);
    free(stderr_text);
    return NEW_OBJ(result);
}
#endif

Value os_run(vm_t *vm, int argc, Value *argv)
{
    if (argc != 1 || !IS_STRING(argv[0]))
        vm_error(vm, "[os.run] expects a single command string.");

#ifdef _WIN32
    return run_processWindows(vm, AS_CSTRING(argv[0]), true);
#else
    return run_processPosix(vm, AS_CSTRING(argv[0]), true);
#endif
}

Value os_spawn(vm_t *vm, int argc, Value *argv)
{
    if (argc != 1 || !IS_STRING(argv[0]))
        vm_error(vm, "[os.spawn] expects a single command string.");

#ifdef _WIN32
    return run_processWindows(vm, AS_CSTRING(argv[0]), false);
#else
    return run_processPosix(vm, AS_CSTRING(argv[0]), false);
#endif
}

/**
 * @brief Finds the path of an executable in $PATH.
 *
 * @param vm The virtual machine instance.
 * @param argc Number of arguments (should be 1).
 * @param argv Argument: [executable name]
 * @return true if successful, otherwise raises an error.
 */
Value os_which(vm_t *vm, int argc, Value *argv)
{
    if (argc != 1 || !IS_STRING(argv[0]))
        vm_error(vm, "[os.which] expects a single executable name string.");

    const char *name = AS_CSTRING(argv[0]);

#ifdef _WIN32
    char resolved[MAX_PATH];
    DWORD len = SearchPathA(NULL, name, ".exe", MAX_PATH, resolved, NULL);
    if (len == 0 || len >= MAX_PATH)
    {
        len = SearchPathA(NULL, name, NULL, MAX_PATH, resolved, NULL);
        if (len == 0 || len >= MAX_PATH)
            return NEW_NIL();
    }
    return os_string(vm, resolved);
#else
    const char *path = getenv("PATH");
    if (!path)
        return NEW_NIL();

    char *paths = strdup(path);
    char *token = strtok(paths, ":");
    while (token)
    {
        char candidate[PATH_MAX];
        snprintf(candidate, sizeof(candidate), "%s/%s", token, name);
        if (access(candidate, X_OK) == 0)
        {
            Value result = os_string(vm, candidate);
            free(paths);
            return result;
        }
        token = strtok(NULL, ":");
    }
    free(paths);
    return NEW_NIL();
#endif
}

Value os_signal(vm_t *vm, int argc, Value *argv)
{
    if (argc != 2 || !IS_NUM(argv[0]))
        vm_error(vm, "[os.signal] expects (signal_number, action_string).");

    if (!IS_STRING(argv[1]))
        vm_error(vm, "[os.signal] currently supports only string actions: 'ignore' or 'default'.");

    int sig = (int)as_number(argv[0]);
    const char *action = AS_CSTRING(argv[1]);
    void (*handler)(int) = NULL;

    if (strcmp(action, "ignore") == 0)
        handler = SIG_IGN;
    else if (strcmp(action, "default") == 0)
        handler = SIG_DFL;
    else
        vm_error(vm, "[os.signal] action must be 'ignore' or 'default'.");

    if (signal(sig, handler) == SIG_ERR)
        return NEW_BOOL(false);

    return NEW_BOOL(true);
}

Value os_kill(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || argc > 2 || !IS_NUM(argv[0]))
        vm_error(vm, "[os.kill] expects (pid, [signal]).");

    int pid = (int)as_number(argv[0]);
    int sig = (argc == 2) ? (int)as_number(argv[1]) : SIGTERM;

#ifdef _WIN32
    HANDLE proc = OpenProcess(PROCESS_TERMINATE, FALSE, (DWORD)pid);
    if (!proc)
        return NEW_BOOL(false);

    BOOL ok = TerminateProcess(proc, (UINT)sig);
    CloseHandle(proc);
    return NEW_BOOL(ok != 0);
#else
    return NEW_BOOL(kill((pid_t)pid, sig) == 0);
#endif
}

Value os_hostname(vm_t *vm, int argc, Value *argv)
{
    (void)argv;
    if (argc != 0)
        vm_error(vm, "[os.hostname] expects no arguments.");

#ifdef _WIN32
    char buffer[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = sizeof(buffer);
    if (!GetComputerNameA(buffer, &size))
        vm_error(vm, "[os.hostname] failed.");
    return os_string(vm, buffer);
#else
    char buffer[256];
    if (gethostname(buffer, sizeof(buffer)) != 0)
        vm_error(vm, "[os.hostname] failed.");
    buffer[sizeof(buffer) - 1] = '\0';
    return os_string(vm, buffer);
#endif
}

Value os_cpus(vm_t *vm, int argc, Value *argv)
{
    (void)vm;
    (void)argv;
    if (argc != 0)
        vm_error(vm, "[os.cpus] expects no arguments.");

#ifdef _WIN32
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return NEW_NUM((double)info.dwNumberOfProcessors);
#else
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    return NEW_NUM((double)(cpus > 0 ? cpus : 1));
#endif
}

Value os_ram(vm_t *vm, int argc, Value *argv)
{
    (void)vm;
    (void)argv;
    if (argc != 0)
        vm_error(vm, "[os.ram] expects no arguments.");

#ifdef _WIN32
    MEMORYSTATUSEX status;
    ZeroMemory(&status, sizeof(status));
    status.dwLength = sizeof(status);
    if (!GlobalMemoryStatusEx(&status))
        vm_error(vm, "[os.ram] failed.");
    return NEW_NUM((double)status.ullTotalPhys);
#elif defined(__linux__)
    struct sysinfo info;
    if (sysinfo(&info) != 0)
        vm_error(vm, "[os.ram] failed.");
    return NEW_NUM((double)info.totalram * (double)info.mem_unit);
#else
    long pages = sysconf(_SC_PHYS_PAGES);
    long page_size = sysconf(_SC_PAGE_SIZE);
    if (pages < 0 || page_size < 0)
        vm_error(vm, "[os.ram] failed.");
    return NEW_NUM((double)pages * (double)page_size);
#endif
}

Value os_user(vm_t *vm, int argc, Value *argv)
{
    (void)argv;
    if (argc != 0)
        vm_error(vm, "[os.user] expects no arguments.");

#ifdef _WIN32
    char buffer[UNLEN + 1];
    DWORD size = sizeof(buffer);
    if (!GetUserNameA(buffer, &size))
        vm_error(vm, "[os.user] failed.");
    return os_string(vm, buffer);
#else
    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_name)
        return os_string(vm, pw->pw_name);

    const char *user = getenv("USER");
    if (user)
        return os_string(vm, user);
    return NEW_NIL();
#endif
}

#endif

static BuiltinConst os_consts[] = {
    {"SIGINT", NEW_NUM(SIGINT)},
    {"SIGTERM", NEW_NUM(SIGTERM)},
    {"SIGABRT", NEW_NUM(SIGABRT)},
};

static BuiltinFunc os_funcs[] = {
    {"run", os_run},
    {"spawn", os_spawn},
    {"which", os_which},
    {"signal", os_signal},
    {"kill", os_kill},
    {"hostname", os_hostname},
    {"cpus", os_cpus},
    {"ram", os_ram},
    {"user", os_user},
};

DEFINE_BUILTIN_MODULE(module_os, "os", os_funcs, os_consts);