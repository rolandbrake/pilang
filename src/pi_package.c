#include "pi_package.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "builtin/pi_loon.h"

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#endif

static bool _copy_file(FILE *source, FILE *destination);
static void _bundle_error(char *error, size_t error_size, const char *message);

static FILE *_open_splashAsset(const char *runtime_path, const char *asset)
{
    const char *separator = strrchr(runtime_path, '/');
    const char *backslash = strrchr(runtime_path, '\\');
    if (!separator || (backslash && backslash > separator))
        separator = backslash;
    if (separator)
    {
        size_t directory_length = (size_t)(separator - runtime_path);
        char *path = malloc(directory_length + strlen("/../resources/") + strlen(asset) + 1);
        if (path)
        {
            memcpy(path, runtime_path, directory_length);
            sprintf(path + directory_length, "/../resources/%s", asset);
            FILE *file = fopen(path, "rb");
            free(path);
            if (file)
                return file;
        }
    }
    char path[256];
    snprintf(path, sizeof(path), "resources/%s", asset);
    return fopen(path, "rb");
}

static char *_read_text(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file)
        return NULL;
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *text = length >= 0 ? malloc((size_t)length + 1) : NULL;
    if (!text || fread(text, 1, (size_t)length, file) != (size_t)length)
    {
        free(text);
        fclose(file);
        return NULL;
    }
    text[length] = '\0';
    fclose(file);
    return text;
}

static const LoonValue *_required_string(const LoonValue *manifest, const char *key)
{
    const LoonValue *value = loon_objectGet(manifest, key);
    if (!value || value->type != LOON_STRING)
        fprintf(stderr, "Manifest field '%s' must be a string.\n", key);
    return value && value->type == LOON_STRING ? value : NULL;
}

static bool _optional_string(const LoonValue *object, const char *key, const LoonValue **value)
{
    *value = loon_objectGet(object, key);
    if (*value && (*value)->type != LOON_STRING)
    {
        fprintf(stderr, "Manifest field '%s' must be a string.\n", key);
        return FALSE;
    }
    return TRUE;
}

bool package_load(PiPackage *package, const char *target)
{
    memset(package, 0, sizeof(*package));
    char *source = _read_text("package.loon");
    if (!source)
    {
        fprintf(stderr, "Could not read package.loon in the current directory.\n");
        return FALSE;
    }
    LoonError error = {0};
    LoonValue *manifest = loon_parse(source, &error);
    free(source);
    if (!manifest)
    {
        fprintf(stderr, "Invalid package.loon at %zu:%zu: %s\n", error.line, error.column, error.message);
        return FALSE;
    }
    if (manifest->type != LOON_OBJECT)
    {
        fprintf(stderr, "package.loon must contain an object.\n");
        loon_free(manifest);
        return FALSE;
    }
    const LoonValue *name = _required_string(manifest, "name");
    const LoonValue *version = _required_string(manifest, "version");
    const LoonValue *entry = _required_string(manifest, "entry");
    const LoonValue *targets = loon_objectGet(manifest, "targets");
    const LoonValue *config = targets ? loon_objectGet(targets, target) : NULL;
    if (targets && targets->type != LOON_OBJECT)
    {
        fprintf(stderr, "Manifest field 'targets' must be an object.\n");
        config = NULL;
    }
    const LoonValue *output = config ? _required_string(config, "output") : NULL;
    const LoonValue *icon = NULL;
    const LoonValue *files = config ? loon_objectGet(config, "files") : NULL;
    const LoonValue *console = config ? loon_objectGet(config, "console") : NULL;
    if (!name || !version || !entry || !output || !config || config->type != LOON_OBJECT ||
        !_optional_string(config, "icon", &icon) ||
        (console && console->type != LOON_BOOL))
    {
        if (!config)
            fprintf(stderr, "Manifest has no '%s' target.\n", target);
        else if (console && console->type != LOON_BOOL)
            fprintf(stderr, "Manifest field 'console' must be true or false.\n");
        loon_free(manifest);
        return FALSE;
    }
    package->name = strdup(name->as.string);
    package->version = strdup(version->as.string);
    package->entry = strdup(entry->as.string);
    package->output = strdup(output->as.string);
    package->icon = icon ? strdup(icon->as.string) : NULL;
    package->console = !console || console->as.boolean;
    if (files)
    {
        if (files->type != LOON_ARRAY)
        {
            fprintf(stderr, "Manifest field 'files' must be an array of strings.\n");
            loon_free(manifest);
            package_free(package);
            return FALSE;
        }
        package->files = calloc(files->as.array.count, sizeof(char *));
        package->file_count = files->as.array.count;
        for (size_t i = 0; package->files && i < package->file_count; i++)
        {
            if (files->as.array.items[i]->type != LOON_STRING ||
                !(package->files[i] = strdup(files->as.array.items[i]->as.string)))
            {
                fprintf(stderr, "Manifest field 'files' must be an array of strings.\n");
                loon_free(manifest);
                package_free(package);
                return FALSE;
            }
        }
    }
    loon_free(manifest);
    if (!package->name || !package->version || !package->entry || !package->output ||
        (icon && !package->icon))
    {
        package_free(package);
        return FALSE;
    }
    return TRUE;
}

void package_free(PiPackage *package)
{
    free(package->name);
    free(package->version);
    free(package->entry);
    free(package->output);
    free(package->icon);
    for (size_t i = 0; i < package->file_count; i++)
        free(package->files[i]);
    free(package->files);
    memset(package, 0, sizeof(*package));
}

#ifdef _WIN32
static bool _web_safePath(const char *path)
{
    return path && path[0] && path[0] != '/' && path[0] != '\\' && !strchr(path, ':') && !strstr(path, "..");
}

static bool _web_copyAsset(const char *runtime_path, const char *asset, const char *output)
{
    FILE *source = _open_splashAsset(runtime_path, asset);
    FILE *destination = source ? fopen(output, "wb") : NULL;
    bool ok = source && destination && _copy_file(source, destination);
    if (source)
        fclose(source);
    if (destination)
        fclose(destination);
    return ok;
}

static bool _web_writeHtmlEscaped(FILE *output, const char *text)
{
    for (; *text; text++)
    {
        const char *escaped = NULL;
        switch (*text)
        {
        case '&':
            escaped = "&amp;";
            break;
        case '<':
            escaped = "&lt;";
            break;
        case '>':
            escaped = "&gt;";
            break;
        case '"':
            escaped = "&quot;";
            break;
        case '\'':
            escaped = "&#39;";
            break;
        }
        if (escaped)
        {
            if (fputs(escaped, output) == EOF)
                return FALSE;
        }
        else if (fputc((unsigned char)*text, output) == EOF)
            return FALSE;
    }
    return TRUE;
}

static bool _web_writeJsonString(FILE *output, const char *text)
{
    if (fputc('"', output) == EOF)
        return FALSE;
    for (; *text; text++)
    {
        if (*text == '"' || *text == '\\')
        {
            if (fputc('\\', output) == EOF)
                return FALSE;
        }
        if (fputc((unsigned char)*text, output) == EOF)
            return FALSE;
    }
    return fputc('"', output) != EOF;
}

static bool _web_writeTemplate(FILE *output, const char *template, const char *files, const PiPackage *package)
{
    const char *cursor = template;
    while (*cursor)
    {
        const char *token = strstr(cursor, "{{");
        if (!token)
            return fputs(cursor, output) != EOF;
        if (fwrite(cursor, 1, (size_t)(token - cursor), output) != (size_t)(token - cursor))
            return FALSE;
        if (!strncmp(token, "{{TITLE}}", 9))
        {
            if (!_web_writeHtmlEscaped(output, package->name))
                return FALSE;
            cursor = token + 9;
        }
        else if (!strncmp(token, "{{ICON}}", 8))
        {
            if (package->icon && _web_safePath(package->icon) &&
                (fputs("<link rel=\"icon\" href=\"", output) == EOF ||
                 !_web_writeHtmlEscaped(output, package->icon) || fputs("\">", output) == EOF))
                return FALSE;
            cursor = token + 8;
        }
        else if (!strncmp(token, "{{FILES}}", 9))
        {
            if (fputs(files, output) == EOF)
                return FALSE;
            cursor = token + 9;
        }
        else if (fputc(*cursor++, output) == EOF)
            return FALSE;
    }
    return TRUE;
}

static bool _web_makeParents(char *path)
{
    for (char *p = path + 1; *p; p++)
        if (*p == '/' || *p == '\\')
        {
            char saved = *p;
            *p = '\0';
            if (_mkdir(path) != 0 && errno != EEXIST)
            {
                *p = saved;
                return FALSE;
            }
            *p = saved;
        }
    return TRUE;
}

static bool _web_removeTree(const char *path)
{
    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES)
        return GetLastError() == ERROR_FILE_NOT_FOUND;
    if (!(attr & FILE_ATTRIBUTE_DIRECTORY))
        return DeleteFileA(path) != 0;

    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*", path);
    WIN32_FIND_DATAA item;
    HANDLE find = FindFirstFileA(pattern, &item);
    if (find == INVALID_HANDLE_VALUE)
        return RemoveDirectoryA(path) != 0;

    bool ok = TRUE;
    do
    {
        if (!strcmp(item.cFileName, ".") || !strcmp(item.cFileName, ".."))
            continue;
        char child[MAX_PATH];
        snprintf(child, sizeof(child), "%s\\%s", path, item.cFileName);
        if (!_web_removeTree(child))
        {
            ok = FALSE;
            break;
        }
    } while (FindNextFileA(find, &item));
    FindClose(find);
    return ok && RemoveDirectoryA(path) != 0;
}

static bool _web_copyPath(const char *output, const char *source, const char *relative, FILE *files, bool *first)
{
    DWORD attr = GetFileAttributesA(source);
    if (attr == INVALID_FILE_ATTRIBUTES)
        return FALSE;
    if (!(attr & FILE_ATTRIBUTE_DIRECTORY))
    {
        char target[MAX_PATH];
        snprintf(target, sizeof(target), "%s/%s", output, relative);
        if (!_web_makeParents(target))
            return FALSE;
        FILE *in = fopen(source, "rb"), *out = in ? fopen(target, "wb") : NULL;
        bool ok = in && out && _copy_file(in, out);
        if (in)
            fclose(in);
        if (out)
            fclose(out);
        if (ok)
        {
            if ((!*first && fputc(',', files) == EOF) || !_web_writeJsonString(files, relative))
                return FALSE;
            *first = FALSE;
        }
        return ok;
    }
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*", source);
    WIN32_FIND_DATAA item;
    HANDLE find = FindFirstFileA(pattern, &item);
    if (find == INVALID_HANDLE_VALUE)
        return FALSE;
    bool ok = TRUE;
    do
    {
        if (!strcmp(item.cFileName, ".") || !strcmp(item.cFileName, ".."))
            continue;
        char child_source[MAX_PATH], child_relative[MAX_PATH];
        snprintf(child_source, sizeof(child_source), "%s\\%s", source, item.cFileName);
        snprintf(child_relative, sizeof(child_relative), "%s/%s", relative, item.cFileName);
        if (!_web_copyPath(output, child_source, child_relative, files, first))
        {
            ok = FALSE;
            break;
        }
    } while (FindNextFileA(find, &item));
    FindClose(find);
    return ok;
}

bool package_web(const char *runtime_path, const PiPackage *package, char *error, size_t error_size)
{
    char root[MAX_PATH];
    if (!GetModuleFileNameA(NULL, root, sizeof(root)))
    {
        _bundle_error(error, error_size, "could not locate Pilang runtime");
        return FALSE;
    }
    char *bin = strrchr(root, '\\');
    if (!bin)
        return FALSE;
    *bin = '\0';
    char *project = strrchr(root, '\\');
    if (!project)
        return FALSE;
    *project = '\0';
    char command[MAX_PATH + 32];
    snprintf(command, sizeof(command), "make -C \"%s\" web", root);
    if (system(command) != 0)
    {
        _bundle_error(error, error_size, "Emscripten web runtime build failed");
        return FALSE;
    }
    if (!_web_safePath(package->output))
    {
        _bundle_error(error, error_size, "could not create web output directory");
        return FALSE;
    }
    if (!_web_removeTree(package->output))
    {
        DWORD attr = GetFileAttributesA(package->output);
        if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY))
        {
            _bundle_error(error, error_size, "could not create web output directory");
            return FALSE;
        }
    }
    else if (_mkdir(package->output) != 0)
    {
        _bundle_error(error, error_size, "could not create web output directory");
        return FALSE;
    }
    char entry_px[MAX_PATH];
    snprintf(entry_px, sizeof(entry_px), "%s", package->entry);
    char *extension = strrchr(entry_px, '.');
    if (extension && strcmp(extension, ".pi") == 0)
        strcpy(extension, ".px");
    else
        strncat(entry_px, ".px", sizeof(entry_px) - strlen(entry_px) - 1);

    FILE *files = tmpfile();
    bool first = TRUE;
    bool ok = files && _web_safePath(entry_px) && _web_copyPath(package->output, entry_px, entry_px, files, &first);
    for (size_t i = 0; ok && i < package->file_count; i++)
        ok = _web_safePath(package->files[i]) && _web_copyPath(package->output, package->files[i], package->files[i], files, &first);
    char splash[MAX_PATH];
    snprintf(splash, sizeof(splash), "%s/_launch.png", package->output);
    ok = ok && _web_copyAsset(runtime_path, "_launch.png", splash);
    long files_size = files && fseek(files, 0, SEEK_END) == 0 ? ftell(files) : -1;
    if (files_size < 0 || fseek(files, 0, SEEK_SET) != 0)
        ok = FALSE;
    char *file_list = ok ? malloc((size_t)files_size + 1) : NULL;
    if (!file_list || fread(file_list, 1, (size_t)files_size, files) != (size_t)files_size)
        ok = FALSE;
    if (files)
        fclose(files);
    if (file_list)
        file_list[files_size] = '\0';
    FILE *template_file = ok ? _open_splashAsset(runtime_path, "web-package.html") : NULL;
    if (!template_file)
    {
        free(file_list);
        _bundle_error(error, error_size, "could not read web template 'resources/web-package.html'");
        return FALSE;
    }
    if (fseek(template_file, 0, SEEK_END) != 0)
        ok = FALSE;
    long template_size = ok ? ftell(template_file) : -1;
    if (template_size < 0 || fseek(template_file, 0, SEEK_SET) != 0)
        ok = FALSE;
    char *template = ok ? malloc((size_t)template_size + 1) : NULL;
    if (!template || fread(template, 1, (size_t)template_size, template_file) != (size_t)template_size)
        ok = FALSE;
    fclose(template_file);
    if (template)
        template[template_size] = '\0';
    char html[MAX_PATH];
    snprintf(html, sizeof(html), "%s/index.html", package->output);
    FILE *index = ok ? fopen(html, "wb") : NULL;
    if (!index || !_web_writeTemplate(index, template, file_list, package))
        ok = FALSE;
    if (index)
        fclose(index);
    free(template);
    free(file_list);
    char js[MAX_PATH], wasm[MAX_PATH], target[MAX_PATH];
    snprintf(js, sizeof(js), "%s/bin/pilang.js", root);
    snprintf(wasm, sizeof(wasm), "%s/bin/pilang.wasm", root);
    snprintf(target, sizeof(target), "%s/pilang.js", package->output);
    FILE *in = fopen(js, "rb"), *out = in ? fopen(target, "wb") : NULL;
    ok = ok && in && out && _copy_file(in, out);
    if (in)
        fclose(in);
    if (out)
        fclose(out);
    snprintf(target, sizeof(target), "%s/pilang.wasm", package->output);
    in = fopen(wasm, "rb");
    out = in ? fopen(target, "wb") : NULL;
    ok = ok && in && out && _copy_file(in, out);
    if (in)
        fclose(in);
    if (out)
        fclose(out);
    if (!ok)
        _bundle_error(error, error_size, "could not copy web runtime or assets");
    return ok;
}
#else
bool package_web(const char *runtime_path, const PiPackage *package, char *error, size_t error_size)
{
    (void)runtime_path;
    (void)package;
    _bundle_error(error, error_size, "web packaging is available from Windows development builds only");
    return FALSE;
}
#endif

static bool _copy_file(FILE *source, FILE *destination)
{
    char buffer[8192];
    size_t count;
    while ((count = fread(buffer, 1, sizeof(buffer), source)) != 0)
        if (fwrite(buffer, 1, count, destination) != count)
            return FALSE;
    return !ferror(source);
}

static void _bundle_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size)
        snprintf(error, error_size, "%s", message);
}

#ifdef _WIN32
static bool _set_windowsIcon(const char *executable, const char *icon_path)
{
    unsigned char signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    FILE *file = fopen(icon_path, "rb");
    if (!file)
        return FALSE;
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    unsigned char *png = length > 24 ? malloc((size_t)length) : NULL;
    bool ok = png && fread(png, 1, (size_t)length, file) == (size_t)length;
    fclose(file);
    if (!ok)
    {
        free(png);
        return FALSE;
    }
    const unsigned char *image = png;
    uint32_t image_size = (uint32_t)length;
    unsigned char group[20] = {0, 0, 1, 0, 1, 0};
    if (memcmp(png, signature, sizeof(signature)) == 0)
    {
        uint32_t width = ((uint32_t)png[16] << 24) | ((uint32_t)png[17] << 16) |
                         ((uint32_t)png[18] << 8) | png[19];
        uint32_t height = ((uint32_t)png[20] << 24) | ((uint32_t)png[21] << 16) |
                          ((uint32_t)png[22] << 8) | png[23];
        if (!width || !height || width > 256 || height > 256)
            ok = FALSE;
        group[6] = (unsigned char)(width == 256 ? 0 : width);
        group[7] = (unsigned char)(height == 256 ? 0 : height);
        group[10] = 1;
        group[12] = 32;
    }
    else if (length >= 22 && png[0] == 0 && png[1] == 0 && png[2] == 1 && png[3] == 0)
    {
        uint16_t count = (uint16_t)png[4] | ((uint16_t)png[5] << 8);
        const unsigned char *selected = NULL;
        uint32_t selected_pixels = 0;
        for (uint16_t i = 0; i < count && 6 + (size_t)(i + 1) * 16 <= (size_t)length; i++)
        {
            const unsigned char *entry = png + 6 + (size_t)i * 16;
            uint32_t width = entry[0] ? entry[0] : 256;
            uint32_t height = entry[1] ? entry[1] : 256;
            uint32_t size = (uint32_t)entry[8] | ((uint32_t)entry[9] << 8) |
                            ((uint32_t)entry[10] << 16) | ((uint32_t)entry[11] << 24);
            uint32_t offset = (uint32_t)entry[12] | ((uint32_t)entry[13] << 8) |
                              ((uint32_t)entry[14] << 16) | ((uint32_t)entry[15] << 24);
            if (size && offset <= (uint32_t)length && size <= (uint32_t)length - offset &&
                width * height > selected_pixels)
            {
                selected = entry;
                selected_pixels = width * height;
            }
        }
        if (!selected)
            ok = FALSE;
        else
        {
            image_size = (uint32_t)selected[8] | ((uint32_t)selected[9] << 8) |
                         ((uint32_t)selected[10] << 16) | ((uint32_t)selected[11] << 24);
            uint32_t offset = (uint32_t)selected[12] | ((uint32_t)selected[13] << 8) |
                              ((uint32_t)selected[14] << 16) | ((uint32_t)selected[15] << 24);
            image = png + offset;
            memcpy(group + 6, selected, 8);
        }
    }
    else
        ok = FALSE;
    group[14] = (unsigned char)image_size;
    group[15] = (unsigned char)(image_size >> 8);
    group[16] = (unsigned char)(image_size >> 16);
    group[17] = (unsigned char)(image_size >> 24);
    group[18] = 1;
    HANDLE update = BeginUpdateResourceA(executable, FALSE);
    if (!update)
        ok = FALSE;
    else
    {
        ok = ok && UpdateResourceA(update, RT_ICON, MAKEINTRESOURCEA(1), 0, (void *)image, image_size) &&
             UpdateResourceA(update, RT_GROUP_ICON, MAKEINTRESOURCEA(1), 0, group, sizeof(group));
        if (ok)
            ok = EndUpdateResourceA(update, FALSE);
        else
            EndUpdateResourceA(update, TRUE);
    }
    free(png);
    return ok;
}
#endif

bool package_bundle(const char *runtime_path, const PiPackage *package, char *error, size_t error_size)
{
    if (error && error_size)
        error[0] = '\0';
    size_t length = strlen(package->entry);
    char *payload_path = malloc(length + 2);
    if (!payload_path)
    {
        _bundle_error(error, error_size, "out of memory while preparing the compiled application");
        return FALSE;
    }
    memcpy(payload_path, package->entry, length + 1);
    char *extension = strrchr(payload_path, '.');
    if (extension && strcmp(extension, ".pi") == 0)
        strcpy(extension, ".px");
    else
        strcat(payload_path, ".px");
    FILE *runtime = fopen(runtime_path, "rb");
    FILE *payload = fopen(payload_path, "rb");
    FILE *launch = _open_splashAsset(runtime_path, "_launch.png");
    FILE *font = _open_splashAsset(runtime_path, "JetBrainsMono-Light.ttf");
    free(payload_path);
    if (!runtime || !payload || !launch || !font)
    {
        if (!runtime)
            snprintf(error, error_size, "could not read Pilang runtime '%s': %s", runtime_path, strerror(errno));
        else
            snprintf(error, error_size, "could not read compiled application '%s'", package->entry);
        if (!launch)
            snprintf(error, error_size, "could not read launch logo 'resources/_launch.png'");
        if (!font)
            snprintf(error, error_size, "could not read splash font 'resources/JetBrainsMono-Light.ttf'");
        if (runtime)
            fclose(runtime);
        if (payload)
            fclose(payload);
        if (launch)
            fclose(launch);
        if (font)
            fclose(font);
        return FALSE;
    }
    FILE *output = fopen(package->output, "wb");
    if (!output)
    {
        snprintf(error, error_size, "could not create '%s': %s", package->output, strerror(errno));
        fclose(runtime);
        fclose(payload);
        fclose(launch);
        fclose(font);
        return FALSE;
    }
    bool ok = _copy_file(runtime, output);
    if (!ok)
        _bundle_error(error, error_size, "failed while copying the Pilang runtime");
    fclose(runtime);
    fclose(output);
    if (ok && package->icon)
    {
#ifdef _WIN32
        ok = _set_windowsIcon(package->output, package->icon);
        if (!ok)
            snprintf(error, error_size, "could not embed icon '%s' (use a PNG up to 256x256 or an ICO file)", package->icon);
#else
        ok = FALSE;
        _bundle_error(error, error_size, "executable icons are supported only for Windows packages");
#endif
    }
    output = ok ? fopen(package->output, "ab") : NULL;
    if (!output)
    {
        if (ok)
            snprintf(error, error_size, "could not reopen '%s': %s", package->output, strerror(errno));
        ok = FALSE;
    }
    if (ok && fseek(output, 0, SEEK_END) != 0)
    {
        _bundle_error(error, error_size, "could not seek the package output");
        ok = FALSE;
    }
    long launch_offset = ok ? ftell(output) : -1;
    if (ok && fseek(launch, 0, SEEK_END) != 0)
    {
        _bundle_error(error, error_size, "could not measure the launch image");
        ok = FALSE;
    }
    long launch_size = ftell(launch);
    if (launch_size < 0 || fseek(launch, 0, SEEK_SET) != 0)
    {
        _bundle_error(error, error_size, "could not read the launch image");
        ok = FALSE;
    }
    if (ok)
        ok = _copy_file(launch, output);
    if (!ok && (!error || !error[0]))
        _bundle_error(error, error_size, "failed while appending the launch image");
    long font_offset = ok ? ftell(output) : -1;
    if (ok && fseek(font, 0, SEEK_END) != 0)
    {
        _bundle_error(error, error_size, "could not measure the splash font");
        ok = FALSE;
    }
    long font_size = ftell(font);
    if (font_size < 0 || fseek(font, 0, SEEK_SET) != 0)
    {
        _bundle_error(error, error_size, "could not read the splash font");
        ok = FALSE;
    }
    if (ok)
        ok = _copy_file(font, output);
    if (!ok && (!error || !error[0]))
        _bundle_error(error, error_size, "failed while appending the splash font");
    long offset = ok ? ftell(output) : -1;
    if (fseek(payload, 0, SEEK_END) != 0)
    {
        _bundle_error(error, error_size, "could not measure the compiled application");
        ok = FALSE;
    }
    long size = ftell(payload);
    if (size < 0 || fseek(payload, 0, SEEK_SET) != 0)
    {
        _bundle_error(error, error_size, "could not read the compiled application");
        ok = FALSE;
    }
    if (ok)
        ok = _copy_file(payload, output);
    if (!ok && (!error || !error[0]))
        _bundle_error(error, error_size, "failed while appending the compiled application");
    PiPackageTrailer trailer = {{'P', 'I', 'P', 'A', 'C', 'K', '3', package->console ? 'C' : 'G'},
                                (uint64_t)offset,
                                (uint64_t)size,
                                (uint64_t)launch_offset,
                                (uint64_t)launch_size,
                                (uint64_t)font_offset,
                                (uint64_t)font_size};
    if (ok)
        ok = fwrite(&trailer, 1, sizeof(trailer), output) == sizeof(trailer);
    if (!ok && (!error || !error[0]))
        _bundle_error(error, error_size, "failed while writing package information");
    fclose(payload);
    fclose(launch);
    fclose(font);
    if (output)
        fclose(output);
    if (!ok)
        remove(package->output);
    return ok;
}

bool package_embeddedOffset(const char *path, uint64_t *offset, bool *console,
                            uint64_t *launch_offset, uint64_t *launch_size,
                            uint64_t *font_offset, uint64_t *font_size)
{
    FILE *file = fopen(path, "rb");
    PiPackageTrailer trailer;
    if (!file || fseek(file, -(long)sizeof(trailer), SEEK_END) != 0 ||
        fread(&trailer, 1, sizeof(trailer), file) != sizeof(trailer))
    {
        if (file)
            fclose(file);
        return FALSE;
    }
    fclose(file);
    if (memcmp(trailer.magic, "PIPACK3", 7) != 0)
        return FALSE;
    *offset = trailer.payload_offset;
    if (console)
        *console = trailer.magic[7] != 'G';
    if (launch_offset)
        *launch_offset = trailer.launch_offset;
    if (launch_size)
        *launch_size = trailer.launch_size;
    if (font_offset)
        *font_offset = trailer.font_offset;
    if (font_size)
        *font_size = trailer.font_size;
    return TRUE;
}
