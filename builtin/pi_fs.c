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
#include <direct.h> // _mkdir
#else
#include <sys/stat.h> // mkdir
#endif

#include "pi_fs.h"
#include "pi_builtin.h"

/**
 * @brief Reads a string from the file at the current position.
 *
 * @param vm The virtual machine instance.
 * @param argc Number of arguments (should be 1).
 * @param argv Argument: [file handler]
 * @return true if successful, otherwise raises an error.
 */

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

/**
 * @brief Reads all lines from the file at the current position.
 *
 * @param vm The virtual machine instance.
 * @param argc Number of arguments (should be 1).
 * @param argv Argument: [file handler]
 * @return A string containing all lines from the file.
 */
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

/**
 * @brief Opens a file and returns a file handler.
 *
 * @param vm The virtual machine instance.
 * @param argc Number of arguments (should be 1 or 2).
 * @param argv Arguments: [file path], [file mode]
 * @return A file handler object.
 */
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
 * @brief Sets the file position to the given number of bytes from the beginning of the file.
 *
 * @param vm The virtual machine instance.
 * @param argc Number of arguments (should be 2).
 * @param argv Arguments: [file handler, byte position as number]
 * @return true if successful, otherwise raises an error.
 */
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

/**
 * @brief Writes a string to the file at the current position.
 *
 * @param vm The virtual machine instance.
 * @param argc Number of arguments (should be 2).
 * @param argv Arguments: [file handler, string to write]
 * @return true if successful, otherwise raises an error.
 */
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

    return NEW_BOOL(true); // or return number of bytes written if you want
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

    return NEW_BOOL(true); // or return number of bytes written if you want
}

/**
 * @brief Closes the file stream and marks it as closed.
 *
 * @param vm The virtual machine instance.
 * @param argc Number of arguments (should be 1).
 * @param argv Argument: [file handler]
 * @return true if successful, otherwise raises an error.
 */
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

/**
 * @brief Checks whether a file exists at the given path.
 *
 * @param vm The virtual machine instance.
 * @param argc Number of arguments (should be 1).
 * @param argv Argument: [path as string]
 * @return true if the file exists, otherwise false.
 */
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

/**
 * @brief Checks whether a file is a regular file.
 *
 * @param vm The virtual machine instance.
 * @param argc Number of arguments (should be 1).
 * @param argv Argument: [path as string]
 * @return true if the file is a regular file, otherwise false.
 */
Value fs_isfile(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_STRING(argv[0]))
        vm_error(vm, "[isfile] expects a string path as argument.");

    struct stat st;
    if (stat(AS_CSTRING(argv[0]), &st) != 0)
        return NEW_BOOL(false);

    // Check if the file is a regular file
    return NEW_BOOL(S_ISREG(st.st_mode));
}

/**
 * @brief Checks whether a file path is a directory.
 *
 * @param vm The virtual machine instance.
 * @param argc Number of arguments (should be 1).
 * @param argv Argument: [path as string]
 * @return true if the file path is a directory, otherwise false.
 */
Value fs_isdir(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_STRING(argv[0]))
        vm_error(vm, "[isdir] expects a string path as argument.");

    struct stat st;
    if (stat(AS_CSTRING(argv[0]), &st) != 0)
        return NEW_BOOL(false);

    // Check if the file is a directory
    return NEW_BOOL(S_ISDIR(st.st_mode));
}

/**
 * @brief Returns the size of the file at the given path.
 *
 * @param vm The virtual machine instance.
 * @param argc Number of arguments (should be 1).
 * @param argv Argument: [path as string]
 * @return The size of the file in bytes as a number.
 */
Value fs_size(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_STRING(argv[0]))
        vm_error(vm, "[size] expects a string path as argument.");

    struct stat st;
    if (stat(AS_CSTRING(argv[0]), &st) != 0)
        vm_error(vm, "[size] cannot stat file.");

    // Return the size of the file in bytes
    return NEW_NUM((double)st.st_size);
}

/**
 * Creates a new directory with the given path.
 *
 * @param vm The virtual machine instance.
 * @param argc Number of arguments (should be 1).
 * @param argv Argument: [path as string]
 * @return true if the directory was created successfully, false otherwise.
 */
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

/**
 * @brief Returns the base name of the given path.
 *
 * @param vm The virtual machine instance.
 * @param argc Number of arguments (should be 1).
 * @param argv Argument: [path as string]
 * @return The base name of the given path as a string.
 *
 * @note This function uses the strrchr() system call to find the last
 * occurrence of the '/' character in the given path. If no
 * such character is found, the entire path is returned as the
 * base name. On Windows systems, the function also checks for
 * the '\\' character.
 */
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

/**
 * @brief Returns the directory name of the given path.
 *
 * @param vm The virtual machine instance.
 * @param argc Number of arguments (should be 1).
 * @param argv Argument: [path as string]
 * @return The directory name of the given path as a string.
 *
 * @note This function uses the strrchr() system call to find the last
 * occurrence of the '/' character in the given path. If no
 * such character is found, the entire path is returned as the
 * directory name. On Windows systems, the function also checks for
 * the '\\' character.
 */
Value fs_dirname(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_STRING(argv[0]))
        vm_error(vm, "[dirname] expects a string as argument.");

    // Duplicate the path to avoid modifying the original string
    char *path = strdup(AS_CSTRING(argv[0]));

    // Find the last occurrence of the '/' character in the given path
    char *last = strrchr(path, '/');

#ifdef _WIN32
    // On Windows, also check for the '\\' character
    char *bslash = strrchr(path, '\\');
    if (!last || (bslash && bslash > last))
        last = bslash;
#endif

    // If the last occurrence is found, truncate the path at that point
    if (last)
        *last = '\0';
    else
        // If no last occurrence is found, the entire path is the directory name
        strcpy(path, ".");

    // Return the directory name as a string
    Value result = NEW_OBJ(new_pistring(strdup(path)));
    free(path);
    return result;
}

/**
 * Returns the file extension of the given path.
 *
 * @param vm The virtual machine instance.
 * @param argc Number of arguments (should be 1).
 * @param argv Argument: [path as string]
 * @return The file extension of the given path as a string.
 */
Value fs_ext(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_STRING(argv[0]))
        vm_error(vm, "[ext] expects a string.");

    const char *path = AS_CSTRING(argv[0]);
    const char *dot = strrchr(path, '.');

    // If no dot is found, return an empty string
    if (!dot || dot == path)
        return NEW_OBJ(new_pistring(strdup("")));

    // Return the file extension as a string
    return NEW_OBJ(new_pistring(strdup(dot + 1)));
}

/**
 * Joins two paths together using the appropriate separator.
 *
 * @param vm The virtual machine instance.
 * @param argc Number of arguments (should be 2).
 * @param argv Arguments: [path1 as string, path2 as string]
 * @return The joined path as a string.
 */
Value fs_join(vm_t *vm, int argc, Value *argv)
{
    // Check argument count and types
    if (argc < 2 || !IS_STRING(argv[0]) || !IS_STRING(argv[1]))
        vm_error(vm, "[join] expects two strings.");

    // Get the path strings
    const char *a = AS_CSTRING(argv[0]);
    const char *b = AS_CSTRING(argv[1]);

    // Calculate the length of the joined path
    size_t len = strlen(a) + strlen(b) + 2; // +2 for the separator and the null terminator

    // Allocate memory for the joined path
    char *result = malloc(len);

    // Join the paths using snprintf to avoid buffer overflow
    snprintf(result, len, "%s/%s", a, b);

    // Create a new string object for the joined path
    Value v = NEW_OBJ(new_pistring(strdup(result)));

    // Free the allocated memory
    free(result);

    // Return the joined path
    return v;
}

/**
 * Creates a new directory with the given path.
 *
 * @param vm The virtual machine instance.
 * @param argc Number of arguments (should be 1).
 * @param argv Argument: [path as string]
 * @return true if the directory was created successfully, false otherwise.
 */
Value fs_mkdir(vm_t *vm, int argc, Value *argv)
{
    // Check argument count and type
    if (argc < 1 || !IS_STRING(argv[0]))
        vm_error(vm, "[mkdir] expects a string path.");

    // Get the path string
    const char *path = AS_CSTRING(argv[0]);

    // Create the directory using the appropriate system call
#ifdef _WIN32
    int result = _mkdir(path);
#else
    int result = mkdir(path, 0755);
#endif

    // Handle errors
    if (result != 0)
    {
        if (errno == EEXIST)
            return NEW_BOOL(false);

        vm_error(vm, "[mkdir] failed.");
    }

    // Return true if the directory was created successfully
    return NEW_BOOL(true);
}

/**
 * Removes a directory with the given path.
 *
 * @param vm The virtual machine instance.
 * @param argc Number of arguments (should be 1).
 * @param argv Argument: [path as string]
 * @return true if the directory was removed successfully, false otherwise.
 */
Value fs_rmdir(vm_t *vm, int argc, Value *argv)
{
    // Check argument count and type
    if (argc < 1 || !IS_STRING(argv[0]))
        vm_error(vm, "[rmdir] expects a string path as argument.");

    // Get the path string
    const char *path = AS_CSTRING(argv[0]);

    // Attempt to remove the directory
    int result = rmdir(path);

    // Handle errors
    if (result != 0)
        vm_error(vm, "[rmdir] failed to remove directory.");

    // Return true if the directory was removed successfully
    return NEW_BOOL(true);
}

/**
 * Lists the contents of a directory.
 *
 * @param vm The virtual machine instance.
 * @param argc Number of arguments (should be 1).
 * @param argv Argument: [path as string]
 * @return An array of strings, each representing the name of a file or directory.
 */
Value fs_listdir(vm_t *vm, int argc, Value *argv)
{
    // Check argument count and types
    if (argc < 1 || !IS_STRING(argv[0]))
        vm_error(vm, "[listdir] expects a path.");

    // Open the directory
    DIR *dir = opendir(AS_CSTRING(argv[0]));
    if (!dir)
        vm_error(vm, "[listdir] cannot open directory.");

    // Create an array to store the directory contents
    list_t *list = list_create(sizeof(Value));

    // Iterate over the directory contents
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        // Push each entry as a string into the list
        list_add(list, &NEW_OBJ(new_pistring(strdup(entry->d_name))));
    }

    // Close the directory
    closedir(dir);

    // Return the list of directory contents
    return NEW_OBJ(new_list(list));
}

/**
 * Returns the current working directory.
 *
 * @param vm The virtual machine instance.
 * @param argc Number of arguments (should be 0).
 * @param argv No arguments.
 * @return The current working directory as a string.
 */
Value fs_cwd(vm_t *vm, int argc, Value *argv)
{
    // Allocate a buffer to store the current working directory
    char buffer[PATH_MAX];

    // Get the current working directory
    if (!getcwd(buffer, sizeof(buffer)))
        vm_error(vm, "[cwd] failed.");

    // Return the current working directory as a string
    return NEW_OBJ(new_pistring(strdup(buffer)));
}

/**
 * Changes the current working directory to the given path.
 *
 * @param vm The virtual machine instance.
 * @param argc Number of arguments (should be 1).
 * @param argv Argument: [path as string]
 * @return true if successful, otherwise raises an error.
 */
Value fs_chdir(vm_t *vm, int argc, Value *argv)
{
    // Check argument count and types
    if (argc < 1 || !IS_STRING(argv[0]))
        vm_error(vm, "[chdir] expects a path.");

    // Change the current working directory
    if (chdir(AS_CSTRING(argv[0])) != 0)
        vm_error(vm, "[chdir] failed.");

    // Return true if successful
    return NEW_BOOL(true);
}

/**
 * Copies the file at the source path to the destination path.
 *
 * @param vm The virtual machine instance.
 * @param argc Number of arguments (should be 2).
 * @param argv Arguments: [source path as string], [destination path as string]
 * @return true if successful, otherwise raises an error.
 */
Value fs_copy(vm_t *vm, int argc, Value *argv)
{
    // Check argument count and types
    if (argc < 2 || !IS_STRING(argv[0]) || !IS_STRING(argv[1]))
        vm_error(vm, "[copy] expects src and dst.");

    // Open source and destination files
    FILE *src = fopen(AS_CSTRING(argv[0]), "rb");
    if (!src)
        vm_error(vm, "[copy] cannot open source.");

    FILE *dst = fopen(AS_CSTRING(argv[1]), "wb");
    if (!dst)
    {
        fclose(src);
        vm_error(vm, "[copy] cannot open destination.");
    }

    // Read from source and write to destination in chunks
    char buffer[4096];
    size_t n;

    while ((n = fread(buffer, 1, sizeof(buffer), src)) > 0)
        fwrite(buffer, 1, n, dst);

    // Close files
    fclose(src);
    fclose(dst);

    // Return true if successful
    return NEW_BOOL(true);
}

/**
 * Renames the file or directory at the source path to the destination path.
 *
 * @param vm The virtual machine instance.
 * @param argc Number of arguments (should be 2).
 * @param argv Arguments: [source path as string], [destination path as string]
 * @return true if successful, otherwise raises an error.
 */
Value fs_move(vm_t *vm, int argc, Value *argv)
{
    // Check argument count and types
    if (argc < 2 || !IS_STRING(argv[0]) || !IS_STRING(argv[1]))
        vm_error(vm, "[move] expects src and dst.");

    // Rename the file or directory
    if (rename(AS_CSTRING(argv[0]), AS_CSTRING(argv[1])) != 0)
        vm_error(vm, "[move] failed.");

    // Return true if successful
    return NEW_BOOL(true);
}

/**
 * Deletes the file or directory at the given path.
 *
 * @param vm The virtual machine instance.
 * @param argc Number of arguments (should be 1).
 * @param argv Argument: [path as string]
 * @return true if successful, otherwise raises an error.
 */
Value fs_delete(vm_t *vm, int argc, Value *argv)
{
    // Check argument count and types
    if (argc < 1 || !IS_STRING(argv[0]))
        vm_error(vm, "[delete] expects a path.");

    // Delete the file or directory
    if (remove(AS_CSTRING(argv[0])) != 0)
        vm_error(vm, "[delete] failed.");

    // Return true if successful
    return NEW_BOOL(true);
}

// Register the module
static BuiltinConst fs_consts[] = {

};

static BuiltinFunc fs_functions[] = {
    {"read", fs_read},
    {"readlines", fs_readlines},
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
    // {"stat", fs_stat},
    {"cwd", fs_cwd},
    {"chdir", fs_chdir},
    {"copy", fs_copy},
    {"move", fs_move},
    {"delete", fs_delete},
};

DEFINE_BUILTIN_MODULE(module_fs, "fs", fs_functions, fs_consts);
