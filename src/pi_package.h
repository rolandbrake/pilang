#ifndef PI_PACKAGE_H
#define PI_PACKAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct
{
    char *name;
    char *version;
    char *entry;
    char *output;
    char *icon;
    char **files;
    size_t file_count;
    bool console;
} PiPackage;

typedef struct
{
    char magic[8];
    uint64_t payload_offset;
    uint64_t payload_size;
    uint64_t launch_offset;
    uint64_t launch_size;
    uint64_t font_offset;
    uint64_t font_size;
} PiPackageTrailer;

/* Loads and validates a package.loon manifest from the current directory. */
bool package_load(PiPackage *package, const char *target);
void package_free(PiPackage *package);
bool package_targets(char ***targets, size_t *target_count);
void package_freeTargets(char **targets, size_t target_count);
bool package_bundle(const char *runtime_path, const PiPackage *package, char *error, size_t error_size);
bool package_web(const char *runtime_path, const PiPackage *package, char *error, size_t error_size);
bool package_embeddedOffset(const char *path, uint64_t *offset, bool *console,
                            uint64_t *launch_offset, uint64_t *launch_size,
                            uint64_t *font_offset, uint64_t *font_size);
void package_showLaunchScreen(int width, int height, const char *title);

#endif
