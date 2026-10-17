#include "pi_package.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "builtin/pi_loon.h"

#ifdef _WIN32
#include <windows.h>
#endif

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
    memset(package, 0, sizeof(*package));
}

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
        return FALSE;
    }
    FILE *output = fopen(package->output, "wb");
    if (!output)
    {
        snprintf(error, error_size, "could not create '%s': %s", package->output, strerror(errno));
        fclose(runtime);
        fclose(payload);
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
    PiPackageTrailer trailer = {{'P', 'I', 'P', 'A', 'C', 'K', '1', package->console ? 'C' : 'G'}, (uint64_t)offset, (uint64_t)size};
    if (ok)
        ok = fwrite(&trailer, 1, sizeof(trailer), output) == sizeof(trailer);
    if (!ok && (!error || !error[0]))
        _bundle_error(error, error_size, "failed while writing package information");
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
    PiPackageTrailer trailer;
    if (!file || fseek(file, -(long)sizeof(trailer), SEEK_END) != 0 ||
        fread(&trailer, 1, sizeof(trailer), file) != sizeof(trailer))
    {
        if (file)
            fclose(file);
        return FALSE;
    }
    fclose(file);
    if (memcmp(trailer.magic, "PIPACK1", 7) != 0)
        return FALSE;
    *offset = trailer.payload_offset;
    if (console)
        *console = trailer.magic[7] != 'G';
    return TRUE;
}
