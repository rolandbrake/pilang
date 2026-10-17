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
    bool console;
} pi_package_t;

typedef struct
{
    char magic[8];
    uint64_t payload_offset;
    uint64_t payload_size;
} pi_package_trailer_t;

/* Loads and validates a package.loon manifest from the current directory. */
bool package_load(pi_package_t *package, const char *target);
void package_free(pi_package_t *package);
bool package_bundle(const char *runtime_path, const pi_package_t *package, char *error, size_t error_size);
bool package_embeddedOffset(const char *path, uint64_t *offset, bool *console);

#endif
