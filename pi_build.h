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
} PX_Header;

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
} PX_ModuleRecord;

typedef struct
{
    List *code;
    List *constants;
    List *names;
} PX_ModulePayload;

typedef struct
{
    char *name;
    char *path;
    Compiler *compiler;
    bool is_entry;
    size_t source_size;
    uint64_t source_hash;
} PX_BuiltModule;

typedef struct PX_ProjectBuild
{
    PX_BuiltModule *modules;
    uint32_t module_count;
} PX_ProjectBuild;

bool px_writeHeader(FILE *file, const PX_Header *header);
bool px_readHeader(FILE *file, PX_Header *header);
bool px_headerIsValid(const PX_Header *header, uint32_t vm_abi);

bool px_writeModuleRecord(FILE *file, const PX_ModuleRecord *record);
bool px_readModuleRecord(FILE *file, PX_ModuleRecord *record);

uint64_t px_hashBytes(const void *data, size_t size);

bool px_writeModulePayload(FILE *file, const List *code,
                           const List *constants, const List *names);
bool px_readModulePayload(FILE *file, PX_ModulePayload *payload);
void px_freeModulePayload(PX_ModulePayload *payload);
bool px_applyModulePayload(Compiler *compiler, PX_ModulePayload *payload);
Compiler *px_readCompiler(FILE *file);

bool px_writeEntryBuild(const char *build_path, const char *source_path,
                        const char *source, const Compiler *compiler);
Compiler *px_readEntryBuild(const char *build_path, const char *source_path,
                              const char *source);
bool px_writeProjectBuild(const char *build_path, const char *entry_path,
                          const char *entry_source, const Compiler *entry_compiler,
                          PX_ProjectBuild *modules);
bool px_readProjectBuild(const char *build_path, const char *entry_path,
                         const char *entry_source, PX_ProjectBuild *build);
bool px_readExecutableBuild(const char *build_path, PX_ProjectBuild *build);
bool px_readEmbeddedBuild(const char *path, uint64_t offset, PX_ProjectBuild *build);
void px_freeProjectBuild(PX_ProjectBuild *build);
PX_BuiltModule *px_findBuiltModule(PX_ProjectBuild *build, const char *path);
PX_BuiltModule *px_findBuiltModuleByName(PX_ProjectBuild *build, const char *name);

#endif // PI_BUILD_H
