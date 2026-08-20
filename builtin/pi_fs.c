#include <sys/stat.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include <errno.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

#include "pi_fs.h"
#include "pi_builtin.h"

Value fs_read(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_OBJ_TYPE(argv[0], OBJ_FILE))
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
        if (length + buffer_size + 1 > capacity)
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

    Value result = NEW_OBJ(new_pistring(content));
    return result;
}

Value fs_readlines(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_OBJ_TYPE(argv[0], OBJ_FILE))
        vm_error(vm, "[readlines] expects a single file handler as argument.");

    ObjFile *file = AS_FILE(argv[0]);

    if (file->closed)
        vm_error(vm, "[readlines] File is closed.");

    size_t buffer_size = BUFFER_SIZE;
    size_t capacity = buffer_size;
    size_t length = 0;

    char *content = malloc(capacity);
    if (!content)
        vm_error(vm, "[readlines] Out of memory.");

    while (!feof(file->fp))
    {
        if (length + buffer_size + 1 > capacity)
        {
            capacity *= 2;
            char *new_content = realloc(content, capacity);
            if (!new_content)
            {
                free(content);
                vm_error(vm, "[readlines] Out of memory during read.");
            }
            content = new_content;
        }

        size_t bytes = fread(content + length, 1, buffer_size, file->fp);
        if (ferror(file->fp))
        {
            free(content);
            vm_errorf(vm, "[readlines] Failed to read file: %s", file->filename);
        }
        length += bytes;
    }

    content[length] = '\0';

    Value result = NEW_OBJ(new_pistring(content));
    return result;
}

Value fs_readLinesLimit(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_STRING(argv[0]))
        vm_error(vm, "[read_lines] expects a path string.");

    int limit = -1;
    if (argc >= 2)
    {
        if (!IS_NUM(argv[1]))
            vm_error(vm, "[read_lines] limit must be a number.");
        limit = (int)AS_NUM(argv[1]);
        if (limit < 0)
            vm_error(vm, "[read_lines] limit must be non-negative.");
    }

    FILE *file = fopen(AS_CSTRING(argv[0]), "r");
    if (!file)
        vm_errorf(vm, "[read_lines] Failed to open file: %s", AS_CSTRING(argv[0]));

    list_t *lines = list_create(sizeof(Value));
    if (!lines)
    {
        fclose(file);
        vm_error(vm, "[read_lines] allocation failed.");
    }

    char buffer[16384];
    int count = 0;

    while ((limit < 0 || count < limit) && fgets(buffer, sizeof(buffer), file))
    {
        size_t len = strlen(buffer);
        while (len > 0 && (buffer[len - 1] == '\n' || buffer[len - 1] == '\r'))
            buffer[--len] = '\0';

        Value line = NEW_OBJ(add_obj(vm, new_pistring(strdup(buffer))));
        list_add(lines, &line);
        count++;
    }

    fclose(file);

    PiList *result = (PiList *)new_list(lines);
    result->is_numeric = false;
    return NEW_OBJ(add_obj(vm, (Object *)result));
}

Value fs_open(vm_t *vm, int argc, Value *argv)
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

    const char *fullpath = path->chars;
    const char *last = strrchr(fullpath, '/');

#ifdef _WIN32
    /* Accept both POSIX and Windows separators when extracting the display name. */
    const char *_last = strrchr(fullpath, '\\');
    if (!last || (_last && _last > last))
        last = _last;
#endif
    const char *filename = last ? last + 1 : fullpath;

    char *_filename = strdup(filename);
    char *_mode = strdup(mode);
    ObjFile *f = (ObjFile *)new_file(file, _filename, _mode);

    return NEW_OBJ(f);
}

Value fs_seek(vm_t *vm, int argc, Value *argv)
{

    if (argc < 2 || !IS_OBJ_TYPE(argv[0], OBJ_FILE))
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

Value fs_write(vm_t *vm, int argc, Value *argv)
{

    if (argc < 2 || !IS_OBJ_TYPE(argv[0], OBJ_FILE))
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

    return NEW_BOOL(true);
}

Value fs_append(vm_t *vm, int argc, Value *argv)
{

    if (argc < 2 || !IS_OBJ_TYPE(argv[0], OBJ_FILE))
        vm_error(vm, "[append] expects a file handler and a string as arguments.");

    if (!IS_STRING(argv[1]))
        vm_error(vm, "[append] second argument must be a string.");

    ObjFile *file = AS_FILE(argv[0]);

    if (file->closed)
        vm_error(vm, "[append] File is closed.");

    char *str = AS_CSTRING(argv[1]);

    size_t written = fwrite(str, 1, strlen(str), file->fp);

    if (written < strlen(str) || ferror(file->fp))
        vm_errorf(vm, "[append] Failed to write to file: %s", file->filename);

    return NEW_BOOL(true);
}

Value fs_close(vm_t *vm, int argc, Value *argv)
{

    if (argc < 1 || !IS_OBJ_TYPE(argv[0], OBJ_FILE))
        vm_error(vm, "[close] expects a file handler as argument.");

    ObjFile *file = AS_FILE(argv[0]);

    if (fclose(file->fp) != 0)
        vm_errorf(vm, "[close] Failed to close file: %s", file->filename);

    file->closed = true;
    return NEW_BOOL(true);
}

Value fs_exists(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_STRING(argv[0]))
        vm_error(vm, "[exists] expects a string path as argument.");

    const char *path = AS_CSTRING(argv[0]);

    struct stat st;
    int result = stat(path, &st);
    if (result == -1 && errno == ENOENT)
        return NEW_BOOL(false);

    if (result != 0)
        vm_errorf(vm, "[exists] Failed to stat file: %s", path);

    return NEW_BOOL(true);
}

Value fs_isfile(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_STRING(argv[0]))
        vm_error(vm, "[isfile] expects a string path as argument.");

    struct stat st;
    if (stat(AS_CSTRING(argv[0]), &st) != 0)
        return NEW_BOOL(false);

    return NEW_BOOL(S_ISREG(st.st_mode));
}

Value fs_isdir(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_STRING(argv[0]))
        vm_error(vm, "[isdir] expects a string path as argument.");

    struct stat st;
    if (stat(AS_CSTRING(argv[0]), &st) != 0)
        return NEW_BOOL(false);

    return NEW_BOOL(S_ISDIR(st.st_mode));
}

Value fs_size(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_STRING(argv[0]))
        vm_error(vm, "[size] expects a string path as argument.");

    struct stat st;
    if (stat(AS_CSTRING(argv[0]), &st) != 0)
        vm_error(vm, "[size] cannot stat file.");

    return NEW_NUM((double)st.st_size);
}

Value fs_abspath(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_STRING(argv[0]))
        vm_error(vm, "[abspath] expects a string path as argument.");

    const char *path = AS_CSTRING(argv[0]);

#ifdef _WIN32
    char resolved[MAX_PATH];
    if (!_fullpath(resolved, path, MAX_PATH))
        vm_error(vm, "[abspath] failed.");
#else
    char resolved[PATH_MAX];
    if (!realpath(path, resolved))
        vm_error(vm, "[abspath] failed.");
#endif

    return NEW_OBJ(new_pistring(strdup(resolved)));
}

Value fs_basename(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_STRING(argv[0]))
        vm_error(vm, "[basename] expects a string.");

    const char *path = AS_CSTRING(argv[0]);
    const char *last = strrchr(path, '/');

#ifdef _WIN32
    const char *bslash = strrchr(path, '\\');
    if (!last || (bslash && bslash > last))
        last = bslash;
#endif

    const char *name = last ? last + 1 : path;
    return NEW_OBJ(new_pistring(strdup(name)));
}

Value fs_dirname(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_STRING(argv[0]))
        vm_error(vm, "[dirname] expects a string as argument.");

    char *path = strdup(AS_CSTRING(argv[0]));

    char *last = strrchr(path, '/');

#ifdef _WIN32
    /* Accept both POSIX and Windows separators. */
    char *bslash = strrchr(path, '\\');
    if (!last || (bslash && bslash > last))
        last = bslash;
#endif

    if (last)
        *last = '\0';
    else
        strcpy(path, ".");

    Value result = NEW_OBJ(new_pistring(strdup(path)));
    free(path);
    return result;
}

Value fs_ext(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_STRING(argv[0]))
        vm_error(vm, "[ext] expects a string.");

    const char *path = AS_CSTRING(argv[0]);
    const char *dot = strrchr(path, '.');

    if (!dot || dot == path)
        return NEW_OBJ(new_pistring(strdup("")));

    return NEW_OBJ(new_pistring(strdup(dot + 1)));
}

Value fs_join(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_STRING(argv[0]) || !IS_STRING(argv[1]))
        vm_error(vm, "[join] expects two strings.");

    const char *a = AS_CSTRING(argv[0]);
    const char *b = AS_CSTRING(argv[1]);

    size_t len = strlen(a) + strlen(b) + 2;

    char *result = malloc(len);

    snprintf(result, len, "%s/%s", a, b);

    Value v = NEW_OBJ(new_pistring(strdup(result)));

    free(result);

    return v;
}

Value fs_mkdir(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_STRING(argv[0]))
        vm_error(vm, "[mkdir] expects a string path.");

    const char *path = AS_CSTRING(argv[0]);

#ifdef _WIN32
    int result = _mkdir(path);
#else
    int result = mkdir(path, 0755);
#endif

    if (result != 0)
    {
        if (errno == EEXIST)
            return NEW_BOOL(false);

        vm_error(vm, "[mkdir] failed.");
    }

    return NEW_BOOL(true);
}

Value fs_rmdir(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_STRING(argv[0]))
        vm_error(vm, "[rmdir] expects a string path as argument.");

    const char *path = AS_CSTRING(argv[0]);

    int result = rmdir(path);

    if (result != 0)
        vm_error(vm, "[rmdir] failed to remove directory.");

    return NEW_BOOL(true);
}

Value fs_listdir(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_STRING(argv[0]))
        vm_error(vm, "[listdir] expects a path.");

    DIR *dir = opendir(AS_CSTRING(argv[0]));
    if (!dir)
        vm_error(vm, "[listdir] cannot open directory.");

    list_t *list = list_create(sizeof(Value));

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        list_add(list, &NEW_OBJ(new_pistring(strdup(entry->d_name))));
    }

    closedir(dir);

    return NEW_OBJ(new_list(list));
}

Value fs_cwd(vm_t *vm, int argc, Value *argv)
{
    char buffer[PATH_MAX];

    if (!getcwd(buffer, sizeof(buffer)))
        vm_error(vm, "[cwd] failed.");

    return NEW_OBJ(new_pistring(strdup(buffer)));
}

Value fs_chdir(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_STRING(argv[0]))
        vm_error(vm, "[chdir] expects a path.");

    if (chdir(AS_CSTRING(argv[0])) != 0)
        vm_error(vm, "[chdir] failed.");

    return NEW_BOOL(true);
}

Value fs_copy(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_STRING(argv[0]) || !IS_STRING(argv[1]))
        vm_error(vm, "[copy] expects src and dst.");

    FILE *src = fopen(AS_CSTRING(argv[0]), "rb");
    if (!src)
        vm_error(vm, "[copy] cannot open source.");

    FILE *dst = fopen(AS_CSTRING(argv[1]), "wb");
    if (!dst)
    {
        fclose(src);
        vm_error(vm, "[copy] cannot open destination.");
    }

    char buffer[4096];
    size_t n;

    while ((n = fread(buffer, 1, sizeof(buffer), src)) > 0)
    {
        if (fwrite(buffer, 1, n, dst) != n)
        {
            fclose(src);
            fclose(dst);
            vm_error(vm, "[copy] failed while writing destination.");
        }
    }

    if (ferror(src))
    {
        fclose(src);
        fclose(dst);
        vm_error(vm, "[copy] failed while reading source.");
    }

    fclose(src);
    fclose(dst);

    return NEW_BOOL(true);
}

Value fs_move(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_STRING(argv[0]) || !IS_STRING(argv[1]))
        vm_error(vm, "[move] expects src and dst.");

    if (rename(AS_CSTRING(argv[0]), AS_CSTRING(argv[1])) != 0)
        vm_error(vm, "[move] failed.");

    return NEW_BOOL(true);
}

Value fs_delete(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_STRING(argv[0]))
        vm_error(vm, "[delete] expects a path.");

    if (remove(AS_CSTRING(argv[0])) != 0)
        vm_error(vm, "[delete] failed.");

    return NEW_BOOL(true);
}

static BuiltinConst fs_consts[] = {

};

static BuiltinFunc fs_functions[] = {
    {"read", fs_read},
    {"readlines", fs_readlines},
    {"read_lines", fs_readLinesLimit},
    {"open", fs_open},
    {"seek", fs_seek},
    {"write", fs_write},
    {"append", fs_append},
    {"close", fs_close},
    {"exists", fs_exists},
    {"isdir", fs_isdir},
    {"isfile", fs_isfile},
    {"size", fs_size},
    {"abspath", fs_abspath},
    {"basename", fs_basename},
    {"dirname", fs_dirname},
    {"ext", fs_ext},
    {"join", fs_join},
    {"listdir", fs_listdir},
    {"mkdir", fs_mkdir},
    {"rmdir", fs_rmdir},
    {"cwd", fs_cwd},
    {"chdir", fs_chdir},
    {"copy", fs_copy},
    {"move", fs_move},
    {"delete", fs_delete},
};

DEFINE_BUILTIN_MODULE(module_fs, "fs", fs_functions, fs_consts);
