// clang-format off
#include "pi_build.h"

#include <string.h>
#include <limits.h>

#include "gc.h"
#include "pi_module.h"
#include "pi_object.h"

typedef enum
{
    PX_VALUE_NIL,
    PX_VALUE_BOOL,
    PX_VALUE_NUMBER,
    PX_VALUE_STRING,
    PX_VALUE_CODE,
} PX_ValueTag;

static bool _write_uint(FILE *file, uint64_t value, int bytes)
{
    for (int i = 0; i < bytes; i++)
    {
        if (fputc((int)(value & 0xff), file) == EOF)
            return FALSE;
        value >>= 8;
    }
    return TRUE;
}

static bool _read_uint(FILE *file, uint64_t *value, int bytes)
{
    uint64_t result = 0;
    for (int i = 0; i < bytes; i++)
    {
        int Byte = fgetc(file);
        if (Byte == EOF)
            return FALSE;
        result |= (uint64_t)(uint8_t)Byte << (i * 8);
    }
    *value = result;
    return TRUE;
}

static bool _read_bytes(FILE *file, void *buffer, size_t size)
{
    return fread(buffer, 1, size, file) == size;
}

bool px_writeHeader(FILE *file, const PX_Header *header)
{
    return _write_uint(file, header->magic, 4)               &&
           _write_uint(file, header->format_version, 2)      &&
           _write_uint(file, header->header_size, 2)         &&
           _write_uint(file, header->vm_abi, 4)              &&
           _write_uint(file, header->flags, 4)               &&
           _write_uint(file, header->project_hash, 8)        &&
           _write_uint(file, header->module_count, 4)        &&
           _write_uint(file, header->reserved, 4)            &&
           _write_uint(file, header->module_table_offset, 8) &&
           _write_uint(file, header->string_table_offset, 8) &&
           _write_uint(file, header->payload_offset, 8);
}

bool px_readHeader(FILE *file, PX_Header *header)
{
    uint64_t value;
#define READ_FIELD(field, bytes)          \
    if (!_read_uint(file, &value, bytes)) \
        return FALSE;                     \
    header->field = value;

    READ_FIELD(magic, 4);
    READ_FIELD(format_version, 2);
    READ_FIELD(header_size, 2);
    READ_FIELD(vm_abi, 4);
    READ_FIELD(flags, 4);
    READ_FIELD(project_hash, 8);
    READ_FIELD(module_count, 4);
    READ_FIELD(reserved, 4);
    READ_FIELD(module_table_offset, 8);
    READ_FIELD(string_table_offset, 8);
    READ_FIELD(payload_offset, 8);
#undef READ_FIELD
    return TRUE;
}

bool px_headerIsValid(const PX_Header *header, uint32_t vm_abi)
{
    return header->magic == PX_MAGIC                   &&
           header->format_version == PX_FORMAT_VERSION &&
           header->header_size == PX_HEADER_SIZE       &&
           header->vm_abi == vm_abi                    &&
           header->reserved == 0;
}

bool px_writeModuleRecord(FILE *file, const PX_ModuleRecord *record)
{
    return _write_uint(file, record->name_offset, 4)    &&
           _write_uint(file, record->name_length, 4)    &&
           _write_uint(file, record->path_offset, 4)    &&
           _write_uint(file, record->path_length, 4)    &&
           _write_uint(file, record->source_size, 8)    &&
           _write_uint(file, record->source_hash, 8)    &&
           _write_uint(file, record->payload_offset, 8) &&
           _write_uint(file, record->payload_size, 8)   &&
           _write_uint(file, record->flags, 4)          &&
           _write_uint(file, record->reserved, 4);
}

bool px_readModuleRecord(FILE *file, PX_ModuleRecord *record)
{
    uint64_t value;
#define READ_FIELD(field, bytes)          \
    if (!_read_uint(file, &value, bytes)) \
        return FALSE;                     \
    record->field = value;
    READ_FIELD(name_offset, 4);
    READ_FIELD(name_length, 4);
    READ_FIELD(path_offset, 4);
    READ_FIELD(path_length, 4);
    READ_FIELD(source_size, 8);
    READ_FIELD(source_hash, 8);
    READ_FIELD(payload_offset, 8);
    READ_FIELD(payload_size, 8);
    READ_FIELD(flags, 4);
    READ_FIELD(reserved, 4);
#undef READ_FIELD
    return TRUE;
}

uint64_t px_hashBytes(const void *data, size_t size)
{
    const uint8_t *bytes = data;
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < size; i++)
    {
        hash ^= bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static bool _write_string(FILE *file, const char *chars, size_t length)
{
    return length <= UINT32_MAX &&
           _write_uint(file, length, 4) &&
           fwrite(chars, 1, length, file) == length;
}

static bool _write_code(FILE *file, const ObjCode *code)
{
    if (!code || !code->data || code->data->size > UINT32_MAX)
        return FALSE;

    uint8_t flags = (code->need_args ? 1 : 0)        |
                    (code->need_kwargs ? 2 : 0)      |
                    (code->method_need_args ? 4 : 0) |
                    (code->method_need_kwargs ? 8 : 0);
    uint32_t param_count = code->param_names ? (uint32_t)code->param_names->size : 0;
    if (!_write_uint(file, code->data->size, 4) ||
        !_write_uint(file, flags, 1)            ||
        !_write_uint(file, param_count, 4)      ||
        fwrite(code->data->data, 1, (size_t)code->data->size, file) != (size_t)code->data->size)
        return FALSE;

    for (uint32_t i = 0; i < param_count; i++)
    {
        String *name = _list_getAt(code->param_names, (int)i);
        if (!name || !_write_string(file, name->data, name->length))
            return FALSE;
    }
    return TRUE;
}

static bool _write_value(FILE *file, Value value)
{
    if (IS_NIL(value))
        return _write_uint(file, PX_VALUE_NIL, 1);
    if (IS_BOOL(value))
        return _write_uint(file, PX_VALUE_BOOL, 1) && _write_uint(file, AS_BOOL(value), 1);
    if (IS_NUM(value))
    {
        uint64_t bits;
        memcpy(&bits, &value.data.number, sizeof(bits));
        return _write_uint(file, PX_VALUE_NUMBER, 1) && _write_uint(file, bits, 8);
    }
    if (!IS_OBJ(value))
        return FALSE;

    Object *object = AS_OBJ(value);
    if (object->type == OBJ_STRING)
    {
        PiString *string = (PiString *)object;
        return _write_uint(file, PX_VALUE_STRING, 1) &&
               _write_string(file, string->chars, string->length);
    }
    if (object->type == OBJ_CODE)
        return _write_uint(file, PX_VALUE_CODE, 1) && _write_code(file, (ObjCode *)object);
    return FALSE;
}

bool px_writeModulePayload(FILE *file, const List *code,
                           const List *constants, const List *names)
{
    if (!code || !constants || !names || code->size  > UINT32_MAX ||
        constants->size > UINT32_MAX  || names->size > UINT32_MAX)
        return FALSE;

    if (!_write_uint(file, code->size, 4)      ||
        !_write_uint(file, constants->size, 4) ||
        !_write_uint(file, names->size, 4)     ||
        fwrite(code->data, 1, (size_t)code->size, file) != (size_t)code->size)
        return FALSE;

    for (int i = 0; i < constants->size; i++)
    {
        Value value = *(Value *)_list_getAt((List *)constants, i);
        if (!_write_value(file, value))
            return FALSE;
    }

    for (int i = 0; i < names->size; i++)
    {
        String *name = _list_getAt((List *)names, i);
        if (!name || !_write_string(file, name->data, name->length))
            return FALSE;
    }
    return TRUE;
}

static char *_read_string(FILE *file)
{
    uint64_t length;
    if (!_read_uint(file, &length, 4) || length > PI_MAX_LIST_SIZE)
        return NULL;

    char *chars = malloc((size_t)length + 1);
    if (!chars || !_read_bytes(file, chars, (size_t)length))
    {
        free(chars);
        return NULL;
    }
    chars[length] = '\0';
    return chars;
}

static void _free_names(List *names)
{
    if (!names)
        return;
    for (int i = 0; i < names->size; i++)
    {
        String *name = _list_getAt(names, i);
        free(name->data);
    }
    list_free(names);
}

static void _free_code(ObjCode *code)
{
    if (!code)
        return;
    _free_names(code->param_names);
    code->param_names = NULL;
    free_object((Object *)code);
}

static void _free_constants(List *constants)
{
    if (!constants)
        return;
    for (int i = 0; i < constants->size; i++)
    {
        Value *value = _list_getAt(constants, i);
        if (!IS_OBJ(*value))
            continue;
        Object *object = AS_OBJ(*value);
        if (object->type == OBJ_CODE)
            _free_code((ObjCode *)object);
        else
            free_object(object);
    }
    list_free(constants);
}

void px_freeModulePayload(PX_ModulePayload *payload)
{
    if (!payload)
        return;
    _free_constants(payload->constants);
    _free_names(payload->names);
    list_free(payload->code);
    memset(payload, 0, sizeof(*payload));
}

static ObjCode *_read_code(FILE *file)
{
    uint64_t bytecode_size;
    uint64_t flags;
    uint64_t param_count;
    if (!_read_uint(file, &bytecode_size, 4) || bytecode_size > PI_MAX_LIST_SIZE  ||
        !_read_uint(file, &flags, 1) || !_read_uint(file, &param_count, 4)        ||
        param_count > PI_MAX_LIST_SIZE)
        return NULL;

    List *bytecode = list_createCap(sizeof(uint8_t), (int)bytecode_size);
    if (!_read_bytes(file, bytecode->data, (size_t)bytecode_size))
    {
        list_free(bytecode);
        return NULL;
    }
    bytecode->size = (int)bytecode_size;

    ObjCode *code = (ObjCode *)new_code(bytecode);
    code->need_args = (flags & 1) != 0;
    code->need_kwargs = (flags & 2) != 0;
    code->method_need_args = (flags & 4) != 0;
    code->method_need_kwargs = (flags & 8) != 0;
    if (param_count == 0)
        return code;

    code->param_names = list_createCap(sizeof(String), (int)param_count);
    for (uint32_t i = 0; i < param_count; i++)
    {
        char *chars = _read_string(file);
        String *name = chars ? new_string(chars) : NULL;
        free(chars);
        if (!name)
        {
            _free_code(code);
            return NULL;
        }
        _list_add(code->param_names, name);
        free(name);
    }
    return code;
}

static bool _read_value(FILE *file, Value *value)
{
    uint64_t tag;
    uint64_t data;
    if (!_read_uint(file, &tag, 1))
        return FALSE;

    switch (tag)
    {
    case PX_VALUE_NIL:
        *value = NEW_NIL();
        return TRUE;
    case PX_VALUE_BOOL:
        if (!_read_uint(file, &data, 1) || data > 1)
            return FALSE;
        *value = NEW_BOOL(data != 0);
        return TRUE;
    case PX_VALUE_NUMBER:
        if (!_read_uint(file, &data, 8))
            return FALSE;
        *value = NEW_NUM(0);
        memcpy(&value->data.number, &data, sizeof(data));
        return TRUE;
    case PX_VALUE_STRING:
    {
        char *chars = _read_string(file);
        if (!chars)
            return FALSE;
        *value = NEW_OBJ(new_pistring(chars));
        return TRUE;
    }
    case PX_VALUE_CODE:
    {
        ObjCode *code = _read_code(file);
        if (!code)
            return FALSE;
        *value = NEW_OBJ(code);
        return TRUE;
    }
    default:
        return FALSE;
    }
}

bool px_readModulePayload(FILE *file, PX_ModulePayload *payload)
{
    uint64_t bytecode_size;
    uint64_t constant_count;
    uint64_t name_count;
    if (!payload || !_read_uint(file, &bytecode_size, 4)                           ||
        !_read_uint(file, &constant_count, 4) || !_read_uint(file, &name_count, 4) ||
        bytecode_size > PI_MAX_LIST_SIZE || constant_count > PI_MAX_LIST_SIZE      ||
        name_count > PI_MAX_LIST_SIZE)
        return FALSE;

    memset(payload, 0, sizeof(*payload));
    payload->code = list_createCap(sizeof(uint8_t), (int)bytecode_size);
    payload->constants = list_createCap(sizeof(Value), (int)constant_count);
    payload->names = list_createCap(sizeof(String), (int)name_count);
    if (!_read_bytes(file, payload->code->data, (size_t)bytecode_size))
        goto failed;
    payload->code->size = (int)bytecode_size;

    for (uint32_t i = 0; i < constant_count; i++)
    {
        Value value;
        if (!_read_value(file, &value))
            goto failed;
        _list_add(payload->constants, &value);
    }

    for (uint32_t i = 0; i < name_count; i++)
    {
        char *chars = _read_string(file);
        String *name = chars ? new_string(chars) : NULL;
        free(chars);
        if (!name)
            goto failed;
        _list_add(payload->names, name);
        free(name);
    }
    return TRUE;

failed:
    px_freeModulePayload(payload);
    return FALSE;
}

bool px_applyModulePayload(Compiler *compiler, PX_ModulePayload *payload)
{
    if (!compiler || !payload || !payload->code || !payload->constants || !payload->names)
        return FALSE;

    /* This is intentionally for a fresh compiler created by init_compiler(). */
    list_free(compiler->code);
    list_free(compiler->constants);
    free_strings(compiler->names);

    compiler->code = payload->code;
    compiler->constants = payload->constants;
    compiler->names = payload->names;
    compiler->current->code = compiler->code;
    memset(&compiler->global_cache, 0, sizeof(compiler->global_cache));
    memset(payload, 0, sizeof(*payload));
    return TRUE;
}

Compiler *px_readCompiler(FILE *file)
{
    PX_ModulePayload payload;
    if (!px_readModulePayload(file, &payload))
        return NULL;

    Compiler *compiler = init_compiler();
    if (!px_applyModulePayload(compiler, &payload))
    {
        px_freeModulePayload(&payload);
        free_compiler(compiler);
        return NULL;
    }
    return compiler;
}

static uint64_t read_base;

static bool _seek_to(FILE *file, uint64_t offset)
{
    if (offset > UINT64_MAX - read_base) return FALSE;
    offset += read_base;
    return offset <= LONG_MAX && fseek(file, (long)offset, SEEK_SET) == 0;
}

bool px_writeEntryBuild(const char *build_path, const char *source_path,
                        const char *source, const Compiler *compiler)
{
    if (!build_path || !source_path || !source || !compiler)
        return FALSE;

    FILE *file = fopen(build_path, "wb");
    if (!file)
        return FALSE;

    const char name[] = "<main>";
    size_t source_size = strlen(source);
    size_t path_length = strlen(source_path);

    if (source_size > UINT32_MAX || path_length > UINT32_MAX)
        goto failed;

    uint64_t string_table_offset = PX_HEADER_SIZE + PX_MODULE_RECORD_SIZE;
    uint64_t payload_offset = string_table_offset + sizeof(name) - 1 + path_length;

    PX_Header header = {
        .magic = PX_MAGIC,
        .format_version = PX_FORMAT_VERSION,
        .header_size = PX_HEADER_SIZE,
        .vm_abi = PX_VM_ABI,
        .project_hash = px_hashBytes(source, source_size),
        .module_count = 1,
        .module_table_offset = PX_HEADER_SIZE,
        .string_table_offset = string_table_offset,
        .payload_offset = payload_offset,
    };

    PX_ModuleRecord record = {
        .name_offset = 0,
        .name_length = sizeof(name) - 1,
        .path_offset = sizeof(name) - 1,
        .path_length = (uint32_t)path_length,
        .source_size = source_size,
        .source_hash = header.project_hash,
        .payload_offset = payload_offset,
        .flags = PX_MODULE_ENTRY,
    };

    if (!px_writeHeader(file, &header) OR !px_writeModuleRecord(file, &record) ||
        fwrite(name, 1, sizeof(name) - 1, file) != sizeof(name) - 1            ||
        fwrite(source_path, 1, path_length, file) != path_length               ||
        !px_writeModulePayload(file, compiler->code, compiler->constants, compiler->names))
        goto failed;

    long end = ftell(file);
    if (end < 0)
        goto failed;
    record.payload_size = (uint64_t)end - payload_offset;
    if (!_seek_to(file, 0) || !px_writeHeader(file, &header) ||
        !_seek_to(file, header.module_table_offset) || !px_writeModuleRecord(file, &record))
        goto failed;

    fclose(file);
    return TRUE;

failed:
    fclose(file);
    return FALSE;
}

static bool _read_path(FILE *file, const PX_Header *header,
                      const PX_ModuleRecord *record, const char *source_path)
{
    if (record->path_length != strlen(source_path) ||
        !_seek_to(file, header->string_table_offset + record->path_offset))
        return FALSE;

    for (uint32_t i = 0; i < record->path_length; i++)
    {
        int Byte = fgetc(file);
        if (Byte == EOF || Byte != (unsigned char)source_path[i])
            return FALSE;
    }
    return TRUE;
}

Compiler *px_readEntryBuild(const char *build_path, const char *source_path,
                              const char *source)
{
    if (!build_path || !source_path || !source)
        return NULL;

    FILE *file = fopen(build_path, "rb");
    if (!file)
        return NULL;

    PX_Header header;
    PX_ModuleRecord record;

    size_t source_size = strlen(source);
    uint64_t source_hash = px_hashBytes(source, source_size);

    bool valid = px_readHeader(file, &header)                    &&
                 px_headerIsValid(&header, PX_VM_ABI)            &&

                 header.module_count == 1                        &&
                 header.project_hash == source_hash              &&

                 _seek_to(file, header.module_table_offset)      &&
                 px_readModuleRecord(file, &record)              &&

                 (record.flags & PX_MODULE_ENTRY) != 0           &&

                 record.source_size == source_size               &&
                 record.source_hash == source_hash               &&

                 _read_path(file, &header, &record, source_path) &&
                 _seek_to(file, record.payload_offset);

    Compiler *compiler = valid ? px_readCompiler(file) : NULL;
    fclose(file);
    return compiler;
}

typedef struct
{
    const char *name;
    const char *path;
    char *source;
    size_t source_size;
    uint64_t source_hash;
    const List *code;
    const List *constants;
    const List *names;
    bool owns_source;
    PX_ModuleRecord record;
} PX_BuildSource;

static char *_read_sourceFile(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    if (!file || fseek(file, 0, SEEK_END) != 0)
    {
        if (file)
            fclose(file);
        return NULL;
    }
    long length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET) != 0)
    {
        fclose(file);
        return NULL;
    }

    char *source = malloc((size_t)length + 1);
    if (!source || fread(source, 1, (size_t)length, file) != (size_t)length)
    {
        free(source);
        fclose(file);
        return NULL;
    }
    fclose(file);
    source[length] = '\0';
    *size = (size_t)length;
    return source;
}

static void _free_buildSources(PX_BuildSource *sources, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++)
        if (sources[i].owns_source)
            free(sources[i].source);
    free(sources);
}

static uint64_t _project_hash(const PX_BuildSource *sources, uint32_t count)
{
    uint64_t hash = FNV_OFFSET;
    for (uint32_t i = 0; i < count; i++)
        for (size_t j = 0; j < sources[i].source_size; j++)
            hash = (hash ^ (uint8_t)sources[i].source[j]) * FNV_PRIME;
    return hash;
}

bool px_writeProjectBuild(const char *build_path, const char *entry_path,
                          const char *entry_source, const Compiler *entry_compiler,
                          PX_ProjectBuild *modules)
{
    if (!build_path || !entry_path || !entry_source || !entry_compiler)
        return FALSE;

    uint32_t capacity = (modules ? modules->module_count : 0) + 1;
    PX_BuildSource *sources = calloc(capacity, sizeof(*sources));
    if (!sources)
        return FALSE;

    uint32_t count = 1;
    sources[0] = (PX_BuildSource){
        .name = "<main>",
        .path = entry_path,
        .source = (char *)entry_source,
        .source_size = strlen(entry_source),
        .code = entry_compiler->code,
        .constants = entry_compiler->constants,
        .names = entry_compiler->names,
    };
    sources[0].source_hash = px_hashBytes(entry_source, sources[0].source_size);

    if (modules)
    {
        for (uint32_t i = 0; i < modules->module_count; i++)
        {
            PX_BuiltModule *module = &modules->modules[i];
            if (!module->compiler)
                continue;

            size_t source_size;
            char *source = _read_sourceFile(module->path, &source_size);
            if (!source || source_size != module->source_size ||
                px_hashBytes(source, source_size) != module->source_hash)
            {
                free(source);
                _free_buildSources(sources, count);
                return FALSE;
            }
            sources[count++] = (PX_BuildSource){
                .name = module->name,
                .path = module->path,
                .source = source,
                .source_size = source_size,
                .source_hash = module->source_hash,
                .code = module->compiler->code,
                .constants = module->compiler->constants,
                .names = module->compiler->names,
                .owns_source = TRUE,
            };
        }
    }

    uint64_t string_size = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        size_t name_length = strlen(sources[i].name);
        size_t path_length = strlen(sources[i].path);
        if (name_length > UINT32_MAX || path_length > UINT32_MAX ||
            string_size > UINT32_MAX ||
            string_size > UINT32_MAX - name_length ||
            string_size + name_length > UINT32_MAX - path_length)
        {
            _free_buildSources(sources, count);
            return FALSE;
        }
        sources[i].record.name_offset = (uint32_t)string_size;
        sources[i].record.name_length = (uint32_t)name_length;
        string_size += name_length;
        sources[i].record.path_offset = (uint32_t)string_size;
        sources[i].record.path_length = (uint32_t)path_length;
        string_size += path_length;
        sources[i].record.source_size = sources[i].source_size;
        sources[i].record.source_hash = sources[i].source_hash;
        sources[i].record.flags = i == 0 ? PX_MODULE_ENTRY : 0;
    }

    PX_Header header = {
        .magic = PX_MAGIC,
        .format_version = PX_FORMAT_VERSION,
        .header_size = PX_HEADER_SIZE,
        .vm_abi = PX_VM_ABI,
        .project_hash = _project_hash(sources, count),
        .module_count = count,
        .module_table_offset = PX_HEADER_SIZE,
        .string_table_offset = PX_HEADER_SIZE + (uint64_t)count * PX_MODULE_RECORD_SIZE,
    };
    header.payload_offset = header.string_table_offset + string_size;

    FILE *file = fopen(build_path, "wb");
    if (!file)
    {
        _free_buildSources(sources, count);
        return FALSE;
    }

    PX_ModuleRecord empty_record = {0};
    if (!px_writeHeader(file, &header))
        goto failed;
    for (uint32_t i = 0; i < count; i++)
        if (!px_writeModuleRecord(file, &empty_record))
            goto failed;
    for (uint32_t i = 0; i < count; i++)
        if (fwrite(sources[i].name, 1, sources[i].record.name_length, file) != sources[i].record.name_length ||
            fwrite(sources[i].path, 1, sources[i].record.path_length, file) != sources[i].record.path_length)
            goto failed;

    for (uint32_t i = 0; i < count; i++)
    {
        long offset = ftell(file);
        if (offset < 0)
            goto failed;
        sources[i].record.payload_offset = (uint64_t)offset;
        if (!px_writeModulePayload(file, sources[i].code, sources[i].constants, sources[i].names))
            goto failed;
        long end = ftell(file);
        if (end < 0)
            goto failed;
        sources[i].record.payload_size = (uint64_t)end - sources[i].record.payload_offset;
    }

    if (!_seek_to(file, 0) || !px_writeHeader(file, &header) ||
        !_seek_to(file, header.module_table_offset))
        goto failed;
    for (uint32_t i = 0; i < count; i++)
        if (!px_writeModuleRecord(file, &sources[i].record))
            goto failed;

    fclose(file);
    _free_buildSources(sources, count);
    return TRUE;

failed:
    fclose(file);
    _free_buildSources(sources, count);
    return FALSE;
}

void px_freeProjectBuild(PX_ProjectBuild *build)
{
    if (!build)
        return;
    for (uint32_t i = 0; i < build->module_count; i++)
    {
        free(build->modules[i].name);
        free(build->modules[i].path);
        if (build->modules[i].compiler)
            free_compiler(build->modules[i].compiler);
    }
    free(build->modules);
    memset(build, 0, sizeof(*build));
}

PX_BuiltModule *px_findBuiltModule(PX_ProjectBuild *build, const char *path)
{
    if (!build || !path)
        return NULL;
    for (uint32_t i = 0; i < build->module_count; i++)
        if (strcmp(build->modules[i].path, path) == 0)
            return &build->modules[i];
    return NULL;
}

PX_BuiltModule *px_findBuiltModuleByName(PX_ProjectBuild *build, const char *name)
{
    if (!build || !name)
        return NULL;
    for (uint32_t i = 0; i < build->module_count; i++)
        if (strcmp(build->modules[i].name, name) == 0)
            return &build->modules[i];
    return NULL;
}

static char *_read_tableString(FILE *file, const PX_Header *header,
                              uint32_t offset, uint32_t length)
{
    if (offset > header->payload_offset - header->string_table_offset          ||
        length > header->payload_offset - header->string_table_offset - offset ||
        !_seek_to(file, header->string_table_offset + offset))
        return NULL;

    char *string = malloc((size_t)length + 1);
    if (!string || !_read_bytes(file, string, length))
    {
        free(string);
        return NULL;
    }
    string[length] = '\0';
    return string;
}

static uint64_t _update_projectHash(uint64_t hash, const char *source, size_t size)
{
    for (size_t i = 0; i < size; i++)
        hash = (hash ^ (uint8_t)source[i]) * FNV_PRIME;
    return hash;
}

static bool _read_projectBuild(const char *build_path, const char *entry_path,
                              const char *entry_source, bool validate_sources,
                              PX_ProjectBuild *build, uint64_t base)
{
    if (!build_path || !build || (validate_sources && (!entry_path || !entry_source)))
        return FALSE;
    memset(build, 0, sizeof(*build));

    FILE *file = fopen(build_path, "rb");
    if (!file)
        return FALSE;

    read_base = base;

    PX_Header header;
    if (!_seek_to(file, 0) || !px_readHeader(file, &header) || !px_headerIsValid(&header, PX_VM_ABI)             ||
        header.module_count == 0 || header.module_count > PI_MAX_LIST_SIZE                 ||
        header.module_table_offset < header.header_size                                    ||
        header.string_table_offset < header.module_table_offset +
                                     (uint64_t)header.module_count * PX_MODULE_RECORD_SIZE ||
        header.payload_offset < header.string_table_offset                                 ||
        !_seek_to(file, header.module_table_offset))
        goto failed;

    PX_ModuleRecord *records = calloc(header.module_count, sizeof(*records));
    build->modules = calloc(header.module_count, sizeof(*build->modules));
    if (!records || !build->modules)
    {
        free(records);
        goto failed;
    }
    build->module_count = header.module_count;

    for (uint32_t i = 0; i < header.module_count; i++)
        if (!px_readModuleRecord(file, &records[i]))
        {
            free(records);
            goto failed;
        }

    uint64_t hash = FNV_OFFSET;
    uint32_t entry_count = 0;
    for (uint32_t i = 0; i < header.module_count; i++)
    {
        PX_ModuleRecord *record = &records[i];
        PX_BuiltModule *module = &build->modules[i];
        module->name = _read_tableString(file, &header, record->name_offset, record->name_length);
        module->path = _read_tableString(file, &header, record->path_offset, record->path_length);
        module->is_entry = (record->flags & PX_MODULE_ENTRY) != 0;
        module->source_size = record->source_size;
        module->source_hash = record->source_hash;
        if (!module->name || !module->path || record->reserved != 0)
        {
            free(records);
            goto failed;
        }

        if (module->is_entry)
        {
            entry_count++;
        }
        if (validate_sources)
        {
            const char *source = NULL;
            size_t source_size = 0;
            char *owned_source = NULL;
            if (module->is_entry)
            {
                if (strcmp(module->path, entry_path) != 0)
                {
                    free(records);
                    goto failed;
                }
                source = entry_source;
                source_size = strlen(source);
            }
            else
            {
                owned_source = _read_sourceFile(module->path, &source_size);
                source = owned_source;
            }

            if (!source || source_size != record->source_size ||
                px_hashBytes(source, source_size) != record->source_hash)
            {
                free(owned_source);
                free(records);
                goto failed;
            }
            hash = _update_projectHash(hash, source, source_size);
            free(owned_source);
        }
    }

    if (entry_count != 1 || (validate_sources && hash != header.project_hash))
    {
        free(records);
        goto failed;
    }

    for (uint32_t i = 0; i < header.module_count; i++)
    {
        PX_ModuleRecord *record = &records[i];
        if (record->payload_offset < header.payload_offset ||
            record->payload_size > UINT64_MAX - record->payload_offset)
        {
            free(records);
            goto failed;
        }
        if (!_seek_to(file, record->payload_offset))
        {
            free(records);
            goto failed;
        }
        build->modules[i].compiler = px_readCompiler(file);
        long end = ftell(file);
        if (!build->modules[i].compiler || end < 0 ||
            (uint64_t)end - read_base != record->payload_offset + record->payload_size)
        {
            free(records);
            goto failed;
        }
    }

    free(records);
    fclose(file);
    read_base = 0;
    return TRUE;

failed:
    fclose(file);
    read_base = 0;
    px_freeProjectBuild(build);
    return FALSE;
}

bool px_readProjectBuild(const char *build_path, const char *entry_path,
                         const char *entry_source, PX_ProjectBuild *build)
{
    return _read_projectBuild(build_path, entry_path, entry_source, TRUE, build, 0);
}

bool px_readExecutableBuild(const char *build_path, PX_ProjectBuild *build)
{
    return _read_projectBuild(build_path, NULL, NULL, FALSE, build, 0);
}

bool px_readEmbeddedBuild(const char *path, uint64_t offset, PX_ProjectBuild *build)
{
    return _read_projectBuild(path, NULL, NULL, FALSE, build, offset);
}
