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

static char *read_text(const char *path)
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

static const loon_value_t *required_string(const loon_value_t *manifest, const char *key)
{
    const loon_value_t *value = loon_objectGet(manifest, key);
    if (!value || value->type != LOON_STRING)
        fprintf(stderr, "Manifest field '%s' must be a string.\n", key);
    return value && value->type == LOON_STRING ? value : NULL;
}

static bool optional_string(const loon_value_t *object, const char *key, const loon_value_t **value)
{
    *value = loon_objectGet(object, key);
    if (*value && (*value)->type != LOON_STRING)
    {
        fprintf(stderr, "Manifest field '%s' must be a string.\n", key);
        return false;
    }
    return true;
}

bool package_load(pi_package_t *package, const char *target)
{
    memset(package, 0, sizeof(*package));
    char *source = read_text("package.loon");
    if (!source)
    {
        fprintf(stderr, "Could not read package.loon in the current directory.\n");
        return false;
    }
    loon_error_t error = {0};
    loon_value_t *manifest = loon_parse(source, &error);
    free(source);
    if (!manifest)
    {
        fprintf(stderr, "Invalid package.loon at %zu:%zu: %s\n", error.line, error.column, error.message);
        return false;
    }
    if (manifest->type != LOON_OBJECT)
    {
        fprintf(stderr, "package.loon must contain an object.\n");
        loon_free(manifest);
        return false;
    }
    const loon_value_t *name = required_string(manifest, "name");
    const loon_value_t *version = required_string(manifest, "version");
    const loon_value_t *entry = required_string(manifest, "entry");
    const loon_value_t *targets = loon_objectGet(manifest, "targets");
    const loon_value_t *config = targets ? loon_objectGet(targets, target) : NULL;
    if (targets && targets->type != LOON_OBJECT)
    {
        fprintf(stderr, "Manifest field 'targets' must be an object.\n");
        config = NULL;
    }
    const loon_value_t *output = config ? required_string(config, "output") : NULL;
    const loon_value_t *icon = NULL;
    const loon_value_t *files = config ? loon_objectGet(config, "files") : NULL;
    const loon_value_t *console = config ? loon_objectGet(config, "console") : NULL;
    if (!name || !version || !entry || !output || !config || config->type != LOON_OBJECT ||
        !optional_string(config, "icon", &icon) ||
        (console && console->type != LOON_BOOL))
    {
        if (!config)
            fprintf(stderr, "Manifest has no '%s' target.\n", target);
        else if (console && console->type != LOON_BOOL)
            fprintf(stderr, "Manifest field 'console' must be true or false.\n");
        loon_free(manifest);
        return false;
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
            return false;
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
                return false;
            }
        }
    }
    loon_free(manifest);
    if (!package->name || !package->version || !package->entry || !package->output ||
        (icon && !package->icon))
    {
        package_free(package);
        return false;
    }
    return true;
}

void package_free(pi_package_t *package)
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

static bool copy_file(FILE *source, FILE *destination)
{
    char buffer[8192];
    size_t count;
    while ((count = fread(buffer, 1, sizeof(buffer), source)) != 0)
        if (fwrite(buffer, 1, count, destination) != count)
            return false;
    return !ferror(source);
}

static void bundle_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size)
        snprintf(error, error_size, "%s", message);
}

#ifdef _WIN32
static bool web_safePath(const char *path)
{
    return path && path[0] && path[0] != '/' && path[0] != '\\' && !strchr(path, ':') && !strstr(path, "..");
}

static bool web_makeParents(char *path)
{
    for (char *p = path + 1; *p; p++)
        if (*p == '/' || *p == '\\')
        {
            char saved = *p;
            *p = '\0';
            if (_mkdir(path) != 0 && errno != EEXIST)
            {
                *p = saved;
                return false;
            }
            *p = saved;
        }
    return true;
}

static bool web_copyPath(const char *output, const char *source, const char *relative, FILE *index)
{
    DWORD attr = GetFileAttributesA(source);
    if (attr == INVALID_FILE_ATTRIBUTES)
        return false;
    if (!(attr & FILE_ATTRIBUTE_DIRECTORY))
    {
        char target[MAX_PATH];
        snprintf(target, sizeof(target), "%s/%s", output, relative);
        if (!web_makeParents(target))
            return false;
        FILE *in = fopen(source, "rb"), *out = in ? fopen(target, "wb") : NULL;
        bool ok = in && out && copy_file(in, out);
        if (in)
            fclose(in);
        if (out)
            fclose(out);
        if (ok)
            fprintf(index, "\"%s\",", relative);
        return ok;
    }
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\\\*", source);
    WIN32_FIND_DATAA item;
    HANDLE find = FindFirstFileA(pattern, &item);
    if (find == INVALID_HANDLE_VALUE)
        return false;
    bool ok = true;
    do
    {
        if (!strcmp(item.cFileName, ".") || !strcmp(item.cFileName, ".."))
            continue;
        char child_source[MAX_PATH], child_relative[MAX_PATH];
        snprintf(child_source, sizeof(child_source), "%s\\\\%s", source, item.cFileName);
        snprintf(child_relative, sizeof(child_relative), "%s/%s", relative, item.cFileName);
        if (!web_copyPath(output, child_source, child_relative, index))
        {
            ok = false;
            break;
        }
    } while (FindNextFileA(find, &item));
    FindClose(find);
    return ok;
}

bool package_web(const char *runtime_path, const pi_package_t *package, char *error, size_t error_size)
{
    (void)runtime_path;
    char root[MAX_PATH];
    if (!GetModuleFileNameA(NULL, root, sizeof(root)))
    {
        bundle_error(error, error_size, "could not locate Pilang runtime");
        return false;
    }
    char *bin = strrchr(root, '\\');
    if (!bin)
        return false;
    *bin = '\0';
    char *project = strrchr(root, '\\');
    if (!project)
        return false;
    *project = '\0';
    char command[MAX_PATH + 32];
    snprintf(command, sizeof(command), "make -C \"%s\" web", root);
    if (system(command) != 0)
    {
        bundle_error(error, error_size, "Emscripten web runtime build failed");
        return false;
    }
    if (_mkdir(package->output) != 0 && errno != EEXIST)
    {
        bundle_error(error, error_size, "could not create web output directory");
        return false;
    }
    char html[MAX_PATH];
    snprintf(html, sizeof(html), "%s/index.html", package->output);
    FILE *index = fopen(html, "wb");
    if (!index)
    {
        bundle_error(error, error_size, "could not create index.html");
        return false;
    }
    fprintf(index, "<!doctype html><meta charset=\"utf-8\"><title>%s</title><canvas></canvas><script type=\"module\">import createPilang from './pilang.js';const app=await createPilang({canvas:document.querySelector('canvas'),noInitialRun:true});const files=[", package->name);
    bool ok = web_safePath(package->entry) && web_copyPath(package->output, package->entry, package->entry, index);
    for (size_t i = 0; ok && i < package->file_count; i++)
        ok = web_safePath(package->files[i]) && web_copyPath(package->output, package->files[i], package->files[i], index);
    fprintf(index, "];for(let i=0;i<files.length;i++){const p=files[i],r=await fetch(p),b=new Uint8Array(await r.arrayBuffer());app.FS.mkdirTree('/'+p.split('/').slice(0,-1).join('/'));app.FS.writeFile('/'+p,b);if(i===0)app.ccall('set_source',null,['string'],[new TextDecoder().decode(b)]);}app.ccall('execute_source','number',[],[]);</script>");
    fclose(index);
    char js[MAX_PATH], wasm[MAX_PATH], target[MAX_PATH];
    snprintf(js, sizeof(js), "%s/bin/pilang.js", root);
    snprintf(wasm, sizeof(wasm), "%s/bin/pilang.wasm", root);
    snprintf(target, sizeof(target), "%s/pilang.js", package->output);
    FILE *in = fopen(js, "rb"), *out = in ? fopen(target, "wb") : NULL;
    ok = ok && in && out && copy_file(in, out);
    if (in)
        fclose(in);
    if (out)
        fclose(out);
    snprintf(target, sizeof(target), "%s/pilang.wasm", package->output);
    in = fopen(wasm, "rb");
    out = in ? fopen(target, "wb") : NULL;
    ok = ok && in && out && copy_file(in, out);
    if (in)
        fclose(in);
    if (out)
        fclose(out);
    if (!ok)
        bundle_error(error, error_size, "could not copy web runtime or assets");
    return ok;
}
#else
bool package_web(const char *runtime_path, const pi_package_t *package, char *error, size_t error_size)
{
    (void)runtime_path;
    (void)package;
    bundle_error(error, error_size, "web packaging is available from Windows development builds only");
    return false;
}
#endif

#ifdef _WIN32
static bool set_windowsIcon(const char *executable, const char *icon_path)
{
    unsigned char signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    FILE *file = fopen(icon_path, "rb");
    if (!file)
        return false;
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    unsigned char *png = length > 24 ? malloc((size_t)length) : NULL;
    bool ok = png && fread(png, 1, (size_t)length, file) == (size_t)length;
    fclose(file);
    if (!ok)
    {
        free(png);
        return false;
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
            ok = false;
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
            ok = false;
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
        ok = false;
    group[14] = (unsigned char)image_size;
    group[15] = (unsigned char)(image_size >> 8);
    group[16] = (unsigned char)(image_size >> 16);
    group[17] = (unsigned char)(image_size >> 24);
    group[18] = 1;
    HANDLE update = BeginUpdateResourceA(executable, FALSE);
    if (!update)
        ok = false;
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

bool package_bundle(const char *runtime_path, const pi_package_t *package, char *error, size_t error_size)
{
    if (error && error_size)
        error[0] = '\0';
    size_t length = strlen(package->entry);
    char *payload_path = malloc(length + 2);
    if (!payload_path)
    {
        bundle_error(error, error_size, "out of memory while preparing the compiled application");
        return false;
    }
    memcpy(payload_path, package->entry, length + 1);
    char *extension = strrchr(payload_path, '.');
    if (extension && strcmp(extension, ".pi") == 0)
        strcpy(extension, ".px");
    else
        strcat(payload_path, ".px");
    FILE *runtime = fopen(runtime_path, "rb");
    FILE *payload = fopen(payload_path, "rb");
    free(payload_path);
    if (!runtime || !payload)
    {
        if (!runtime)
            snprintf(error, error_size, "could not read Pilang runtime '%s': %s", runtime_path, strerror(errno));
        else
            snprintf(error, error_size, "could not read compiled application '%s'", package->entry);
        if (runtime)
            fclose(runtime);
        if (payload)
            fclose(payload);
        return false;
    }
    FILE *output = fopen(package->output, "wb");
    if (!output)
    {
        snprintf(error, error_size, "could not create '%s': %s", package->output, strerror(errno));
        fclose(runtime);
        fclose(payload);
        return false;
    }
    bool ok = copy_file(runtime, output);
    if (!ok)
        bundle_error(error, error_size, "failed while copying the Pilang runtime");
    fclose(runtime);
    fclose(output);
    if (ok && package->icon)
    {
#ifdef _WIN32
        ok = set_windowsIcon(package->output, package->icon);
        if (!ok)
            snprintf(error, error_size, "could not embed icon '%s' (use a PNG up to 256x256 or an ICO file)", package->icon);
#else
        ok = false;
        bundle_error(error, error_size, "executable icons are supported only for Windows packages");
#endif
    }
    output = ok ? fopen(package->output, "ab") : NULL;
    if (!output)
    {
        if (ok)
            snprintf(error, error_size, "could not reopen '%s': %s", package->output, strerror(errno));
        ok = false;
    }
    if (ok && fseek(output, 0, SEEK_END) != 0)
    {
        bundle_error(error, error_size, "could not seek the package output");
        ok = false;
    }
    long offset = ok ? ftell(output) : -1;
    if (fseek(payload, 0, SEEK_END) != 0)
    {
        bundle_error(error, error_size, "could not measure the compiled application");
        ok = false;
    }
    long size = ftell(payload);
    if (size < 0 || fseek(payload, 0, SEEK_SET) != 0)
    {
        bundle_error(error, error_size, "could not read the compiled application");
        ok = false;
    }
    if (ok)
        ok = copy_file(payload, output);
    if (!ok && (!error || !error[0]))
        bundle_error(error, error_size, "failed while appending the compiled application");
    pi_package_trailer_t trailer = {{'P', 'I', 'P', 'A', 'C', 'K', '1', package->console ? 'C' : 'G'}, (uint64_t)offset, (uint64_t)size};
    if (ok)
        ok = fwrite(&trailer, 1, sizeof(trailer), output) == sizeof(trailer);
    if (!ok && (!error || !error[0]))
        bundle_error(error, error_size, "failed while writing package information");
    fclose(payload);
    if (output)
        fclose(output);
    if (!ok)
        remove(package->output);
    return ok;
}

bool package_embeddedOffset(const char *path, uint64_t *offset, bool *console)
{
    FILE *file = fopen(path, "rb");
    pi_package_trailer_t trailer;
    if (!file || fseek(file, -(long)sizeof(trailer), SEEK_END) != 0 ||
        fread(&trailer, 1, sizeof(trailer), file) != sizeof(trailer))
    {
        if (file)
            fclose(file);
        return false;
    }
    fclose(file);
    if (memcmp(trailer.magic, "PIPACK1", 7) != 0)
        return false;
    *offset = trailer.payload_offset;
    if (console)
        *console = trailer.magic[7] != 'G';
    return true;
}
