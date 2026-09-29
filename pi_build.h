#ifndef PI_BUILD_H
#define PI_BUILD_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "pi_compiler.h"

/* .px files are encoded little-endian, field by field; never fwrite these structs. */
#define PX_MAGIC 31415u
#define PX_FORMAT_VERSION 1u
#define PX_VM_ABI 1u
#define PX_HEADER_SIZE 56u
#define PX_MODULE_RECORD_SIZE 56u

#define PX_MODULE_ENTRY 0x00000001u

typedef struct
{
    uint32_t magic;
    uint16_t format_version;
    uint16_t header_size;
    uint32_t vm_abi;
    uint32_t flags;
    uint64_t project_hash;
    uint32_t module_count;
    uint32_t reserved;
    uint64_t module_table_offset;
    uint64_t string_table_offset;
    uint64_t payload_offset;
} px_header_t;

typedef struct
{
    uint32_t name_offset;
    uint32_t name_length;
    uint32_t path_offset;
    uint32_t path_length;
    uint64_t source_size;
    uint64_t source_hash;
    uint64_t payload_offset;
    uint64_t payload_size;
    uint32_t flags;
    uint32_t reserved;
} px_module_record_t;

typedef struct
{
    list_t *code;
    list_t *constants;
    list_t *names;
} px_module_payload_t;

typedef struct
{
    char *name;
    char *path;
    compiler_t *compiler;
    bool is_entry;
    size_t source_size;
    uint64_t source_hash;
} px_built_module_t;

typedef struct px_project_build
{
    px_built_module_t *modules;
    uint32_t module_count;
} px_project_build_t;

bool px_writeHeader(FILE *file, const px_header_t *header);
bool px_readHeader(FILE *file, px_header_t *header);
bool px_headerIsValid(const px_header_t *header, uint32_t vm_abi);

bool px_writeModuleRecord(FILE *file, const px_module_record_t *record);
bool px_readModuleRecord(FILE *file, px_module_record_t *record);

uint64_t px_hashBytes(const void *data, size_t size);

bool px_writeModulePayload(FILE *file, const list_t *code,
                           const list_t *constants, const list_t *names);
bool px_readModulePayload(FILE *file, px_module_payload_t *payload);
void px_freeModulePayload(px_module_payload_t *payload);
bool px_applyModulePayload(compiler_t *compiler, px_module_payload_t *payload);
compiler_t *px_readCompiler(FILE *file);

bool px_writeEntryBuild(const char *build_path, const char *source_path,
                        const char *source, const compiler_t *compiler);
compiler_t *px_readEntryBuild(const char *build_path, const char *source_path,
                              const char *source);
bool px_writeProjectBuild(const char *build_path, const char *entry_path,
                          const char *entry_source, const compiler_t *entry_compiler,
                          px_project_build_t *modules);
bool px_readProjectBuild(const char *build_path, const char *entry_path,
                         const char *entry_source, px_project_build_t *build);
bool px_readExecutableBuild(const char *build_path, px_project_build_t *build);
void px_freeProjectBuild(px_project_build_t *build);
px_built_module_t *px_findBuiltModule(px_project_build_t *build, const char *path);
px_built_module_t *px_findBuiltModuleByName(px_project_build_t *build, const char *name);

#endif // PI_BUILD_H
